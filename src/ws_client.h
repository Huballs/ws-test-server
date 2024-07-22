#pragma once

#include <vector>
#include <chrono>

struct ws_client_t{
    std::chrono::steady_clock::time_point last_message_time;
    std::chrono::steady_clock::time_point connect_time;
    std::string last_message;
};

extern std::vector<ws_client_t> ws_clients;