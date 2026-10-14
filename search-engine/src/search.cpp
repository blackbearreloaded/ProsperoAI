// SPDX-License-Identifier: GPL-3.0-or-later
// The whole search: cache, sequential fan-out under one deadline, merge.
// Providers run one after another so exactly one parse window is ever live.
#include "prospero_search.hpp"

#include "cache.hpp"
#include "scan.hpp"
#include "transport.hpp"

#ifndef PROSPERO_SEARCH_CACHE_PATH
#define PROSPERO_SEARCH_CACHE_PATH "/download0/prosperoai-search-cache.txt"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>

namespace prospero_search {
namespace {

bool wanted(const Request &request, const Provider &provider) {
    if (request.scopes.empty()) return true;
    return std::find(request.scopes.begin(), request.scopes.end(), provider.scope) !=
           request.scopes.end();
}

unsigned per_provider_results(const Request &request) {
    return std::min(kMaxPerProvider, std::max(1u, request.maximum));
}

Outcome classify(const transport::Outcome &outcome, const ParseState &state, unsigned found) {
    if (state.throttled) return Outcome::Throttled;
    if (found > 0) return Outcome::Ok;
    switch (outcome.result) {
    case transport::Outcome::Timeout:
    case transport::Outcome::Failed:
        return Outcome::Transport;
    case transport::Outcome::Unavailable:
        return Outcome::Unusable;
    default:
        break;
    }
    // 202 is how DuckDuckGo says "too many requests" while pretending to answer.
    if (outcome.http_status == 202 || outcome.http_status == 429) return Outcome::Throttled;
    if (outcome.http_status && outcome.http_status != 200) return Outcome::HttpStatus;
    return Outcome::Ok;
}

}  // namespace

bool search(const Request &request, Response *response) {
    if (!response) return false;
    response->results.clear();
    response->reports.clear();
    response->from_cache = false;
    response->error.clear();
    if (!valid_query(request.query)) {
        response->error = "Check the search query.";
        return false;
    }
    const unsigned maximum = std::min(kMaxResults, std::max(1u, request.maximum));
    const auto key = cache::key_for(request.query, request.scopes, maximum);
    const auto now = static_cast<long long>(std::time(nullptr));
    if (request.use_cache &&
        cache::load(PROSPERO_SEARCH_CACHE_PATH, key, now, cache::kTtlSeconds, &response->results)) {
        response->from_cache = true;
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max(3u, request.timeout_seconds));
    std::size_t count = 0;
    const auto *table = providers(&count);
    std::vector<const Provider *> queue;
    for (std::size_t i = 0; i < count; ++i)
        if (wanted(request, table[i])) queue.push_back(&table[i]);

    // Providers are asked a few at a time. Sequentially, one slow site would eat
    // the whole budget and the rest would never be asked; with a small pool the
    // peak cost stays a handful of 64 KB sessions instead of one 4 MB download
    // buffer, which is what the console's unified memory cares about.
    std::vector<Result> gathered;
    std::mutex guard;
    std::atomic<std::size_t> next{0};
    const auto per_provider = std::chrono::seconds(std::max(2u, request.provider_timeout_seconds));
    const auto ask = [&]() {
        for (;;) {
            const auto index = next.fetch_add(1);
            if (index >= queue.size()) return;
            const auto &provider = *queue[index];
            const auto now_point = std::chrono::steady_clock::now();
            ProviderReport report;
            report.id = provider.id;
            if (now_point >= deadline) {
                report.outcome = Outcome::Skipped;
                report.detail = "the search ran out of time before this provider";
                std::lock_guard<std::mutex> lock(guard);
                response->reports.push_back(std::move(report));
                continue;
            }
            const auto url = provider.url(request.query, per_provider_results(request));
            if (url.empty()) {
                report.outcome = Outcome::Skipped;
                std::lock_guard<std::mutex> lock(guard);
                response->reports.push_back(std::move(report));
                continue;
            }
            ParseState state;
            std::vector<Result> mine;
            transport::Spec spec;
            spec.accept = provider.accept;
            spec.user_agent = provider.user_agent;
            const auto own_deadline = std::min(deadline, now_point + per_provider);
            const auto outcome = transport::get(url, spec, own_deadline, kBodyLimit,
                                                [&](std::string_view chunk) {
                return provider.feed(&state, chunk, per_provider_results(request), &mine);
            });
            report.http_status = outcome.http_status;
            report.results = static_cast<unsigned>(mine.size());
            report.outcome = classify(outcome, state, report.results);
            report.detail = outcome.detail;
            std::lock_guard<std::mutex> lock(guard);
            response->reports.push_back(std::move(report));
            gathered.insert(gathered.end(), mine.begin(), mine.end());
        }
    };
    const auto workers = std::min<std::size_t>(kConcurrency, std::max<std::size_t>(queue.size(), 1));
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (std::size_t i = 1; i < workers; ++i) pool.emplace_back(ask);
    ask();
    for (auto &worker : pool) worker.join();
    // Reports come back in whatever order the pool finished; the table's order is
    // what a reader expects.
    std::sort(response->reports.begin(), response->reports.end(),
              [&](const ProviderReport &left, const ProviderReport &right) {
                  const auto rank = [&](const char *id) {
                      for (std::size_t i = 0; i < count; ++i)
                          if (std::string_view(table[i].id) == id) return i;
                      return count;
                  };
                  return rank(left.id) < rank(right.id);
              });

    merge(&gathered, maximum);
    response->results = std::move(gathered);
    if (response->results.empty()) {
        const bool throttled = std::any_of(response->reports.begin(), response->reports.end(),
                                           [](const ProviderReport &report) {
                                               return report.outcome == Outcome::Throttled;
                                           });
        // Nothing reachable at all means this is a host build, not a bad query:
        // say which, or the caller cannot tell the two apart.
        const bool unusable = !response->reports.empty() &&
                              std::all_of(response->reports.begin(), response->reports.end(),
                                          [](const ProviderReport &report) {
                                              return report.outcome == Outcome::Unusable;
                                          });
        response->error = unusable  ? "Search is available in the PS5 build only."
                          : throttled ? "Every search provider is rate-limiting this console right now."
                                      : "No search provider returned a usable result.";
        return false;
    }
    if (request.use_cache)
        cache::store(PROSPERO_SEARCH_CACHE_PATH, key, now, response->results, cache::kMaxEntries,
                     cache::kMaxBytes);
    return true;
}

bool fetch_page(const std::string &url, unsigned timeout_seconds, std::size_t limit,
                std::string *text, std::string *error) {
    if (!text) return false;
    text->clear();
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
        if (error) *error = "Only http and https addresses can be read.";
        return false;
    }
    if (url.size() > 2048 || url.find_first_of("\r\n\t <>\"") != std::string::npos) {
        if (error) *error = "That address cannot be read.";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max(3u, timeout_seconds));
    // Markup arrives in chunks and is turned into text as it comes, so the page
    // is never held in memory whole: only the text kept so far, up to `limit`.
    std::string pending;
    transport::Spec spec;
    const auto outcome = transport::get(url, spec, deadline, kBodyLimit,
                                        [&](std::string_view chunk) {
        pending.append(chunk);
        // Convert on tag boundaries so an element split across reads survives.
        const auto safe = pending.rfind('<');
        const auto convert = safe == std::string::npos ? pending.size() : safe;
        text->append(html_to_text(std::string_view(pending).substr(0, convert),
                                  limit > text->size() ? limit - text->size() : 0));
        pending.erase(0, convert);
        if (pending.size() > kWindowLimit) pending.erase(0, pending.size() - kWindowLimit);
        return text->size() < limit;
    });
    if (text->size() < limit && !pending.empty())
        text->append(html_to_text(pending, limit - text->size()));
    if (outcome.result == transport::Outcome::Unavailable) {
        if (error) *error = outcome.detail;
        return false;
    }
    if (outcome.http_status && outcome.http_status != 200 && text->empty()) {
        if (error) *error = "That page answered HTTP " + std::to_string(outcome.http_status) + ".";
        return false;
    }
    if (text->empty()) {
        if (error) *error = outcome.detail.empty() ? "That page had no readable text." : outcome.detail;
        return false;
    }
    if (error) error->clear();
    return true;
}

}  // namespace prospero_search
