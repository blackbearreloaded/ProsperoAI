// SPDX-License-Identifier: GPL-3.0-or-later
// Internal scanning helpers: they never hold a whole response, only the record
// being read. Shared by the providers and by fetch_page.
#pragma once

#include "prospero_search.hpp"

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace prospero_search::scan {

// Largest single record (one JSON object, one XML entry) we are willing to buffer.
constexpr std::size_t kRecordLimit = 16u * 1024u;

// Drops tags, decodes HTML entities and collapses whitespace.
std::string text_from_markup(std::string_view markup);

// Decodes a JSON string body (the bytes between the quotes), including \uXXXX.
std::string json_unescape(std::string_view raw);

// Copies the string value of `key` found anywhere in `object`. False when the
// key is absent or its value is not a string.
bool json_string(std::string_view object, std::string_view key, std::string *out);

// Copies the integer value of `key` as text (JSON numbers stay unquoted).
bool json_number(std::string_view object, std::string_view key, std::string *out);

// Splits a JSON body into the objects of one array, chunk by chunk, keeping its
// state in `state` so a body may arrive in any number of pieces. Give
// `array_key` to enter `"<key>":[`, or null for a top-level array. `sink`
// receives each complete object and returns false to stop the transfer.
enum : unsigned { kEntered = 1u, kInString = 2u, kEscaped = 4u, kOverflow = 8u };

template <typename Sink>
bool json_array_feed(const char *array_key, ParseState *state, std::string_view chunk, Sink sink) {
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        const char c = chunk[i];
        if (!(state->flags & kEntered)) {
            state->window.push_back(c);
            if (state->window.size() > 512) state->window.erase(0, state->window.size() - 512);
            if (c != '[') continue;
            if (!array_key) { state->flags |= kEntered; state->window.clear(); continue; }
            std::string needle = "\"";
            needle += array_key;
            needle += "\"";
            const auto key_at = state->window.rfind(needle);
            if (key_at == std::string::npos) continue;
            bool clean = true;  // only ':' and spaces may sit between key and bracket
            for (std::size_t j = key_at + needle.size(); j + 1 < state->window.size(); ++j) {
                const char between = state->window[j];
                if (between != ':' && between != ' ' && between != '\n' && between != '\r' && between != '\t') {
                    clean = false;
                    break;
                }
            }
            if (!clean) continue;
            state->flags |= kEntered;
            state->window.clear();
            continue;
        }
        if (state->depth > 0) {
            if (state->record.size() < kRecordLimit) state->record.push_back(c);
            else state->flags |= kOverflow;
        }
        if (state->flags & kInString) {
            if (state->flags & kEscaped) state->flags &= ~kEscaped;
            else if (c == '\\') state->flags |= kEscaped;
            else if (c == '"') state->flags &= ~kInString;
            continue;
        }
        if (c == '"') { state->flags |= kInString; continue; }
        if (c == '{') {
            if (state->depth == 0) {
                state->record.clear();
                state->record.push_back(c);
                state->flags &= ~kOverflow;
            }
            ++state->depth;
            continue;
        }
        if (c == '}') {
            if (state->depth > 0 && --state->depth == 0) {
                const bool usable = !(state->flags & kOverflow);
                std::string finished;
                finished.swap(state->record);
                if (usable && !sink(std::string_view(finished))) return false;
            }
            continue;
        }
        if (c == ']' && state->depth == 0) return false;  // the array ended
    }
    return true;
}

// Pulls complete `<tag>...</tag>` sections out of a growing window.
// Returns false when the sink asked to stop.
template <typename Sink>
bool xml_window_feed(std::string *window, std::string_view chunk, const char *open,
                     const char *close, Sink sink) {
    window->append(chunk);
    for (;;) {
        const auto begin = window->find(open);
        if (begin == std::string::npos) break;
        const auto end = window->find(close, begin);
        if (end == std::string::npos) break;
        const auto stop = end + std::strlen(close);
        const std::string_view record(window->data() + begin, stop - begin);
        const bool more = sink(record);
        window->erase(0, stop);
        if (!more) return false;
    }
    // Keep the window bounded: nothing useful can start further back than the
    // longest record we accept.
    if (window->size() > kWindowLimit) window->erase(0, window->size() - kWindowLimit);
    return true;
}

}  // namespace prospero_search::scan
