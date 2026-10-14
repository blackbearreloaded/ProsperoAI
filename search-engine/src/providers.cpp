// SPDX-License-Identifier: GPL-3.0-or-later
// The provider table. Every entry is a URL builder plus a parser that consumes
// the body in pieces and stops the transfer as soon as it has enough results.
//
// Each endpoint here was measured from the console's network with one plain GET
// and the app's fixed user agent; see
// docs/superpowers/specs/2026-10-14-search-engine-design.md.
#include "prospero_search.hpp"

#include "scan.hpp"

#include <cctype>
#include <cstring>

namespace prospero_search {
namespace {

std::string encode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

// For values that become part of an address rather than a parameter: a path
// separator and a port colon must survive, or "/works/OL893414W" and
// "owner/image" would turn into one escaped blob.
std::string encode_path(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':')
            out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

bool usable_url(const std::string &url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return false;
    return url.size() <= 2048 && url.find_first_of("\r\n\t <>\"") == std::string::npos;
}

// Keeps the model's context predictable and bounds what one result can cost.
void clamp(Result *result) {
    if (result->title.size() > 512) result->title.resize(512);
    if (result->snippet.size() > 2048) result->snippet.resize(2048);
}

bool accept(ParseState *state, unsigned maximum, std::vector<Result> *out, Result result,
            const char *id) {
    if (result.title.empty() || !usable_url(result.url)) return state->found < maximum;
    result.source = id;
    result.position = ++state->found;
    clamp(&result);
    out->push_back(std::move(result));
    return state->found < maximum;
}

// What to pull out of one JSON object of an array.
struct JsonPlan {
    const char *array_key;      // null for a top-level array
    const char *title_key;
    const char *url_key;        // null when the URL is built from id_key
    const char *snippet_key;
    const char *id_key;
    const char *url_prefix;     // prepended to url_key or id_key
    bool snippet_is_markup;     // strip tags (Wikipedia returns highlighted HTML)
    bool title_is_markup;
};

bool feed_json(const JsonPlan &plan, const char *id, ParseState *state, std::string_view chunk,
               unsigned maximum, std::vector<Result> *out) {
    if (state->done) return false;
    const bool more = scan::json_array_feed(plan.array_key, state, chunk,
                                            [&](std::string_view object) {
        Result result;
        std::string value;
        if (scan::json_string(object, plan.title_key, &value))
            result.title = plan.title_is_markup ? scan::text_from_markup(value) : value;
        if (plan.url_key && scan::json_string(object, plan.url_key, &value) && !value.empty())
            result.url = plan.url_prefix ? plan.url_prefix + value : value;
        else if (plan.id_key && plan.url_prefix &&
                 (scan::json_string(object, plan.id_key, &value) ||
                  scan::json_number(object, plan.id_key, &value)))
            result.url = plan.url_prefix + encode_path(value);
        if (plan.snippet_key && scan::json_string(object, plan.snippet_key, &value))
            result.snippet = plan.snippet_is_markup ? scan::text_from_markup(value) : value;
        return accept(state, maximum, out, std::move(result), id);
    });
    if (!more) state->done = true;
    return more;
}

// --- shared request shapes ---

// APIs whose terms ask callers to identify themselves get a contact address.
#define PROSPERO_CONTACT_UA "ProsperoAI/1.0 (+https://homebrew.page/app/PPSA99004/)"
#define PROSPERO_ACCEPT_JSON "Accept: application/json"

// One Stack Exchange API serves dozens of sites; only the site name changes.
std::string se_url(const char *site, const std::string &query, unsigned maximum) {
    return std::string("https://api.stackexchange.com/2.3/search/advanced?order=desc&sort=relevance"
                       "&site=") + site + "&pagesize=" + std::to_string(maximum) + "&q=" + encode(query);
}

// The same for MediaWiki: one search API, one page prefix per wiki.
std::string wiki_url(const char *host, const std::string &query, unsigned maximum) {
    return std::string("https://") + host + "/w/api.php?action=query&list=search&format=json&srlimit=" +
           std::to_string(maximum) + "&srsearch=" + encode(query);
}
std::string wiki_url_root(const char *host, const std::string &query, unsigned maximum) {
    return std::string("https://") + host + "/api.php?action=query&list=search&format=json&srlimit=" +
           std::to_string(maximum) + "&srsearch=" + encode(query);
}

// Declares the parser of a provider whose answer is a JSON array of records.
#define JSON_FEED(name, id_literal, array_key, title_key, url_key, snippet_key, id_key, prefix, \
                  snippet_markup)                                                               \
    bool feed_##name(ParseState *state, std::string_view chunk, unsigned maximum,               \
                     std::vector<Result> *out) {                                                \
        static constexpr JsonPlan plan{array_key,  title_key, url_key,        snippet_key,      \
                                       id_key,     prefix,    snippet_markup, false};           \
        return feed_json(plan, id_literal, state, chunk, maximum, out);                         \
    }

