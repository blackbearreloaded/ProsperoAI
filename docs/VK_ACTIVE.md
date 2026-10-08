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

## Tried and ruled out (2026-10-08, later still)

Found Mihawk's **PS5_VulkanTemplate** on his GitHub (`gh api users/mihawk-99/repos`) — a complete,
console-proven RADV title foundation with its own `AGENTS.md`, not just the smoke test from
`mihawk-vulkan-review`. Fetched `AGENTS.md`, `ps5/src/platform.c` and `platform.h` (not the full
repo — it's large; see VK_PLAN.md invariants on what to pull from it on demand).

- **Found the documented rule we were breaking:** "A title never calls `exit()` and never returns
  from `_start`... The CRT calls `catchReturnFromMain` after main returns; the shell closes the
  title asynchronously, so it must wait and never return" (`platform.c`). Our test programs
  (`vk_std.c`, and Mihawk's own `radv_smoke.c`) just `return` from `main`. **Fixed** `vk_std.c` to
  call `sceSystemServiceLoadExec("exit", NULL)` then spin forever instead of returning
  (`libSceSystemService.prx` confirmed in NEEDED after relink). Repackaged as `PPSA99023`.
  **Result: identical crash** — `ProcessTerm()` right after launch, no marker, no klog line, not
  even `radv_marker("main: entered")`, the very first line of `main`. This rules out
  return-from-main as *our* crash's cause (it's a real rule, just not what's killing us — we
  never get far enough into `main` to hit it).
- **KStuff vs etaHEN, retracted:** first guessed this console (which has
  `/data/.kstuff_noautomount`) lacks the "enabler (etaHEN)" `AGENTS.md` names as a prerequisite.
  Wrong: on most current firmware ranges etaHEN itself runs *on top of* KStuff as its low-level
  kernel-patch backend (they are not alternatives), so that file's presence says nothing about
  whether etaHEN is or isn't in the picture. No longer a lead on its own.
- **Lapy daemon, a more specific and still-open lead:** the same `platform.c` explicitly talks to
  a "Lapy daemon" for `/data` access elevation (`ps5_elevation_request`; "without a running daemon
  it times out and the title keeps to `/app0`"). This console has `PPSA99008` installed, which
  earlier inspection (10-05/06 session) identified as **Lapy JB Daemon** (`lapy.elf`,
  `lapy-manifest.json`, `self-updater.elf`). Installed as a title, not confirmed resident/running
  in the background the way the control payload or shadowmount are. Whether it needs to be running
  for a RADV title to start at all (not just for `/data` writes) is untested.
  **Tested and ruled out:** launched `PPSA99008` (it stays resident: `agent procs` showed
  `app=24 title=PPSA99008 count=1 pids=128`), then launched the RADV test (`PPSA99023`) while it
  was running. The shell switched foreground apps and killed Lapy to do it (same one-foreground-
  app-at-a-time behavior as any title swap); the RADV crash was identical. Lapy being installed
  and launchable isn't the same as it running as a true background elevation daemon the whole
  time, and a title swap kills whatever was foreground — so this doesn't fully rule out "Lapy
  resident in the background" as a factor, only "Lapy launched right before" as one.

## Next, in order

1. Check whether the Lapy daemon (`PPSA99008`) is actually resident/running, and whether launching
   it first changes anything about the RADV crash (not just `/data` access, which is a narrower
   claim than what we need explained).
2. Find a way to keep the coredump instead of racing to read it: a console/debug setting that
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
