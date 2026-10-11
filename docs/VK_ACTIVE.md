# Active work

Updated: 2026-10-11. Gates: [VK_PLAN.md](VK_PLAN.md); evidence: [VK_LOG.md](VK_LOG.md).

## Verified vulkan-runtime

- One complete Prism llama/ggml pin `e8fd209`, `GGML_MAX_NAME=160`, source-built SD and
  Vulkan media bridge. AGC inference, routing, scratch allocation and private ggml copies
  are removed. RADV still requires the console AGC platform driver.
- The signed `PPSA99004` release candidate boots with the expected ELF foundation. The
  console was not rebooted.
- Fresh post-update Bonsai verification passed: the cold correctness prompt, three
  identical warm runs at 11.205240 tokens/s median, Thinking mode, 16K context,
  Mistral/MiniCPM5 regression and return to Bonsai.
- Fresh media verification passed with valid artifacts and a text round trip after every
  model: SD-Turbo 30.324 s, Stable Audio 148.888 s and Kokoro 89.014 s. Audio has the
  expected sample rates, frame counts and no clipping.
- Shared web/native conversation create, read and delete passed against the console HTTP
  server. Host self-update integration passes outside the managed sandbox; its Unix socket
  transport is blocked by the sandbox itself.
- Long-generation plumbing now accepts up to 131072 output tokens and 262144 context tokens.
  A full 32712-token MiniCPM5 run reached the 32K context boundary with `length`; a 0.8B
  Qwen3.5 smoke run loaded and generated successfully at 256K context. A full 128K output
  run is not yet claimed because that model emitted EOS after one token.
- An omitted output-length request now fills the remaining context (verified 8192- and
  32768-token full fills on `PPSA99023`) instead of a fixed 512-token default; native
  Response length gained a matching "Auto" default. The downloader's fit badge now reads
  the Vulkan backend's real reported heap instead of a hardcoded figure. The web UI can now
  view/play/download generated images and audio inline (new authenticated `/download0/<path>`
  route plus client-side TGA decode), matching what the native app already did. PR #22.

## Release preparation

- The `01.003.000` Vulkan app initiated a real console self-update from a temporary GitHub
  prerelease asset. It verified and applied the exact `01.004.000` candidate ZIP; the
  installed metadata matches the candidate and the updated app passed Bonsai and media.
- PR #15 targets version `01.004.000`. Release notes credit Quatrixone, Bilawal Riaz and
  BlackBearReloaded. Documentation and tools use configured test-title variables and do not
  publish the private console address or test-title ID.
- The release folder/ZIP, lint and full host suite pass. CI fixes install OpenGL headers and
  discover Mesa's software Vulkan ICD; the repaired PR checks still need to pass.

## Deferred scope

Draft #20 carries AirLLM/Strata bounded-residency research, voice profiling, peak memory,
extended switching and capacity gates. PR #15 does not claim streaming inference, universal
quality or real-time audio.

Evidence: `performance/2026-10-11/vulkan-runtime-{build,bonsai,media,legacy-alias}.json`.
See [runtime details](VULKAN_RUNTIME.md) and [media comparison](MEDIA_VULKAN_COMPARISON.md).
