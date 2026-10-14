// SPDX-License-Identifier: GPL-3.0-or-later
// The network side: one bounded, streaming GET. Nothing here keeps a body; each
// chunk goes straight to a parser, and the transfer ends the moment the parser
// says it has enough. On a host build there is no network and every call fails
// with a clear reason, which keeps the parsers testable without a console.
#include "transport.hpp"

#if defined(PS5_DUAL_BACKEND) || defined(PS5_LLAMA_VULKAN)
#include "net/https_get.h"
#endif

namespace prospero_search::transport {

#if defined(PS5_DUAL_BACKEND) || defined(PS5_LLAMA_VULKAN)
namespace {
struct Deadline {
    std::chrono::steady_clock::time_point end;
};
int expired(void *opaque) {
    return std::chrono::steady_clock::now() >= static_cast<Deadline *>(opaque)->end;
}
}  // namespace

Outcome get(const std::string &url, const Spec &spec,
            std::chrono::steady_clock::time_point deadline, std::size_t byte_limit,
            const Sink &sink) {
    Outcome outcome;
    if (std::chrono::steady_clock::now() >= deadline) {
        outcome.result = Outcome::Timeout;
        outcome.detail = "no time left for this provider";
        return outcome;
    }
    // A session of its own, sized for an answer measured in kilobytes: the
    // downloader's 4 MB buffer would be borrowed from whatever model is loaded.
    auto *http = prospero_https_open_sized(kWindowLimit);
    if (!http) {
        outcome.result = Outcome::Failed;
        outcome.detail = "could not start the HTTPS client";
        return outcome;
    }
    Deadline stop{deadline};
    prospero_https_set_stop(http, expired, &stop);
    if (spec.user_agent) prospero_https_set_user_agent(http, spec.user_agent);
    if (spec.accept) prospero_https_add_header(http, spec.accept);
    prospero_https_set_compression(http, spec.compression ? 1 : 0);
    long status = 0;
    if (prospero_https_get(http, url.c_str(), &status) != 0) {
        const char *error = prospero_https_error(http);
        outcome.detail = error && *error ? error : "request failed";
        outcome.result = std::chrono::steady_clock::now() >= deadline ? Outcome::Timeout : Outcome::Failed;
        prospero_https_close(http);
        return outcome;
    }
    outcome.http_status = static_cast<int>(status);
    // One fixed chunk, reused for the whole transfer and released with it.
    std::vector<char> chunk(kChunkSize);
    std::size_t total = 0;
    for (;;) {
        const int count = prospero_https_read(http, chunk.data(), chunk.size());
        if (count < 0) {
            const char *error = prospero_https_error(http);
            outcome.detail = error && *error ? error : "read failed";
            outcome.result = Outcome::Failed;
            break;
        }
        if (count == 0) {
            outcome.result = Outcome::Complete;
            break;
        }
        total += static_cast<std::size_t>(count);
        if (!sink(std::string_view(chunk.data(), static_cast<std::size_t>(count)))) {
            outcome.result = Outcome::Stopped;  // the parser has what it needs
            break;
        }
        if (total > byte_limit) {
            outcome.result = Outcome::TooLarge;
            outcome.detail = "body exceeded its ceiling";
            break;
        }
    }
    outcome.bytes = total;
    prospero_https_close(http);
    return outcome;
}

#else

Outcome get(const std::string &, const Spec &, std::chrono::steady_clock::time_point, std::size_t,
            const Sink &) {
    Outcome outcome;
    outcome.result = Outcome::Unavailable;
    outcome.detail = "search needs the console build";
    return outcome;
}

#endif

}  // namespace prospero_search::transport
