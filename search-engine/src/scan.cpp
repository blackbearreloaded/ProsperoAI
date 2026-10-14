// SPDX-License-Identifier: GPL-3.0-or-later
#include "scan.hpp"

#include <cstring>
#include <vector>

namespace prospero_search::scan {
namespace {

void append_utf8(std::string *out, unsigned code) {
    if (code < 0x80) out->push_back(static_cast<char>(code));
    else if (code < 0x800) {
        out->push_back(static_cast<char>(0xc0 | (code >> 6)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else {
        out->push_back(static_cast<char>(0xe0 | (code >> 12)));
        out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes the named and numeric entities that search results actually carry.
std::size_t decode_entity(std::string_view text, std::size_t at, std::string *out) {
    const auto end = text.find(';', at + 1);
    if (end == std::string_view::npos || end - at > 12) { out->push_back(text[at]); return at + 1; }
    const auto entity = text.substr(at, end - at + 1);
    if (entity == "&amp;") out->push_back('&');
    else if (entity == "&quot;") out->push_back('"');
    else if (entity == "&#39;" || entity == "&apos;" || entity == "&#x27;") out->push_back('\'');
    else if (entity == "&lt;") out->push_back('<');
    else if (entity == "&gt;") out->push_back('>');
    else if (entity == "&nbsp;") out->push_back(' ');
    else if (entity.size() > 3 && entity[1] == '#') {
        unsigned code = 0;
        const bool hex = entity[2] == 'x' || entity[2] == 'X';
        for (std::size_t i = hex ? 3 : 2; i + 1 < entity.size(); ++i) {
            const int value = hex ? nibble(entity[i]) : (entity[i] >= '0' && entity[i] <= '9' ? entity[i] - '0' : -1);
            if (value < 0) { code = 0; break; }
            code = code * (hex ? 16u : 10u) + static_cast<unsigned>(value);
        }
        if (code) append_utf8(out, code);
    } else { out->append(entity); return end + 1; }
    return end + 1;
}

bool skip_section(std::string_view markup, std::size_t *at, const char *open, const char *close) {
    const std::size_t open_length = std::strlen(open);
    if (markup.compare(*at, open_length, open) != 0) return false;
    const auto end = markup.find(close, *at + open_length);
    *at = end == std::string_view::npos ? markup.size() : end + std::strlen(close);
    return true;
}

}  // namespace

std::string text_from_markup(std::string_view markup) {
    std::string out;
    out.reserve(markup.size() / 2);
    bool space_pending = false;
    for (std::size_t i = 0; i < markup.size();) {
        if (markup[i] == '<') {
            if (skip_section(markup, &i, "<script", "</script>") ||
                skip_section(markup, &i, "<style", "</style>") ||
                skip_section(markup, &i, "<head", "</head>") ||
                skip_section(markup, &i, "<!--", "-->")) { space_pending = !out.empty(); continue; }
            const auto end = markup.find('>', i + 1);
            if (end == std::string_view::npos) break;
            i = end + 1;
            space_pending = !out.empty();
            continue;
        }
        if (markup[i] == '&') {
            if (space_pending && !out.empty()) { out.push_back(' '); space_pending = false; }
            i = decode_entity(markup, i, &out);
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(markup[i++]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space_pending = !out.empty(); continue; }
        if (c < 0x20 || c == 0x7f) continue;
        if (space_pending) { out.push_back(' '); space_pending = false; }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string json_unescape(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) { out.push_back(raw[i]); continue; }
        const char code = raw[++i];
        switch (code) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': case 'f': out.push_back(' '); break;
        case 'u': {
            unsigned value = 0;
            if (i + 4 >= raw.size()) return out;
            for (int digit = 0; digit < 4; ++digit) {
                const int part = nibble(raw[i + 1 + static_cast<std::size_t>(digit)]);
                if (part < 0) return out;
                value = value * 16u + static_cast<unsigned>(part);
            }
            i += 4;
            if (value >= 0xd800 && value <= 0xdbff && i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
                unsigned low = 0;
                bool ok = true;
                for (int digit = 0; digit < 4; ++digit) {
                    const int part = nibble(raw[i + 3 + static_cast<std::size_t>(digit)]);
                    if (part < 0) { ok = false; break; }
                    low = low * 16u + static_cast<unsigned>(part);
                }
                if (ok && low >= 0xdc00 && low <= 0xdfff) {
                    const unsigned point = 0x10000u + ((value - 0xd800u) << 10) + (low - 0xdc00u);
                    i += 6;
                    out.push_back(static_cast<char>(0xf0 | (point >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
                    out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
                    out.push_back(static_cast<char>(0x80 | (point & 0x3f)));
                    break;
                }
            }
            append_utf8(&out, value);
            break;
        }
        default: out.push_back(code); break;
        }
    }
    return out;
}

namespace {

// Walks `object` and compares the key path of each value against `path`, a
// dot-separated path relative to the object ("link", "package.links.npm").
// Nothing is copied until a value matches, and the walk never recurses, so an
// object of any shape costs one pass and no extra memory.
bool path_matches(const std::vector<std::string> &keys, int depth, std::string_view path) {
    std::size_t at = 0;
    for (int level = 1; level <= depth; ++level) {
        const auto dot = path.find('.', at);
        const auto segment = path.substr(at, dot == std::string_view::npos ? dot : dot - at);
        if (segment != keys[static_cast<std::size_t>(level)]) return false;
        if (dot == std::string_view::npos) return level == depth;
        at = dot + 1;
    }
    return false;
}

// Reads the string starting at the opening quote. Returns the offset of the
// closing quote, or npos.
std::size_t string_end(std::string_view object, std::size_t quote) {
    for (std::size_t i = quote + 1; i < object.size(); ++i) {
        if (object[i] == '\\') { ++i; continue; }
        if (object[i] == '"') return i;
    }
    return std::string_view::npos;
}

bool json_find(std::string_view object, std::string_view path, std::string *out, bool allow_number) {
    std::vector<std::string> keys(1);
    int depth = 0;
    for (std::size_t i = 0; i < object.size(); ++i) {
        const char c = object[i];
        if (c == '[') continue;  // arrays are transparent: "title":["X"] matches "title"
        if (c == '{') {
            ++depth;
            if (keys.size() <= static_cast<std::size_t>(depth)) keys.resize(static_cast<std::size_t>(depth) + 1);
            keys[static_cast<std::size_t>(depth)].clear();
            continue;
        }
        if (c == '}') {
            if (depth > 0) --depth;
            continue;
        }
        if (c == ']') continue;
        if (c == '"') {
            const auto end = string_end(object, i);
            if (end == std::string_view::npos) return false;
            const auto raw = object.substr(i + 1, end - i - 1);
            std::size_t after = end + 1;
            while (after < object.size() && (object[after] == ' ' || object[after] == '\n' ||
                                             object[after] == '\r' || object[after] == '\t'))
                ++after;
            if (after < object.size() && object[after] == ':') {
                if (depth > 0) keys[static_cast<std::size_t>(depth)] = json_unescape(raw);
                i = after;
                continue;
            }
            if (depth > 0 && path_matches(keys, depth, path)) {
                *out = json_unescape(raw);
                return true;
            }
            i = end;
            continue;
        }
        if (allow_number && (std::isdigit(static_cast<unsigned char>(c)) || c == '-')) {
            std::size_t end = i;
            while (end < object.size() && (std::isdigit(static_cast<unsigned char>(object[end])) ||
                                           object[end] == '-' || object[end] == '.' ||
                                           object[end] == 'e' || object[end] == '+'))
                ++end;
            if (depth > 0 && path_matches(keys, depth, path)) {
                *out = std::string(object.substr(i, end - i));
                return true;
            }
            i = end - 1;
        }
    }
    return false;
}

}  // namespace

bool json_string(std::string_view object, std::string_view key, std::string *out) {
    return json_find(object, key, out, false);
}

bool json_number(std::string_view object, std::string_view key, std::string *out) {
    return json_find(object, key, out, true);
}

}  // namespace prospero_search::scan
