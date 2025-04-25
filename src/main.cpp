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
#include "boost/random.hpp"
#include "boost/generator_iterator.hpp"
#include <boost/thread/mutex.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <format>
#include <vector>
#include <ctime>
#include <functional>
#include <thread>
#include <fstream>
#include <condition_variable>

#include "utl_log.hpp"

#include <windows.h>
#include "device_payloads.hpp"

namespace beast     = boost::beast;         // from <boost/beast.hpp>
namespace http      = beast::http;          // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
namespace net       = boost::asio;          // from <boost/asio.hpp>
namespace random    = boost::random;        // from <boost/random.hpp>
namespace ssl       = boost::asio::ssl;       // from <boost/asio/ssl.hpp>

using tcp           = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using resolver_result_t = boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>;

struct ws_state_t {

    // ws_state_t(websocket::stream<tcp::socket> ws, beast::flat_buffer buffer, std::string device_id) 
    // : ws(std::move(ws)), buffer(std::move(buffer)),   {}

    // ws_state_t(ws_state_t&& other) : ws(std::move(other.ws)), buffer(std::move(other.buffer)){
    //     device_id = other.device_id;
    //     connected = other.connected;
    //     last_run_time = other.last_run_time;
    // }

    websocket::stream<tcp::socket> ws;
    beast::flat_buffer buffer;
    std::string device_id;
    bool connected = false;

    std::optional<std::vector<payload_t>> extra_payload;

    std::chrono::steady_clock::time_point last_run_time;
};

// struct ws_states_t {
//     std::vector<ws_state_t> v_ws;
// };

const char* ws_path = "/socket-units-server/"; 
// constexpr long long time_between_packets = 4;
constexpr long long time_between_devices_ms = 20;

std::string time_and_date() {
    auto current_time = std::time(0);
    auto res = boost::lexical_cast<std::string>(std::put_time(std::gmtime(& current_time), "%Y-%m-%d %X"));
    res.insert(res.begin(), '[');
    res.push_back(']');
    return res;
}

void read_handler(beast::error_code ec, std::size_t bytes_transferred, ws_state_t& ws_state) {
    if(!ec) {
        UTL_LOG_DINFO("Read handler: count: ",  bytes_transferred, " Message: ", beast::make_printable(ws_state.buffer.data()));

        auto read_h = [&ws_state](beast::error_code ec, std::size_t bytes_transferred) {
            read_handler(ec, bytes_transferred, ws_state);
        };
    
        ws_state.ws.async_read(ws_state.buffer, beast::bind_front_handler(read_h));

    }
    else {
        UTL_LOG_DERR("Read handler error: ", ec.message());
    }

    ws_state.buffer.clear();
    
}

void write_handler(beast::error_code ec, size_t) {
    if(ec) {
        std::cout << time_and_date() << " write_handler err: " << ec.message() << "\n";
    }
}

struct ws_init_res_t{
    int port;
};

struct ws_conn_res_t {
    bool error = false;
};

void generate_random_ids(std::string filename, size_t count) {
    typedef boost::mt19937 RNGType;
    RNGType rng(std::random_device{}());
    boost::uniform_int<size_t> one_to_six( 1, 0xffffffffff);
    boost::variate_generator< RNGType, boost::uniform_int<size_t> >
                  dice(rng, one_to_six);
    
    std::fstream file(filename, std::ios::out | std::ios::trunc);

    if(!file.is_open()) {
        UTL_LOG_DERR("Failed to open file: ", filename);
        return;
    }

    for(int i = 0; i < count; i++) {
        // std::string id = std::to_string(dist(mt));
//         std::ostringstream ss;
// ss << std::setw(6) << std::setfill('0') << i;
        size_t d = dice();
        file << std::hex << std::setw(10) << std::setfill('0') << d << std::endl;
    }
}

ws_init_res_t ws_init(websocket::stream<tcp::socket>& ws, std::string device_id, std::string fw) {

    ws_init_res_t res{};

    // Set a decorator to change the User-Agent of the handshake
    ws.set_option(websocket::stream_base::decorator([device_id, fw](websocket::request_type& req) {
        req.set(http::field::user_agent,
            std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
        req.set("DeviceID", device_id);
        req.set("fw", fw);
    }));

    return res;
}

bool run_with_timeout(std::function<void()> f, std::chrono::milliseconds timeout, std::string_view text) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool done = false;

    auto wrapper = [&done, f]() {
        try {
            f();
        } catch(std::exception const& e) {
            UTL_LOG_DERR("Exception in timeout handler: ", e.what());
        }
        done = true;
    };

    auto t = new std::thread(wrapper);
    t->detach();

    while(!done) {
        if(std::chrono::steady_clock::now() > deadline) {
            UTL_LOG_DWARN("Timeout: ", text);
            break;
        }
    }

    delete t;

    return done;
}

