// SPDX-License-Identifier: GPL-3.0-or-later
#include "cache.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace prospero_search::cache {
namespace {

struct Entry {
    long long stamp = 0;
    std::string key;
    std::vector<Result> results;
};

std::string escape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\\') out.append("\\\\");
        else if (c == '\t') out.append("\\t");
        else if (c == '\n') out.append("\\n");
        else if (c == '\r') out.append("\\r");
        else out.push_back(c);
    }
    return out;
}

std::string unescape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) { out.push_back(value[i]); continue; }
        switch (value[++i]) {
        case 't': out.push_back('\t'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        default: out.push_back(value[i]); break;
        }
    }
    return out;
}

std::vector<std::string> split(const std::string &line, std::size_t fields) {
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (parts.size() + 1 < fields) {
        const auto tab = line.find('\t', at);
        if (tab == std::string::npos) break;
        parts.push_back(line.substr(at, tab - at));
        at = tab + 1;
    }
    parts.push_back(line.substr(at));
    return parts;
}

std::vector<Entry> read_all(const char *path) {
    std::vector<Entry> entries;
    auto *file = std::fopen(path, "rb");
    if (!file) return entries;
    std::string line;
    int chunk = 0;
    unsigned pending = 0;
    const auto handle = [&]() {
        if (line.empty()) return;
        if (line[0] == 'E') {
            const auto parts = split(line, 4);
            if (parts.size() == 4) {
                Entry entry;
                entry.stamp = std::strtoll(parts[1].c_str(), nullptr, 10);
                entry.key = unescape(parts[2]);
                pending = static_cast<unsigned>(std::strtoul(parts[3].c_str(), nullptr, 10));
                entries.push_back(std::move(entry));
            }
        } else if (line[0] == 'R' && !entries.empty() && pending > 0) {
            const auto parts = split(line, 5);
            if (parts.size() == 5) {
                Result result;
                result.source = unescape(parts[1]);
                result.url = unescape(parts[2]);
                result.title = unescape(parts[3]);
                result.snippet = unescape(parts[4]);
                result.position = static_cast<unsigned>(entries.back().results.size() + 1);
                entries.back().results.push_back(std::move(result));
                --pending;
            }
        }
        line.clear();
    };
    while ((chunk = std::fgetc(file)) != EOF) {
        if (chunk == '\n') handle();
        else if (line.size() < 8192) line.push_back(static_cast<char>(chunk));
    }
    handle();
    std::fclose(file);
    return entries;
}

std::string serialize(const std::vector<Entry> &entries) {
    std::string out;
    for (const auto &entry : entries) {
        out += "E\t" + std::to_string(entry.stamp) + "\t" + escape(entry.key) + "\t" +
               std::to_string(entry.results.size()) + "\n";
        for (const auto &result : entry.results)
            out += "R\t" + escape(result.source) + "\t" + escape(result.url) + "\t" +
                   escape(result.title) + "\t" + escape(result.snippet) + "\n";
    }
    return out;
}

bool write_atomically(const char *path, const std::string &contents) {
    const std::string temporary = std::string(path) + ".tmp";
    auto *file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    std::fclose(file);
    if (!written) { std::remove(temporary.c_str()); return false; }
    std::remove(path);
    if (std::rename(temporary.c_str(), path) != 0) { std::remove(temporary.c_str()); return false; }
    return true;
}

}  // namespace

bool store(const char *path, const std::string &key, long long now,
           const std::vector<Result> &results, std::size_t max_entries, std::size_t max_bytes) {
    if (!path || key.empty()) return false;
    auto entries = read_all(path);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const Entry &entry) { return entry.key == key; }),
                  entries.end());
    Entry fresh;
    fresh.stamp = now;
    fresh.key = key;
    fresh.results = results;
    entries.push_back(std::move(fresh));
    // Newest first, then cut to both bounds: oldest entries go first.
    std::sort(entries.begin(), entries.end(),
              [](const Entry &left, const Entry &right) { return left.stamp > right.stamp; });
    if (entries.size() > max_entries) entries.resize(max_entries);
    while (entries.size() > 1 && serialize(entries).size() > max_bytes) entries.pop_back();
    return write_atomically(path, serialize(entries));
}

bool load(const char *path, const std::string &key, long long now, long long ttl,
          std::vector<Result> *out) {
    if (!path || !out) return false;
    for (const auto &entry : read_all(path)) {
        if (entry.key != key) continue;
        if (now - entry.stamp > ttl) return false;
        if (entry.results.empty()) return false;
        *out = entry.results;
        return true;
    }
    return false;
}

std::size_t entry_count(const char *path) { return read_all(path).size(); }

std::string key_for(const std::string &query, const std::vector<Scope> &scopes, unsigned maximum) {
    std::string key;
    if (scopes.empty()) key = "all";
    else {
        auto sorted = scopes;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        for (auto scope : sorted) {
            if (!key.empty()) key.push_back(',');
            key.append(scope_name(scope));
        }
    }
    key.push_back('|');
    key.append(std::to_string(maximum));
    key.push_back('|');
    for (char c : query) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return key;
}

}  // namespace prospero_search::cache
