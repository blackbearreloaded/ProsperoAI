// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "prospero_search.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace prospero_search::transport {

// Receives the body one socket read at a time. Returning false ends the
// transfer, so a parser that has enough results stops the download.
using Sink = std::function<bool(std::string_view)>;

// How one request should be shaped. The defaults suit every JSON API in the
// provider table; a provider overrides what it needs.
struct Spec {
    const char *accept = nullptr;      // e.g. "Accept: application/json"
    const char *user_agent = nullptr;  // when an API asks callers to identify themselves
    bool compression = true;           // fewer bytes on the wire and in the buffer
};

struct Outcome {
    enum Result { Complete, Stopped, Failed, Timeout, TooLarge, Unavailable } result = Failed;
    int http_status = 0;
    std::size_t bytes = 0;
    std::string detail;
};

Outcome get(const std::string &url, const Spec &spec, std::chrono::steady_clock::time_point deadline,
            std::size_t byte_limit, const Sink &sink);

}  // namespace prospero_search::transport