// Declares a Stack Exchange site: question title and its link.
#define SE_PROVIDER(name, site)                                                                 \
    std::string url_##name(const std::string &query, unsigned maximum) {                        \
        return se_url(site, query, maximum);                                                    \
    }                                                                                           \
    JSON_FEED(name, "qa." #name, "items", "title", "link", nullptr, nullptr, nullptr, false)

// Declares a MediaWiki site: the search array carries no URL, so the page
// address is built from the title.
#define WIKI_PROVIDER(name, host, page_prefix, api)                                             \
    std::string url_##name(const std::string &query, unsigned maximum) {                        \
        return api(host, query, maximum);                                                       \
    }                                                                                           \
    JSON_FEED(name, "reference." #name, "search", "title", nullptr, "snippet", "title",          \
              page_prefix, true)

// --- general web ---

std::string url_fourget(const std::string &query, unsigned) {
    return "https://4get.nadeko.net/api/v1/web?s=" + encode(query);
}
bool feed_fourget(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{"web", "title", "url", "description", nullptr, nullptr, false, false};
    return feed_json(plan, "web.4get", state, chunk, maximum, out);
}

// DuckDuckGo answers HTTP 202 with an "anomaly" page under repeated use; both of
// its endpoints do. Recognize it so the caller reports throttling rather than an
// empty index.
bool ddg_throttled(ParseState *state, std::string_view window) {
    if (window.find("anomaly-modal") == std::string_view::npos &&
        window.find("Unfortunately, bots use DuckDuckGo too") == std::string_view::npos)
        return false;
    state->throttled = true;
    state->done = true;
    return true;
}

// Lite serves a table of result-link anchors followed by result-snippet cells.
std::string url_ddglite(const std::string &query, unsigned) {
    return "https://lite.duckduckgo.com/lite/?q=" + encode(query);
}
bool feed_ddglite(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    if (state->done) return false;
    state->window.append(chunk);
    if (ddg_throttled(state, state->window)) { state->window.clear(); return false; }
    for (;;) {
        const auto anchor = state->window.find("class=\"result-link\"");
        if (anchor == std::string::npos) break;
        const auto tag_begin = state->window.rfind('<', anchor);
        const auto tag_end = state->window.find('>', anchor);
        const auto title_end = tag_end == std::string::npos ? std::string::npos : state->window.find("</a>", tag_end);
        if (tag_begin == std::string::npos || tag_end == std::string::npos || title_end == std::string::npos) break;
        const std::string_view tag(state->window.data() + tag_begin, tag_end - tag_begin + 1);
        Result result;
        const auto href = tag.find("href=\"");
        if (href != std::string_view::npos) {
            const auto href_end = tag.find('"', href + 6);
            if (href_end != std::string_view::npos)
                result.url = scan::text_from_markup(tag.substr(href + 6, href_end - href - 6));
        }
        result.title = scan::text_from_markup(
            std::string_view(state->window).substr(tag_end + 1, title_end - tag_end - 1));
        const auto snippet_at = state->window.find("class=\"result-snippet\"", title_end);
        if (snippet_at != std::string::npos) {
            const auto snippet_begin = state->window.find('>', snippet_at);
            const auto snippet_end = snippet_begin == std::string::npos
                                         ? std::string::npos
                                         : state->window.find("</td>", snippet_begin);
            if (snippet_end != std::string::npos)
                result.snippet = scan::text_from_markup(
                    std::string_view(state->window).substr(snippet_begin + 1, snippet_end - snippet_begin - 1));
        }
        const bool more = accept(state, maximum, out, std::move(result), "web.ddglite");
        state->window.erase(0, title_end + 4);
        if (!more) { state->done = true; state->window.clear(); return false; }
    }
    if (state->window.size() > kWindowLimit) state->window.erase(0, state->window.size() - kWindowLimit);
    return true;
}

