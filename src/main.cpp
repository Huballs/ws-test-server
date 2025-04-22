//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: WebSocket client, synchronous
//
//------------------------------------------------------------------------------

//[example_websocket_client

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <format>
#include <vector>
#include <ctime>
#include <functional>
#include <thread>
#include<windows.h>

namespace beast     = boost::beast;         // from <boost/beast.hpp>
namespace http      = beast::http;          // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
namespace net       = boost::asio;          // from <boost/asio.hpp>
using tcp           = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using resolver_result_t = boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>;


const char* ws_path = "/socket-units-server/"; 
constexpr long long time_between_packets = 5;

std::vector<uint8_t> ws_payload = {
    250, 2, 32, 3, 224, 4, 32, 5, 32, 9, 32, 10, 32, 11, 224, 6, 128, 0, 0, 0, 0, 7, 128, 100, 0, 0, 0, 8, 192, 139, 15, 27, 66, 1, 8, 192, 204, 240, 0, 0, 8, 192, 0, 0, 0, 0, 8, 192, 0, 0, 0, 0, 8, 192, 10, 164, 160, 64, 8, 192, 0, 0, 0, 0, 8, 192, 0, 0, 0, 0, 8, 192, 0, 0, 0, 0, 65, 192, 51, 51, 211, 65, 65, 193, 31, 133, 217, 65, 65, 66, 0, 0, 0, 0, 65, 67, 0, 0, 0, 0, 65, 196, 0, 0, 160, 64, 65, 69, 0, 0, 0, 0, 66, 192, 61, 10, 207, 65, 66, 193, 143, 194, 163, 65, 66, 194, 124, 188, 59, 68, 66, 195, 176, 246, 218, 64, 66, 68, 0, 0, 0, 0, 66, 69, 0, 0, 0, 0, 26, 148
};

std::string time_and_date() {
    auto current_time = std::time(0);
    auto res = boost::lexical_cast<std::string>(std::put_time(std::gmtime(& current_time), "%Y-%m-%d %X"));
    res.insert(res.begin(), '[');
    res.push_back(']');
    return res;
}

net::io_context ioc;
websocket::stream<tcp::socket> ws{ ioc };
beast::flat_buffer buffer;

void read_handler(beast::error_code ec, std::size_t bytes_transferred) {
    if(!ec) {
        std::cout << time_and_date() << " read_handler: " << beast::make_printable(buffer.data()) << std::endl;
        buffer.clear();
        ws.async_read(buffer, beast::bind_front_handler(read_handler));
    }
    else {
        std::cout << time_and_date() << " read_handler err: " << ec.message() << "\n";
        std::cout << time_and_date() << " reason: " << ws.reason() << "\n";
        buffer.clear();
    }
    
}

struct ws_init_res_t{
    int port;
};

struct ws_conn_res_t {
    bool error = false;
};

ws_init_res_t ws_init(websocket::stream<tcp::socket>& ws) {

    ws_init_res_t res{};

    // Set a decorator to change the User-Agent of the handshake
    ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(http::field::user_agent,
            std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
        req.set("DeviceID", " 18ADDF88EE");
        req.set("fw", " 1.0.0");
    }));

    return res;
}

ws_conn_res_t ws_connect(std::string host, websocket::stream<tcp::socket>& ws, net::io_context& ioc, beast::flat_buffer& buffer, resolver_result_t& resolver_result) {
    ws_conn_res_t res{};

    try {
        auto ep      = net::connect(ws.next_layer(), resolver_result);
    
        host += ':' + std::to_string(ep.port());
        
        ws.handshake(host, ws_path);
    
        ws.async_read(buffer, beast::bind_front_handler(read_handler));
        std::cout << time_and_date() << " Reconnect" << std::endl;
        res.error = false;
    } catch(std::exception const& e) {
        std::cout << time_and_date() << " Reconnect error: " << e.what() <<  std::endl;
        res.error = true;
    }

    return res;
}

