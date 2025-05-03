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

//#include <windows.h>
#include "device_payloads.hpp"
#include "util.hpp"
#include "coro_read.hpp"

namespace beast     = boost::beast;         // from <boost/beast.hpp>
namespace http      = beast::http;          // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
namespace net       = boost::asio;          // from <boost/asio.hpp>
//namespace random    = boost::random;        // from <boost/random.hpp>
namespace ssl       = boost::asio::ssl;       // from <boost/asio/ssl.hpp>

using tcp           = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using resolver_result_t = boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>;

// jj,,
// const char* ws_path = "/socket-units-server/"; 
// constexpr long long time_between_packets = 4;

// void read_handler(beast::error_code ec, std::size_t bytes_transferred, ws_state_t& ws_state) {
//     if(!ec) {
//         UTL_LOG_DINFO("Read handler: count: ",  bytes_transferred, " Message: ", beast::make_printable(ws_state.buffer.data()));

//         auto read_h = [&ws_state](beast::error_code ec, std::size_t bytes_transferred) {
//             read_handler(ec, bytes_transferred, ws_state);
//         };
    
//         ws_state.ws.async_read(ws_state.buffer, beast::bind_front_handler(read_h));

//     }
//     else {
//         UTL_LOG_DERR("Read handler error: ", ec.message());
//     }

//     ws_state.buffer.clear();
    
// }

void generate_random_ids(std::string filename, size_t count) {
    typedef boost::mt19937 RNGType;
    RNGType rng(std::random_device{}());
    boost::uniform_int<size_t> one_to_six( 1, 0xffffffffff);
    boost::variate_generator< RNGType, boost::uniform_int<size_t> >
                  dice(rng, one_to_six);
    
    std::fstream file(filename, std::ios::out | std::ios::trunc);

    if(!file.is_open()) {
        UTL_LOG_ERR("Failed to open file: ", filename);
        return;
    }

    for(int i = 0; i < count; i++) {
        
        size_t d = dice();
        file << std::hex << std::setw(10) << std::setfill('0') << d << std::endl;
    }
}