// The HTML site, kept as a fallback: it is the route upstream SearXNG abandoned,
// and it throttles the same way, but its markup is stable and proven.
std::string url_ddghtml(const std::string &query, unsigned) {
    return "https://html.duckduckgo.com/html/?q=" + encode(query) + "&kd=-1&s=0";
}
std::string unwrap_redirect(std::string href) {
    const auto marker = href.find("uddg=");
    if (marker == std::string::npos) {
        if (href.rfind("//", 0) == 0) return "https:" + href;
        return href;
    }
    const auto begin = marker + 5;
    const auto end = href.find('&', begin);
    std::string encoded = href.substr(begin, end == std::string::npos ? end : end - begin);
    std::string out;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '+') { out.push_back(' '); continue; }
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = digit(encoded[i + 1]), lo = digit(encoded[i + 2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>((hi << 4) | lo)); i += 2; continue; }
        }
        out.push_back(encoded[i]);
    }
    return out;
}
bool feed_ddghtml(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    if (state->done) return false;
    state->window.append(chunk);
    if (ddg_throttled(state, state->window)) { state->window.clear(); return false; }
    for (;;) {
        const auto anchor = state->window.find("class=\"result__a\"");
        if (anchor == std::string::npos) break;
        const auto tag_begin = state->window.rfind('<', anchor);
        const auto tag_end = state->window.find('>', anchor);
        const auto title_end = tag_end == std::string::npos ? std::string::npos : state->window.find("</a>", tag_end);
        if (tag_begin == std::string::npos || tag_end == std::string::npos || title_end == std::string::npos) break;
        const std::string_view tag(state->window.data() + tag_begin, tag_end - tag_begin + 1);
        Result result;
        const auto href = tag.find("href=\"");
        if (href != std::string_view::npos) {
            const auto href_end = tag.find('"', href + 6);
            if (href_end != std::string_view::npos)
                result.url = unwrap_redirect(scan::text_from_markup(tag.substr(href + 6, href_end - href - 6)));
        }
        result.title = scan::text_from_markup(
            std::string_view(state->window).substr(tag_end + 1, title_end - tag_end - 1));
        const auto snippet_at = state->window.find("class=\"result__snippet\"", title_end);
        if (snippet_at != std::string::npos && snippet_at - title_end < 2048) {
            const auto snippet_begin = state->window.find('>', snippet_at);
            const auto snippet_end = snippet_begin == std::string::npos
                                         ? std::string::npos
                                         : state->window.find("</a>", snippet_begin);
            if (snippet_end != std::string::npos)
                result.snippet = scan::text_from_markup(
                    std::string_view(state->window).substr(snippet_begin + 1, snippet_end - snippet_begin - 1));
        }
        const bool more = accept(state, maximum, out, std::move(result), "web.ddghtml");
        state->window.erase(0, title_end + 4);
        if (!more) { state->done = true; state->window.clear(); return false; }
    }
    if (state->window.size() > kWindowLimit) state->window.erase(0, state->window.size() - kWindowLimit);
    return true;
}

// --- site-scoped providers ---

std::string url_github(const std::string &query, unsigned maximum) {
    return "https://api.github.com/search/repositories?q=" + encode(query) + "&per_page=" +
           std::to_string(maximum);
}
bool feed_github(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{"items", "full_name", "html_url", "description", nullptr, nullptr, false, false};
    return feed_json(plan, "code.github", state, chunk, maximum, out);
}

std::string url_wikipedia(const std::string &query, unsigned maximum) {
    return "https://en.wikipedia.org/w/api.php?action=query&list=search&format=json&srlimit=" +
           std::to_string(maximum) + "&srsearch=" + encode(query);
}
bool feed_wikipedia(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    // The search array holds no URL: the page address is built from the title.
    static constexpr JsonPlan plan{"search", "title", nullptr, "snippet", "title",
                                   "https://en.wikipedia.org/wiki/", true, false};
    return feed_json(plan, "reference.wikipedia", state, chunk, maximum, out);
}

std::string url_stackoverflow(const std::string &query, unsigned maximum) {
    return se_url("stackoverflow", query, maximum);
}
bool feed_stackoverflow(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{"items", "title", "link", nullptr, nullptr, nullptr, false, true};
    return feed_json(plan, "qa.stackoverflow", state, chunk, maximum, out);
}