void ws_manage_thread(std::string host, net::io_context& ioc, websocket::stream<tcp::socket>& ws, beast::flat_buffer& buffer, resolver_result_t& resolver_result) {


    auto res = ws_connect(host, ws, ioc, buffer, resolver_result);
    std::thread t{ [&] { ioc.run(); } };
    t.detach();
    while(true) {

        if(res.error) {
            res = ws_connect(host, ws, ioc, buffer, resolver_result);
            std::this_thread::sleep_for(std::chrono::seconds(time_between_packets));
            continue;
        }

        ws.binary(true);
        boost::beast::error_code ec;
        auto bytes = ws.write(net::buffer(ws_payload), ec);

        if(ec || !ws.next_layer().is_open() || (bytes == 0)) {
            res.error = true;
        } else {
            //auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
            std::cout << time_and_date() << " payload sent" << std::endl;
        }

        // std::cout << beast::make_printable(buffer.data()) << std::endl;
        ec.clear();

        std::this_thread::sleep_for(std::chrono::seconds(time_between_packets));
    }
}

// Sends a WebSocket message and prints the response
int main(int argc, char** argv) {

    SetConsoleOutputCP(CP_UTF8);

    // Check command line arguments.
    if(argc != 4) {
        std::cerr << "Usage: websocket-client-sync <host> <port> <text>\n"
                  << "Example:\n"
                  << "    websocket-client-sync echo.websocket.org 80 \"Hello, world!\"\n";
        return EXIT_FAILURE;
    }
    std::string host = argv[1];
    auto const port  = argv[2];
    auto const text  = argv[3];

    // The io_context is required for all I/O
    //net::io_context ioc;

    // These objects perform our I/O
    tcp::resolver resolver{ ioc };
    //websocket::stream<tcp::socket> ws{ ioc };

    // Look up the domain name
    std::cout << time_and_date() << "Looking up " << host << " on port " << port <<  std::endl;

    resolver_result_t resolver_result;

    try {
        resolver_result = resolver.resolve(host, port);
    } catch(std::exception const& e) {
        std::cerr << "Resolve Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Resolver result: " << (resolver_result.begin()->endpoint().address().to_string()) << std::endl;

    ws_init(ws);

    ws_manage_thread(host, ioc, ws, buffer, resolver_result);

    // // Perform the websocket handshake
    // ws.handshake(host, ws_path);

    // // This buffer will hold the incoming message
    
    // std::thread t{ [&] { ioc.run(); } };
    // t.detach();

    // ws.async_read(buffer, beast::bind_front_handler(read_handler));

    // size_t reconnect_count = 0;

    // while(1) {
    //     ws.binary(true);
    //     boost::beast::error_code ec;
    //     ws.write(net::buffer(ws_payload), ec);

    //     if(ec || !ws.next_layer().is_open()) {
            
    //         if(ec)
    //             std::cout << time_and_date() << " Error: " << ec.message() << std::endl;
    //         else
    //             std::cout << time_and_date() << "!ws.next_layer().is_open()" << std::endl;

    //         std::cout << time_and_date() << " reason: " << ws.reason() << std::endl;

    //         try {
    //             // ws.close(websocket::close_code::normal);

    //             //results = resolver.resolve(host, port);
    //             ep      = net::connect(ws.next_layer(), results);
    //             host += ':' + std::to_string(ep.port());

    //             ws.handshake(host, ws_path);
    //             ws.async_read(buffer, beast::bind_front_handler(read_handler));
    //             reconnect_count++;
    //             std::cout << time_and_date() << "Reconnect" << std::endl;
    //         } catch(std::exception const& e) {
    //             std::cout << time_and_date() << "Reconnect error: " << e.what() << std::endl;
    //         }
    //         ec.clear();
    //     } else {
    //         //auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
    //         std::cout << time_and_date() << " Sent, rc: " << reconnect_count << std::endl;
    //     }

    //     // std::cout << beast::make_printable(buffer.data()) << std::endl;

    //     std::this_thread::sleep_for(std::chrono::seconds(30));
    // }

    // // Send the message
    // ws.write(net::buffer(std::string(text)));

    // // Read a message into our buffer
    // ws.read(buffer);

    // // Close the WebSocket connection
    // ws.close(websocket::close_code::normal);

    // // If we get here then the connection is closed gracefully

    // // The make_printable() function helps print a ConstBufferSequence
    // std::cout << beast::make_printable(buffer.data()) << std::endl;

    return EXIT_SUCCESS;
}
