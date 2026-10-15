# Native multi-site search engine for ProsperoAI

Status: design, approved conversationally on 2026-10-14. Supersedes the single
`prospero_librey` adapter.

## Goal

Search many sites from the console itself — general web, GitHub, YouTube,
Wikipedia and more — return as many usable pages as the limits allow, let the
model fetch the contents of a result, and keep the memory cost near zero so the
feature never competes with a loaded model for the console's unified memory.

## What the console can actually do

`prospero_https_get()` sends a plain GET with a fixed `ProsperoAI/1.0` user
agent. It has no API for request headers or cookies, and it cannot run
JavaScript. Every provider must work under exactly that. Measured from the
console's network on 2026-10-14:

| Provider | Endpoint | Result |
| --- | --- | --- |
| 4get | `4get.nadeko.net/api/v1/web?s=` | 200, 7.7 KB JSON |
| DuckDuckGo Lite | `lite.duckduckgo.com/lite/?q=` | 200, 23.5 KB HTML |
| GitHub | `api.github.com/search/repositories?q=` | 200, 27.6 KB JSON |
| Wikipedia | `en.wikipedia.org/w/api.php?action=query&list=search` | 200, 1.9 KB JSON |
| Stack Overflow | `api.stackexchange.com/2.3/search/advanced?site=stackoverflow` | 200, 4.0 KB JSON |
| Hacker News | `hn.algolia.com/api/v1/search?query=` | 200, 5.3 KB JSON |
| npm | `registry.npmjs.org/-/v1/search?text=` | 200, 4.9 KB JSON |
| crates.io | `crates.io/api/v1/crates?q=` | 200, 5.2 KB JSON |
| MDN | `developer.mozilla.org/api/v1/search?q=` | 200, 8.5 KB JSON |
| arXiv | `export.arxiv.org/api/query?search_query=` | 200, 12.8 KB Atom; http redirects, use https |
| YouTube | `invidious.f5.si/api/v1/search?q=` | 200, 40 KB JSON |

Ruled out by measurement, not by preference: `html.duckduckgo.com` (HTTP 202
with an anomaly page on a quick second request, and upstream SearXNG abandoned
that route for a `vqd` token flow), Mojeek (captcha challenge with a `chllg`
cookie), Reddit (403), Qwant and Yep (403), `youtube.com/results` (1.5 MB with
`ytInitialData` at offset 815942), and all but one Invidious or Piped instance
tried.

## Architecture

Everything search lives in `search-engine/`. The OpenCode layer stays outside
it and keeps talking to `/api/search`.

```
search-engine/
  include/prospero_search.hpp   public API; the only header consumers include
  src/query.cpp                 query validation, URL normalization, dedup, scoring, prompt context
  src/registry.cpp              provider table, sequential fan-out under one deadline
  src/cache.cpp                 persistent bounded cache on /download0
  src/transport.cpp             bounded streaming fetch over net/https_get.h, with early abort
  src/provider_*.cpp            one file per provider, each a pure parser
  src/fetch_page.cpp            one URL to bounded plain text
  tests/                        fixture-driven parser, scoring and cache tests; no network
```

### Provider interface

```cpp
struct Result { std::string title, url, snippet, source; };

struct Provider {
    const char *id;            // "web.4get", "code.github", "video.youtube", ...
    const char *scope;         // "web", "code", "video", "reference", "qa", "package", "paper"
    std::string (*url)(const std::string &query, unsigned maximum);
    // Fed the body in chunks; returns false to stop the transfer early.
    bool (*feed)(ParseState *state, std::string_view chunk, unsigned maximum,
                 std::vector<Result> *out);
};
```

`feed` is a pure function over a chunk and a small state. That keeps every
parser testable on the host from a captured fixture, with no network and no PS5
build guard, and it is what makes early abort possible: as soon as a parser has
`maximum` results, the transport's stop callback ends the request and the rest
of the body is never transferred.

### Memory budget

The console's memory is unified, so bytes spent here are bytes a model cannot
use. The design holds a hard ceiling that does not grow with the number of
providers:

- one 16 KiB read chunk and one 64 KiB sliding window, both stack- or
  arena-allocated per request and released when it ends;
- at most six providers are in flight, so at most six windows are live;
- a per-provider byte ceiling (256 KiB) aborts a body that will not end;
- parsers keep only the fields of at most `maximum` results, never the body;
- the cache lives on `/download0`, not in RAM;
- no GPU or Vulkan allocation anywhere in this module, which is enforced rather
  than asserted: the module's sources carry no graphics call or header, and its
  six compiled console objects import no graphics symbol — only libc, the C++
  runtime and `prospero_https_*`. A test fails if that ever changes, so video
  memory cannot move because of a search.

### Fan-out and scoring

One deadline from `web_search_timeout` covers the whole search, and each provider
gets a deadline of its own (six seconds) so one slow site cannot spend the whole
budget. A request names the scopes it wants, and the registry asks the matching
providers through a pool of six workers.