std::string url_hackernews(const std::string &query, unsigned maximum) {
    return "https://hn.algolia.com/api/v1/search?hitsPerPage=" + std::to_string(maximum) +
           "&query=" + encode(query);
}
bool feed_hackernews(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    // A story without a link is a discussion: fall back to its item page.
    static constexpr JsonPlan plan{"hits", "title", "url", "story_text", "objectID",
                                   "https://news.ycombinator.com/item?id=", false, false};
    return feed_json(plan, "qa.hackernews", state, chunk, maximum, out);
}

std::string url_npm(const std::string &query, unsigned maximum) {
    return "https://registry.npmjs.org/-/v1/search?size=" + std::to_string(maximum) + "&text=" +
           encode(query);
}
bool feed_npm(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{"objects", "package.name", "package.links.npm",
                                   "package.description", nullptr, nullptr, false, false};
    return feed_json(plan, "package.npm", state, chunk, maximum, out);
}

std::string url_crates(const std::string &query, unsigned maximum) {
    return "https://crates.io/api/v1/crates?per_page=" + std::to_string(maximum) + "&q=" + encode(query);
}
bool feed_crates(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{"crates", "name", nullptr, "description", "id",
                                   "https://crates.io/crates/", false, false};
    return feed_json(plan, "package.crates", state, chunk, maximum, out);
}

std::string url_mdn(const std::string &query, unsigned) {
    return "https://developer.mozilla.org/api/v1/search?q=" + encode(query);
}
bool feed_mdn(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{"documents", "title", "mdn_url", "summary", nullptr,
                                   "https://developer.mozilla.org", false, false};
    return feed_json(plan, "reference.mdn", state, chunk, maximum, out);
}

std::string url_youtube(const std::string &query, unsigned) {
    return "https://invidious.f5.si/api/v1/search?q=" + encode(query);
}
bool feed_youtube(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    static constexpr JsonPlan plan{nullptr, "title", nullptr, "description", "videoId",
                                   "https://www.youtube.com/watch?v=", false, false};
    return feed_json(plan, "video.youtube", state, chunk, maximum, out);
}

std::string url_arxiv(const std::string &query, unsigned maximum) {
    return "https://export.arxiv.org/api/query?max_results=" + std::to_string(maximum) +
           "&search_query=all:" + encode(query);
}
bool feed_arxiv(ParseState *state, std::string_view chunk, unsigned maximum, std::vector<Result> *out) {
    if (state->done) return false;
    const bool more = scan::xml_window_feed(&state->window, chunk, "<entry>", "</entry>",
                                            [&](std::string_view entry) {
        const auto value = [&](const char *open, const char *close) -> std::string {
            const auto begin = entry.find(open);
            if (begin == std::string_view::npos) return {};
            const auto from = begin + std::strlen(open);
            const auto end = entry.find(close, from);
            if (end == std::string_view::npos) return {};
            return scan::text_from_markup(entry.substr(from, end - from));
        };
        Result result;
        result.title = value("<title>", "</title>");
        result.snippet = value("<summary>", "</summary>");
        result.url = value("<id>", "</id>");
        if (result.url.rfind("http://", 0) == 0) result.url = "https://" + result.url.substr(7);
        return accept(state, maximum, out, std::move(result), "paper.arxiv");
    });
    if (!more) { state->done = true; state->window.clear(); }
    return more;
}


// --- general web, independent indexes ---

std::string url_marginalia(const std::string &query, unsigned) {
    return "https://api.marginalia.nu/public/search/" + encode(query);
}
JSON_FEED(marginalia, "web.marginalia", "results", "title", "url", "description", nullptr, nullptr, false)

std::string url_wiby(const std::string &query, unsigned) {
    return "https://wiby.me/json/?q=" + encode(query);
}
JSON_FEED(wiby, "web.wiby", nullptr, "Title", "URL", "Snippet", nullptr, nullptr, false)

// --- code hosting ---

std::string url_gitlab(const std::string &query, unsigned maximum) {
    return "https://gitlab.com/api/v4/projects?search=" + encode(query) + "&per_page=" +
           std::to_string(maximum);
}
JSON_FEED(gitlab, "code.gitlab", nullptr, "name_with_namespace", "web_url", "description", nullptr,
          nullptr, false)

