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
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>
#include <json_rpc.h>

// boost beast/asio
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// WebSockets session
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    explicit WsSession(tcp::socket&& socket);
    WsSession(const WsSession &o) = delete;
    ~WsSession() = default;
    const WsSession &operator=(const WsSession &o) = delete;

    void run();
    void on_run();
    void on_accept(beast::error_code ec);
    void do_read();
    void on_read(beast::error_code ec, std::size_t bt);
    void on_write(beast::error_code ec, std::size_t bt);
    void send_buff(beast::flat_buffer &b, std::size_t sz);
    void gdt_push(const json_rpc::JsonRpc &jrpc, const WsSession *ws);

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
};

// WebSockets connection listener
class WsListener : public std::enable_shared_from_this<WsListener> {
public:
    WsListener(net::io_context &ioc, tcp::endpoint endpoint);
    WsListener(const WsListener &o) = delete;
    ~WsListener() = default;
    const WsListener &operator=(const WsListener &o) = delete;

    void run();

private:
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

    net::io_context &ioc_;
    tcp::acceptor acceptor_;
};

#endif /* ifndef MINK_WS_SERVER_H */
