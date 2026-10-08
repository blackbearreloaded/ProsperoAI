# Run log

Append-only. Add dated entries at the end; never rewrite an existing one. Summaries for status
questions belong in [VK_ACTIVE.md](VK_ACTIVE.md), not here.

## 2026-10-05/06

- Built and installed ProsperoAI (`PPSA99004`) after moving `.deps` siblings in-repo; confirmed
  `make app` + `make ffpfsc` still produce a working build (HTTP server responds on console).
- RADV-as-payload (no title): `vkCreateInstance` returns `-3`
  (`VK_ERROR_INITIALIZATION_FAILED`) because AGC is a local stub in payload context — expected,
  since AGC is a system module unavailable outside a title.
- First RADV titles (`PPSA99014`..`PPSA99018`, our link recipe: `ps5-pie.ld`, hand-built stubs for
  `__eh_frame_*`, `__dlopen` family, `__real_fclose`/`fflush`, two AGC driver symbols): each
  registers and launches, each terminates before any of our code runs (no marker, no klog). Each
  attempt used a fresh title ID, which left stale registration records — cleaned up afterward via
  UI delete and the ShadowMountPlus API (`games/delete`, confirm=true).
- Added `tools/ps5ctl.py` commands: `agent` (control payload, port 9111, built from
  `.deps/mihawk-vulkan-review/payload/ps5vkctl`), `cat` (read a console file over FTP), `shadow`
  (ShadowMountPlus HTTP API, port 10101 — `config.ini`'s `api_bind_address` changed from
  `127.0.0.1` to `0.0.0.0` so the host can reach it; **this has no auth, LAN-exposed**).
- klog (port 3232) needed its server payload resident; restarting it
  (`/data/payloads/klogsrv-ps5.elf`) made the stream work, but no `[PS5VK]` line ever appeared for
  any RADV attempt.

## 2026-10-08

- Repo moved from `~/Projects/ProsperoAI` to `~/Projects/console/ProsperoAI` (same remote, same
  history; `.deps` and scratch carried over).
- Built Mihawk's own `radv_smoke` title fresh from his `tools/build-radv-title.sh` against our
  RADV archive (`.deps/mihawk-vulkan-review/.deps/native/radv-release`). Static validation
  (`make inspect`) reports `StaticErrors: 0`, structurally identical in shape to ProsperoAI's
  working eboot (same OS/ABI, entry point `0x120`, 14 program headers, 4 mapped LOADs). On
  console: registers (as `.ffpfsc`; folder deploy did not register — see VK_PLAN.md), launches,
  terminates within ~1s, no `[PS5VK]` klog line. Swapping his `libc.prx` for ProsperoAI's made no
  difference. **This moves the leading hypothesis from "our link is wrong" to "something in this
  console's environment differs from Mihawk's validated setup."**
- Tried ShadowMountPlus folder-registration workarounds suggested from general guidance:
  `backport_fakelib=1` + `scan_depth=2` in `config.ini`, `chmod 777` recursively on the title
  folder and then on all of `/data/homebrew`, naming per Mihawk's own folder convention. None
  changed the `0x80aa001a` ("not registered") result for a folder deploy.
- Introduced this plan/active/log structure (`docs/VK_PLAN.md`, `docs/VK_ACTIVE.md`,
  `docs/VK_LOG.md`), modeled on `.deps/mihawk-vulkan-review/AGENTS.md`'s read order and volatility
  contract, so a session can resume without re-deriving the above.

## 2026-10-08 (after console wake)

- Console had gone to standby since the earlier session today; woken via P5 Manager
  (`/api/remoteplay/wake`). Jailbroken payload loader (9021) and FTP (2120) survived standby; the
  control payload (`ps5vkctl`, 9111) and klog server (3232) did not and were resent.
- `/data/homebrew` content had changed entirely since the 10-05/06 session: our test packages
  (`PPSA99004`, `PPSA99014`..`PPSA99021`) are gone; new content includes `PPSA99008` (Lapy JB
  Daemon), `PPSA99169` (PS5 RetroArch, with a populated `radv-shader-cache/`), and two
  unidentified folders (`PPSA99203`, `PPSA99109`). Re-uploaded and re-registered `PPSA99019`
  (RADV Build) from the local `.ffpfsc` built on 10-06; it registered and launched the same as
  before.
- With klog capturing cleanly this time, caught the system's own crash-handling sequence for the
  first time: `[CRS][coredump_seq]` kill sequence, `SceLncService BlockingKill()`, and (on another
  attempt, during re-registration) `CrashReportSequencerReporter` events with
  `crashErrorCode=SCE_SHELL_UTIL_ERROR_APPLICATION_CRASH` and a coredump path under
  `/devlog/system/sce_coredumps.0/`. This confirms a real crash (signal/trap) rather than a
  launch-permission refusal. Two FTP races to grab the coredump directory before its cleaner
  deletes it (roughly 1s window) both missed; the path itself is reachable and otherwise empty.
- Found `PPSA99169` (PS5 RetroArch) has a non-empty `radv-shader-cache/`, meaning a RADV-linked
  title has actually rendered something on this exact console — RADV support itself is not the
  blocker; something specific to the minimal smoke title's startup is.
