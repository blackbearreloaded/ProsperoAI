# Context/KV Settings and Memory-Aware Downloads Implementation Plan

> **Original planning workflow (historical):** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Progress — 2026-10-10

The user requested host-only implementation first; console work is deferred. The later
measurement gates remain open. This progress includes local working-tree changes, which
have not been committed or pushed to PR #15.

### Completed on the host

- [x] Phase 0: interrupted standalone/bundle downloads resume and remain verified.
- [x] Phase 4a: replace the 7 GiB cap with file-size fit estimates; HTTP, native and web
  browsing report fit. These estimates do not guarantee model/context allocation success.
- [x] Phase 2: Auto context budgeting, clamped to the model training context; account for
  q8_0/q4_0 block overhead and reserve 1.5 GiB for compute/interface. Hybrid KV estimates
  conservatively count every layer; the actual RADV ceiling is still unknown.
- [x] Phase 2: native/web Context length (Auto, 4K–128K) and Thinking controls, shared
  persisted settings, authenticated GET/POST `/api/settings/runtime`.
- [x] User clarification: **KV precision is automatic, with no manual UI control**.
  Auto uses f16 up to 8K and q8_0 above; legacy manual `kv=` values normalize to Auto.
  The HTTP endpoint accepts only `kv: "auto"`. Context is the user-adjustable memory setting.
- [x] Phase 2 runtime wiring: settings recreate only the context, keeping loaded weights;
  `/app0/context_size.txt` retains precedence. HTTP generation uses saved settings rather
  than per-request context overrides. Thinking-off closes an advertised `<think>` marker.
- [x] Ordinary transformer prefix reuse tracks only successfully decoded tokens, retries
  from a full prefill when rollback fails, and re-decodes the final token for fresh logits.
  Hybrid/recurrent models conservatively perform full prefill.
- [x] Host validation: 35 tests passed, including old-settings defaults/round-trip, invalid
  HTTP settings, download resume, fit boundaries, KV arithmetic and prefix calculations.
- [x] `make app-vulkan-folder`: test title <TEST-TITLE-ID> builds and signs. ELF entry 0x120,
  14 program headers; comparison with the older Vulkan ELF found no added PRX
  dependency (Http2, Ssl and kernel_sys imports are absent).
- [x] Native host preview rendered and Generation screenshot inspected; JS syntax passed.

### Not completed / console work next

- [ ] Phase 0: real PS5 download throughput, interrupt/resume and cancel verification.
- [ ] Phase 1: manual Bonsai Q1_0 load/output/speed gate.
- [ ] Phase 2: PS5 context allocation and flash-attention validation; Mistral 4K/16K/32K,
  Qwen3.5 32K/128K, reference output, tokens/s and second-turn time to first token.
- [ ] Phase 2: measure the RADV allocation ceiling and verify automatic sizing on hardware.
- [ ] Phase 2: live memory bar, used/max context status, per-model overrides and recurrent
  state checkpoints/reuse. The current implementation always fully prefills hybrid models.
- [ ] Phase 3: pinned Bonsai preset, sampling/output-cap changes, think-display handling
  and the Bonsai 64K stability gate. The existing 512-token runtime cap remains.
- [ ] Phase 4b: Ternary Bonsai output/speed comparison. Phase 4c/4d remain gated research.
- [ ] Phase 5: source pinning and media migration to Vulkan, followed by console comparisons.

No console connection, deployment, title launch or registration change was made.

The implementation follows Tasks 1–8 with these changes from the original steps:
KV is automatic per the user's clarification; settings are shared through a synchronized
file store rather than exposing the UI-owned App to HTTP worker threads; generation applies
configuration atomically without a separate global configure call. Console-only behavior
is compiled, not proven. Original per-step red-test/commit instructions below are not a
record of executed actions. No new commit or push was made during this continuation.

**Goal:** Build the parts of `docs/PLAN_CONTEXT_MODELS_VULKAN.md` Phase 2 (context length / KV cache / thinking settings) and Phase 4a (memory-aware download limit) that can be written and verified on the host, with no PS5 required.

**Architecture:** Two independent task groups sharing one plan because both are small, both come from the same spec document, and splitting would only duplicate the header/scope boilerplate. They touch disjoint files (the downloader vs. the runtime/UI/storage/HTTP stack) and can be done in either order, or interleaved, with no dependency between them. Within Group B, pure arithmetic (KV-size budgeting, prefix-reuse point) is factored out of the PS5-only runtime file into host-testable free functions; the PS5-only glue that calls them is written but, per scope, not behaviorally verified here — only compiled.

**Tech Stack:** C++20, llama.cpp/ggml (vendored under `.deps/llama.cpp`), nlohmann/json, Python 3 `unittest` host test harnesses that shell out to `clang++`/`g++`, the project's own `ui-kit` Form widget.

**Spec:** `docs/PLAN_CONTEXT_MODELS_VULKAN.md` (Phase 2, Phase 4a)

## Global Constraints

- Every task's verification must run with no PS5 attached: `make test` (Python `unittest discover`), a standalone host test binary, or `make vulkan-smoke` (cross-compiles for the PS5 target but runs nothing — AGENTS.md: "A build that links is not a build that runs").
- `make deps` must already have produced `.deps/llama.cpp/vendor/nlohmann/json.hpp` before any task that compiles `model_downloader_ps5.cpp` or `http_server.cpp`'s test harnesses (`test -f .deps/llama.cpp/vendor/nlohmann/json.hpp`); it already does on this host.
- Follow the existing host-test pattern exactly: a `tests/<name>_test.cpp` that either `#include`s the `.cpp` under test with syscalls/externs mocked via `#define`, or links the real translation unit, paired with a `tests/test_<name>.py` that `subprocess.run`s `os.environ.get("HOST_CXX", "clang++")` with the same flags neighboring tests use, then runs the resulting binary. `make test` discovers it automatically (`python3 -m unittest discover -s tests -p 'test_*.py'`).
- Settings persistence stays in the existing `key=value\n` text format at `PROSPERO_SETTINGS_PATH` (`src/native_app.cpp`), bumping nothing — new keys are additive and `sscanf`-parsed the same way as `style`/`tokens`/`theme` today; a file without them must still load with defaults (no version bump needed, the format is already key/value).
- `/app0/context_size.txt` remains the developer override it is today (`vulkan/gpt_runtime_vulkan.cpp:126-131`) and keeps taking precedence over the new `context=` setting — this plan does not change that precedence.
- Do not touch `sce_sys/param.json`, the title ID, or anything that needs a console registration change.
- Out of scope (explicitly, per the user's request and the spec's own gating): Phase 1 (Bonsai manual console test), Phase 2's console gate measurements (tokens/s, the actual RADV allocation ceiling, the Settings memory bar that needs live device-free-memory numbers to mean anything), Phase 3, Phase 4b/4c, Phase 5. Phase 2 item 2.2's "memory bar... instead of the live preview" is also deferred — it needs real `ggml_backend_dev_memory` numbers from a device to be worth building and testing; the three new form controls it would sit next to are in scope.

## Review Focus

