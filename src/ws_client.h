#pragma once

#include <vector>
#include <chrono>
#include <string>

struct ws_client_t{
    std::chrono::system_clock::time_point last_message_time;
    std::chrono::system_clock::time_point connect_time;
    std::string last_message;
};
