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

    // param map for non-variant params
    std::vector<gdt::ServiceParam*> pmap;
};


static void fail(beast::error_code ec, char const *what) {
    std::cerr << what << ": " << ec.message() << "\n";
}

/**********************/
/* WebSockets session */
/**********************/
WsSession::WsSession(tcp::socket &&socket) : ws_(std::move(socket)) {}

template<class Body, class Allocator> 
void WsSession::do_accept(http::request<Body, http::basic_fields<Allocator>> req){
    // Set suggested timeout settings for the websocket
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    ws_.set_option(
        websocket::stream_base::decorator([](websocket::response_type &res) {
            res.set(http::field::server,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " mINK server");
        }));

    // Accept the websocket handshake
    ws_.async_accept(req, beast::bind_front_handler(&WsSession::on_accept,
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
            int id = jrpc.get_id();
            // check if method is supported
            if(jrpc.get_method_id() > -1){
                // tmp guid
                uint8_t guid_b[16];
                // push via gdt
                if (!gdt_push(jrpc, shared_from_this(), guid_b)) {
                    throw std::runtime_error("GDT push error!");

                // setup timeout
                }else{
                    std::shared_ptr<WsSession> ws = shared_from_this();
                    // timeout handler
                    std::thread tt_th([ws, guid_b, id] {
                        mink_utils::Guid g;
                        g.set(guid_b);
                        // sleep
                        sleep(2);
                        // get current guid (generated in gdt_push)
                        auto dd = static_cast<JsonRpcdDescriptor *>(mink::CURRENT_DAEMON);
                        // correlate guid
                        dd->cmap.lock();
                        JrpcPayload *pld = dd->cmap.get(g);
                        if (pld) {
                            dd->cmap.remove(g);
                            dd->cmap.unlock();
                            std::string th_rpl = json_rpc::JsonRpc::gen_err(-2, id).dump();
                            beast::flat_buffer &b = ws->get_buffer();
                            std::size_t th_rpl_sz = net::buffer_copy(b.prepare(th_rpl.size()),
                                                                     net::buffer(th_rpl));

                            ws->send_buff(b, th_rpl_sz);
                            return;
                        }
                        dd->cmap.unlock();
                    });
                    tt_th.detach();
                }
            }

        } catch (std::exception &e) {
            std::cout << e.what() << std::endl;
            ws_rpl = json_rpc::JsonRpc::gen_err(-1).dump();
            sz = net::buffer_copy(buffer_.prepare(ws_rpl.size()),
                                  net::buffer(ws_rpl));
            send_buff(buffer_, sz);
        }
    }
}