std::string url_codeberg(const std::string &query, unsigned maximum) {
    return "https://codeberg.org/api/v1/repos/search?q=" + encode(query) + "&limit=" +
           std::to_string(maximum);
}
JSON_FEED(codeberg, "code.codeberg", "data", "full_name", "html_url", "description", nullptr, nullptr, false)

std::string url_dockerhub(const std::string &query, unsigned maximum) {
    return "https://hub.docker.com/v2/search/repositories/?query=" + encode(query) + "&page_size=" +
           std::to_string(maximum);
}
JSON_FEED(dockerhub, "code.dockerhub", "results", "repo_name", nullptr, "short_description",
          "repo_name", "https://hub.docker.com/r/", false)

// --- question and answer sites ---

SE_PROVIDER(superuser, "superuser")
SE_PROVIDER(askubuntu, "askubuntu")
SE_PROVIDER(unix, "unix")

// --- package registries ---

std::string url_packagist(const std::string &query, unsigned) {
    return "https://packagist.org/search.json?q=" + encode(query);
}
JSON_FEED(packagist, "package.packagist", "results", "name", "url", "description", nullptr, nullptr, false)

std::string url_rubygems(const std::string &query, unsigned) {
    return "https://rubygems.org/api/v1/search.json?query=" + encode(query);
}
JSON_FEED(rubygems, "package.rubygems", nullptr, "name", "project_uri", "info", nullptr, nullptr, false)

std::string url_hexpm(const std::string &query, unsigned) {
    return "https://hex.pm/api/packages?search=" + encode(query);
}
JSON_FEED(hexpm, "package.hexpm", nullptr, "name", "html_url", "meta.description", nullptr, nullptr, false)

std::string url_nuget(const std::string &query, unsigned maximum) {
    return "https://azuresearch-usnc.nuget.org/query?q=" + encode(query) + "&take=" +
           std::to_string(maximum);
}
JSON_FEED(nuget, "package.nuget", "data", "id", nullptr, "description", "id",
          "https://www.nuget.org/packages/", false)

std::string url_archpkg(const std::string &query, unsigned) {
    return "https://archlinux.org/packages/search/json/?q=" + encode(query);
}
JSON_FEED(archpkg, "package.archlinux", "results", "pkgname", nullptr, "pkgdesc", "pkgname",
          "https://archlinux.org/packages/?name=", false)

// --- reference works ---

std::string url_wikidata(const std::string &query, unsigned) {
    return "https://www.wikidata.org/w/api.php?action=wbsearchentities&language=en&format=json&search=" +
           encode(query);
}
JSON_FEED(wikidata, "reference.wikidata", "search", "label", "concepturi", "description", nullptr,
          nullptr, false)

WIKI_PROVIDER(wiktionary, "en.wiktionary.org", "https://en.wiktionary.org/wiki/", wiki_url)
WIKI_PROVIDER(commons, "commons.wikimedia.org", "https://commons.wikimedia.org/wiki/", wiki_url)
WIKI_PROVIDER(archwiki, "wiki.archlinux.org", "https://wiki.archlinux.org/title/", wiki_url_root)
WIKI_PROVIDER(gentoowiki, "wiki.gentoo.org", "https://wiki.gentoo.org/wiki/", wiki_url_root)

std::string url_openlibrary(const std::string &query, unsigned maximum) {
    return "https://openlibrary.org/search.json?q=" + encode(query) + "&limit=" + std::to_string(maximum);
}
JSON_FEED(openlibrary, "reference.openlibrary", "docs", "title", nullptr, "author_name", "key",
          "https://openlibrary.org", false)

std::string url_archive(const std::string &query, unsigned maximum) {
    return "https://archive.org/advancedsearch.php?output=json&rows=" + std::to_string(maximum) +
           "&q=" + encode(query);
}
JSON_FEED(archive, "reference.archive", "docs", "title", nullptr, "description", "identifier",
          "https://archive.org/details/", false)

// --- papers ---

std::string url_crossref(const std::string &query, unsigned maximum) {
    return "https://api.crossref.org/works?rows=" + std::to_string(maximum) + "&query=" + encode(query);
}
JSON_FEED(crossref, "paper.crossref", "items", "title", nullptr, "container-title", "DOI",
          "https://doi.org/", false)

