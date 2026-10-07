# Active work

Volatile by design. Keep this file short. The plan is in [VK_PLAN.md](VK_PLAN.md); run records
are in [VK_LOG.md](VK_LOG.md).

_Updated: 2026-10-08_

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

## Next, in order

1. Check what differs at the process level right as it dies: attach `ps5dbg`/`ps5debug-NG` style
   inspection if feasible, or at minimum capture `procstat`-equivalent output in the instant
   between launch and termination (current tooling is too slow — `agent status` after a sleep
   always sees it already gone).
2. Ask Mihawk's docs (or upstream) directly whether a specific payload/loader
   (HEN/etaHEN/kstuff version) is a documented prerequisite for a *custom* title to pass
   `LaunchFlow`, as opposed to a pre-registered retail/homebrew one. `/data/etaHEN` does not exist
   on this console; unclear if that matters for a `.ffpfsc`-registered title.
3. If (1) and (2) don't resolve it: try a title built by *this console's own working pipeline*
   (ProsperoAI's `tools/build.sh`) with nothing but a `radv_GetInstanceProcAddr` call added, so
   the only variable left is "does RADV's static initializers alone crash it" vs. "does this
   console's loader reject any RADV-linked title regardless of who built it".

## Last verified runs

- ProsperoAI (`PPSA99004`, AGC backend): launches and stays running (2026-10-06, repeated
  2026-10-08). HTTP server on 11434 responds. This is gate "launch" for the control payload test
  harness itself, not a RADV result.
- `PPSA99014` (Mihawk's smoke test, our RADV archive, his link recipe, `.ffpfsc`): registers,
  launches (`result=0x80940010` / `0x0000...018` family), terminates within ~1s, no klog output.
- `PPSA99019` (our RADV Build, `vk_std.c` + marker constructor): same outcome as above.