void ws_async_read(ws_state_t& ws_state, bool send_bad_payloads) {
    auto handler = [&]() {
        while(ws_state.ws.is_open()) {
            try {
                ws_state.ws.read(ws_state.buffer);
                UTL_LOG_DNOTE("Async read: ", beast::make_printable(ws_state.buffer.data()), " ID: ", ws_state.device_id);
                ws_state.extra_payload = ws_get_payload(beast::buffers_to_string(ws_state.buffer.data()), send_bad_payloads);
            } catch(std::exception const& e) {
                UTL_LOG_DERR("Async read error: ", e.what(), "ID: ", ws_state.device_id);
            }
            ws_state.buffer.clear();
        }
    };

    std::thread t(handler);
    t.detach();
}

ws_conn_res_t ws_connect(std::string host, ws_state_t& ws_state, const resolver_result_t& resolver_result, bool send_bad_payloads) {
    ws_conn_res_t res{};

    try {

        UTL_LOG_DINFO("Connecting to: ", host, "ID: ", ws_state.device_id);
        auto ep      = net::connect(ws_state.ws.next_layer(), resolver_result);
        host += ':' + std::to_string(ep.port());

    } catch(std::exception const& e) {
        UTL_LOG_DERR("Connect Error: ", e.what(), "ID: ", ws_state.device_id);
        res.error = true;
        return res;
    }

    try {
        UTL_LOG_DINFO("Handshake, ID: ", ws_state.device_id);
        bool done = run_with_timeout(
            [&]() {
               ws_state.ws.handshake(host, ws_path);
            }, 
            std::chrono::milliseconds(8000),
            "Handshake"
        );

        if(!done) {
            res.error = true;
            return res;
        }

        // auto read_h = [&ws_state](beast::error_code ec, std::size_t bytes_transferred) {
        //     read_handler(ec, bytes_transferred, ws_state);
        // };

        // ws_state.ws.async_read(ws_state.buffer, beast::bind_front_handler(read_h));

        ws_async_read(ws_state, send_bad_payloads);
        

        res.error = false;
    } catch(std::exception const& e) {
        UTL_LOG_DERR("Handshake Error: ", e.what(), "ID: ", ws_state.device_id);
        res.error = true;
    }

    return res;
}

bool is_time(long long time, std::chrono::steady_clock::time_point& last_run_time) {

    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_run_time);
    // std::cout << time_and_date() << " elapsed: " << elapsed.count() << " DeviceId: " << ws_state.device_id << std::endl;
    if(elapsed < std::chrono::seconds(time)) {
        return false;
    }

    last_run_time = now;

    return true;
}

void ws_manage_ws(std::string host, ws_state_t& ws_state, resolver_result_t& resolver_result, long long time_between_packets, long long time_reconnect, bool send_bad_payloads) {

    if(ws_state.ws.is_open() && ws_state.extra_payload) {
        UTL_LOG_DINFO("Sending extra payload, ID:", ws_state.device_id);
        run_with_timeout(
            [&]() {
                for(auto& p : ws_state.extra_payload.value()) {
                    ws_state.ws.write(net::buffer(p));
                }
            }, 
            std::chrono::milliseconds(5000),
            "Write"
        );

        ws_state.extra_payload = std::nullopt;
    }

    if(!ws_state.connected || !ws_state.ws.is_open()) {

        if((ws_state.last_run_time == std::chrono::steady_clock::time_point{}) || is_time(time_between_packets, ws_state.last_run_time)) {

            auto conn_res = ws_connect(host, ws_state, resolver_result, send_bad_payloads);
            if(conn_res.error) {
                return;
            }

            ws_state.connected = true;
        }
    }

    if(!is_time(time_between_packets, ws_state.last_run_time)) return;

    ws_state.ws.binary(true);
    boost::beast::error_code ec;

    UTL_LOG_DDEBUG("Sending payload, ID:", ws_state.device_id);

    run_with_timeout(
        [&]() {
            auto payload = ws_get_payload("main_payload", send_bad_payloads);
            if(!payload) return;
            for(auto& p : payload.value()) {
                ws_state.ws.write(net::buffer(p));
            }
            // ws_state.ws.write(net::buffer(ws_get_payload("main_payload").value()), ec);
        }, 
        std::chrono::milliseconds(5000),
        "Write"
    );

    // ws_state.ws.async_write(net::buffer(ws_payload), beast::bind_front_handler(write_handler));

    if(ec || !ws_state.ws.next_layer().is_open()) {
        ws_state.connected = false;
        UTL_LOG_DWARN("Connection closed, ID: ", ws_state.device_id);
        //ws_state.ws.close(websocket::close_code::normal);

        run_with_timeout(
            [&]() {
                ws_state.ws.close(websocket::close_code::normal);
            }, 
            std::chrono::milliseconds(5000),
            "Close"
        );

    } else {
        //auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
        UTL_LOG_DDEBUG("Payload sent, ID: ", ws_state.device_id);
    }

}