bool WsSession::gdt_push(const json_rpc::JsonRpc &jrpc, std::shared_ptr<WsSession> ws, uint8_t *guid){
    auto dd = static_cast<JsonRpcdDescriptor*>(mink::CURRENT_DAEMON);
    // local routing daemon pointer
    gdt::GDTClient *gdtc = nullptr;
    // smsg
    gdt::ServiceMessage *msg = nullptr;
    // payload
    //JrpcPayload *pld = nullptr;
    // randomizer
    mink_utils::Randomizer rand;

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
        return false;
    }
    // allocate new service message
    msg = dd->gdtsmm->new_smsg();
    // msg sanity check
    if (!msg) {
        // TODO stats
        return false;
    }

    // header and body
    //const gdt_grpc::Header &hdr = req.header();
    //const gdt_grpc::Body &bdy = req.body();

    // service id
    msg->set_service_id(47);

    // extra params
    EVUserCB *ev_usr_cb = nullptr;
    std::vector<gdt::ServiceParam*> *pmap = nullptr;

    // mandatory params
    // ================
    // - mink service id
    // - mink command id
    // - mink destination type

    // optional params
    // ===============
    // - mink destination id

    // service id
    msg->set_service_id(jrpc.get_mink_service_id());

    // command id (method from json rpc)
    msg->vpmap.erase_param(asn1::ParameterType::_pt_mink_command_id);
    msg->vpmap.set_int(asn1::ParameterType::_pt_mink_command_id,
                       jrpc.get_method_id());
    

    // process params
    jrpc.process_params([&ev_usr_cb, &pmap, msg, &jrpc](int id, const std::string &s) {
        if(s.size() > msg->vpmap.get_max()) {
            gdt::ServiceParam *sp = msg->get_smsg_manager()
                                       ->get_param_factory()
                                       ->new_param(gdt::SPT_OCTETS);
            if (sp) {
                // creat only once
                if (!ev_usr_cb) {
                    auto ev_usr_cb = new EVUserCB();
                    msg->params.set_param(3, ev_usr_cb);
                    pmap = &ev_usr_cb->pmap;
                }
                sp->set_data(s.c_str(), s.size());
                sp->set_id(id);
                sp->set_index(0);
                sp->set_extra_type(0);
                pmap->push_back(sp);
            }

        }else{
            // set gdt data 
            msg->vpmap.set_cstr(id, s.c_str());
        }
        
        return true;
    });

    // set source daemon type
    msg->vpmap.set_cstr(asn1::ParameterType::_pt_mink_daemon_type,
                        dd->get_daemon_type());
    // set source daemon id
    msg->vpmap.set_cstr(asn1::ParameterType::_pt_mink_daemon_id,
                        dd->get_daemon_id());

    // set credentials
    msg->vpmap.set_cstr(asn1::ParameterType::_pt_mink_auth_id,
                        usr_.c_str());
    msg->vpmap.set_cstr(asn1::ParameterType::_pt_mink_auth_password,
                        pwd_.c_str());

    // create payload object for correlation (grpc <-> gdt)
    JrpcPayload pld;
    
    // set correlation payload data
    pld.cdata = ws;
    pld.id = jrpc.get_id();
    // generate guid
    rand.generate(guid, 16);
    pld.guid.set(guid);
    msg->vpmap.set_octets(asn1::ParameterType::_pt_mink_guid, 
                          pld.guid.data(), 
                          16);
 
    // sync vpmap
    if (dd->gdtsmm->vpmap_sparam_sync(msg, pmap) != 0) {
        // TODO stats
        dd->gdtsmm->free_smsg(msg);
        return false;
    }

    // destination id
    const std::string *dest_id = jrpc.get_mink_did();

    // send service message
    int r = dd->gdtsmm->send(msg, 
                             gdtc, 
                             jrpc.get_mink_dtype().c_str(), 
                             (dest_id != nullptr ? dest_id->c_str() : nullptr),
                             true, 
                             &dd->ev_srvcm_tx);
    if (r) {
        // TODO stats
        dd->gdtsmm->free_smsg(msg);
        return false;
    }

    // save to correlarion map
    dd->cmap.lock();
    dd->cmap.set(pld.guid, pld);
    dd->cmap.unlock();

    return true;
}

beast::flat_buffer &WsSession::get_buffer(){
    return buffer_;
}

websocket::stream<beast::tcp_stream> &WsSession::get_tcp_stream(){
    return ws_;
}

void WsSession::set_credentials(const std::string &usr, const std::string &pwd){
    usr_.assign(usr);
    pwd_.assign(pwd);
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


/***************/
/* HttpSession */
/***************/
HttpSession::HttpSession(tcp::socket &&socket,
                         std::shared_ptr<std::string const> const &droot)
    : stream_(std::move(socket)), 
      droot_(droot) {}

void HttpSession::run(){
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&HttpSession::do_read,
                                            this->shared_from_this()));
}

void HttpSession::do_read(){
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(10000);

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request using the parser-oriented interface
    http::async_read(stream_, 
                     buffer_, 
                     *parser_,
                     beast::bind_front_handler(&HttpSession::on_read, 
                                               shared_from_this()));

}

