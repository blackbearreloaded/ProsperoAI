// SPDX-License-Identifier: GPL-3.0-or-later
// ProsperoAI - multi-site search that runs on the console.
//
// Providers are plain structs with a URL builder and an incremental parser, so
// each one is testable on the host from a captured body and none of them ever
// holds a whole response in memory. See
// docs/superpowers/specs/2026-10-14-search-engine-design.md.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prospero_search {

// One result, normalized across every provider.
struct Result {
    std::string title;
    std::string url;
    std::string snippet;
    std::string source;   // provider id, e.g. "code.github"
    unsigned position = 0; // 1-based rank the provider gave it
    double score = 0.0;    // filled in by merge()
};

// What a search may ask for. A provider belongs to exactly one scope.
enum class Scope { Web, Code, Video, Reference, Qa, Package, Paper, Music, Image, Count };

const char *scope_name(Scope scope);
bool scope_from_name(std::string_view name, Scope *scope);

// Why a provider produced nothing, so the caller can tell throttling from an
// empty index.
enum class Outcome { Ok, Throttled, HttpStatus, Transport, Unusable, Skipped };

struct ProviderReport {
    const char *id = "";
    Outcome outcome = Outcome::Skipped;
    int http_status = 0;
    unsigned results = 0;
    std::string detail;
};

// Carries a parser between chunks: the unconsumed tail of the body and the
// provider's own small counters. Bounded by kWindowLimit.
struct ParseState {
    std::string window;   // bytes before the records start, or an HTML window
    std::string record;   // the record being read; never a whole body
    unsigned found = 0;   // results handed over so far
    int depth = 0;        // scanner-private
    unsigned flags = 0;   // scanner-private
    bool throttled = false;
    bool done = false;
};

constexpr std::size_t kWindowLimit = 64u * 1024u;  // sliding window ceiling
constexpr std::size_t kChunkSize = 16u * 1024u;    // one read from the socket
constexpr std::size_t kBodyLimit = 256u * 1024u;   // give up on a body this long
constexpr unsigned kMaxResults = 25u;              // across all providers
constexpr unsigned kMaxPerProvider = 5u;
// How many providers are asked at once. Each one costs its own small session, so
// the peak stays well under a megabyte however many sites the table grows to.
constexpr std::size_t kConcurrency = 6;

struct Provider {
    const char *id;
    Scope scope;
    double weight;
    // Sent with the request when set: some APIs answer JSON only when asked for
    // it, and a few ask callers to identify themselves.
    const char *accept;
    const char *user_agent;
    // Builds the request URL. Returns an empty string when the query cannot be
    // expressed for this provider.
    std::string (*url)(const std::string &query, unsigned maximum);
    // Consumes one chunk. Appends at most `maximum` results. Returns false when
    // it wants no more of the body, which ends the transfer early.
    bool (*feed)(ParseState *state, std::string_view chunk, unsigned maximum,
                 std::vector<Result> *out);
};

// The provider table, in query order.
const Provider *providers(std::size_t *count);
const Provider *provider_by_id(std::string_view id);

// --- pieces, all usable and testable without a console ---

bool valid_query(const std::string &query);

// Canonical form used for deduplication: scheme and host lowercased, a leading
// "www." and a trailing slash removed, fragment and utm_* parameters dropped.
std::string normalize_url(std::string_view url);

// Scores and deduplicates results gathered from several providers, highest
// first, and truncates to `maximum`. A result several providers returned ranks
// above one only a single provider found.
void merge(std::vector<Result> *results, unsigned maximum);

// Feeds a whole body through a provider in kChunkSize pieces. Tests use it; the
// transport uses the same parser chunk by chunk.
bool parse_body(const Provider &provider, std::string_view body, unsigned maximum,
                std::vector<Result> *out, ParseState *state = nullptr);

// The prompt the model sees. Frames every result as untrusted data.
std::string prompt_context(const std::string &query, const std::vector<Result> &results);

// Plain text extracted from an HTML (or text) body, at most `limit` bytes.
std::string html_to_text(std::string_view body, std::size_t limit);

struct Request {
    std::string query;
    std::vector<Scope> scopes;      // empty means every scope
    unsigned maximum = 5;           // results to return after merging
    unsigned timeout_seconds = 15;  // for the whole search
    unsigned provider_timeout_seconds = 6;  // for any single provider
    bool use_cache = true;
};

struct Response {
    std::vector<Result> results;
    std::vector<ProviderReport> reports;
    bool from_cache = false;
    std::string error;              // set only when nothing usable came back
};

// The whole search. Blocking; needs a console for the network part.
bool search(const Request &request, Response *response);

// One URL to bounded plain text. Blocking; needs a console.
bool fetch_page(const std::string &url, unsigned timeout_seconds, std::size_t limit,
                std::string *text, std::string *error);

}  // namespace prospero_search
