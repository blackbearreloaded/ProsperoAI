# Mistral AGC / Vulkan benchmark — 2026-10-08

Measured on the same PS5, firmware 12.70, test title PPSA99023. Original PPSA99004 was not modified.

| Backend | Run 1 | Run 2 | Run 3 | Median decode tok/s |
| --- | ---: | ---: | ---: | ---: |
| Official upstream AGC release 01.000.000 | 47.96 | 47.92 | 48.02 | **47.96** |
| llama.cpp Vulkan / RADV | 69.73 | 69.72 | 69.72 | **69.72** |

Vulkan decode is 1.454×, or 45.4% faster in this test. Both runs generated coherent text. This is a short-context result, not a long-context or numerical-equivalence test.

## Protocol

- Mistral 7B Instruct v0.3 Q4_0, exact source recorded in `model-tools/recipes/mistral-7b-instruct-v0.3-q4-0.json`.
- Source SHA-256: `19c3cf872246d48e7a9784d6def7a694a6c9056cfbeb98f7061d43cd0a7c8b7f`.
- AGC conversion verified all output sizes and hashes against that recipe.
- Official AGC [release 01.000.000](https://github.com/blackbearreloaded/ProsperoAI/releases/tag/01.000.000): ZIP SHA-256 `19c2bacebf6fb1587b32d458fb5af2fb472d9f3844085993b0f1a30669dc0c69`. Eboot, runtime libraries and UI assets from the release; only title metadata changed to the test ID.
- Vulkan llama.cpp commit: `cb7934c52ca8710994b2ecc19775ebefcfdb8d01`; all 33 layers offloaded to Vulkan0.
- Three fresh title launches each, model warmup before generation, default UI style, greedy sampling, 128 generated tokens, context capacity 4096. Built-in `auto_chat.txt` supplies the same user text to both apps.
- System text: “You are ProsperoAI, a thoughtful AI assistant running locally on a PlayStation 5. Be clear, helpful, and concise.”
- User text: “Explain in detail how a GPU executes a matrix multiplication, covering threads, memory tiers and synchronisation.”
- Upstream AGC chat encoding produces 62 input tokens; llama.cpp template produces 61. Text is the same, tokenized formatting differs. AGC reuses three special prefix tokens from its warmup; Vulkan clears KV before the request.
- Decode rate is `(generated_tokens - 1) / (elapsed_seconds - prefill_seconds)`. First generated token is supplied by prefill; 127 decode steps remain. Logs provide actual counts and microsecond timing, not word estimates or network wall-time estimates.

## Timing and memory

AGC prefill: 0.690–0.696 s; Vulkan prefill: 0.343–0.345 s. AGC model load: 1.43–1.93 s. Vulkan reported load time: 59.93–59.95 s, including its current initialization path. Vulkan cold startup is substantially slower and remains a performance limitation.

RADV reported 11712 MiB (11.4375 GiB) total heap and 11573.98 MiB free before model loading. Vulkan main buffers at context 4096: 3850.02 MiB weights, 512 MiB KV, 52.01 MiB compute, approximately 4.31 GiB total; driver/UI overhead is additional. Reported heap capacity was queried, not proved by allocating the whole heap.

## Network access

Current Vulkan app serves http://10.0.0.127:11434/ while running. Final read-only verification returned HTTP 200; /api/tags lists Mistral-7B-Instruct-v0.3.Q4_0.gguf.

## Development workflow

Folder deployment works after setting executable permissions (755) on eboot.bin and PRX modules. FTP initially created a non-executable binary, causing 0x80aa001a / errno 13; SITE CHMOD resolved it. `tools/ps5ctl.py ftp put-dir` now applies these modes. Stop the test title before updating its binary. Keep models installed and transfer only changed files.

`make app-vulkan-folder` skips image compression. Four build jobs, project-local ccache. No-change llama library build: 1.593 s; no-change Vulkan folder compile/link/sign: 3.776 s.

## Artifacts

- `build/benchmark-comparison-mistral.json`: complete comparison.
- `build/benchmark-upstream-agc-mistral.json`, `build/benchmark-vulkan-fair-mistral.json`: individual runs.
- `/tmp/prospero-upstream-agc{2,3,4}-klog.txt`, `/tmp/prospero-vulkan-fair{1b,2b,3}-klog.txt`: raw logs.
- `/tmp/prospero-upstream-agc-result.jpg`, `/tmp/prospero-vulkan-final.jpg`: screenshots.

The earlier 2048-context Vulkan HTTP result is preliminary and uses a shorter prompt. Failed WIP AGC HTTP result files are diagnostic failures and must not be used as baseline performance numbers.