static std::tuple<std::string, std::string, bool> user_auth(boost::string_view &auth_hdr){
    // check for "Basic"
    std::string::size_type n = auth_hdr.find("Basic");
    if (n == std::string::npos)
        return std::make_tuple("", "", false);
    // skip "Basic "
    auth_hdr.remove_prefix(6);
    // decode base64
    const std::size_t sz = base64::decoded_size(auth_hdr.size()); 
    std::vector<char> arr(sz);
    base64::decode(arr.data(), auth_hdr.data(), auth_hdr.size());

    // extract user and pwd hash 
    std::string user;
    std::string pwd;

    // split header
    for (auto it = arr.cbegin(); it != arr.cend(); ++it) {
        if (*it == ':') {
            // username
            user.assign(arr.cbegin(), it);
            // skip ':'
            ++it;
            // sanity check
            if (it == arr.cend())
                return std::make_tuple("", "", false);
            // pwd hash
            pwd.assign(it, arr.cend());
        }
    }
    // pwd sanity check
    if (pwd.size() < 6)
        return std::make_tuple("", "", false);

    // remove "\r\n"
    pwd.resize(pwd.size() - 2);

    // find user in db and auth
    auto dd = static_cast<JsonRpcdDescriptor*>(mink::CURRENT_DAEMON);
    // return credentials
    return std::make_tuple(user, pwd, dd->dbm.user_auth(user, pwd));
}

void HttpSession::on_read(beast::error_code ec, std::size_t bt){
    boost::ignore_unused(bt);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
        return do_close();

    if (ec)
        return fail(ec, "read");

    // See if it is a WebSocket Upgrade
    if (websocket::is_upgrade(parser_->get())) {
        auto dd = static_cast<JsonRpcdDescriptor*>(mink::CURRENT_DAEMON);
        // auth
        auto req = parser_->get();
        boost::string_view auth_hdr = req[http::field::authorization];
        // Auth header missing
        if(auth_hdr.empty()){
            return do_close();

        // Auth header found, verify
        }else{
            // connect with DB
            auto ua = user_auth(auth_hdr);
            if (!std::get<2>(ua))
                return do_close();

            // Create a websocket session, transferring ownership
            // of both the socket and the HTTP request.
            auto ws = std::make_shared<WsSession>(stream_.release_socket());
            // save session credentials 
            ws->set_credentials(std::get<0>(ua), std::get<1>(ua));
            // run session 
            ws->do_accept(parser_->release());
            return;
        }
    }

    // websockets only
    return do_close();
}

void HttpSession::on_write(beast::error_code ec, std::size_t bt){
    boost::ignore_unused(bt);

    if (ec)
        return fail(ec, "write");

    if (close) {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        return do_close();
    }

    // Read another request
    do_read();
}

void HttpSession::do_close(){
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
}


/***********************/
/* Connection listener */
/***********************/
Listener::Listener(net::io_context &ioc, 
                   tcp::endpoint endpoint,
                   std::shared_ptr<std::string const> const &droot) : ioc_(ioc), 
                                                                      acceptor_(ioc),
                                                                      droot_(droot) {
                                                                    
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


// start accepting connections
void Listener::run(){
    net::dispatch(acceptor_.get_executor(),
                  beast::bind_front_handler(&Listener::do_accept,
                                            this->shared_from_this()));

}

void Listener::do_accept(){
    // The new connection gets its own strand
    acceptor_.async_accept(net::make_strand(ioc_),
                           beast::bind_front_handler(&Listener::on_accept, 
                                                     shared_from_this()));
}

void Listener::on_accept(beast::error_code ec, tcp::socket socket){
    if (ec) {
        fail(ec, "accept");
    } else {
        // Create the session and run it
        std::make_shared<HttpSession>(std::move(socket), droot_)->run();
    }

    // Accept another connection
    do_accept();
}


