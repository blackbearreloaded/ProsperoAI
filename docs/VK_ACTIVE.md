# Active work

Volatile by design. Keep this file short. The plan is in [VK_PLAN.md](VK_PLAN.md); run records
are in [VK_LOG.md](VK_LOG.md).

_Updated: 2026-10-08 (later)_

## Now

**Stuck at gate 3 (boots to `main`).** Every RADV title tried so far — our own builds
(`PPSA99016`..`PPSA99019`, linked with ProsperoAI's `ps5-pie.ld` recipe plus hand-added stubs)
*and* Mihawk's own smoke-test binary (`radv_smoke.c`, built fresh from his
`tools/build-radv-title.sh` against our RADV archive, static-validated clean by his `make
inspect`) terminate on this console right after launch, before any code runs: no `[PS5VK]` klog
line, no marker file in `/download0`, `procs` empty within seconds, sentinel toast "crashed
before KStuff was paused".

Since Mihawk's own binary fails the same way ours does, **the cause is not our link recipe.**
Ruled out so far: `param.json` fields (`amm`/`kernel` sections present or absent — no difference),
`sce_module/libc.prx` (swapped ProsperoAI's for Mihawk's and back — no difference), NEEDED module
list (differs between the two builds, neither list size correlates with the crash), static ELF/
FSELF structure (`make inspect` reports `StaticErrors: 0` for both his eboot and ProsperoAI's
working one), deploy method (folder vs `.ffpfsc` — folder doesn't even register, see VK_PLAN.md
invariants; `.ffpfsc` registers and still crashes).

Open hypothesis: something in this console's environment that Mihawk's validated 6.02/12.70 setup
has and this one doesn't — a loader component, a kernel patch, or a sandbox/entitlement check his
docs don't document as a prerequisite because he's never run into a console that lacks it.

**New evidence (2026-10-08, after a console wake):** klog finally showed the system's own crash
pipeline instead of just the sentinel toast. Launching `PPSA99019` now reliably produces, in
order: `[CRS][coredump_seq] canKill=true` / `canContinue=false` / `killing app (appId=...) ...`,
then `[SceLncService] BlockingKill() appId=..., titleId=PPSA99019, is_coredumpFinished={0},
forceKill={1}`, then `[CRS][coredump_seq] done`, and separately (on the registration-replace path)
`CrashReportSequencerReporter` events with `crashErrorCode=SCE_SHELL_UTIL_ERROR_APPLICATION_CRASH`
and a coredump written to `/devlog/system/sce_coredumps.0/<title>_<timestamp>/` before a cleaner
deletes it within roughly a second. **This confirms the process is actually crashing (a real
signal/trap), not being refused a launch slot** — `CRS` (Crash Report Sequencer) only runs for an
app that started and faulted. Two attempts to race the cleaner and grab the coredump directory
over FTP both missed it (one `nlst` loop never saw it populate in 8.7s; path confirmed reachable
and otherwise empty: `/devlog/system/sce_coredumps.0`).

Also notable: `/data/homebrew` on this console now holds unrelated content that wasn't there in
the 10-05/06 session (`PPSA99008` = Lapy JB Daemon, `PPSA99169` = PS5 RetroArch **with a populated
`radv-shader-cache/` folder**, `PPSA99203`/`PPSA99109` unidentified). A RADV-linked title
(RetroArch) with evidence of actually having rendered something on this console means **RADV
itself works here** — the gap is specific to how our/Mihawk's minimal smoke title starts, not to
RADV support on this hardware/firmware in general. Worth inspecting RetroArch's eboot (NEEDED
list, link flags via `make inspect` equivalent) as a second known-good reference alongside
ProsperoAI's AGC build.

## Next, in order

1. **Grab a coredump before the cleaner deletes it.** The directory
   (`/devlog/system/sce_coredumps.0/<title>_<ts>/`) exists for under ~1s. A tighter race (parallel
   poller started *before* the launch command is even sent, or using `MLSD`/raw FTP without
   `ftplib`'s per-call overhead) might catch it. If caught, the dump's register state (PC/LR) or
   signal number would say exactly where/why it faults.
2. Compare PS5 RetroArch's RADV eboot (`/data/homebrew/PPSA99169/eboot.bin`) against ours: NEEDED
   list, FSELF header, and — if obtainable — how it links RADV. It is proof that a RADV title
   *can* run on this console, so its differences from our/Mihawk's smoke title are the most
   direct remaining lead.
3. Ask Mihawk's docs (or upstream) directly whether a specific payload/loader
   (HEN/etaHEN/kstuff version) is a documented prerequisite for a *custom* title to pass
   `LaunchFlow`, as opposed to a pre-registered retail/homebrew one. `/data/etaHEN` does not exist
   on this console; unclear if that matters for a `.ffpfsc`-registered title.

## Last verified runs

- ProsperoAI (`PPSA99004`, AGC backend): launches and stays running (2026-10-06, repeated
  2026-10-08). HTTP server on 11434 responds. This is gate "launch" for the control payload test
  harness itself, not a RADV result.
- `PPSA99014` (Mihawk's smoke test, our RADV archive, his link recipe, `.ffpfsc`): registers,
  launches (`result=0x80940010` / `0x0000...018` family), terminates within ~1s, no klog output.
- `PPSA99019` (our RADV Build, `vk_std.c` + marker constructor): same outcome as above.