std::string url_europepmc(const std::string &query, unsigned maximum) {
    return "https://www.ebi.ac.uk/europepmc/webservices/rest/search?format=json&pageSize=" +
           std::to_string(maximum) + "&query=" + encode(query);
}
JSON_FEED(europepmc, "paper.europepmc", "result", "title", nullptr, "authorString", "doi",
          "https://doi.org/", false)

std::string url_openalex(const std::string &query, unsigned maximum) {
    return "https://api.openalex.org/works?per-page=" + std::to_string(maximum) + "&search=" +
           encode(query);
}
JSON_FEED(openalex, "paper.openalex", "results", "display_name", "id", nullptr, nullptr, nullptr, false)

std::string url_doaj(const std::string &query, unsigned maximum) {
    return "https://doaj.org/api/search/articles/" + encode(query) + "?pageSize=" +
           std::to_string(maximum);
}
JSON_FEED(doaj, "paper.doaj", "results", "bibjson.title", nullptr, "bibjson.abstract", "id",
          "https://doaj.org/article/", false)

// --- video, music, images ---

std::string url_peertube(const std::string &query, unsigned maximum) {
    return "https://sepiasearch.org/api/v1/search/videos?count=" + std::to_string(maximum) +
           "&search=" + encode(query);
}
JSON_FEED(peertube, "video.peertube", "data", "name", "url", "description", nullptr, nullptr, false)

std::string url_deezer(const std::string &query, unsigned maximum) {
    return "https://api.deezer.com/search?limit=" + std::to_string(maximum) + "&q=" + encode(query);
}
JSON_FEED(deezer, "music.deezer", "data", "title", "link", "artist.name", nullptr, nullptr, false)

std::string url_itunes(const std::string &query, unsigned maximum) {
    return "https://itunes.apple.com/search?limit=" + std::to_string(maximum) + "&term=" + encode(query);
}
JSON_FEED(itunes, "music.itunes", "results", "trackName", "trackViewUrl", "artistName", nullptr,
          nullptr, false)

std::string url_musicbrainz(const std::string &query, unsigned maximum) {
    return "https://musicbrainz.org/ws/2/recording?fmt=json&limit=" + std::to_string(maximum) +
           "&query=" + encode(query);
}
JSON_FEED(musicbrainz, "music.musicbrainz", "recordings", "title", nullptr, nullptr, "id",
          "https://musicbrainz.org/recording/", false)

std::string url_openverse(const std::string &query, unsigned maximum) {
    return "https://api.openverse.org/v1/images/?page_size=" + std::to_string(maximum) + "&q=" +
           encode(query);
}
JSON_FEED(openverse, "image.openverse", "results", "title", "foreign_landing_url", "creator", nullptr,
          nullptr, false)

