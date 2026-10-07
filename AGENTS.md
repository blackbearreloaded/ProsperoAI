# Notes for agents working on ProsperoAI

ProsperoAI is a PS5 homebrew app (title `PPSA99004`) that runs llama.cpp on the console. The
branch `wip/llama-vulkan-migration` is work in progress and is not verified to boot.

## Read order

Read in this order and stop as soon as you have what you need:

1. This file. Stable: the build commands, console talking points and debugging order below
   change only when the workflow itself changes.
2. If the task touches the RADV/Vulkan backend migration: `docs/VK_PLAN.md` (static — gates,
   invariants, what must be proved) once per session, then `docs/VK_ACTIVE.md` (volatile —
   current step, blockers, last verified runs) last, right before starting work.
3. On demand only: `docs/VK_LOG.md` (dated run records — don't read it end to end to answer a
   status question; VK_ACTIVE.md's summary is enough), `docs/BUILDING.md`, and
   `.deps/mihawk-vulkan-review/AGENTS.md` plus its `docs/` (the reference project the RADV link
   recipe and this read-order convention are drawn from).

### Volatility contract

| File | Rule |
| --- | --- |
| This file | Stable. Edit when the workflow itself changes. |
| `docs/VK_PLAN.md` | Static. Edit only when a gate or an invariant changes; never record progress here. |
| `docs/VK_ACTIVE.md` | Volatile. Rewrite in place; keep it short. |
| `docs/VK_LOG.md` | Append-only. Add dated entries at the end; never rewrite an existing one. |

Your task is whatever the user's prompt asks. `VK_ACTIVE.md`'s "Next" list is background on
where things stand, not a queue to work through unprompted — say so if the prompt and that list
disagree about what to do next.

## Build and verify

- `make deps`, `make app`, `make ffpfsc` work on the host. Read `docs/BUILDING.md` first.
- A build that links is not a build that runs. Check the NEEDED list (`readelf -d`) and the
  program headers against a working build before deploying.
- Never send a payload or install a title to the console without the user's explicit approval
  for that step. Approval for one step does not cover the next.

## Talking to the console

Use `tools/ps5ctl.py`. It covers status, payloads, FTP, title removal, klog, shell (shsrv on
port 2323), process list, launch and screenshots.

- Ports: payload 9021, FTP 2120, klog 3232, shsrv 2323, P5 Manager 3001 (remote play).
- Screenshot before every confirming press (Cross) in the UI. Keep a screenshot in
  `~/radv_title/` or the scratchpad, and read it before pressing.
- `kill`, `rm`, `launch` and `title rm` are destructive. Use `--yes` only after the user has
  confirmed the specific target.
- shsrv's `kill` does not accept signal options. A process in state `STOP` did not die from
  `SIGCONT` plus `SIGKILL` sent by a payload, so closing it through the UI or a reboot may be
  needed.
- A reboot of the console is not a routine step. Ask first.

## Titles and registration

- Test titles use IDs other than `PPSA99004`, for example `PPSA99014`. Never delete or overwrite
  `PPSA99004` for a test.
- Replacing a package under a title the console has already registered leaves the registration
  inconsistent. The UI then reports "The data is corrupted". Remove the registration in the
  console UI first, then install the new package.
- Deleting files from `/data/homebrew` does not remove the registration.
- Working way to replace a test title without a reboot (confirmed by the user):
  1. Upload the new package (`ps5ctl.py ftp put NEW.ffpfsc /data/homebrew/NEW.ffpfsc`).
  2. In the console UI, open the title, press Options and choose Delete.
  3. Send shadowmountplus again (`ps5ctl.py payload /data/payloads/shadowmountplus.elf`).
     It rescans `/data/homebrew` and registers the new package.
  Step 2 is the one that removes the stale record. Shadowmount alone does not.
- Icons and metadata come from `sce_sys/`. Use this project's `sce_sys`, not another project's.

## Git and attribution

- Commit only on the WIP branch unless the user asks otherwise. Push only when asked.
- Do not add `Co-Authored-By` or "Generated with Claude Code" lines to commits or PR text.


## Debugging a title that does not start (lessons from PPSA99014-19)

The live status of the RADV investigation (what's ruled out, what's next) is in
`docs/VK_ACTIVE.md`, not here — this section is the stable, reusable procedure.

Read-only checks first, in this order:

1. `agent status` / `agent procs` (control payload, port 9111): is the title running, and is the payload resident? `agent launch <ID>` returns the call-by-call result.
2. `shadow info <ID>` (ShadowMountPlus API, port 10101): is the title registered with shadowmount? 404 means it is not.
3. `cat /data/ps5vkctl.log --tail 2000` (payload log, one line per call) and `cat /data/shadowmount/debug.log --tail 2000` (install log).
4. `cat /download0/<marker>.txt` for markers the title writes itself. Use `/download0`, not `/data`: a title did not write to `/data`.
5. klog (`klog --seconds 60`, port 3232) lines with `[PS5VK]` prefix, as Mihawk's title writes them. An empty or banner-only capture means klog is not streaming.

Launch result codes seen on this console (FW 12.70):
- `0x0000a018`, `0x00008018`, `0x0000e018`, `0x0000c018`: launched; the title may still crash right after.
- `0x80940031` (Resource temporarily unavailable): title is not ready or not registered (or another app is running). Check `shadow info`.
- `0x80a40010`: not enough free space. Free space in Settings > Storage, or delete an unfinished `.part` download.
- Title gone right after launch with `procs` empty and the sentinel toast `crashed before KStuff was paused`: the crash is before `main` (loader, relocations, TLS, init).

Working rules that came out of this:
- One title ID per test slot; do not create a new ID per attempt (each one leaves a registration record).
- Replace a package only after deleting the old registration in the UI (Options > Delete) or, for shadowmount-only titles, through `shadow delete <ID> --yes`.
- Check the minimal case before the feature case: a title that only writes a marker from a constructor separates a link or packaging problem from a Vulkan problem.
- Compare the link with the one that works (Mihawk: `tools/radv-link.sh`, linker script `tooling/psbc/ps5-pie-unwind.ld`, `-ffunction-sections`, CRT from `tooling/native`); our own link used a different script and no platform wrappers.