void ws_init(websocket::stream<tcp::socket>& ws, std::string device_id, std::string fw) {

    // Set a decorator to change the User-Agent of the handshake
    ws.set_option(websocket::stream_base::decorator([device_id, fw](websocket::request_type& req) {
        req.set(http::field::user_agent,
            std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
        req.set("DeviceID", device_id);
        req.set("fw", fw);
    }));
}

void ws_async_read(ws_state_t& ws_state, bool send_bad_payloads) {
    auto handler = [&]() {
        while(1) {
            try {
                if(ws_state.ws.is_open()) {
                    ws_state.ws.read(ws_state.buffer);
                    UTL_LOG_DNOTE("Async read: ", beast::make_printable(ws_state.buffer.data()), " ID: ", ws_state.device_id);
                    LOCK_GUARD(*ws_state.ws_state_mutex);
                    ws_state.extra_payload = ws_get_payload(beast::buffers_to_string(ws_state.buffer.data()), send_bad_payloads);
                }
            } catch(std::exception const& e) {
                UTL_LOG_ERR("Async read error: ", e.what(), "ID: ", ws_state.device_id);
            }
            ws_state.buffer.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };

    std::thread t(handler);
    t.detach();
}

ws_conn_res_t ws_connect(std::string_view original_host, std::string_view path
                            , ws_state_t& ws_state, const resolver_result_t& resolver_result
                            , bool send_bad_payloads
                        ) {
    ws_conn_res_t res{};

    std::string host{original_host};

    try {

        UTL_LOG_DINFO("Connecting to: ", host, "ID: ", ws_state.device_id);
        auto ep      = net::connect(ws_state.ws.next_layer(), resolver_result);
        host += ':' + std::to_string(ep.port());

    } catch(std::exception const& e) {
        UTL_LOG_ERR("Connect Error: ", e.what(), "ID: ", ws_state.device_id);
        res.error = true;
        return res;
    }

    try {
        UTL_LOG_DINFO("Handshake, ID: ", ws_state.device_id);
        bool done = run_with_timeout(
            [&]() {
               ws_state.ws.handshake(host, path);
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
        UTL_LOG_ERR("Handshake Error: ", e.what(), "ID: ", ws_state.device_id);
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

void ws_manage_ws(std::string_view host, std::string_view path, ws_state_t& ws_state, resolver_result_t& resolver_result
                    , long long time_between_packets, long long time_reconnect
                    , bool send_bad_payloads, bool send_events
                ) {

    if(ws_state.ws.is_open() && ws_state.extra_payload) {
        UTL_LOG_DINFO("Sending extra payload, ID:", ws_state.device_id);
        run_with_timeout(
            [&]() {
                LOCK_GUARD(*ws_state.ws_state_mutex);
                for(auto& p : ws_state.extra_payload.value()) {
                    ws_state.ws.write(net::buffer(p));
                }
            }, 
            std::chrono::milliseconds(5000),
            "Extra payload Write"
        );

        ws_state.extra_payload = std::nullopt;
    }

    if(!ws_state.connected || !ws_state.ws.is_open()) {

        if((ws_state.last_run_time == std::chrono::steady_clock::time_point{}) || is_time(time_between_packets, ws_state.last_run_time)) {

            auto conn_res = ws_connect(host, path, ws_state, resolver_result, send_bad_payloads);
            if(conn_res.error) {
                return;
            }

            ws_state.connected = true;
        }
    }

    if(!is_time(time_between_packets, ws_state.last_run_time)) return;

    ws_state.ws.binary(true);
    boost::beast::error_code ec;

    if(send_events) {
        UTL_LOG_DDEBUG("Sending event payload, ID:", ws_state.device_id);
        run_with_timeout(
            [&]() {
                auto payload = ws_get_payload("event", send_bad_payloads);
                if(!payload) return;

                //LOCK_GUARD(*ws_state.ws_state_mutex);
                for(auto& p : payload.value()) {
                    ws_state.ws.write(net::buffer(p));
                }
                // ws_state.ws.write(net::buffer(ws_get_payload("main_payload").value()), ec);
            }, 
            std::chrono::milliseconds(5000),
            "Event write"
        );
    }

    UTL_LOG_DDEBUG("Sending main payload, ID:", ws_state.device_id);
    run_with_timeout(
        [&]() {
            auto payload = ws_get_payload("main_payload", send_bad_payloads);
            if(!payload) return;
            for(auto& p : payload.value()) {

                //LOCK_GUARD(*ws_state.ws_state_mutex);
                ws_state.ws.write(net::buffer(p));
            }
            // ws_state.ws.write(net::buffer(ws_get_payload("main_payload").value()), ec);
        }, 
        std::chrono::milliseconds(5000),
        "Main payload write"
    );

    // ws_state.ws.async_write(net::buffer(ws_payload), beast::bind_front_handler(write_handler));

    // if(ec || !ws_state.ws.next_layer().is_open()) {
    //     ws_state.connected = false;
    //     UTL_LOG_DWARN("Connection closed, ID: ", ws_state.device_id);
    //     //ws_state.ws.close(websocket::close_code::normal);

    //     run_with_timeout(
    //         [&]() {
    //             ws_state.ws.close(websocket::close_code::normal);
    //         }, 
    //         std::chrono::milliseconds(5000),
    //         "Close"
    //     );

    if (ws_state.ws.next_layer().is_open()) {
        //auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
        UTL_LOG_DDEBUG("Payload sent, ID: ", ws_state.device_id);
    }

}

void ws_manage_thread(std::string host, std::string path, std::vector<ws_state_t>* ws_states
    , resolver_result_t& resolver_result, long long time_between_packets
    , long long time_reconnect, bool send_bad_payloads
    , bool send_events
    ) {

    UTL_LOG_DINFO("Thread started, device count: ", ws_states->size());

    while(true) {
        for(auto& ws_state : *ws_states) {
            
            try {
                ws_manage_ws(host, path, ws_state, resolver_result, time_between_packets, time_reconnect, send_bad_payloads, send_events);
            } catch(std::exception const& e) {
                UTL_LOG_ERR("Exception: ", e.what(), " ID: ", ws_state.device_id);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(time_between_devices_ms));
        }

    }

    delete ws_states;
}

void print_usage() {
    std::cout << "Usage: websocket-client-sync <host> <path> <port> <time-between-packets s> <time-reconnect s> <thread-count> <bad/no-bad> <events/no-events> <ids-file>\n"
              << "      bad - send bad payloads\n"
              << "Example:\n"
              << "      ws-test-client.exe test.secbuild.ru /socket-units-server/ 81 30 10 4 no-bad events ids.txt\n"
              << "\n"
              << "Usage: websocket-client-sync gen <ids-file> <count>"
              << std::endl;
}

int main(int argc, char** argv) {

//    SetConsoleCP(CP_UTF8);

    std::string host;
    std::string path;
    const char * port;
    const char * ids_file;
    const char * count;
    long long time_between_packets;
    long long time_reconnect;
    long long thread_count;
    bool bad_payloads = false;
    bool send_events = false;

    // Check command line arguments.
    if((argc == 10) && (std::string(argv[1]) != "gen")) {
        host = argv[1];
        path = argv[2];
        port = argv[3];
        time_between_packets = std::stoi(argv[4]);
        time_reconnect = std::stoi(argv[5]);
        thread_count = std::stoi(argv[6]);
        if(std::string(argv[7]) == "bad") {
            bad_payloads = true;
        }

        if(std::string(argv[8]) == "events") {
            send_events = true;
        }
        ids_file = argv[9];

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
        UTL_LOG_INFO("Resolver result: ", resolver_result.begin()->endpoint().address().to_string());
    } catch(std::exception const& e) {
        UTL_LOG_ERR("Resolver error: ", e.what());
        return EXIT_FAILURE;
    }

    std::fstream ids_file_stream(ids_file, std::ios::in);

    if(!ids_file_stream.is_open()) {
        UTL_LOG_ERR("Failed to open file: ", ids_file);
        return EXIT_FAILURE;
    }

    auto line_count = std::count(std::istreambuf_iterator<char>(ids_file_stream), std::istreambuf_iterator<char>(), '\n') + 1;
    UTL_LOG_INFO("File Line count: ", line_count);


    auto devices_per_thread = line_count / thread_count;

    ids_file_stream.seekg(0, std::ios::beg);

    std::vector<std::thread> ws_threads;
    std::vector<ws_state_t>* one_thread_ws_states = new std::vector<ws_state_t>();

    std::string line;

    long long actual_thread_count = 0;

    while(std::getline(ids_file_stream, line)) {
        ws_state_t ws_state{.ws_state_mutex = std::make_unique<std::mutex>(),
                            .ws = websocket::stream<tcp::socket>(ws_states_ioc),
                            .buffer = beast::flat_buffer{},
                            .device_id = line,
                            .connected = false
        };

        ws_init(ws_state.ws, line, "1.0.0");

        one_thread_ws_states->push_back(std::move(ws_state));

        if((one_thread_ws_states->size() == devices_per_thread) && (actual_thread_count < (thread_count-1))) {
            UTL_LOG_INFO("Starting Thread with ", one_thread_ws_states->size(), " devices");

            ws_threads.emplace_back(std::thread{[&, one_thread_ws_states]() {
                ws_manage_thread(host, path,  one_thread_ws_states, resolver_result, time_between_packets, time_reconnect, bad_payloads, send_events);
            }});

            ws_threads.back().detach();
            actual_thread_count++;
            one_thread_ws_states = new std::vector<ws_state_t>();
        }

    }

    if(one_thread_ws_states->size() > 0) {
        UTL_LOG_INFO("Starting Thread with ", one_thread_ws_states->size(), " devices");
        ws_threads.emplace_back(std::thread{[&, one_thread_ws_states]() {
            ws_manage_thread(host, path, one_thread_ws_states, resolver_result, time_between_packets, time_reconnect, bad_payloads, send_events);
        }});

        ws_threads.back().detach();
        actual_thread_count++;
        one_thread_ws_states = nullptr;
    }

    UTL_LOG_INFO("Actual Thread count: ", actual_thread_count);
    while(1){};

    for(auto& thread : ws_threads) {
        thread.join();
    }

    return EXIT_SUCCESS;
}