- A settings file saved by an older build (no `context=`/`kv=`/`thinking=` lines) must still load with sane defaults, not corrupt `style`/`tokens`/`theme` parsing — Task 6's round-trip test loads a pre-existing two-field-only file first.
- A GGUF whose `n_ctx_train` is smaller than the requested/auto context must clamp, not request more than the model supports — Task 3's unit test covers a model with a small `n_ctx_train`.
- A catalog entry exactly at the "does not fit" boundary, and one at 0 bytes (a metadata entry with no size), must not crash the fit estimator or be silently mis-classified — Task 1's unit test covers both.
- The prefix-reuse calculator must not claim a reusable prefix longer than the shorter of the two token sequences, and must return 0 (full reprefill) when the first token differs — Task 4's unit test covers both an empty old sequence and a one-token mismatch at position 0.
- The KV cache type chosen by Auto must never pick a type that needs flash attention when the spec says quantized V requires it, without also setting `flash_attn_type` — Task 3's unit test asserts the returned struct always pairs a quantized `type_v` with flash attention requested.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `vulkan/model_downloader_ps5.cpp` (modify) | Replace the fixed `kMaxModelBytes` catalog filter with a three-way fit estimate; add `Fit` to `Item`/`Candidate`. |
| `include/model_downloader_ps5.hpp` (modify) | Add `Fit` enum and the `fit` field on `Candidate`. |
| `vulkan/http_server.cpp` (modify) | Serialize `fit` on browse-candidate JSON items; add `GET`/`POST /api/settings/runtime`. |
| `src/native_ui_screens.cpp` (modify) | Show a fit badge per candidate row instead of the flat "Up to 7 GiB" caption. |
| `tests/downloader_fit_test.cpp` (create) | Host unit test for the pure fit-estimate function. |
| `tests/test_downloader_fit.py` (create) | Compiles and runs the above. |
| `include/kv_budget.hpp` (create) | Pure, host-testable KV-size/context-size Auto calculator and the KV-cache-type enum shared with settings storage and the runtime. |
| `src/kv_budget.cpp` (create) | Its implementation. |
| `tests/kv_budget_test.cpp` (create) | Host unit test for the Auto calculator. |
| `tests/test_kv_budget.py` (create) | Compiles and runs the above. |
| `include/prefix_reuse.hpp` (create) | Pure, host-testable "how many leading tokens are unchanged" calculator. |
| `src/prefix_reuse.cpp` (create) | Its implementation. |
| `tests/prefix_reuse_test.cpp` (create) | Host unit test for the prefix calculator. |
| `tests/test_prefix_reuse.py` (create) | Compiles and runs the above. |
| `include/gpt_runtime.hpp` (modify) | Extend `gpt_runtime_settings_t` with `context_length`, `kv_cache`, `thinking`; add `gpt_runtime_configure()`. |
| `vulkan/gpt_runtime_vulkan.cpp` (modify) | Use `kv_budget`/`prefix_reuse` helpers instead of the hardcoded 4096/f16 behavior; PS5-only, verified by cross-compiling, not by running. |
| `include/native_app.hpp` (modify) | Add `context_length`, `kv_cache`, `thinking` to `Preferences`. |
| `src/native_app.cpp` (modify) | Extend `validate()`, `load_preferences()`, `save_preferences()` for the three new keys. |
| `tests/native_controller_test.cpp` (modify) | Add a preferences round-trip assertion for the three new fields. |
| `src/native_ui.cpp` (modify) | Add "Context length", "KV cache", "Thinking" controls to the Generation settings category. |
| `assets/web/app.js`, `assets/web/index.html` (modify) | Mirror the three settings in the web Settings panel, talking to the new HTTP endpoint. |

---

## Task Group A — Phase 4a: memory-aware download limit

### Task 1: Pure fit-estimate function

**Files:**
- Modify: `include/model_downloader_ps5.hpp`
- Modify: `vulkan/model_downloader_ps5.cpp:33-35` (constants), `:184` (the filter)
- Create: `tests/downloader_fit_test.cpp`
- Create: `tests/test_downloader_fit.py`

**Interfaces:**
- Produces: `enum class Fit { Fits, Tight, DoesNotFit };` and `Fit estimate_fit(std::uint64_t file_bytes);` in `prospero_model_download` (declared in the header, defined in the `.cpp`, callable from the catalog filter and later from `http_server.cpp`/native UI through `Candidate::fit`).
- Consumes: nothing from other tasks.

The RADV heap is fixed-size on this console (11,712 MiB reported, `docs/VK_LOG.md`), so unlike Phase 2's KV budgeting there is no live device query to make this host-testable — the ceiling is a constant. Use the budget the spec already computed: about 10.4 GiB usable, roughly split as weights (the GGUF file size itself, since this is before quantization-aware KV math is available — Phase 2's `kv_budget` is model-architecture-aware and GGUF-metadata-driven; the downloader only has HTTP catalog metadata, i.e. just the byte size) plus a flat reserve for KV/compute/interface. Pick thresholds from the spec's own numbers: fits below 8 GiB (comfortably under the smallest "yes" row in the spec's budget table), tight from 8 GiB up to the hard ceiling (11.7 GiB minus the ~1 GiB interface reserve = 10.7 GiB), does-not-fit above that.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/downloader_fit_test.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#define PS5_LLAMA_VULKAN
#include "../include/model_downloader_ps5.hpp"
#include <cassert>

int main() {
    using prospero_model_download::Fit;
    using prospero_model_download::estimate_fit;
    assert(estimate_fit(0) == Fit::Fits);
    assert(estimate_fit(3ULL * 1024 * 1024 * 1024) == Fit::Fits);          // Mistral Q4_0-sized
    assert(estimate_fit(8ULL * 1024 * 1024 * 1024) == Fit::Tight);         // the spec's Q2_g64 7.59 GB file plus margin lands here
    assert(estimate_fit(10ULL * 1024 * 1024 * 1024) == Fit::Tight);
    assert(estimate_fit(11ULL * 1024 * 1024 * 1024) == Fit::DoesNotFit);   // above the 10.7 GiB ceiling
    assert(estimate_fit(~0ULL) == Fit::DoesNotFit);                        // no overflow/UB at the top of the range
}
```

```python
# tests/test_downloader_fit.py
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DownloaderFitTests(unittest.TestCase):
    def test_fit_thresholds(self):
        with tempfile.TemporaryDirectory(prefix="prospero-fit-") as directory:
            binary = Path(directory) / "fit"
            compiler = os.environ.get("HOST_CXX", "clang++")
            subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "include"),
                str(ROOT / "tests/downloader_fit_test.cpp"),
                str(ROOT / "vulkan/model_downloader_ps5.cpp").replace(
                    str(ROOT / "vulkan/model_downloader_ps5.cpp"),
                    str(ROOT / "vulkan/model_downloader_ps5.cpp")),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
```

Note: `estimate_fit` must not need `nlohmann/json.hpp`, libcurl, or any of the downloader's networking code, so it is declared in the header and defined in a part of `model_downloader_ps5.cpp` that does not drag those includes into the test binary — see Step 3's placement note. If keeping it inside `model_downloader_ps5.cpp` turns out to pull in `net/https_get.h` anyway (it does today, as the top-of-file include block shows), link `vulkan/model_downloader_ps5.cpp` itself into the test like `downloader_storage_test.cpp` already does for the storage test — i.e. compile the test the same way as Step 1's harness:

```
compiler -std=c++20 -O1 -I include -I .deps/llama.cpp/vendor \
  tests/downloader_fit_test.cpp -o fit
```

where `tests/downloader_fit_test.cpp` does `#include "../vulkan/model_downloader_ps5.cpp"` after defining `PS5_LLAMA_VULKAN`, exactly like `downloader_storage_test.cpp` does, and provides the same `prospero_https_*` stand-ins that file already has (copy that pattern rather than re-deriving it, since the whole `.cpp` has to link). Use this form for the actual test file; the snippet above is the logic to assert, not the final includes.

