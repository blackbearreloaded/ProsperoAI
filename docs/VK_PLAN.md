# RADV/Vulkan backend plan

ProsperoAI runs llama.cpp on the PS5 through AGC (`src/*_runtime_ps5.cpp`, title `PPSA99004`,
verified on console). This branch's goal is a second backend: llama.cpp's Vulkan backend on
Mihawk's RADV driver for the PS5, so the GPU is reachable from ordinary Vulkan code instead of
AGC's native interface. AGC stays the fallback; nothing here removes it.

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
   proves the loader/relocation/TLS/init path independently of Vulkan. This is the current gate;
   see VK_ACTIVE.md.
4. **`vkCreateInstance` succeeds** — proves RADV reaches the real AGC driver (not AGC stubs).
5. **A model runs through llama.cpp's Vulkan backend** — the actual goal: compare tokens/s against
   the AGC backend for the same model and quantization.

A pass at one gate does not imply the next. Record exact launch result codes, klog output (or its
absence), and screenshots for every attempt — see AGENTS.md's debugging order.

## Known invariants (this console, FW 12.70)

- Title installs that register correctly use `.ffpfsc` images uploaded over FTP and picked up by
  ShadowMountPlus (`shadow` command / resend the `shadowmountplus.elf` payload). Folder deploys
  (`/data/homebrew/<ID>/` as a directory) mount via nullfs but do **not** register in the system's
  title database on this console: launch returns `0x80aa001a` even after `games/mount` succeeds.
  This contradicts Mihawk's own docs (which validate folder deploys on 12.70), so the console's
  environment differs from his in some unidentified way — open question, not resolved.
- A package re-uploaded under a title ID the console has already registered leaves the
  registration inconsistent ("The data is corrupted"). Delete the registration in the UI
  (Options > Delete) first, or use a fresh ID.
- `PPSA99004` is ProsperoAI itself — never overwritten for a RADV test. RADV tests use their own
  IDs (`PPSA99019` is the current RADV Build slot; reuse it rather than minting a new ID each
  attempt).
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