void ws_manage_thread(std::string host, std::vector<ws_state_t>* ws_states, resolver_result_t& resolver_result, long long time_between_packets, long long time_reconnect, bool send_bad_payloads) {

    UTL_LOG_DINFO("Thread started, device count: ", ws_states->size());

    while(true) {
        for(auto& ws_state : *ws_states) {
            
            try {
                ws_manage_ws(host, ws_state, resolver_result, time_between_packets, time_reconnect, send_bad_payloads);
            } catch(std::exception const& e) {
                UTL_LOG_DERR("Exception: ", e.what(), " ID: ", ws_state.device_id);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(time_between_devices_ms));
        }

    }

    delete ws_states;
}

void print_usage() {
    std::cout << "Usage: websocket-client-sync <host> <port> <time-between-packets s> <time-reconnect s> <thread-count> <bad/no-bad> <ids-file>\n"
              << "bad - send bad payloads\n"
              << "Example:\n"
              << "    websocket-client-sync echo.websocket.org 80 30 10 4 bad ids.txt\n"
              << "\n"
              << "Usage: websocket-client-sync gen <ids-file> <count>"
              << std::endl;
}

int main(int argc, char** argv) {

    SetConsoleCP(CP_UTF8);

    std::string host;
    const char * port;
    const char * ids_file;
    const char * count;
    long long time_between_packets;
    long long time_reconnect;
    long long thread_count;
    bool bad_payloads = false;

    // Check command line arguments.
    if((argc == 8) && (std::string(argv[1]) != "gen")) {
        host = argv[1];
        port = argv[2];
        time_between_packets = std::stoi(argv[3]);
        time_reconnect = std::stoi(argv[4]);
        thread_count = std::stoi(argv[5]);
        if(std::string(argv[6]) == "bad") {
            bad_payloads = true;
        }
        ids_file = argv[7];

        if(thread_count < 1) {
            std::cout << "Thread count must be at least 1" << std::endl;
            return EXIT_FAILURE;
        }

    } else if((argc == 4) && (std::string(argv[1]) == "gen")) {
        ids_file = argv[2];
        count = argv[3];

        generate_random_ids(ids_file, std::stoi(count));

        return EXIT_SUCCESS;
    } else {
        print_usage();
        return EXIT_FAILURE;
    }

    net::io_context ws_states_ioc;
    std::thread t_ws {[&]() { ws_states_ioc.run(); }};
    t_ws.detach(); 

    tcp::resolver resolver{ ws_states_ioc};
    resolver_result_t resolver_result;

    net::io_context ioc;
    std::thread t{ [&] { ioc.run(); } };
    t.detach();

    try {
        resolver_result = resolver.resolve(host, port);
        UTL_LOG_DINFO("Resolver result: ", resolver_result.begin()->endpoint().address().to_string());
    } catch(std::exception const& e) {
        UTL_LOG_DERR("Resolver error: ", e.what());
        return EXIT_FAILURE;
    }

    std::fstream ids_file_stream(ids_file, std::ios::in | std::ios::app);

    if(!ids_file_stream.is_open()) {
        UTL_LOG_DERR("Failed to open file: ", ids_file);
        return EXIT_FAILURE;
    }

    auto line_count = std::count(std::istreambuf_iterator<char>(ids_file_stream), std::istreambuf_iterator<char>(), '\n') + 1;
    UTL_LOG_DINFO("File Line count: ", line_count);


    auto devices_per_thread = line_count / thread_count;

    ids_file_stream.seekg(0, std::ios::beg);

    std::vector<std::thread> ws_threads;
    std::vector<ws_state_t>* one_thread_ws_states = new std::vector<ws_state_t>();

    std::string line;

    long long actual_thread_count = 0;

    while(std::getline(ids_file_stream, line)) {
        ws_state_t ws_state{.ws = websocket::stream<tcp::socket>(ws_states_ioc),
                            .buffer = beast::flat_buffer{},
                            .device_id = line,
                            .connected = false
        };

        ws_init(ws_state.ws, line, "1.0.0");

        one_thread_ws_states->push_back(std::move(ws_state));

        if((one_thread_ws_states->size() == devices_per_thread) && (actual_thread_count < (thread_count-1))) {
            UTL_LOG_DINFO("Starting Thread with ", one_thread_ws_states->size(), " devices");

            ws_threads.emplace_back(std::thread{[&, one_thread_ws_states]() {
                ws_manage_thread(host, one_thread_ws_states, resolver_result, time_between_packets, time_reconnect, bad_payloads);
            }});

            ws_threads.back().detach();
            actual_thread_count++;
            one_thread_ws_states = new std::vector<ws_state_t>();
        }

    }

    if(one_thread_ws_states->size() > 0) {
        UTL_LOG_DINFO("Starting Thread with ", one_thread_ws_states->size(), " devices");
        ws_threads.emplace_back(std::thread{[&, one_thread_ws_states]() {
            ws_manage_thread(host, one_thread_ws_states, resolver_result, time_between_packets, time_reconnect, bad_payloads);
        }});

        ws_threads.back().detach();
        actual_thread_count++;
        one_thread_ws_states = nullptr;
    }

    UTL_LOG_DINFO("Actual Thread count: ", actual_thread_count);
    while(1){};

    for(auto& thread : ws_threads) {
        thread.join();
    }

    return EXIT_SUCCESS;
}

