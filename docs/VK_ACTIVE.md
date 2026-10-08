# Active work

Volatile by design. Keep this file short. The plan is in [VK_PLAN.md](VK_PLAN.md); the full
dated narrative behind every line here is in [VK_LOG.md](VK_LOG.md) — read this file first, and
only open the log if you need the reasoning or the exact commands behind an entry.

_Updated: 2026-10-08_

## Now

**Stuck at gate 3 (boots to `main`).** Every RADV title tried so far — our own builds
(`PPSA99016`..`PPSA99023`) *and* Mihawk's own `radv_smoke.c`, built fresh from his
`tools/build-radv-title.sh` against our RADV archive — terminate on this console right after
launch, before any of our code runs: no `[PS5VK]`/marker output, `procs` empty within seconds.
klog confirms this is a **real crash** (the system's `[CRS][coredump_seq]` kill sequence and
`CrashReportSequencerReporter` with `crashErrorCode=SCE_SHELL_UTIL_ERROR_APPLICATION_CRASH` fire),
not a launch refusal — and a coredump is briefly written to
`/devlog/system/sce_coredumps.0/<title>_<ts>/` before a cleaner deletes it within ~1s.

**RADV itself works on this console**: `PPSA99169` (PS5 RetroArch, already installed) has a
populated `radv-shader-cache/`, proof it has rendered here. So the gap is specific to how a
*minimal* RADV title starts, not to RADV/this hardware in general.

Since Mihawk's own binary fails exactly like ours, **the cause is not our link recipe.**

## Ruled out (don't retry these)

- `param.json`: `amm`/`kernel` sections, `attribute3` (tried `0x80040`, RetroArch's value) — no
  effect.
- `sce_module/libc.prx`: swapped ours for Mihawk's and back — no effect.
- Static ELF/FSELF structure: Mihawk's `make inspect` reports `StaticErrors: 0` for our build, his
  smoke eboot, *and* RetroArch's eboot — all structurally identical at that level.
- Deploy method: folder deploy doesn't even register on this console (`0x80aa001a`); `.ffpfsc`
  registers fine and still crashes. `backport_fakelib`, `scan_depth`, `chmod 777` on
  `/data/homebrew` didn't change the folder-deploy registration failure either.
- Returning from `main`: PS5_VulkanTemplate's rule ("never return from `_start`/call `exit()`;
  call `sceSystemServiceLoadExec("exit", NULL)` and spin instead") is real, but applying it
  (`PPSA99023`) made no difference — we never get far enough into `main` for it to matter.
- "KStuff vs etaHEN" as an environment gap: **wrong, retracted** — etaHEN runs on top of KStuff as
  its kernel-patch backend on most firmware; they aren't alternatives.
- Lapy daemon (`PPSA99008`) resident before launch: no difference (though the launch necessarily
  killed it first — one foreground app at a time — so "resident the whole time, never killed"
  isn't fully ruled out, just "launched right before").
- Coredump race (FTP, two different timing strategies): both missed the ~1s cleanup window.

## Next, in order

1. **Get the coredump instead of racing for it.** A payload that copies it out *from inside the
   console* the instant it appears (no network round-trip) is the most direct way to finally see
   a signal number / PC and stop guessing. Alternatively, find a setting that stops
   `CrashReportSequencerReporter` from auto-discarding it (its `needsToReport=False` suggests a
   "Developer" build behavior that might be toggleable).
2. Extract RetroArch's real raw ELF (`.deps/scratch/retroarch/eboot.bin` on this machine) properly
   — a naive byte slice at the FSELF's computed ELF offset is truncated/unparseable because a
   development FSELF's segments follow its own segment table, not a contiguous plain-ELF layout.
   Need segment-table-aware extraction to diff its real NEEDED/dynamic section against ours.
3. If both of those stall: this may need PS5 jailbreak/kernel-patch expertise beyond what static
   build comparison can resolve. Worth asking in the relevant homebrew community with the exact
   evidence above (crash is real, RADV works for other titles, static structure is identical to a
   working title) rather than continuing to guess build-side causes alone.

## Reference builds on this machine

- `.deps/scratch/radv_title/eboot-m3.bin` / `PPSA99023.ffpfsc`: our latest test (shell-exit fix
  applied), still crashes. Source: `.deps/scratch/radv_init/vk_std.c` + `marker.c`.
- `.deps/scratch/retroarch/eboot.bin`: downloaded from the console, the one working reference.
- `.deps/scratch/vktemplate-ref/`: `AGENTS.md`, `platform.c`, `platform.h` from Mihawk's
  `PS5_VulkanTemplate` (fetched via the GitHub API, not a full clone — see VK_PLAN.md).
- `.deps/scratch/vkctl.py`: superseded by `tools/ps5ctl.py agent <words>` — use the latter.

## Last verified runs

- ProsperoAI (`PPSA99004`, AGC backend): launches and stays running; HTTP server on 11434
  responds. Not a RADV result — this is the control-payload harness's own sanity check.
- Every RADV title `PPSA99014`..`PPSA99023`: registers, launch call succeeds
  (`result=0x0000...018` / `0x80940010` family), crashes within ~1s, no klog/marker output.
