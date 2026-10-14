# OpenCode coding-agent integration status

## Goal

Use a text model served by ProsperoAI on the PS5 in OpenCode, while OpenCode runs
repository tools on the development computer. The console performs inference and
optional embedded search; it does not receive project files or execute project
commands.

## Implemented

- Vulkan builds expose `/v1/models` and `/v1/chat/completions`, including SSE
  responses and OpenAI-style function calls. OpenCode connects through its
  custom OpenAI-compatible provider using a model ID returned by `/v1/models`.
- Tool declarations, names, call IDs, result pairing, and generated arguments
  are validated. The supported JSON Schema subset covers common types, required
  properties, additional properties, arrays, enums/constants, alternatives,
  string lengths, and numeric bounds. Unsupported keywords fail closed.
- ProsperoAI embeds a native C++ search engine of its own (`search-engine/`),
  which began as an adapter for LibreY's DuckDuckGo HTML engine and now asks 41
  providers in nine groups. Native chat and web UI share the `/search` and
  `/fetch` flows, with a settings toggle, bounded requests and source URLs.
- The `tools/prosperoai_search_mcp.py` bridge exposes the app search route to
  OpenCode as `web_search`. OpenCode's own `websearch`/`webfetch` remains a
  separate selectable provider route.
- `docs/examples/opencode.prosperoai.json` is a copyable profile. Repository
  reading is allowed; edits, shell commands, web access, and MCP search ask for
  permission. It reads an optional bearer key from the local environment.
- API tests cover tool argument schemas, malformed declarations and history,
  tool-choice modes, round trips, parallel-call limits, and streaming.

## Still needs qualification

- The protocol round trip is verified on-console and with OpenCode 1.1.36, but
  that does not prove autonomous coding quality. Mistral 7B failed an automatic
  read-tool selection test by inventing file contents; Qwen passed a small,
  controlled task but has not completed broader coding qualification.
- Qwen 3.8 9B Distill Q4_K_M passed a controlled read-edit-test task in
  OpenCode 1.1.36 at 16K context; the same test was not reliable at 4K because
  OpenCode's system/tool prompt exceeded the context. This is a useful candidate
  for guided tasks, not a broad coding-quality benchmark.
- The same model called the ProsperoAI MCP search tool and returned a real result
  with its URL when explicitly instructed to call it. A vague search prompt
  produced a fabricated answer, so automatic search selection remains unreliable.
- Native `/search` in the PS5 app and the MCP bridge both returned three live
  results with source URLs. The web UI `/search` flow still needs a fresh visual
  interaction check; its backend shares the verified handler.
- OpenCode streaming, tool calls and the supported schema subset are covered;
  cancellation/disconnect behavior and broad model/template compatibility still
  need targeted verification.

## Planned: build a native C++ metasearch

The goal is a lightweight metasearch implemented in ProsperoAI's native C++
backend and exposed through the existing conversation, web UI, and OpenCode MCP
search paths. It should query a small set of selectable upstream search
providers, normalize and deduplicate their results, and enforce strict time,
response-size, and result-count limits. Keep provider/network buffers bounded
and release them after each request so the feature fits the PS5 memory budget.