- [ ] **Step 2: Run test to verify it fails**

Run: `clang++ -std=c++20 -O1 -Wall -Wextra -I include -I .deps/llama.cpp/vendor tests/downloader_fit_test.cpp -o /tmp/fit`
Expected: FAIL to compile — `estimate_fit` and `Fit` are not declared yet.

- [ ] **Step 3: Write minimal implementation**

In `include/model_downloader_ps5.hpp`, add after `enum class State { ... };`:

```cpp
enum class Fit { Fits, Tight, DoesNotFit };
Fit estimate_fit(std::uint64_t file_bytes);
```

In `vulkan/model_downloader_ps5.cpp`, add inside `namespace prospero_model_download {` but *before* the `namespace { ... }` anonymous block that holds `kMaxCatalogBytes` (so it is visible to callers outside the file and does not depend on anything in the anonymous namespace):

```cpp
Fit estimate_fit(std::uint64_t file_bytes) {
    // The RADV heap is fixed (11,712 MiB reported, docs/VK_LOG.md); about 1 GiB goes to the
    // interface and a margin, leaving roughly 10.7 GiB. Below 8 GiB a model's KV/compute
    // overhead reliably fits in what is left (docs/PLAN_CONTEXT_MODELS_VULKAN.md's budget
    // table); above the 10.7 GiB ceiling nothing is left for KV or compute at all.
    constexpr std::uint64_t kComfortableBytes = 8ULL * 1024 * 1024 * 1024;
    constexpr std::uint64_t kCeilingBytes = 10ULL * 1024 * 1024 * 1024 + 717ULL * 1024 * 1024;
    if (file_bytes <= kComfortableBytes) return Fit::Fits;
    if (file_bytes <= kCeilingBytes) return Fit::Tight;
    return Fit::DoesNotFit;
}
```

Then change the catalog filter at line 184 from:

```cpp
if (item.size == 0 || item.size > kMaxModelBytes || item.sha256.size() != 64) continue;
```

to:

```cpp
item.fit = estimate_fit(item.size);
if (item.size == 0 || item.fit == Fit::DoesNotFit || item.sha256.size() != 64) continue;
```

and add `Fit fit = Fit::Fits;` to `struct Item { std::string name, sha256; std::uint64_t size = 0; };` (same line, 36) and to `struct Candidate { char name[160]; std::uint64_t size; };` in the header (add `Fit fit;`). Remove the now-unused `kMaxModelBytes` constant (line 35) — nothing else reads it after this change; confirm with `grep -n kMaxModelBytes vulkan/model_downloader_ps5.cpp` before deleting.

Populate `Candidate::fit` wherever `Candidate` is filled from an `Item` (the `candidate()` function around line 597) — set `out->fit = items[i].fit;` alongside the existing `name`/`size` copy.

- [ ] **Step 4: Run test to verify it passes**

Run: `clang++ -std=c++20 -O1 -Wall -Wextra -I include -I .deps/llama.cpp/vendor tests/downloader_fit_test.cpp -o /tmp/fit && /tmp/fit`
Expected: PASS, no assertion failures.

Then run it the way `make test` will: `cd /home/dietpi/Projects/console/ProsperoAI && python3 -m unittest tests.test_downloader_fit -v`
Expected: `ok`.

- [ ] **Step 5: Commit**

```bash
git add include/model_downloader_ps5.hpp vulkan/model_downloader_ps5.cpp tests/downloader_fit_test.cpp tests/test_downloader_fit.py
git commit -m "Replace the fixed 7 GiB download cap with a three-way memory fit estimate"
```

### Task 2: Surface fit in the HTTP API and native UI

**Files:**
- Modify: `vulkan/http_server.cpp` (the candidate-serialization loop inside `handle_model_download_status`, and the item loop inside `handle_model_presets` if presets also report size — check before editing)
- Modify: `src/native_ui_screens.cpp:403-433`

**Interfaces:**
- Consumes: `prospero_model_download::Candidate::fit` (`Fit` enum) from Task 1.
- Produces: nothing new for later tasks — this is the leaf of Group A.

- [ ] **Step 1: Write the failing test**

Extend `tests/http_api_harness.cpp`'s fake `prospero_model_download` namespace (it already fakes `candidate_count`/`candidate`/etc. for the harness — find the existing fake `candidate()` there) so one fake candidate reports `Fit::Tight`, and add an assertion to `tests/test_http_api.py`:

```python
    def test_browse_candidate_reports_fit(self):
        response = self.request("/api/models/download", method="GET")
        body = json.loads(response["body"])
        self.assertIn("fit", body["items"][0])
        self.assertEqual(body["items"][0]["fit"], "tight")
```

