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

## Ruled out today, in addition to the earlier list

- **`param.json`'s `attribute3`:** RetroArch (the one confirmed-working RADV title on this
  console) sets `0x80040` ("120 Hz output", per Mihawk's `HARDWARE_FINDINGS.md`); our builds had
  `0`. Set it to `0x80040` on a fresh ID (`PPSA99022`) and retested: registered, launched, same
  `ProcessTerm()` outcome. Not the fix, though it may still be worth keeping set since it's one
  less difference from a known-working title.
- **Static FSELF/ELF comparison with RetroArch:** downloaded its `eboot.bin` from the console
  (`/data/homebrew/PPSA99169/eboot.bin`, 38.2 MB) and ran Mihawk's `make inspect` on it —
  identical shape to ours (FSELF, 12 segment entries, entry `0x120`, 14 program headers, 4 mapped
  LOADs, `StaticErrors: 0`). Tried to extract the raw ELF to diff NEEDED lists the way we did
  between our build and Mihawk's smoke eboot earlier; failed — a development FSELF's segments are
  stored per the FSELF segment table, not as a contiguous plain ELF, so a naive byte slice at the
  computed ELF offset produces a truncated, unparseable file beyond the headers. Need proper
  FSELF segment-table-aware extraction (or a tool that already does it) to go further this way.
- **Coredump race, attempt 2:** a `threading`-based poller started *before* the launch command
  (rather than after, as the first attempt did) still missed the ~1s window twice. FTP round-trip
  latency over this network is almost certainly too high for `ftplib`'s `NLST` to win this race;
  would need either a lower-latency protocol/path to the console or to disable whatever makes the
  cleanup immediate (`CrashReportSequencerReporter`'s `needsToReport=False` suggests this
  "Developer" console build auto-discards reports it would otherwise keep for a submission flow).

## Next, in order

1. Find a way to keep the coredump instead of racing to read it: a console/debug setting that
   changes `CrashReportSequencerReporter`'s "Developer" auto-discard behavior, or a way to make
   the race winnable (local-network tool faster than Python `ftplib`, or a payload that copies the
   dump out from *inside* the console the instant it appears, with no network round-trip at all).
2. Get proper FSELF extraction working (segment-table-aware, not a flat byte slice) so RetroArch's
   real `.dynamic`/NEEDED list and the shape of its RADV linkage can be compared directly against
   ours and against Mihawk's smoke title.
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
