#pragma once

#include <boost/beast/core.hpp>
#include <vector>
#include <optional>

#include "util.hpp"

// extern payload_t ws_payload_settings_probes_1;
// extern payload_t ws_payload_settings_probes_2;
// extern payload_t ws_payload;

// return payload based on value of request
std::optional<std::vector<payload_t>> ws_get_payload (std::string_view request, bool include_bad_payloads);