// Query order: the cheap, unthrottled APIs first, the rate-limited general web
// engines last. Weights say how much agreement from a provider is worth. Every
// endpoint here answered a plain GET from the console's network; the ones that
// demand headers, cookies or a solved challenge are not in this table.
constexpr Provider kProviders[] = {
    // general web
    {"web.4get", Scope::Web, 1.0, nullptr, nullptr, url_fourget, feed_fourget},
    {"web.marginalia", Scope::Web, 0.9, nullptr, nullptr, url_marginalia, feed_marginalia},
    {"web.wiby", Scope::Web, 0.6, nullptr, nullptr, url_wiby, feed_wiby},
    // code hosting
    {"code.github", Scope::Code, 1.0, nullptr, nullptr, url_github, feed_github},
    {"code.gitlab", Scope::Code, 0.8, nullptr, nullptr, url_gitlab, feed_gitlab},
    {"code.codeberg", Scope::Code, 0.8, nullptr, nullptr, url_codeberg, feed_codeberg},
    {"code.dockerhub", Scope::Code, 0.7, nullptr, nullptr, url_dockerhub, feed_dockerhub},
    // question and answer
    {"qa.stackoverflow", Scope::Qa, 1.0, nullptr, nullptr, url_stackoverflow, feed_stackoverflow},
    {"qa.superuser", Scope::Qa, 0.8, nullptr, nullptr, url_superuser, feed_superuser},
    {"qa.askubuntu", Scope::Qa, 0.8, nullptr, nullptr, url_askubuntu, feed_askubuntu},
    {"qa.unix", Scope::Qa, 0.8, nullptr, nullptr, url_unix, feed_unix},
    {"qa.hackernews", Scope::Qa, 0.7, nullptr, nullptr, url_hackernews, feed_hackernews},
    // reference
    {"reference.wikipedia", Scope::Reference, 1.0, nullptr, nullptr, url_wikipedia, feed_wikipedia},
    {"reference.mdn", Scope::Reference, 0.9, nullptr, nullptr, url_mdn, feed_mdn},
    {"reference.wikidata", Scope::Reference, 0.8, nullptr, nullptr, url_wikidata, feed_wikidata},
    {"reference.wiktionary", Scope::Reference, 0.7, nullptr, nullptr, url_wiktionary, feed_wiktionary},
    {"reference.archwiki", Scope::Reference, 0.8, nullptr, nullptr, url_archwiki, feed_archwiki},
    {"reference.gentoowiki", Scope::Reference, 0.7, nullptr, nullptr, url_gentoowiki, feed_gentoowiki},
    {"reference.commons", Scope::Reference, 0.6, nullptr, nullptr, url_commons, feed_commons},
    {"reference.openlibrary", Scope::Reference, 0.7, nullptr, nullptr, url_openlibrary, feed_openlibrary},
    {"reference.archive", Scope::Reference, 0.6, nullptr, nullptr, url_archive, feed_archive},
    // packages
    {"package.npm", Scope::Package, 0.8, nullptr, nullptr, url_npm, feed_npm},
    {"package.crates", Scope::Package, 0.8, nullptr, nullptr, url_crates, feed_crates},
    {"package.packagist", Scope::Package, 0.8, nullptr, nullptr, url_packagist, feed_packagist},
    {"package.rubygems", Scope::Package, 0.8, nullptr, nullptr, url_rubygems, feed_rubygems},
    {"package.hexpm", Scope::Package, 0.7, nullptr, nullptr, url_hexpm, feed_hexpm},
    {"package.nuget", Scope::Package, 0.8, nullptr, nullptr, url_nuget, feed_nuget},
    {"package.archlinux", Scope::Package, 0.7, nullptr, nullptr, url_archpkg, feed_archpkg},
    // papers
    {"paper.arxiv", Scope::Paper, 0.8, nullptr, nullptr, url_arxiv, feed_arxiv},
    {"paper.crossref", Scope::Paper, 0.8, PROSPERO_ACCEPT_JSON, PROSPERO_CONTACT_UA, url_crossref, feed_crossref},
    {"paper.europepmc", Scope::Paper, 0.8, PROSPERO_ACCEPT_JSON, nullptr, url_europepmc, feed_europepmc},
    {"paper.openalex", Scope::Paper, 0.8, PROSPERO_ACCEPT_JSON, PROSPERO_CONTACT_UA, url_openalex, feed_openalex},
    {"paper.doaj", Scope::Paper, 0.7, PROSPERO_ACCEPT_JSON, nullptr, url_doaj, feed_doaj},
    // video, music, images
    {"video.youtube", Scope::Video, 0.9, nullptr, nullptr, url_youtube, feed_youtube},
    {"video.peertube", Scope::Video, 0.7, nullptr, nullptr, url_peertube, feed_peertube},
    {"music.deezer", Scope::Music, 0.8, nullptr, nullptr, url_deezer, feed_deezer},
    {"music.itunes", Scope::Music, 0.8, nullptr, nullptr, url_itunes, feed_itunes},
    {"music.musicbrainz", Scope::Music, 0.7, nullptr, PROSPERO_CONTACT_UA, url_musicbrainz, feed_musicbrainz},
    {"image.openverse", Scope::Image, 0.8, nullptr, nullptr, url_openverse, feed_openverse},
    // rate-limited, so they come last and may answer with a throttle page
    {"web.ddglite", Scope::Web, 0.9, nullptr, nullptr, url_ddglite, feed_ddglite},
    {"web.ddghtml", Scope::Web, 0.6, nullptr, nullptr, url_ddghtml, feed_ddghtml},
};

}  // namespace

const Provider *providers(std::size_t *count) {
    if (count) *count = sizeof(kProviders) / sizeof(kProviders[0]);
    return kProviders;
}

const Provider *provider_by_id(std::string_view id) {
    for (const auto &provider : kProviders)
        if (id == provider.id) return &provider;
    return nullptr;
}

}  // namespace prospero_search
