/*            _       _
 *  _ __ ___ (_)_ __ | | __
 * | '_ ` _ \| | '_ \| |/ /
 * | | | | | | | | | |   <
 * |_| |_| |_|_|_| |_|_|\_\
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef MINK_WS_SERVER_H
#define MINK_WS_SERVER_H 

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>
#include <json_rpc.h>
#include <gdt_utils.h>

// boost beast/asio
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace base64 = boost::beast::detail::base64;
using tcp = boost::asio::ip::tcp;

/**********************/
/* WebSockets session */
/**********************/
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    explicit WsSession(tcp::socket&& socket);
    WsSession(const WsSession &o) = delete;
    ~WsSession() = default;
    const WsSession &operator=(const WsSession &o) = delete;

    void on_accept(beast::error_code ec);
    template<class Body, class Allocator> 
    void do_accept(http::request<Body, http::basic_fields<Allocator>> req);

    void do_read();
    void on_read(beast::error_code ec, std::size_t bt);
    void on_write(beast::error_code ec, std::size_t bt);
    void send_buff(beast::flat_buffer &b, std::size_t sz);
    bool gdt_push(const json_rpc::JsonRpc &jrpc, std::shared_ptr<WsSession> ws, uint8_t *guid);
    beast::flat_buffer &get_buffer();
    websocket::stream<beast::tcp_stream> &get_tcp_stream();
    void set_credentials(const std::string &usr, const std::string &pwd);

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string usr_;
    std::string pwd_;
};

/***************/
/* HttpSession */
/***************/
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(tcp::socket&& socket,
                         std::shared_ptr<std::string const> const &droot);
    HttpSession(const HttpSession &o) = delete;
    ~HttpSession() = default;
    const HttpSession &operator=(const HttpSession &o) = delete;

    void run();

private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bt);
    void on_write(beast::error_code ec, std::size_t bt);
    void do_close();

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<std::string const> droot_;
    boost::optional<http::request_parser<http::string_body>> parser_;
};

/***********************/
/* Connection listener */
/***********************/
class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context &ioc, 
             tcp::endpoint endpoint,
             std::shared_ptr<std::string const> const &droot);
    Listener(const Listener &o) = delete;
    ~Listener() = default;
    const Listener &operator=(const Listener &o) = delete;

    void run();

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

    net::io_context &ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::string const> droot_;
};

#endif /* ifndef MINK_WS_SERVER_H */
