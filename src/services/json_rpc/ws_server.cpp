/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <iostream>
#include "ws_server.h"
#include "jrpc.h"

/***********************/
/* extra user callback */
/***********************/
class EVUserCB: public gdt::GDTCallbackMethod {
public:
    EVUserCB() = default;
    EVUserCB(const EVUserCB &o) = delete;
    EVUserCB &operator=(const EVUserCB &o) = delete;
    ~EVUserCB() override {std::cout << "==========+FREEING ===============" << std::endl;  }

    // param map for non-variant params
    std::vector<gdt::ServiceParam*> pmap;
};


static void fail(beast::error_code ec, char const *what) {
    std::cerr << what << ": " << ec.message() << "\n";
}

WsSession::WsSession(tcp::socket &&socket) : ws_(std::move(socket)) {}

void WsSession::run(){
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(ws_.get_executor(),
                  beast::bind_front_handler(&WsSession::on_run, 
                                            shared_from_this()));
}

void WsSession::on_run(){
    // Set suggested timeout settings for the websocket
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    ws_.set_option(
        websocket::stream_base::decorator([](websocket::response_type &res) {
            res.set(http::field::server,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-server-async");
        }));
    // Accept the websocket handshake
    ws_.async_accept(beast::bind_front_handler(&WsSession::on_accept, 
                                               shared_from_this()));
}

void WsSession::on_accept(beast::error_code ec){
    if (ec)
        return fail(ec, "accept");

    // Read a message
    do_read();
}

void WsSession::do_read(){
    // Read a message into our buffer
    ws_.async_read(buffer_, beast::bind_front_handler(&WsSession::on_read,
                                                      shared_from_this()));
}


void WsSession::send_buff(beast::flat_buffer &b, std::size_t sz){
    b.commit(sz);
    ws_.async_write(b.data(),
                    beast::bind_front_handler(&WsSession::on_write,
                                              shared_from_this()));


}

void WsSession::on_read(beast::error_code ec, std::size_t bt){
    boost::ignore_unused(bt);

    // This indicates that the session was closed
    if (ec == websocket::error::closed)
        return;

    if (ec)
        fail(ec, "read");

    // accept only text data
    if (!ws_.got_text()){
        // close ws session (code 1000)
        ws_.async_close({websocket::close_code::normal},
                        [](beast::error_code) {});
        return;
    }

    // parse
    std::string rpc_data(net::buffers_begin(buffer_.data()), 
                         net::buffers_end(buffer_.data()));
    json j = json::parse(rpc_data, nullptr, false);
   
    // text reply 
    ws_.text(true);
    // clear buffer
    buffer_.consume(buffer_.size());
    // reply data
    std::string ws_rpl;
    std::size_t sz;

    // validate json
    if (j.is_discarded()){
        ws_rpl = json_rpc::JsonRpc::gen_err(-1).dump();
        sz = net::buffer_copy(buffer_.prepare(ws_rpl.size()),
                              net::buffer(ws_rpl));
        send_buff(buffer_, sz);
        return;

    }else{
        // create json rpc parser
        json_rpc::JsonRpc jrpc(j);

        // verify if json is a valid json rpc data
        try {
            jrpc.verify(true);

            std::cout << "ID: " << jrpc.get_id() << std::endl;
        } catch (std::exception &e) {
            std::cout << e.what() << std::endl;
            ws_rpl = json_rpc::JsonRpc::gen_err(-1).dump();
            sz = net::buffer_copy(buffer_.prepare(ws_rpl.size()),
                                  net::buffer(ws_rpl));
            send_buff(buffer_, sz);
            return;
        }
    }
    
    // no error
    ws_rpl = json_rpc::JsonRpc::gen_err(999).dump();
    sz = net::buffer_copy(buffer_.prepare(ws_rpl.size()), net::buffer(ws_rpl));
    send_buff(buffer_, sz);
}

void WsSession::gdt_push(const json_rpc::JsonRpc &jrpc, const WsSession *ws){
    auto dd = static_cast<JsonRpcdDescriptor*>(mink::CURRENT_DAEMON);
    // local routing daemon pointer
    gdt::GDTClient *gdtc = nullptr;
    // smsg
    gdt::ServiceMessage *msg = nullptr;
    // payload
    JrpcPayload *pld = nullptr;
    // randomizer
    mink_utils::Randomizer rand;
    // tmp guid
    uint8_t guid[16];

    // *********************************************
    // ************ push via GDT *******************
    // *********************************************
    // get new router if connection broken
    if (!(dd->rtrd_gdtc && dd->rtrd_gdtc->is_registered()))
        dd->rtrd_gdtc = dd->gdts->get_registered_client("routingd");
    // local routing daemon pointer
    gdtc = dd->rtrd_gdtc;
    // null check
    if (!gdtc) {
        // TODO stats
        return;
    }
    // allocate new service message
    msg = dd->gdtsmm->new_smsg();
    // msg sanity check
    if (!msg) {
        // TODO stats
        return;
    }

    // header and body
    //const gdt_grpc::Header &hdr = req.header();
    //const gdt_grpc::Body &bdy = req.body();

    // service id
    msg->set_service_id(47);

    // extra params
    EVUserCB *ev_usr_cb = nullptr;
    std::vector<gdt::ServiceParam*> *pmap = nullptr;

}


void WsSession::on_write(beast::error_code ec, std::size_t bt){
    boost::ignore_unused(bt);

    if (ec)
        return fail(ec, "write");

    // Clear the buffer
    buffer_.consume(buffer_.size());

    // Do another read
    do_read();
}


WsListener::WsListener(net::io_context &ioc, tcp::endpoint endpoint) : ioc_(ioc), 
                                                                       acceptor_(ioc) {
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        fail(ec, "open");
        return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        fail(ec, "set_option");
        return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
        fail(ec, "bind");
        return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        fail(ec, "listen");
        return;
    }
}

void WsListener::run(){
    do_accept();
}

void WsListener::do_accept(){
    // The new connection gets its own strand
    acceptor_.async_accept(net::make_strand(ioc_),
                           beast::bind_front_handler(&WsListener::on_accept, 
                                                     shared_from_this()));
}

void WsListener::on_accept(beast::error_code ec, tcp::socket socket){
    if (ec) {
        fail(ec, "accept");
    } else {
        // Create the session and run it
        std::make_shared<WsSession>(std::move(socket))->run();
    }

    // Accept another connection
    do_accept();
}