Use 4get as a reference for provider coverage and normalized search results,
not as a server or runtime dependency in ProsperoAI. Its [upstream README](https://git.lolcat.ca/lolcat/4get)
describes a PHP server footprint of 100–400 MB and its
[license](https://git.lolcat.ca/lolcat/4get/src/branch/master/license.txt) is
AGPLv3-only. Its API is `GET /api/v1/web?s=<query>` and returns JSON fields
including `status`, `web[].title`, `web[].description`, and `web[].url`. It asks
API clients to [cache results and only issue human-triggered searches](https://git.lolcat.ca/lolcat/4get/src/branch/master/api.txt).

Initial manual smoke check: `4get.nadeko.net` returned useful JSON search
results and source URLs. The official `4get.ca` endpoint and
`search.fischbytes.de` returned HTTP 401; public instances may require a
captcha-derived pass. A live OpenCode call through a temporary MCP adapter was
started but stopped before a result, so OpenCode integration is unverified.

All search code moves into a self-contained `search-engine/` module in this
repository: the public header, the provider table, the merge and cache code, and
the parser tests. The OpenCode layer (`tools/prosperoai_search_mcp.py`, the
checked-in profile and `docs/OPENCODE.md`) stays outside that module and keeps
consuming `/api/search`, so it does not change with the refactor.

Next steps: define the C++ provider interface and normalized result type, then
implement two providers behind it, a 4get JSON route and the DuckDuckGo Lite
HTML route, replacing the current `html.duckduckgo.com` path. Add the
persistent bounded cache, a shared deadline across providers, and the merge that
ranks a result returned by both providers above one returned by a single
provider. Then run one human-requested search through the existing OpenCode MCP
bridge, and verify that results carry their source URLs and that idle and
in-request memory stay bounded. Review AGPLv3 obligations before copying or
distributing any 4get-derived or SearXNG-derived code; prefer an independent
implementation against provider interfaces.

## Provider measurements (2026-10-14)

Measured from the console's own network with one plain GET and the app's fixed
`ProsperoAI/1.0` user agent, which is what `prospero_https_get()` can send: it
has no API for request headers or cookies.

| Endpoint | Result |
| --- | --- |
| `html.duckduckgo.com/html/` (current provider) | first request HTTP 200 with results; an immediate second request HTTP 202 with an anomaly page and no `result__a` markers |
| `lite.duckduckgo.com/lite/` | HTTP 200, 23.5 KB, usable results |
| `4get.nadeko.net/api/v1/web` | HTTP 200, 7.7 KB JSON |
| `en.wikipedia.org/w/api.php` | HTTP 200, JSON |
| `www.mojeek.com/search` | HTTP 200 serving a captcha page, with browser-like headers as well |
| `api.qwant.com`, `api.yep.com` | HTTP 403 (Yep behind Cloudflare) |

Two consequences. The current single provider returns HTTP 202 under repeated
use and the code accepts only HTTP 200, so a quick second search reports a bare
transport error; this is the concrete reason for the cache, for per-provider
error reporting, and for a second provider. And Mojeek is not a candidate for
this transport at all.

## Prior art: what we take, and what we do not copy

[SearXNG](https://github.com/searxng/searxng) (AGPL-3.0-or-later) is the
reference for engine behavior and result merging. Three things it tells us
directly:

- `searx/engines/mojeek.py` solves a captcha challenge (`/captcha/challenge`,
  `/captcha/verify`) and caches the resulting `chllg` cookie. Mojeek is
  cookie-gated, not occasionally unlucky, which matches the measurement above.
- `searx/engines/duckduckgo_web.py` no longer parses the HTML site: it fetches a
  `vqd` token and calls `links.duckduckgo.com/d.js` with `Sec-Fetch-*` and
  `Referer` headers. Upstream abandoned the plain-HTML route our provider still
  uses, so that route is legacy and the Lite endpoint is the viable GET-only
  sibling.
- `searx/results.py` scores a result by engine weight multiplied by the number
  of engines that returned it, summed as `weight / position`, deduplicates
  through a result hash, and then sorts by score. Our merge adopts the shape of
  that formula instead of interleaving providers round-robin.

[obscura](https://github.com/h4ckf0r0day/obscura) (Apache-2.0) contains no
search-engine scrapers; it is a V8-based headless browser in Rust whose
`scrape` command dumps a page as html, text, links or markdown. We take only
that capability as an idea: a bounded native `fetch_page` that turns one result
URL into plain text, with no JavaScript, so static pages can be read on the
console without a development host.

Licenses decide the form of all of this. SearXNG is AGPL-3.0-or-later and 4get
is AGPLv3-only; GPLv3 section 13 permits combining AGPLv3 code into this
GPL-3.0-or-later app, but the AGPL portion keeps its obligation to offer source
to users interacting with it over a network, and this app serves HTTP on port
11434. So we copy no code from either project: we take documented facts, URL
parameters and algorithm shapes, and implement them independently in C++,
citing the source here.

## Built: the native multi-site search engine

`search-engine/` now holds the whole feature: a provider table, an incremental
parser per provider, the merge, the persistent cache, the bounded transport and
a page reader. The design and the measurements behind it are in
`docs/superpowers/specs/2026-10-14-search-engine-design.md`.

Forty-one providers answer a plain GET with the app's own user agent, grouped
into nine scopes a caller can select: web (4get, Marginalia, wiby, DuckDuckGo
Lite and HTML), code (GitHub, GitLab, Codeberg, Docker Hub), question sites
(Stack Overflow, Super User, Ask Ubuntu, Unix, Hacker News), reference
(Wikipedia, MDN, Wikidata, Wiktionary, Arch and Gentoo wikis, Wikimedia Commons,
Open Library, Internet Archive), packages (npm, crates.io, Packagist, RubyGems,
Hex, NuGet, Arch), papers (arXiv, Crossref, Europe PMC, OpenAlex, DOAJ), video
(YouTube through Invidious, PeerTube), music (Deezer, iTunes, MusicBrainz) and
images (Openverse).

What it costs on the console: 41 providers in about eight seconds, and 582.2 MiB
resident before and after 328 provider requests, because each request borrows a
64 KB session and gives it back. A repeated query is answered from
`/download0/prosperoai-search-cache.txt` in milliseconds, and that file survives
a restart of the title.

Interfaces: `/api/search` takes `query`, `limit`, optional `scopes` and `fresh`,
and answers with each result's `source` and `score` plus a per-provider report;
`/api/fetch` turns one address into bounded plain text. The chat understands
`/search <query>` and `/fetch <url>`. The MCP bridge offers `web_search` with
scopes and a second tool, `fetch_page`. Settings carry `search_scopes`, a bit
per scope.

The HTTP client gained what a search engine needs: request headers, a user agent
per session, an in-memory cookie jar, compressed transfers, POST, and a session
sized by the caller, so search no longer allocates the model downloader's 4 MB
buffer.

Engines that need more than a header are not in the table, and measurement says
so: Mojeek answers every request with an Altcha proof-of-work challenge, Qwant
and Yep answer 403 from behind Cloudflare, Brave answers 429, Startpage
redirects into a cookie flow, Ecosia blocks the request outright, and DuckDuckGo
throttles both of its endpoints after a few queries. Browser-like headers change
none of those answers. Solving Mojeek's challenge is the one remaining option
with a published client path; whether to pay that proof of work is a decision to
take deliberately, and it is not implemented here.

## Verification sequence

1. Done: on PPSA99023, confirm the title is running, check `/v1/models`, and
   verify `/api/search` and the MCP `web_search` call return live URLs.
2. Done: API tests cover schema validation, tool history, tool-choice modes,
   streaming, round trips, and rejected malformed calls.
3. Done: Qwen 3.8 9B Distill Q4_K_M completed a controlled OpenCode read-edit-test
   task at 16K context; Mistral 7B remains a weaker control that failed automatic
   read-tool selection.
4. Remaining: test a broader sequence (symbol lookup, one-line code change,
   project test, and cited web research) in a disposable checkout and visually
   confirm the web UI search flow.
5. Done: update `docs/OPENCODE.md` with the tested profile and the model/search
   limits above; keep this file as the status record for remaining qualification.

## Architecture

```text
OpenCode ── OpenAI-compatible API ──> ProsperoAI on PS5 ──> local GGUF model
    │                                        │
    ├── repository tools run locally         └── embedded multi-site search engine
    └── optional hosted websearch or MCP web_search bridge
```

The upstream LibreY project is a PHP web app and does not run inside the PS5
title. ProsperoAI started from a native C++ adapter for its DuckDuckGo HTML
text-search flow and grew its own provider table. Search requests go from the
console to the selected providers over HTTPS; results are
untrusted model input. The MCP bridge forwards OpenCode searches to the app.

## References

- [ProsperoAI OpenCode setup](OPENCODE.md)
- [Example OpenCode profile](examples/opencode.prosperoai.json)
- [OpenCode providers](https://opencode.ai/docs/providers/)
- [OpenCode MCP servers](https://opencode.ai/docs/mcp-servers/)
- [OpenCode websearch settings and providers](https://opencode.ai/docs/websearch/)
- [LibreY source and license](https://github.com/Ahwxorg/LibreY)
- [LibreY DuckDuckGo adapter](https://github.com/Ahwxorg/LibreY/blob/main/engines/text/duckduckgo.php)
