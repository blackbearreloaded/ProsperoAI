# Notes for agents working on ProsperoAI

ProsperoAI is a PS5 homebrew app (title `PPSA99004`) that runs llama.cpp on the console. The
branch `wip/llama-vulkan-migration` is work in progress and is not verified to boot.

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
