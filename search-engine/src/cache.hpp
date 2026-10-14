// SPDX-License-Identifier: GPL-3.0-or-later
// A small persistent result cache. It exists because providers throttle:
// DuckDuckGo answers 202 on a quick repeat and 4get asks clients to cache and
// only issue human-triggered searches. It lives on disk, not in memory, so it
// costs the console nothing while a model is loaded.
#pragma once

#include "prospero_search.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace prospero_search::cache {

constexpr std::size_t kMaxEntries = 32;
constexpr std::size_t kMaxBytes = 256u * 1024u;
constexpr long long kTtlSeconds = 24 * 60 * 60;

// Replaces `key`, keeping the newest entries within both bounds.
bool store(const char *path, const std::string &key, long long now,
           const std::vector<Result> &results, std::size_t max_entries, std::size_t max_bytes);

// True when `key` is present and younger than `ttl`.
bool load(const char *path, const std::string &key, long long now, long long ttl,
          std::vector<Result> *out);

std::size_t entry_count(const char *path);

// The cache key: the scopes asked for, then the query, lowercased.
std::string key_for(const std::string &query, const std::vector<Scope> &scopes, unsigned maximum);

}  // namespace prospero_search::cache
