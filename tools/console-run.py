#!/usr/bin/env python3
# ProsperoAI - Play a scripted run on a console and bring back what it saw.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plays a script from tests/console on an installed ProsperoAI and collects the evidence.

usage: tools/console-run.py <host> <script.txt> <results folder>

The installed build is not changed. The script goes into the install folder as
dev/request.txt with a fresh token, the title is started with the launch controller of
ps5-homebrew-dev-protocol (PS5_PROTOCOL, default ~/ps5-homebrew-dev-protocol), and the app
plays the script by itself (src/dev_script.cpp). While it runs, this tool copies the report,
the pictures the report names and the diagnostic log out of the title's storage, records
the kernel log, and waits for the app to close itself. It never closes or kills anything:
if the app does not end, it says so and stops. The request file is removed at the end.

Other settings: PS5_FTP_PORT (2121), PS5_ELF_PORT (9021), PS5_KLOG_PORT (3232),
PS5_FTP_USER / PS5_FTP_PASSWORD, PS5_INSTALL_ROOT (/data/homebrew).
Exit codes: 0 the run completed and the app closed itself; 1 anything else; 3 the title was
already running; 4 the console did not answer.
"""
from ftplib import FTP, all_errors, error_perm
from pathlib import Path
import io
import json
import os
import socket
import subprocess
import sys
import threading
import time
import uuid

TITLE = "PPSA99004"
if len(sys.argv) != 4:
    raise SystemExit(__doc__)
host, script, results = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
ftp_port = int(os.environ.get("PS5_FTP_PORT", "2121"))
install = f"{os.environ.get('PS5_INSTALL_ROOT', '/data/homebrew')}/{TITLE}"
# Where the app keeps its files: /data/prosperoai once it has filesystem access, its
# sandbox otherwise. Its report names files by their sandbox paths either way.
data = "/data/prosperoai"
storage = f"/mnt/sandbox/{TITLE}_000/download0/ProsperoAI"
history = "/system_data/priv/error/history"
protocol = Path(os.environ.get("PS5_PROTOCOL", Path.home() / "ps5-homebrew-dev-protocol"))
results.mkdir(parents=True, exist_ok=True)
started = time.time()


def say(text):
    print(f"[{time.time() - started:6.1f}] {text}", flush=True)


def connect():
    ftp = FTP()
    ftp.connect(host, ftp_port, timeout=15)
    ftp.login(os.environ.get("PS5_FTP_USER", "anonymous"), os.environ.get("PS5_FTP_PASSWORD", "codex"))
    ftp.voidcmd("TYPE I")
    return ftp


def names(ftp, path):
    """The entries of a folder (the server lists the current folder only); None if absent."""
    try:
        ftp.cwd(path)
    except error_perm:
        return None
    return {name: facts for name, facts in ftp.mlsd() if name not in (".", "..")}


def read(ftp, path):
    data = io.BytesIO()
    try:
        ftp.retrbinary(f"RETR {path}", data.write)
    except all_errors:
        return None
    return data.getvalue()


def running(ftp):
    # The sandbox folder can outlive the app; the app's own folder mounted in it cannot.
    return "app0" in (names(ftp, f"/mnt/sandbox/{TITLE}_000") or {})


def errors(ftp):
    return {name: facts.get("modify", "") for name, facts in (names(ftp, history) or {}).items()}


def klog_text():
    try:
        return (results / "klog.txt").read_text(errors="replace")
    except OSError:
        return ""


def crashed():
    """The kernel's report of a fatal signal in this title's process, if there is one."""
    text = klog_text()
    at = text.find("A user thread receives a fatal signal")
    if at < 0:
        return None
    block = text[at:at + 1500]
    return block if "eboot.bin" in block else None


try:
    ftp = connect()
except all_errors as error:
    say(f"console not answering: {error}")
    raise SystemExit(4)
if running(ftp):
    say(f"{TITLE} is already running: nothing done")
    raise SystemExit(3)
if "eboot.bin" not in (names(ftp, install) or {}):
    say(f"{install} has no eboot.bin: install the build first")
    raise SystemExit(1)
errors_before = errors(ftp)
others = sorted(n for n in (names(ftp, "/mnt/sandbox") or {}) if n.startswith("PPSA"))
say(f"other titles running: {others or 'none'}")

token = uuid.uuid4().hex[:12]
request = f"token {token}\n{script.read_text()}".encode()
try:
    ftp.mkd(f"{install}/dev")
except error_perm:
    pass
ftp.storbinary(f"STOR {install}/dev/request.txt", io.BytesIO(request))
ftp.quit()
say(f"request written, token {token}")

# The kernel log, for as long as the run lasts.
klog_stop = threading.Event()


def record_klog():
    with open(results / "klog.txt", "wb") as out:
        while not klog_stop.is_set():
            try:
                with socket.create_connection((host, int(os.environ.get("PS5_KLOG_PORT", "3232"))), timeout=10) as s:
                    s.settimeout(2)
                    while not klog_stop.is_set():
                        try:
                            block = s.recv(65536)
                        except socket.timeout:
                            continue
                        if not block:
                            break
                        out.write(block)
                        out.flush()
            except OSError:
                time.sleep(2)


threading.Thread(target=record_klog, daemon=True).start()
time.sleep(1)

launch = subprocess.run(["bash", str(protocol / "scripts/send-controller.sh"), "launch", TITLE, host,
                         os.environ.get("PS5_ELF_PORT", "9021")], capture_output=True, text=True)
say(f"launch controller sent (rc {launch.returncode}) {launch.stderr.strip()[:200]}")

seen_lines = 0
result = None
appeared = False
last_news = time.time()
fetched = set()
outcome = 1
while True:
    time.sleep(4)
    try:
        ftp = connect()
        alive = running(ftp)
        appeared |= alive
        report, lines = None, []
        for root in (data, storage):
            found = read(ftp, f"{root}/dev/report.txt") if alive or root == data else None
            # A report left by an earlier run stays until this one starts writing.
            if found and token in found.decode(errors="replace").split("\n", 1)[0]:
                report, lines = found, found.decode(errors="replace").splitlines()
                break
        if report:
            for line in lines[seen_lines:]:
                say(f"| {line}")
                last_news = time.time()
                if "picture " in line and " saved" in line:
                    path = line.split("picture ", 1)[1].rsplit(" saved", 1)[0]
                    picture = (read(ftp, path.replace("/download0/ProsperoAI", data, 1)) or
                               read(ftp, f"/mnt/sandbox/{TITLE}_000{path}"))
                    if picture:
                        (results / Path(path).name).write_bytes(picture)
                        fetched.add(Path(path).name)
                if "RESULT:" in line:
                    result = line
            seen_lines = max(seen_lines, len(lines))
            if lines:
                (results / "report.txt").write_bytes(report)
        for name in ("debug-trace.txt", "app.log"):
            log = read(ftp, f"{data}/logs/{name}") or (read(ftp, f"{storage}/logs/{name}") if alive else None)
            if log:
                (results / name).write_bytes(log)
        ftp.quit()
    except all_errors as error:
        say(f"console did not answer: {error}")
        alive = None
    if crashed():
        say("the kernel log reports a fatal signal in the app")
        break
    if alive is False and appeared:
        say("the app is no longer running")
        break
    if alive is False and not appeared and time.time() - started > 90:
        say("the app never appeared")
        break
    if alive is None and time.time() - last_news > 120:
        say("no answer from the console for two minutes: stopping here")
        break
    if result is None and time.time() - last_news > 900:
        say("no news from the app for fifteen minutes: it is left running, nothing is closed")
        break
    if result is not None and time.time() - last_news > 240:
        say("the app reported its result but is still running: it is left as it is")
        break

time.sleep(5)
klog_stop.set()
closed = False
try:
    ftp = connect()
    closed = not running(ftp)
    try:
        # sendcmd, not delete(): this server answers 226, which ftplib's delete() refuses.
        ftp.sendcmd(f"DELE {install}/dev/request.txt")
        say("request removed")
    except all_errors as error:
        say(f"request file could not be removed: {error}")
    new_errors = {n: m for n, m in errors(ftp).items() if errors_before.get(n) != m}
    for name in sorted(new_errors):
        record = read(ftp, f"{history}/{name}") or b"{}"
        try:
            fields = json.loads(record.decode(errors="replace"))
        except ValueError:
            fields = {}
        say(f"new error record {name}: {fields.get('shortError')} title {fields.get('titleId')} "
            f"{str(fields.get('messageBuffer', ''))[:120]}")
    if not new_errors:
        say("no new record in the console's error history")
    ftp.quit()
except all_errors as error:
    say(f"console did not answer at the end: {error}")
    new_errors = {"?": "?"}

# Pictures as PNG when Pillow is here; the BMP files stay as they came.
try:
    from PIL import Image
    for name in sorted(fetched):
        with Image.open(results / name) as image:
            image.save((results / name).with_suffix(".png"))
except ImportError:
    pass

# What the app itself printed, and the kernel's words if it died.
app_lines = [line for line in klog_text().splitlines() if line.startswith(("[HUI]", "[prosperoai]", "[ps5_agc]"))]
(results / "app-lines.txt").write_text("\n".join(app_lines) + "\n")
crash = crashed()
if crash:
    (results / "crash.txt").write_text(crash)
    wanted = ("# signal", "# reason", "# fault address", "# rip", "# thread name")
    for line in crash.splitlines():
        if line.startswith(wanted):
            say(f"crash {line[2:].strip()}")
    for line in app_lines[-6:]:
        say(f"app said: {line[:200]}")

completed = result is not None and "COMPLETED" in result and not crash
say(f"result: {'completed' if completed else 'NOT completed'}; app closed itself: {'yes' if closed else 'NO'}; "
    f"pictures: {len(fetched)}; evidence in {results}")
raise SystemExit(0 if completed and closed and not new_errors else 1)