// Sends a WebSocket message and prints the response
// int main(int argc, char** argv) {

//     SetConsoleCP(CP_UTF8);

//     std::string host;
//     const char * port;
//     const char * ids_file;
//     const char * count;
//     long long time_between_packets;
//     long long time_reconnect;

//     // Check command line arguments.
//     if((argc == 6) && (std::string(argv[1]) != "gen")) {
//         host = argv[1];
//         port = argv[2];
//         time_between_packets = std::stoi(argv[3]);
//         time_reconnect = std::stoi(argv[4]);
//         ids_file = argv[5];
//     } else if((argc == 4) && (std::string(argv[1]) == "gen")) {
//         ids_file = argv[2];
//         count = argv[3];

//         generate_random_ids(ids_file, std::stoi(count));

//         return EXIT_SUCCESS;
//     } else {
//         print_usage();
//         return EXIT_FAILURE;
//     }

//     ws_states_t ws_states{};

//     tcp::resolver resolver{ ws_states.ioc};
//     resolver_result_t resolver_result;

//     net::io_context ioc;
//     std::thread t{ [&] { ioc.run(); } };
//     t.detach();

//     try {
//         resolver_result = resolver.resolve(host, port);
//         UTL_LOG_DINFO("Resolver result: ", resolver_result.begin()->endpoint().address().to_string());
//     } catch(std::exception const& e) {
//         UTL_LOG_DERR("Resolver error: ", e.what());
//         return EXIT_FAILURE;
//     }

//     std::fstream ids_file_stream(ids_file, std::ios::in | std::ios::app);

//     if(!ids_file_stream.is_open()) {
//         UTL_LOG_DERR("Failed to open file: ", ids_file);
//         return EXIT_FAILURE;
//     }

//     std::string line;
//     while(std::getline(ids_file_stream, line)) {
//         ws_state_t ws_state{.ws = websocket::stream<tcp::socket>(ws_states.ioc),
//                             .buffer = beast::flat_buffer{},
//                             .device_id = line,
//                             .connected = false
//         };

//         ws_init(ws_state.ws, line, "1.0.0");

//         ws_states.v_ws.push_back(std::move(ws_state));

//     }

//     std::thread t_ws {[&]() { ws_states.ioc.run(); }};
//     t_ws.detach(); 

//     while(true) {
//         for(auto& ws_state : ws_states.v_ws) {
//             try {
//                 ws_manage_thread(host, ws_state, resolver_result, time_between_packets, time_reconnect);
//             } catch(std::exception const& e) {
//                 UTL_LOG_DERR("Exception: ", e.what(), " ID: ", ws_state.device_id);
//             }

//             std::this_thread::sleep_for(std::chrono::milliseconds(time_between_devices_ms));
//         }
//     }

//     return EXIT_SUCCESS;
// }