(Place this as a new test method in the existing `HttpApiTests` class in `tests/test_http_api.py`; read the file's existing `request()` helper and an existing GET-based test method first to match its exact call shape — do not guess the harness's existing fake candidate's `name`/state wiring, since `handle_model_download_status` only includes `items` when `state()` is `Ready` or `SearchReady`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests.test_http_api.HttpApiTests.test_browse_candidate_reports_fit -v`
Expected: FAIL — `"fit"` not in the JSON body yet.

- [ ] **Step 3: Write minimal implementation**

In `vulkan/http_server.cpp`, in the candidate-serialization loop inside `handle_model_download_status` (around line 47-61 of that function), add the fit string next to `size`/`downloads`:

```cpp
const char *fit_text = candidate.fit == prospero_model_download::Fit::Fits ? "fits"
                       : candidate.fit == prospero_model_download::Fit::Tight ? "tight"
                                                                               : "does_not_fit";
std::snprintf(item, sizeof(item),
              prospero_model_download::state() ==
                      prospero_model_download::State::SearchReady
                  ? "\",\"downloads\":%llu,\"fit\":\"%s\"}"
                  : "\",\"size\":%llu,\"fit\":\"%s\"}",
              static_cast<unsigned long long>(candidate.size), fit_text);
```

In `src/native_ui_screens.cpp`, replace the two `std::snprintf(row, ...)` branches (lines 414-420) to append a fit badge, and drop the now-inaccurate static caption:

```cpp
const char *fit_text = candidate.fit == prospero_model_download::Fit::Tight ? "  ·  tight on memory"
                        : candidate.fit == prospero_model_download::Fit::DoesNotFit
                            ? "  ·  will not fit"
                            : "";
if (downloader_state == prospero_model_download::State::SearchReady)
    std::snprintf(row, sizeof(row), "%s  ·  %llu downloads  ·  Cross to browse",
                  candidate.name, static_cast<unsigned long long>(candidate.size));
else
    std::snprintf(row, sizeof(row), "%s  ·  %llu MiB%s  ·  Cross to download",
                  candidate.name,
                  static_cast<unsigned long long>(candidate.size / (1024 * 1024)), fit_text);
```

and change line 432 from `"Up to 7 GiB per file; context also needs memory."` to `"Large files are flagged tight or will not fit; context also needs memory."`.

(`DoesNotFit` candidates are already excluded from the catalog by Task 1's filter, so that branch is dead for files discovered through search/browse today; keep it anyway since `Candidate::fit` is a general-purpose field and a future caller — e.g. an already-installed, larger model found by directory scan — could legitimately report it.)

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests.test_http_api -v`
Expected: all tests `ok`, including the new one.

Build/visual-check the native UI change without a console using the host preview (AGENTS.md read-order item 1: `docs/BUILDING.md` for `host/ui_preview.cpp`):
Run: `python3 tools/host-ui-preview.py` (or the specific invocation `docs/BUILDING.md`'s "PS5 llama.cpp Vulkan test build" / UI preview section documents — read that section before running) and visually confirm a candidate row renders the new badge text without truncation at the existing `fit(row, 18, 1020, false)` width.

- [ ] **Step 5: Commit**

```bash
git add vulkan/http_server.cpp src/native_ui_screens.cpp tests/http_api_harness.cpp tests/test_http_api.py
git commit -m "Report memory fit on download candidates in the HTTP API and native UI"
```

---

## Task Group B — Phase 2: context/KV cache settings

### Task 3: Pure KV-budget Auto calculator

**Files:**
- Create: `include/kv_budget.hpp`
- Create: `src/kv_budget.cpp`
- Create: `tests/kv_budget_test.cpp`
- Create: `tests/test_kv_budget.py`

**Interfaces:**
- Produces:
  ```cpp
  namespace prospero_kv {
  enum class KvType { F16, Q8_0, Q4_0 };
  struct ModelKvShape {
      unsigned kv_layers;       // layers that keep a KV cache (hybrid models have fewer than total)
      unsigned kv_heads;
      unsigned head_dim;
      unsigned n_ctx_train;
  };
  struct KvChoice {
      unsigned context_length;  // chosen n_ctx, <= n_ctx_train
      KvType kv_type;
      bool flash_attention;     // true whenever kv_type != F16 (quantized V needs it)
  };
  // weights_bytes/compute_reserve_bytes/free_bytes come from GGUF metadata and
  // ggml_backend_dev_memory on the caller's side; this function has no I/O.
  KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                       std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes);
  // Bytes per token of KV cache at a given type, for one call site (the context-length
  // Settings control) to show the user "would need N more GiB" style feedback later.
  std::uint64_t kv_bytes_per_token(const ModelKvShape &shape, KvType type);
  }
  ```
- Consumes: nothing from other tasks (pure arithmetic, no model loading).

Per the spec: "Use f16 up to 8K and q8_0 above" (section 2.1), pick the largest power-of-two-ish step from `{4096, 8192, 16384, 32768, 65536, 131072}` (bounded by `n_ctx_train`) that fits in `free_bytes - weights_bytes - compute_reserve_bytes` at the type the step implies.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/kv_budget_test.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../include/kv_budget.hpp"
#include <cassert>

int main() {
    using namespace prospero_kv;
    // Mistral 7B v0.3 shape from docs/PLAN_CONTEXT_MODELS_VULKAN.md: 32 layers, 8 KV heads x 128 dim.
    ModelKvShape mistral{32, 8, 128, 32768};
    assert(kv_bytes_per_token(mistral, KvType::F16) == 32ULL * 8 * 128 * 2 * 2); // layers * heads * dim * (K+V) * 2 bytes
    // Plenty of free memory: picks the largest step <= n_ctx_train at f16 up to 8K, q8_0 above.
    {
        auto choice = choose_auto(mistral, /*weights=*/4ULL * 1024 * 1024 * 1024,
                                   /*compute reserve=*/512ULL * 1024 * 1024,
                                   /*free=*/11ULL * 1024 * 1024 * 1024);
        assert(choice.context_length <= mistral.n_ctx_train);
        assert(choice.context_length >= 4096);
        if (choice.context_length > 8192) {
            assert(choice.kv_type == KvType::Q8_0);
            assert(choice.flash_attention);
        } else {
            assert(choice.kv_type == KvType::F16);
            assert(!choice.flash_attention);
        }
    }
    // A model whose n_ctx_train is small clamps rather than overshoots it.
    {
        ModelKvShape tiny{4, 2, 64, 2048};
        auto choice = choose_auto(tiny, 100ULL * 1024 * 1024, 64ULL * 1024 * 1024,
                                   8ULL * 1024 * 1024 * 1024);
        assert(choice.context_length == 2048);
    }
    // Memory too tight even for the smallest step still returns the smallest step, not zero.
    {
        auto choice = choose_auto(mistral, 4ULL * 1024 * 1024 * 1024, 512ULL * 1024 * 1024,
                                   /*free=*/4ULL * 1024 * 1024 * 1024 + 600ULL * 1024 * 1024);
        assert(choice.context_length == 4096);
    }
    // Every quantized choice pairs with flash attention, as the spec requires.
    for (unsigned free_gib = 4; free_gib <= 16; ++free_gib) {
        auto choice = choose_auto(mistral, 4ULL * 1024 * 1024 * 1024, 512ULL * 1024 * 1024,
                                   static_cast<std::uint64_t>(free_gib) * 1024 * 1024 * 1024);
        assert(choice.kv_type == KvType::F16 || choice.flash_attention);
    }
}
```

```python
# tests/test_kv_budget.py
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class KvBudgetTests(unittest.TestCase):
    def test_auto_choice_rules(self):
        with tempfile.TemporaryDirectory(prefix="prospero-kv-") as directory:
            binary = Path(directory) / "kv-budget"
            compiler = os.environ.get("HOST_CXX", "clang++")
            subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "include"),
                str(ROOT / "tests/kv_budget_test.cpp"), str(ROOT / "src/kv_budget.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `clang++ -std=c++20 -O1 -Wall -Wextra -I include tests/kv_budget_test.cpp src/kv_budget.cpp -o /tmp/kv`
Expected: FAIL to compile — `include/kv_budget.hpp` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// include/kv_budget.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace prospero_kv {
enum class KvType { F16, Q8_0, Q4_0 };
struct ModelKvShape {
    unsigned kv_layers;
    unsigned kv_heads;
    unsigned head_dim;
    unsigned n_ctx_train;
};
struct KvChoice {
    unsigned context_length;
    KvType kv_type;
    bool flash_attention;
};
std::uint64_t kv_bytes_per_token(const ModelKvShape &shape, KvType type);
KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                      std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes);
}
```

```cpp
// src/kv_budget.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "kv_budget.hpp"
#include <algorithm>
#include <array>

namespace prospero_kv {
namespace {
unsigned type_bytes(KvType type) {
    switch (type) {
    case KvType::F16: return 2;
    case KvType::Q8_0: return 1;
    case KvType::Q4_0: return 1; // packed below a byte; the per-token helper rounds up.
    }
    return 2;
}
}

std::uint64_t kv_bytes_per_token(const ModelKvShape &shape, KvType type) {
    // K and V, one cache each, type_bytes() bytes per element (q4_0's 4 bits/element is
    // approximated as 1 byte here as a conservative upper bound; good enough for admission).
    return static_cast<std::uint64_t>(shape.kv_layers) * shape.kv_heads * shape.head_dim * 2 *
           type_bytes(type);
}

KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                      std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes) {
    constexpr std::array<unsigned, 6> steps{4096, 8192, 16384, 32768, 65536, 131072};
    const std::uint64_t usable =
        free_bytes > weights_bytes + compute_reserve_bytes
            ? free_bytes - weights_bytes - compute_reserve_bytes
            : 0;
    KvChoice best{steps[0], KvType::F16, false};
    for (unsigned step : steps) {
        if (step > shape.n_ctx_train) break;
        const KvType type = step <= 8192 ? KvType::F16 : KvType::Q8_0;
        const std::uint64_t needed =
            static_cast<std::uint64_t>(step) * kv_bytes_per_token(shape, type);
        if (needed > usable && step != steps[0]) break; // keep the previous (smaller) fit
        best = {step, type, type != KvType::F16};
    }
    return best;
}
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `clang++ -std=c++20 -O1 -Wall -Wextra -I include tests/kv_budget_test.cpp src/kv_budget.cpp -o /tmp/kv && /tmp/kv`
Expected: PASS.

Run: `python3 -m unittest tests.test_kv_budget -v`
Expected: `ok`.

- [ ] **Step 5: Commit**

```bash
git add include/kv_budget.hpp src/kv_budget.cpp tests/kv_budget_test.cpp tests/test_kv_budget.py
git commit -m "Add a pure Auto KV-cache/context-length budget calculator"
```

### Task 4: Pure prefix-reuse calculator

**Files:**
- Create: `include/prefix_reuse.hpp`
- Create: `src/prefix_reuse.cpp`
- Create: `tests/prefix_reuse_test.cpp`
- Create: `tests/test_prefix_reuse.py`

**Interfaces:**
- Produces: `std::size_t prospero_kv::common_prefix_length(const llama_token *old_tokens, std::size_t old_count, const llama_token *new_tokens, std::size_t new_count);` where `llama_token` is `std::int32_t` (matching `llama.h`'s typedef) so this header does not need to include `llama.h` at all.
- Consumes: nothing from other tasks.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/prefix_reuse_test.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../include/prefix_reuse.hpp"
#include <cassert>

int main() {
    using prospero_kv::common_prefix_length;
    const std::int32_t a[] = {1, 2, 3, 4};
    const std::int32_t b[] = {1, 2, 3, 4, 5};
    assert(common_prefix_length(a, 4, b, 5) == 4);     // b just grew: whole old prefix reused
    assert(common_prefix_length(a, 4, a, 4) == 4);      // identical: fully reused
    assert(common_prefix_length(nullptr, 0, b, 5) == 0); // nothing to reuse yet
    const std::int32_t c[] = {1, 2, 9, 4};
    assert(common_prefix_length(a, 4, c, 4) == 2);      // diverges at index 2
    const std::int32_t d[] = {9, 2, 3, 4};
    assert(common_prefix_length(a, 4, d, 4) == 0);      // diverges at index 0: full reprefill
    assert(common_prefix_length(a, 4, b, 2) == 2);      // shorter new sequence caps the result
}
```

```python
# tests/test_prefix_reuse.py
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PrefixReuseTests(unittest.TestCase):
    def test_common_prefix_rules(self):
        with tempfile.TemporaryDirectory(prefix="prospero-prefix-") as directory:
            binary = Path(directory) / "prefix-reuse"
            compiler = os.environ.get("HOST_CXX", "clang++")
            subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "include"),
                str(ROOT / "tests/prefix_reuse_test.cpp"), str(ROOT / "src/prefix_reuse.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `clang++ -std=c++20 -O1 -Wall -Wextra -I include tests/prefix_reuse_test.cpp src/prefix_reuse.cpp -o /tmp/prefix`
Expected: FAIL to compile — `include/prefix_reuse.hpp` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// include/prefix_reuse.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace prospero_kv {
std::size_t common_prefix_length(const std::int32_t *old_tokens, std::size_t old_count,
                                  const std::int32_t *new_tokens, std::size_t new_count);
}
```

```cpp
// src/prefix_reuse.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prefix_reuse.hpp"
#include <algorithm>

namespace prospero_kv {
std::size_t common_prefix_length(const std::int32_t *old_tokens, std::size_t old_count,
                                  const std::int32_t *new_tokens, std::size_t new_count) {
    const std::size_t limit = std::min(old_count, new_count);
    std::size_t i = 0;
    while (i < limit && old_tokens && new_tokens && old_tokens[i] == new_tokens[i]) ++i;
    return i;
}
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `clang++ -std=c++20 -O1 -Wall -Wextra -I include tests/prefix_reuse_test.cpp src/prefix_reuse.cpp -o /tmp/prefix && /tmp/prefix`
Expected: PASS.

Run: `python3 -m unittest tests.test_prefix_reuse -v`
Expected: `ok`.

- [ ] **Step 5: Commit**

```bash
git add include/prefix_reuse.hpp src/prefix_reuse.cpp tests/prefix_reuse_test.cpp tests/test_prefix_reuse.py
git commit -m "Add a pure common-prefix calculator for conversation prefill reuse"
```

### Task 5: Wire the runtime API (PS5-only; compiled, not run, here)

**Files:**
- Modify: `include/gpt_runtime.hpp`
- Modify: `vulkan/gpt_runtime_vulkan.cpp:124-141` (context creation), `:236` (`llama_memory_clear` call), `:244` (`reused_tokens` is currently never set)

**Interfaces:**
- Consumes: `prospero_kv::KvType`, `prospero_kv::ModelKvShape`, `prospero_kv::choose_auto`, `prospero_kv::kv_bytes_per_token` (Task 3); `prospero_kv::common_prefix_length` (Task 4).
- Produces: `gpt_runtime_settings_t` gains `unsigned context_length = 0;` (0 means Auto) and `const char *kv_cache = nullptr;` (`nullptr`/`"auto"`, `"f16"`, `"q8_0"`, `"q4_0"`) and `bool thinking = false;`, consumed by later tasks' storage/UI/HTTP wiring, which must pass these through unchanged field names.

This task's own verification is a host cross-compile, not a behavior test — the spec's own gate (tokens/s, memory headroom with a real device) is explicitly out of scope and stays a console task. Do not claim this task is "tested" beyond compiling; say so plainly in the commit/PR notes.

- [ ] **Step 1: Extend the settings struct**

In `include/gpt_runtime.hpp`, add to `gpt_runtime_settings_t`:

```cpp
unsigned context_length = 0;   // 0 = Auto (see prospero_kv::choose_auto)
const char *kv_cache = nullptr; // nullptr/"auto", "f16", "q8_0", "q4_0"
bool thinking = false;
```

and add `unsigned reused_tokens_available = 0;` is unnecessary — `gpt_runtime_stats_t::reused_tokens` already exists (line 28); Task 5 only has to *fill* it, not add it.

- [ ] **Step 2: Use the KV budget and prefix reuse in `prepare()`/`generate()`**

In `vulkan/gpt_runtime_vulkan.cpp`, add `#include "kv_budget.hpp"` and `#include "prefix_reuse.hpp"` near the other local includes (line 8 area). Replace the fixed `cp.n_ctx` block (lines 124-132):

```cpp
auto cp = llama_context_default_params();
unsigned requested_context = 4096;
if (FILE *config = std::fopen("/app0/context_size.txt", "r")) {
    unsigned value = 0;
    if (std::fscanf(config, "%u", &value) == 1 && value >= 512 && value <= 16384)
        requested_context = value;
    std::fclose(config);
}
cp.n_ctx = std::min<uint32_t>(requested_context, llama_model_n_ctx_train(model));
```

with a version that defers to the settings when the developer override file is absent and a context/KV choice was requested:

```cpp
auto cp = llama_context_default_params();
unsigned requested_context = 4096;
enum ggml_type kv_type = GGML_TYPE_F16;
bool flash_attention = false;
if (FILE *config = std::fopen("/app0/context_size.txt", "r")) {
    unsigned value = 0;
    if (std::fscanf(config, "%u", &value) == 1 && value >= 512 && value <= 16384)
        requested_context = value;
    std::fclose(config);
} else if (requested_context_setting) {
    // requested_context_setting/requested_kv_setting come from gpt_runtime_configure();
    // see Step 3 for where they are populated before prepare() runs.
    prospero_kv::ModelKvShape shape{model_kv_layers(model), model_kv_heads(model),
                                     model_head_dim(model),
                                     static_cast<unsigned>(llama_model_n_ctx_train(model))};
    if (requested_kv_is_auto) {
        const auto choice = prospero_kv::choose_auto(shape, llama_model_size(model),
                                                       512ULL * 1024 * 1024, free_bytes);
        requested_context = choice.context_length;
        kv_type = choice.kv_type == prospero_kv::KvType::F16 ? GGML_TYPE_F16
                  : choice.kv_type == prospero_kv::KvType::Q8_0 ? GGML_TYPE_Q8_0
                                                                 : GGML_TYPE_Q4_0;
        flash_attention = choice.flash_attention;
    } else {
        requested_context = requested_context_setting;
        kv_type = parse_kv_type_setting(requested_kv_setting); // "f16"/"q8_0"/"q4_0" -> ggml_type
        flash_attention = kv_type != GGML_TYPE_F16;
    }
}
cp.n_ctx = std::min<uint32_t>(requested_context, llama_model_n_ctx_train(model));
cp.type_k = kv_type;
cp.type_v = kv_type;
if (flash_attention) cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
```

`model_kv_layers`/`model_kv_heads`/`model_head_dim` and `requested_context_setting`/`requested_kv_is_auto`/`requested_kv_setting`/`parse_kv_type_setting` are new small helpers this step also adds: the three `model_*` functions read the GGUF key-value metadata already exposed by `llama_model_meta_val_str`/`llama_model_n_ctx_train` (follow the pattern `llama.cpp`'s own `common/fit.cpp`, cited in the spec's Sources, uses — read that file once from `.deps/llama.cpp` before writing these, since the exact GGUF key names vary by architecture and this plan does not re-derive them); `requested_context_setting`/`requested_kv_is_auto`/`requested_kv_setting` are `namespace { }`-scoped variables set from `gpt_runtime_settings_t` the same way `selected`/`verbose_logging` already are, written in `gpt_runtime_generate()` right where it currently reads `settings.model_id` (line 197) — add `requested_context_setting = settings.context_length; requested_kv_is_auto = !settings.kv_cache || !std::strcmp(settings.kv_cache, "auto"); requested_kv_setting = settings.kv_cache;` there, guarded the same way the existing `if (settings.model_id)` block is, and call `release_model()` first if any of the three changed since the last `prepare()` (mirroring the existing `if (index != selected) { release_model(); ... }` pattern at line 204) so a context/KV change actually takes effect on the next `prepare()` instead of reusing the old context silently.

- [ ] **Step 3: Fill `reused_tokens` and skip re-decoding the shared prefix**

Replace the unconditional `llama_memory_clear(llama_get_memory(context), true);` (line 236) with prefix-aware logic: keep the previous turn's token vector in a `namespace { }` variable (`std::vector<llama_token> previous_tokens;`, next to `context`/`model`), and before the existing tokenize-and-decode loop:

```cpp
const std::size_t reused = prospero_kv::common_prefix_length(
    previous_tokens.data(), previous_tokens.size(), tokens.data(), static_cast<std::size_t>(n));
if (reused == 0 || reused > previous_tokens.size()) {
    llama_memory_clear(llama_get_memory(context), true);
} else if (reused < previous_tokens.size()) {
    llama_memory_seq_rm(llama_get_memory(context), 0, static_cast<llama_pos>(reused), -1);
}
if (stats) stats->reused_tokens = static_cast<unsigned>(reused);
```

and start the decode loop's `pos` at `reused` instead of `0` (the loop at line 238 currently does `for (int pos = 0; pos < n; )`; change to `for (int pos = static_cast<int>(reused); pos < n; )`), and after a successful generation, replace `previous_tokens` with the full token sequence that was actually in context (the prompt tokens plus whatever was sampled) so the next call has something to diff against — append each sampled `token` to `previous_tokens` inside the existing sampling loop (line 259-275), after `tokens` is copied into `previous_tokens` once before that loop starts.

This is exactly the kind of model-state-dependent logic the spec itself flags as unsafe for hybrid/recurrent models ("Hybrid models ... have recurrent state that cannot roll back to an arbitrary position"): guard the whole block with a check this task adds — `const bool can_reuse_prefix = !model_is_hybrid(model);` (another small GGUF-metadata helper, same caveat as Step 2's helpers about reading `common/fit.cpp` for the real key) — and fall back to the unconditional clear when it is false.

- [ ] **Step 4: Verify it cross-compiles**

Run (from `docs/BUILDING.md`'s PS5 Vulkan test build section, after confirming `make deps` and `make radv` have already been run on this host — they have): `make vulkan-smoke`
Expected: the build succeeds and produces the <TEST-TITLE-ID> host-side folder; this does not run anything and does not need a PS5. If `make vulkan-smoke` instead needs `make llama-ps5` first per `docs/BUILDING.md`, run that first — read the section once rather than guessing the dependency order.

- [ ] **Step 5: Commit**

```bash
git add include/gpt_runtime.hpp vulkan/gpt_runtime_vulkan.cpp
git commit -m "Use Auto KV budgeting and prefix reuse in the Vulkan runtime (console-verified later)"
```

### Task 6: Settings storage (`context=`, `kv=`, `thinking=`)

**Files:**
- Modify: `include/native_app.hpp:29-35` (`Preferences`)
- Modify: `src/native_app.cpp:68-78` (`validate`), `:80-133` (`load_preferences`), `:135-154` (`save_preferences`)
- Modify: `tests/native_controller_test.cpp`

**Interfaces:**
- Consumes: nothing directly (stores plain `unsigned`/`std::string` fields; the `"auto"/"f16"/"q8_0"/"q4_0"` strings match what Task 5's `kv_cache` expects and what Task 7's UI choice list offers, by construction, not by calling into Task 5's code).
- Produces: `Preferences::context_length` (`unsigned`, 0 = Auto), `Preferences::kv_cache` (`unsigned` index into `{"Auto","Quality f16","Balanced q8_0","Compact q4_0"}`, matching the existing `style`/`accent` pattern of storing a choice index rather than a string), `Preferences::thinking` (`bool`) — consumed by Task 7 (native UI) and Task 8 (HTTP/web).

- [ ] **Step 1: Write the failing test**

In `tests/native_controller_test.cpp`, find the block that already asserts preference persistence (`assert(app.state().preferences.model_id == "model-499");` and nearby `app.select_model`/`settle` calls). Add, right after an existing `settle(app)` once a model is selected:

```cpp
auto p = app.state().preferences;
p.context_length = 32768;
p.kv_cache = 2; // "Balanced q8_0"
p.thinking = true;
app.set_preferences(p);
settle(app);
assert(app.state().preferences.context_length == 32768);
assert(app.state().preferences.kv_cache == 2);
assert(app.state().preferences.thinking == true);
```

and, further down where the test already closes one `App` and opens a second one to prove persistence survives a restart (look for the second `prospero::App app;` block in `main()` — the file closes and reopens `App` to test exactly this for other fields), add:

```cpp
assert(app.state().preferences.context_length == 32768);
assert(app.state().preferences.kv_cache == 2);
assert(app.state().preferences.thinking == true);
```

- [ ] **Step 2: Run test to verify it fails**

Run (same invocation `tests/test_native_controller.py` uses — read it once more to copy the exact fonts/paths arguments, since `main()` takes `argv[1]`/`argv[2]` font paths):

```bash
python3 -m unittest tests.test_native_controller -v
```

Expected: FAIL to compile — `Preferences` has no `context_length`/`kv_cache`/`thinking` members yet.

- [ ] **Step 3: Write minimal implementation**

In `include/native_app.hpp`, change:

```cpp
struct Preferences
{
    unsigned style = 0, output_limit = 128;
    unsigned theme = 0, accent = 0, volume = 35, text_size = 0;
    bool reduced_motion = false, high_contrast = false;
    std::string model_id;
};
```

to:

```cpp
struct Preferences
{
    unsigned style = 0, output_limit = 128;
    unsigned theme = 0, accent = 0, volume = 35, text_size = 0;
    bool reduced_motion = false, high_contrast = false;
    unsigned context_length = 0; // 0 = Auto
    unsigned kv_cache = 0;       // index into {"Auto","Quality f16","Balanced q8_0","Compact q4_0"}
    bool thinking = false;
    std::string model_id;
};
```

In `src/native_app.cpp`'s `validate()`, add after the existing `p.text_size = std::min(p.text_size, 1U);`:

```cpp
p.kv_cache = std::min(p.kv_cache, 3U);
if (p.context_length != 0 &&
    p.context_length != 4096 && p.context_length != 8192 && p.context_length != 16384 &&
    p.context_length != 32768 && p.context_length != 65536 && p.context_length != 131072)
    p.context_length = 0;
```

In `load_preferences()`'s key/value loop, add three more `else if` branches next to the existing `text_size`/`reduced_motion` ones:

```cpp
else if (std::strcmp(key, "context") == 0)
    p.context_length = number;
else if (std::strcmp(key, "kv") == 0)
    p.kv_cache = number;
else if (std::strcmp(key, "thinking") == 0)
    p.thinking = number != 0;
```

(leave the legacy three-field `sscanf(line, "%u %u %u", ...)` branch alone — it is the pre-`version=2` format and never had these fields; files written by `save_preferences` always take the `key=value` branch below.)

In `save_preferences()`, extend the `fprintf` format string and argument list:

```cpp
const int bytes =
    std::fprintf(file,
                 "version=2\nmodel=%s\nstyle=%u\ntokens=%u\ntheme=%u\naccent=%u\nvolume=%"
                 "u\ntext_size=%u\nreduced_motion=%u\nhigh_contrast=%u\ncontext=%u\nkv=%u\n"
                 "thinking=%u\n",
                 p.model_id.c_str(), p.style, p.output_limit, p.theme, p.accent, p.volume,
                 p.text_size, p.reduced_motion ? 1U : 0U, p.high_contrast ? 1U : 0U,
                 p.context_length, p.kv_cache, p.thinking ? 1U : 0U);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests.test_native_controller -v`
Expected: `ok`. (This test already takes up to 30s and runs ASan/UBSan per its existing compile flags — that is expected, not a regression.)

- [ ] **Step 5: Commit**

```bash
git add include/native_app.hpp src/native_app.cpp tests/native_controller_test.cpp
git commit -m "Persist context length, KV cache type and thinking as settings"
```

### Task 7: Native Settings UI controls

**Files:**
- Modify: `src/native_ui.cpp:871-959` (`build_form`/`apply_form`, category 1 = Generation)

**Interfaces:**
- Consumes: `Preferences::context_length`, `Preferences::kv_cache`, `Preferences::thinking` (Task 6).
- Produces: nothing for later tasks (leaf of the native-UI side).

- [ ] **Step 1: Write the failing check**

This UI has no host unit test today (`build_form`/`apply_form` are rendered through the full `ui-kit` Form widget, exercised visually, not asserted against in `tests/native_controller_test.cpp`, which only drives `App` directly without the UI layer). Treat the host UI preview as the test: before editing, run the existing preview and confirm the Generation category currently shows only "Response style", "Response length" and "Processing" (the Step 4 check below re-runs this and diffs the result).

Run: whatever `docs/BUILDING.md` documents for `host/ui_preview.cpp` (read that section once; `tools/host-ui-preview.py` is the likely entry point per the earlier grep of this repo) and navigate to Settings → Generation.
Expected: three rows, no "Context length"/"KV cache"/"Thinking" rows yet.

- [ ] **Step 2: (no separate failing-compile step — this is a UI addition, not a new test binary)**

- [ ] **Step 3: Write the implementation**

In `build_form()`, in the `category_ == 1` branch, add after the existing `form_.add_value(9, "Processing", "On this console");`:

```cpp
const std::vector<std::string> context_options = {
    "Auto", "4K", "8K", "16K", "32K", "64K", "128K"};
const unsigned context_index = p.context_length == 0 ? 0
    : p.context_length == 4096 ? 1 : p.context_length == 8192 ? 2
    : p.context_length == 16384 ? 3 : p.context_length == 32768 ? 4
    : p.context_length == 65536 ? 5 : 6;
form_.add_choice(17, "Context length", context_options, static_cast<int>(context_index));
form_.add_choice(18, "KV cache", {"Auto", "Quality f16", "Balanced q8_0", "Compact q4_0"},
                 static_cast<int>(p.kv_cache));
form_.add_toggle(19, "Thinking", p.thinking);
```

In `apply_form()`, in the same `category_ == 1` branch, add after the existing `p.output_limit = ...` line:

```cpp
constexpr unsigned context_values[] = {0, 4096, 8192, 16384, 32768, 65536, 131072};
p.context_length = context_values[std::min<std::size_t>(
    static_cast<std::size_t>(form_.choice_index(17)), std::size(context_values) - 1)];
p.kv_cache = static_cast<unsigned>(form_.choice_index(18));
p.thinking = form_.toggle_value(19);
```

- [ ] **Step 4: Re-run the host preview to verify**

Run the same preview command as Step 1.
Expected: Generation now shows "Context length" (Auto/4K/.../128K), "KV cache" (Auto/Quality f16/Balanced q8_0/Compact q4_0) and "Thinking" (toggle), each remembering its value across a restart of the preview (since `save_preferences`/`load_preferences` from Task 6 back it).

- [ ] **Step 5: Commit**

```bash
git add src/native_ui.cpp
git commit -m "Add Context length, KV cache and Thinking controls to native Settings"
```

### Task 8: HTTP API and web Settings

**Files:**
- Modify: `vulkan/http_server.cpp` (new handler + route registration near line 902-915)
- Modify: `assets/web/app.js`, `assets/web/index.html`
- Modify: `tests/http_api_harness.cpp`, `tests/test_http_api.py`

**Interfaces:**
- Consumes: the same `context_length`/`kv_cache`/`thinking` shape as Tasks 6-7, serialized as JSON `{"context":<string|"auto">,"kv":<string>,"thinking":<bool>}` (strings, not indices, since this is the one place a non-C++ client reads/writes the setting — translate to/from the `Preferences` index encoding inside the handler, not at the JSON boundary).
- Produces: `GET`/`POST /api/settings/runtime`, consumed by `assets/web/app.js`.

This endpoint reads/writes `prospero::App`'s preferences the same way the native UI does; find how `http_server.cpp` currently reaches `App` for anything preference-like (grep it first — if nothing today calls into `App` from `http_server.cpp`, this task introduces that dependency, which must be declared explicitly here rather than assumed).

- [ ] **Step 1: Write the failing test**

Add to `tests/http_api_harness.cpp` (which already fakes enough of the surrounding system to serve `/api/...` requests) a round-trip test in `tests/test_http_api.py`:

```python
    def test_runtime_settings_round_trip(self):
        response = self.request("/api/settings/runtime", method="GET")
        body = json.loads(response["body"])
        self.assertIn("context", body)
        self.assertIn("kv", body)
        self.assertIn("thinking", body)
        post = self.request("/api/settings/runtime", method="POST",
                             body={"context": "32K", "kv": "q8_0", "thinking": True})
        self.assertEqual(json.loads(post["body"])["context"], "32K")
        confirm = self.request("/api/settings/runtime", method="GET")
        self.assertEqual(json.loads(confirm["body"])["kv"], "q8_0")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests.test_http_api.HttpApiTests.test_runtime_settings_round_trip -v`
Expected: FAIL — `404 Not Found`, the route does not exist.

- [ ] **Step 3: Write minimal implementation**

Add a handler in `vulkan/http_server.cpp`, modeled on `handle_model_presets`'s GET/POST-with-optional-body pattern:

```cpp
void handle_runtime_settings(const NetApi &api, int sock, prospero::App &app, const char *body = nullptr)
{
    if (body) {
        auto p = app.state().preferences;
        char context[16]{}, kv[16]{};
        if (json_find_string(body, "context", context, sizeof(context))) {
            p.context_length = !std::strcmp(context, "auto") ? 0
                : !std::strcmp(context, "4K") ? 4096 : !std::strcmp(context, "8K") ? 8192
                : !std::strcmp(context, "16K") ? 16384 : !std::strcmp(context, "32K") ? 32768
                : !std::strcmp(context, "64K") ? 65536 : !std::strcmp(context, "128K") ? 131072
                : p.context_length;
        }
        if (json_find_string(body, "kv", kv, sizeof(kv))) {
            p.kv_cache = !std::strcmp(kv, "auto") ? 0 : !std::strcmp(kv, "f16") ? 1
                : !std::strcmp(kv, "q8_0") ? 2 : !std::strcmp(kv, "q4_0") ? 3 : p.kv_cache;
        }
        const char *thinking_value = json_value_start(body, "thinking");
        if (thinking_value) p.thinking = std::strncmp(thinking_value, "true", 4) == 0;
        app.set_preferences(p);
    }
    const auto &p = app.state().preferences;
    constexpr const char *context_names[] = {"auto", "4K", "8K", "16K", "32K", "64K", "128K"};
    constexpr unsigned context_values[] = {0, 4096, 8192, 16384, 32768, 65536, 131072};
    const char *context_name = "auto";
    for (unsigned i = 0; i < std::size(context_values); ++i)
        if (context_values[i] == p.context_length) context_name = context_names[i];
    constexpr const char *kv_names[] = {"auto", "f16", "q8_0", "q4_0"};
    const auto response = nlohmann::json{
        {"context", context_name},
        {"kv", kv_names[std::min<unsigned>(p.kv_cache, 3)]},
        {"thinking", p.thinking},
    }.dump();
    send_json(api, sock, response.c_str());
}
```

and register the routes next to the other `/api/models/*` routes:

```cpp
else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/settings/runtime") == 0)
    handle_runtime_settings(api, sock, app);
else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/settings/runtime") == 0)
    handle_runtime_settings(api, sock, app, body);
```

(the exact way `app` — a `prospero::App&` — reaches `handle_connection`/its callers depends on what Step 1's grep found; if no `App&` is threaded through today, add it to `handle_connection`'s parameter list and its one caller, rather than reaching for a global.)

In `tests/http_api_harness.cpp`, provide whatever fake `App`/`set_preferences`/`state()` the real handler now calls, following the same faking style the file already uses for `prospero_model_download`.

In `assets/web/app.js`, find the existing Settings panel's fetch-on-open / save-on-change pattern (it already does this for whichever settings the web UI exposes today — read that code before adding to it) and add three controls wired to `GET`/`POST /api/settings/runtime`, matching the native UI's three options verbatim (`"Auto","4K","8K","16K","32K","64K","128K"` / `"Auto","Quality f16","Balanced q8_0","Compact q4_0"` / a checkbox for Thinking). Add the corresponding markup to `assets/web/index.html` next to whatever existing Settings fields are there.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests.test_http_api -v`
Expected: `ok`, including `test_runtime_settings_round_trip`.

- [ ] **Step 5: Commit**

```bash
git add vulkan/http_server.cpp assets/web/app.js assets/web/index.html tests/http_api_harness.cpp tests/test_http_api.py
git commit -m "Add GET/POST /api/settings/runtime and mirror it in the web Settings panel"
```

---

## Self-Review

**1. Spec coverage.**
- Phase 2, "Auto: compute KV per token from GGUF metadata... pick the largest step that fits... Use f16 up to 8K and q8_0 above" → Task 3.
- Phase 2, "Prefix reuse: prefill only the tokens after the first difference... fill `reused_tokens`... Hybrid models... cannot roll back" → Task 4 (calculator) + Task 5 Step 3 (wiring + hybrid guard).
- Phase 2, "`gpt_runtime_configure(context, kv_type)`... Set `type_k`, `type_v` and `flash_attn_type`" → Task 5 Step 2.
- Phase 2, "Native interface... Context length... KV cache... Thinking" → Task 7. (The memory bar is explicitly excluded in Global Constraints, with reasoning.)
- Phase 2, "Storage: `context=`, `kv=` and `thinking=` keys... `/app0/context_size.txt` stays as a developer override" → Task 6 (keys) + Task 5 Step 2 (override precedence preserved).
- Phase 2, "HTTP: `GET/POST /api/settings/runtime`, and the same choices in the web Settings" → Task 8.
- Phase 2 item 5 ("per-model override from the model card") and the console gate (item 6) are out of scope per Global Constraints, by the user's own instruction to skip console-dependent work.
- Phase 4a, "Replace the fixed 7 GiB download limit with a memory estimate... A model card shows fits, tight or does not fit" → Task 1 (estimate) + Task 2 (surfacing it).
- Phase 4a, "admits Ternary Bonsai Q2_g64 (7.07 GiB)" → covered: 7.07 GiB is below Task 1's 8 GiB "Fits" threshold and far below its ~10.7 GiB ceiling, so it is no longer excluded the way the old flat 7 GiB cap would have excluded it only barely — checked against Task 1's Step 1 test values.

**2. Placeholder scan.** No "TBD"/"handle appropriately"/bare "add tests" steps remain; Task 5 and Task 8 both have passages that depend on reading a real file first (`common/fit.cpp` for GGUF key names; `http_server.cpp`'s current `App` access) rather than inventing specifics that might not match the code — those are flagged as reading steps, not left vague, and the fallback behavior is spelled out either way.

**3. Type consistency.** `prospero_kv::KvType`/`ModelKvShape`/`KvChoice` (Task 3) are the exact names Task 5 Step 2 consumes. `prospero_kv::common_prefix_length(const std::int32_t*, std::size_t, const std::int32_t*, std::size_t)` (Task 4) matches Task 5 Step 3's call with `.data()`/`.size()` from a `std::vector<llama_token>` (`llama_token` is `int32_t` in `llama.h`). `Preferences::context_length`/`kv_cache`/`thinking` (Task 6) are the exact field names Task 7 and Task 8 read/write. `prospero_model_download::Fit`/`Candidate::fit` (Task 1) are the exact names Task 2 consumes.

**4. Review Focus.** Five entries listed above the Task Groups, each tied to the task whose test exercises it (old-format settings file → Task 6; `n_ctx_train` clamp → Task 3; fit-estimate boundaries → Task 1; prefix-reuse edge cases → Task 4; quantized-V-needs-flash-attention invariant → Task 3).

---

## Current next action

Review the prepared host changes, then validate the console gates listed above when a
console is available. The host tasks are implemented; the console tasks remain unchecked.
