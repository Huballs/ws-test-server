#pragma once

#include <vector>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>
#include <memory>

#include <boost/beast/websocket.hpp>
#include <boost/beast.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>

#include "utl_log.hpp"

using resolver_result_t = boost::asio::ip::basic_resolver_results<boost::asio::ip::tcp>;
using payload_t = std::vector<uint8_t>;

#define LOCK_GUARD(__mutex) std::lock_guard<std::mutex> guard(__mutex)

constexpr long long time_between_devices_ms = 20;

struct ws_state_t {

    std::unique_ptr<std::mutex> ws_state_mutex;

    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws;
    boost::beast::flat_buffer buffer;

    std::string device_id;
    bool connected = false;

    std::optional<std::vector<payload_t>> extra_payload;

    std::chrono::steady_clock::time_point last_run_time;
};

struct ws_conn_res_t {
    bool error = false;
};

bool run_with_timeout(std::function<void()> f, std::chrono::milliseconds timeout, std::string_view text);

// std::string time_and_date() {
//     auto current_time = std::time(0);
//     auto res = boost::lexical_cast<std::string>(std::put_time(std::gmtime(& current_time), "%Y-%m-%d %X"));
//     res.insert(res.begin(), '[');
//     res.push_back(']');
//     return res;
// }
