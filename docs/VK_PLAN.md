# RADV/Vulkan backend plan

The target is `vulkan-runtime`: one pinned Prism ggml/Vulkan runtime for text, image,
audio and voice on Mihawk's RADV driver. Remove legacy AGC inference and private ggml
copies after supported-path correctness checks. Report performance tradeoffs explicitly;
a small voice regression does not require retaining a second backend. Platform AGC
driver imports remain necessary for RADV. Capacity and voice optimization belong to a
separate follow-up PR.

This is the top-level plan: what must be proved, the rules that constrain how, and where the
detail lives. Read once per session. Current step, next action and the last verified runs are in
[VK_ACTIVE.md](VK_ACTIVE.md); run evidence is in [VK_LOG.md](VK_LOG.md).

## Gates

A requirement is proved by a console run, not by a plausible build. In order:

1. **Host build** — `make llama-vulkan` (host llama-cli with Vulkan, PS5 syntax check of
   ggml-vulkan.cpp) and `make radv` (the RADV archive for the PS5, from Mihawk's pinned recipe).
2. **Static validation** — the eboot matches a working build's shape: `readelf -d` NEEDED,
   `readelf -l` program headers, and (once available) Mihawk's `make inspect` validator from
   `.deps/mihawk-vulkan-review`.
3. **Boots to `main`** — a title that only calls `radv_GetInstanceProcAddr` and writes a marker
   proves the loader/relocation/TLS/init path independently of Vulkan. See VK_ACTIVE.md
   for the current gate status.
4. **`vkCreateInstance` succeeds** — proves RADV reaches the real AGC driver (not AGC stubs).
5. **A model runs through llama.cpp's Vulkan backend** — the actual goal: compare tokens/s against
   the AGC backend for the same model and quantization.
6. **Media correctness and performance** — Image, Audio and Voice each produce valid
   outputs on identical fixed fixtures. Compare against the AGC baseline on PS5 and
   disclose regressions rather than implying every path is faster. Report initial load
   separately, generation median over at least three runs, peak memory,
   and repeated text/media switching. Host Vulkan parity is a prerequisite, not this gate.
7. **One runtime** — rebuild every ggml producer/consumer with the same pinned ABI; remove
   private renamed ggml copies, legacy AGC inference, routing and scratch allocation after
   all supported paths pass correctness checks. Migrate legacy text models to GGUF before retiring their path.
   Necessary CPU operations may remain within the shared runtime, explicitly documented.

Keep Lapy's source, SDK and protocol from release 01.003.000 during this migration.
Test-title helpers retain their own title identity; never substitute the production helper.

A pass at one gate does not imply the next. Record exact launch result codes, klog output (or its
absence), and screenshots for every attempt — see AGENTS.md's debugging order.

## Known invariants (this console, FW 12.70)

- Both `.ffpfsc` images and app folders can register through ShadowMountPlus on this console.
  Folder deploys require mode `755` on the uploaded app tree, including executable `eboot.bin`
  and PRX modules; missing execute permission caused the earlier `0x80aa001a` launch failure.
  Prefer folder deploys for development iterations and `.ffpfsc` only when an image is required.
- A package re-uploaded under a title ID the console has already registered leaves the
  registration inconsistent ("The data is corrupted"). Delete the registration in the UI
  (Options > Delete) first, or use a fresh ID.
- `PPSA99004` is ProsperoAI itself — never overwritten for a RADV test. RADV tests use a
  configured test title ID; reuse its slot rather than minting a new ID each time.
- The control payload `ps5vkctl` (built from `.deps/mihawk-vulkan-review/payload/ps5vkctl`,
  loaded once per boot) needs an active Remote Play session before `agent launch` works: without
  one, `sceUserServiceGetLoginUserIdList` returns no user and launch fails outright.
- klog (port 3232) requires its server payload (`/data/payloads/klogsrv-ps5.elf`) resident; it is
  not always running after a console wake. Restart it before trusting an empty capture.

## Index

- [VK_ACTIVE.md](VK_ACTIVE.md) — current step, blockers, last verified runs (volatile).
- [VK_LOG.md](VK_LOG.md) — dated run records (append-only).
- `AGENTS.md` — console talking points (`ps5ctl.py`), the debugging order, and git rules.
- `.deps/mihawk-vulkan-review/AGENTS.md` and its `docs/` — the reference project this plan's
  format and the RADV link recipe are drawn from. Not this repository's docs; read on demand.
