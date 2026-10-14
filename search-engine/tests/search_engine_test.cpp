// SPDX-License-Identifier: GPL-3.0-or-later
// Fixture-driven tests for the search module. No network: every parser is fed a
// body captured from the real endpoint on 2026-10-14.
#include "prospero_search.hpp"

#include "cache.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path fixtures;

std::string read(const char *name) {
    std::ifstream file(fixtures / name, std::ios::binary);
    assert(file && "fixture is missing");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

using prospero_search::Result;

void expect_usable(const std::vector<Result> &results, const char *id) {
    assert(!results.empty() && "provider returned no results");
    for (const auto &result : results) {
        assert(!result.title.empty());
        assert(result.url.rfind("https://", 0) == 0 || result.url.rfind("http://", 0) == 0);
        assert(result.source == id);
        assert(result.position >= 1);
        assert(result.title.size() <= 512 && result.snippet.size() <= 2048);
    }
}

std::vector<Result> parse(const char *id, const char *fixture, unsigned maximum = 5) {
    const auto *provider = prospero_search::provider_by_id(id);
    assert(provider && "unknown provider id");
    std::vector<Result> results;
    prospero_search::parse_body(*provider, read(fixture), maximum, &results);
    expect_usable(results, id);
    return results;
}

void test_query_validation() {
    assert(prospero_search::valid_query("playstation 5 homebrew"));
    assert(!prospero_search::valid_query("x"));
    assert(!prospero_search::valid_query("bad\nquery"));
    assert(!prospero_search::valid_query(std::string(300, 'a')));
}

void test_url_normalization() {
    using prospero_search::normalize_url;
    assert(normalize_url("https://WWW.Example.ORG/Path/") == "https://example.org/Path");
    assert(normalize_url("https://example.org/a#section") == "https://example.org/a");
    assert(normalize_url("https://example.org/a?utm_source=x&q=1") == "https://example.org/a?q=1");
    assert(normalize_url("https://example.org/a?utm_source=x") == "https://example.org/a");
    assert(normalize_url("https://example.org") == "https://example.org");
}

// A result two providers returned outranks a single provider's first hit.
void test_merge_scoring() {
    std::vector<Result> results = {
        {"Single first", "https://only.example/1", "", "web.4get", 1, 0.0},
        {"Shared second", "https://shared.example/x", "", "web.4get", 2, 0.0},
        {"Shared again", "https://www.shared.example/x/", "snippet", "code.github", 3, 0.0},
    };
    prospero_search::merge(&results, 5);
    assert(results.size() == 2 && "duplicates must collapse");
    assert(results[0].url.find("shared.example") != std::string::npos);
    assert(results[0].score > results[1].score);
    assert(!results[0].snippet.empty() && "merge keeps the richer snippet");

    std::vector<Result> many;
    for (unsigned i = 0; i < 12; ++i)
        many.push_back({"T" + std::to_string(i), "https://e.example/" + std::to_string(i), "", "web.4get", i + 1, 0.0});
    prospero_search::merge(&many, 5);
    assert(many.size() == 5);
}

// Every provider is checked against a body captured from its real endpoint, and
// the table below must cover the whole provider table: a provider added without
// a fixture fails here rather than on the console.
struct Coverage {
    const char *id;
    const char *fixture;
    const char *url_contains;
};

constexpr Coverage kCoverage[] = {
    {"web.4get", "fourget.json", "http"},
    {"web.marginalia", "marginalia.json", "http"},
    {"web.wiby", "wiby.json", "http"},
    {"code.github", "github.json", "github.com/"},
    {"code.gitlab", "gitlab.json", "gitlab.com/"},
    {"code.codeberg", "codeberg.json", "codeberg.org/"},
    {"code.dockerhub", "dockerhub.json", "hub.docker.com/r/"},
    {"qa.stackoverflow", "stackoverflow.json", "stackoverflow.com/questions/"},
    {"qa.superuser", "superuser.json", "superuser.com/questions/"},
    // askubuntu and unix share the Stack Exchange parser, so they are checked
    // against the same captured body; only the site in the request URL differs.
    {"qa.askubuntu", "superuser.json", "/questions/"},
    {"qa.unix", "superuser.json", "/questions/"},
    {"qa.hackernews", "hackernews.json", "http"},
    {"reference.wikipedia", "wikipedia.json", "wikipedia.org/wiki/"},
    {"reference.mdn", "mdn.json", "developer.mozilla.org/"},
    {"reference.wikidata", "wikidata.json", "wikidata.org/entity/"},
    {"reference.wiktionary", "wiktionary.json", "wiktionary.org/wiki/"},
    {"reference.archwiki", "archwiki.json", "wiki.archlinux.org/title/"},
    {"reference.gentoowiki", "archwiki.json", "wiki.gentoo.org/wiki/"},
    {"reference.commons", "commons.json", "commons.wikimedia.org/wiki/"},
    {"reference.openlibrary", "openlibrary.json", "openlibrary.org/works/"},
    {"reference.archive", "archiveorg.json", "archive.org/details/"},
    {"package.npm", "npm.json", "npmjs.com/package/"},
    {"package.crates", "crates.json", "crates.io/crates/"},
    {"package.packagist", "packagist.json", "packagist.org/packages/"},
    {"package.rubygems", "rubygems.json", "rubygems.org/gems/"},
    {"package.hexpm", "hexpm.json", "hex.pm/packages/"},
    {"package.nuget", "nuget.json", "nuget.org/packages/"},
    {"package.archlinux", "archpkg.json", "archlinux.org/packages/"},
    {"paper.arxiv", "arxiv.xml", "arxiv.org/abs/"},
    {"paper.crossref", "crossref.json", "doi.org/"},
    {"paper.europepmc", "europepmc.json", "doi.org/"},
    {"paper.openalex", "openalex.json", "openalex.org/"},
    {"paper.doaj", "doaj.json", "doaj.org/article/"},
    {"video.youtube", "youtube_invidious.json", "youtube.com/watch?v="},
    {"video.peertube", "peertube.json", "http"},
    {"music.deezer", "deezer.json", "deezer.com/track/"},
    {"music.itunes", "itunes.json", "apple.com/"},  // tracks and films answer on different hosts
    {"music.musicbrainz", "musicbrainz.json", "musicbrainz.org/recording/"},
    {"image.openverse", "openverse.json", "http"},
};

// The two DuckDuckGo routes have no captured body: the engine answered with its
// throttle page while fixtures were taken, and that page is covered by
// test_throttle_detection instead.
constexpr const char *kWithoutFixture[] = {"web.ddglite", "web.ddghtml"};

void test_providers_against_fixtures() {
    for (const auto &entry : kCoverage) {
        const auto results = parse(entry.id, entry.fixture);
        if (results[0].url.find(entry.url_contains) == std::string::npos) {
            std::cerr << entry.id << ": expected an address containing " << entry.url_contains
                      << ", got " << results[0].url << "\n";
            assert(false);
        }
    }
    // Wikipedia's snippets arrive as highlighted HTML and must reach the model as text.
    const auto wikipedia = parse("reference.wikipedia", "wikipedia.json");
    assert(wikipedia[0].snippet.find('<') == std::string::npos);

    std::size_t count = 0;
    const auto *table = prospero_search::providers(&count);
    for (std::size_t i = 0; i < count; ++i) {
        bool covered = false;
        for (const auto &entry : kCoverage) covered = covered || std::string(table[i].id) == entry.id;
        for (const char *id : kWithoutFixture) covered = covered || std::string(table[i].id) == id;
        if (!covered) {
            std::cerr << "provider " << table[i].id << " has no fixture test\n";
            assert(false);
        }
    }
    assert(count >= 40 && "the table should keep covering many sites");
}

// The parser must behave the same when the body arrives in pieces, and must stop
// asking for more once it has what it needs.
void test_chunked_feeding_and_early_stop() {
    const auto *provider = prospero_search::provider_by_id("code.github");
    const std::string body = read("github.json");
    std::vector<Result> whole;
    prospero_search::parse_body(*provider, body, 3, &whole);
    assert(whole.size() == 3);

    // Pieces smaller than a socket read stand in for a body that arrives slowly:
    // the result must not depend on how the bytes were split.
    constexpr std::size_t kPiece = 1024;
    prospero_search::ParseState state;
    std::vector<Result> streamed;
    bool wants_more = true;
    std::size_t offset = 0;
    while (offset < body.size() && wants_more) {
        const auto piece = std::min(kPiece, body.size() - offset);
        wants_more = provider->feed(&state, std::string_view(body).substr(offset, piece), 3, &streamed);
        offset += piece;
        assert(state.window.size() <= prospero_search::kWindowLimit);
        assert(state.record.size() <= prospero_search::kWindowLimit);
    }
    assert(streamed.size() == whole.size());
    for (std::size_t i = 0; i < whole.size(); ++i) assert(streamed[i].url == whole[i].url);

    // One result is enough to end the transfer, so most of the body is never read.
    prospero_search::ParseState frugal;
    std::vector<Result> one;
    bool more = true;
    std::size_t read_bytes = 0;
    while (read_bytes < body.size() && more) {
        const auto piece = std::min(kPiece, body.size() - read_bytes);
        more = provider->feed(&frugal, std::string_view(body).substr(read_bytes, piece), 1, &one);
        read_bytes += piece;
    }
    assert(one.size() == 1);
    assert(!more && "parser should end the transfer once it has enough");
    assert(read_bytes < body.size() && "early stop should leave part of the body unread");
}

// DuckDuckGo answers 202 with an "anomaly" page under repeated use. That is
// throttling, not an empty index, and the module must say so.
void test_throttle_detection() {
    const auto *provider = prospero_search::provider_by_id("web.ddglite");
    prospero_search::ParseState state;
    std::vector<Result> results;
    prospero_search::parse_body(*provider, read("ddg_lite_anomaly.html"), 5, &results, &state);
    assert(results.empty());
    assert(state.throttled && "the anomaly page must be recognized");
}

void test_html_to_text() {
    const std::string html =
        "<html><head><title>T</title><style>.a{color:red}</style>"
        "<script>var x = 1 < 2;</script></head><body><h1>Heading</h1>"
        "<p>First &amp; second.</p><p>Third</p></body></html>";
    const auto text = prospero_search::html_to_text(html, 4096);
    assert(text.find("Heading") != std::string::npos);
    assert(text.find("First & second.") != std::string::npos);
    assert(text.find("color:red") == std::string::npos && "style must be dropped");
    assert(text.find("var x") == std::string::npos && "script must be dropped");
    assert(prospero_search::html_to_text(html, 12).size() <= 12);
}

void test_prompt_context_frames_untrusted_data() {
    const std::vector<Result> results = {
        {"Title", "https://example.org/a", "Snippet", "web.4get", 1, 1.0}};
    const auto prompt = prospero_search::prompt_context("query", results);
    assert(prompt.find("untrusted data") != std::string::npos);
    assert(prompt.find("https://example.org/a") != std::string::npos);
    assert(prompt.find("web.4get") != std::string::npos && "the model should see the source");
}

void test_cache_round_trip_expiry_and_bounds() {
    const auto path = (std::filesystem::temp_directory_path() / "prospero-search-cache-test.txt").string();
    std::remove(path.c_str());
    const std::vector<Result> stored = {
        {"Cached", "https://example.org/c", "Snippet\twith\ttabs", "web.4get", 1, 2.0}};
    assert(prospero_search::cache::store(path.c_str(), "web|query", 1000, stored, 4, 64 * 1024));

    std::vector<Result> loaded;
    assert(prospero_search::cache::load(path.c_str(), "web|query", 1100, 300, &loaded));
    assert(loaded.size() == 1);
    assert(loaded[0].title == "Cached");
    assert(loaded[0].snippet == "Snippet\twith\ttabs" && "tabs must survive the line format");
    assert(!prospero_search::cache::load(path.c_str(), "web|query", 2000, 300, &loaded) && "entry must expire");
    assert(!prospero_search::cache::load(path.c_str(), "web|other", 1100, 300, &loaded));

    for (unsigned i = 0; i < 8; ++i)
        assert(prospero_search::cache::store(path.c_str(), "k" + std::to_string(i), 1000 + i, stored, 4, 64 * 1024));
    assert(prospero_search::cache::entry_count(path.c_str()) <= 4 && "oldest entries must be evicted");
    assert(!prospero_search::cache::load(path.c_str(), "k0", 1001, 300, &loaded));
    assert(prospero_search::cache::load(path.c_str(), "k7", 1008, 300, &loaded));
    std::remove(path.c_str());
}

}  // namespace

int main(int argc, char **argv) {
    assert(argc > 1 && "pass the fixtures directory");
    fixtures = argv[1];
    test_query_validation();
    test_url_normalization();
    test_merge_scoring();
    test_providers_against_fixtures();
    test_chunked_feeding_and_early_stop();
    test_throttle_detection();
    test_html_to_text();
    test_prompt_context_frames_untrusted_data();
    test_cache_round_trip_expiry_and_bounds();
    std::cout << "search-engine: all tests passed\n";
    return 0;
}