The first console run asked only two providers in fifteen seconds, because a
single unreachable site held the sequential sweep until the budget was gone.
A pool of six fixed that — forty-one providers answer in about eight seconds —
and it keeps the memory ceiling, since six 64 KB sessions still cost a fraction
of the 4 MB buffer one model download borrows. Results merge by normalized URL (lowercase host,
`www.` and trailing slash removed, fragment and `utm_*` dropped). Scoring
follows the shape SearXNG uses in `searx/results.py`: a result's score is the
sum of `weight / position` over the providers that returned it, multiplied by
how many returned it, so agreement between providers ranks above a single
provider's first hit. Ties break by provider order. Failures are reported per
provider, not as one merged string.

### Cache

A line-based file at `/download0/prosperoai-search-cache.txt`, written through a
`.tmp` rename exactly as the settings file is. Key is the scope set plus the
normalized query; entries carry a timestamp and expire after a TTL; the file is
bounded in both entry count and bytes, evicting oldest first. It exists because
providers throttle (DuckDuckGo answers 202 on a quick repeat, and 4get asks
clients to cache and only issue human-triggered searches), and it survives a
title restart so OpenCode does not re-query on every relaunch.

### Reading a page

`fetch_page(url, limits)` does one bounded GET and converts HTML to plain text
with the existing entity decoder, keeping at most a configured number of
kilobytes of text. No JavaScript, so static pages only; this is the idea taken
from obscura, not its code. The text is framed as untrusted data exactly as
search results are.

## Interfaces that change

- `/api/search` keeps its shape and gains `source` per result and an optional
  `scopes` array; a new `/api/fetch` exposes `fetch_page`.
- Chat gains `/fetch <url>` beside `/search <query>`.
- Settings gain per-scope toggles and keep `web_search` and
  `web_search_timeout`, stored as new lines in `prosperoai.cfg`.
- `src/librey_search.cpp` and `include/librey_search.hpp` are removed; their
  parser and entity decoder move into the module.
- `tools/build-vulkan-native.sh` compiles `search-engine/src/*.cpp`, and the
  module's tests join `make test` through a harness like
  `tests/test_librey_search.py`.

## Licensing

SearXNG is AGPL-3.0-or-later and 4get is AGPLv3-only. GPLv3 section 13 allows
combining AGPLv3 code into this GPL-3.0-or-later app, but the AGPL part keeps
its duty to offer source to users who interact with it over a network, and this
app serves HTTP on port 11434. So no code is copied from either project: only
documented facts, URL parameters and algorithm shapes, implemented
independently and cited in `docs/OPENCODE_CODING_AGENT_PLAN.md`. obscura is
Apache-2.0 and contributes an idea, not code.

## Verification

Done on 2026-10-15, on test title PPSA99023 (FW 12.70):

1. Host: `tests/test_search_engine.py` runs every parser against a captured
   body, plus URL normalization, scoring, chunked replay, early stop, throttle
   detection and cache expiry and eviction. No network. The coverage table fails
   when a provider is added without a fixture.
2. Host: the whole suite is 72 tests. The seven that fail do so because this
   host has no clang-14 sanitizer runtime (`libclang_rt.asan-x86_64.a`), which
   predates this work and affects files it does not touch.
3. Console: one query across every scope asked 41 providers in 8.0 s; 39
   answered, and 15 merged results came back carrying their source and score.
   The top result scored 7.80 because several providers returned the same page.
4. Console: `/api/fetch` turned a documentation page into 3783 bytes of text in
   0.4 s.
5. Console memory: 582.2 MiB resident before and 582.2 MiB after eight
   full-scope searches, which is 328 provider requests. No growth. That figure
   is the process's resident memory; video memory is covered by the import
   check above rather than by a measurement, since a module that cannot call the
   GPU cannot allocate from it. A GPU-side reading taken during a search is
   still worth having and needs the console.
6. Console cache: a repeated query answered from the cache in 0.003 s, and it
   still answered from the cache in 0.005 s after the title was stopped and
   launched again, so the file survives a restart.
7. Host: `tests/test_web_ui.py` now runs the 25 browser-interface tests through
   `node --test`, which previously existed but were never run by `make test`.
   Three cover the scope checkboxes and the bitmask they send; five cover the
   `/search` and `/fetch` commands in the web conversation, including what the
   model is shown and what is refused before the console is asked.
8. With `HOST_CXX=clang++-18 HOST_CC=clang-18`, all 80 host tests pass. The
   default `clang++` on that host is llvm-14 without
   `libclang_rt.asan-x86_64.a`, so the sanitized harnesses cannot link.
9. Host, against the console's own `http_server.cpp` through the API harness:
   the `search_scopes` round trip and its rejections (0, 512, -1, a string), a
   request narrowing the scopes it asks, a scope switched off in Settings not
   being asked even when the caller names it, an unknown scope refused, and
   `/api/fetch` separating an unreadable address (400) from a page that failed
   (502). 29 API tests pass.
10. Host: the MCP bridge answers a full `tools/call` round trip for both tools
    against a stub console, passes the scopes through unchanged, and refuses an
    unknown scope, a non-http address and an unknown tool without contacting the
    console at all.
11. Not verified on hardware: the per-scope checkboxes and the web `/fetch`
   command, because the console stopped answering on its payload port before
   that build was deployed; and the native chat commands have not been driven
   through the console UI, which needs Remote Play. The app folder was rebuilt
    so the artifact matches these sources and is ready to deploy.
