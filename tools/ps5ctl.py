#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Control a jailbroken PS5 from the host: status, payloads, FTP, titles, klog and screenshots.

  ps5ctl.py status
  ps5ctl.py payload PATH.elf            send an ELF to the payload port (9021)
  ps5ctl.py ftp ls [REMOTE]             list a directory over the homebrew FTP server
  ps5ctl.py ftp put LOCAL REMOTE        upload a file
  ps5ctl.py ftp rm REMOTE [--yes]       delete a file (or an empty directory with --dir)
  ps5ctl.py title rm TITLE_ID [--yes]   delete /data/homebrew/TITLE_ID* entries
  ps5ctl.py klog [--seconds N]          read the klog stream (port 3232) into stdout
  ps5ctl.py shot OUT.jpg                save one frame from an active Remote Play session

Hosts and ports come from the environment: PS5_HOST (10.0.0.127), PS5_PAYLOAD_PORT (9021),
PS5_FTP_PORT (2120), PS5_KLOG_PORT (3232), P5_MANAGER (http://10.0.0.187:3001),
P5_SESSION (Remote Play session id, needed for shot).

Deleting a title from /data/homebrew removes its files only. The console's title registration
is kept by the system until the title is deleted from the console UI, so re-install after a
delete may need that step too. Destructive commands refuse to run without --yes.
"""
import argparse
import ftplib
import io
import os
import socket
import sys
import time
import urllib.request

HOST = os.environ.get("PS5_HOST", "10.0.0.127")
PAYLOAD_PORT = int(os.environ.get("PS5_PAYLOAD_PORT", "9021"))
FTP_PORT = int(os.environ.get("PS5_FTP_PORT", "2120"))
KLOG_PORT = int(os.environ.get("PS5_KLOG_PORT", "3232"))
MANAGER = os.environ.get("P5_MANAGER", "http://10.0.0.187:3001").rstrip("/")
SESSION = os.environ.get("P5_SESSION", "")
HOMEBREW = "/data/homebrew"


def ftp_connect():
    ftp = ftplib.FTP()
    ftp.connect(HOST, FTP_PORT, timeout=60)
    ftp.login("anonymous", "ps5ctl")
    return ftp


def cmd_status(_args):
    ok = True
    try:
        with socket.create_connection((HOST, PAYLOAD_PORT), timeout=3):
            print(f"payload port {PAYLOAD_PORT}: open")
    except OSError as error:
        print(f"payload port {PAYLOAD_PORT}: {error}")
        ok = False
    try:
        ftp = ftp_connect()
        print(f"ftp port {FTP_PORT}: open, homebrew has {len(ftp.nlst(HOMEBREW))} entries")
        ftp.quit()
    except (ftplib.all_errors, OSError) as error:
        print(f"ftp port {FTP_PORT}: {error}")
        ok = False
    try:
        with urllib.request.urlopen(f"{MANAGER}/api/remoteplay/discover?ip={HOST}", timeout=8) as r:
            print("p5 manager discover:", r.read(200).decode(errors="replace"))
    except OSError as error:
        print(f"p5 manager: {error}")
    return 0 if ok else 1


def cmd_payload(args):
    with open(args.path, "rb") as fh:
        data = fh.read()
    with socket.create_connection((HOST, PAYLOAD_PORT), timeout=args.timeout) as sock:
        sock.sendall(data)
        sock.shutdown(socket.SHUT_WR)
        sent = time.monotonic()
        out = b""
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                out += chunk
                if time.monotonic() - sent > args.timeout:
                    break
        except socket.timeout:
            pass
    # Long-running payloads keep the socket open; a returned timeout is not an error.
    sys.stdout.write(out.decode(errors="replace"))
    print(f"\n[sent {len(data)} bytes]")
    return 0


def cmd_ftp(args):
    ftp = ftp_connect()
    try:
        if args.action == "ls":
            for line in ftp.nlst(args.remote or HOMEBREW):
                print(line)
        elif args.action == "put":
            with open(args.local, "rb") as fh:
                ftp.storbinary(f"STOR {args.remote}", fh)
            print(f"uploaded {args.local} -> {args.remote}")
        elif args.action == "rm":
            require_yes(args)
            if args.dir:
                ftp.rmd(args.remote)
            else:
                ftp.delete(args.remote)
            print(f"removed {args.remote}")
    finally:
        ftp.quit()
    return 0


def remove_tree(ftp, path):
    """Delete a directory recursively. nlst returns bare names, so each entry is joined to path."""
    for name in ftp.nlst(path):
        base = name.rsplit("/", 1)[-1]
        if base in (".", ".."):
            continue
        full = f"{path}/{base}"
        try:
            ftp.delete(full)
        except ftplib.error_perm:
            remove_tree(ftp, full)
            ftp.rmd(full)


def cmd_title(args):
    require_yes(args)
    ftp = ftp_connect()
    removed = []
    try:
        for name in ftp.nlst(HOMEBREW):
            base = name.rsplit("/", 1)[-1]
            if not base.startswith(args.title_id):
                continue
            full = f"{HOMEBREW}/{base}"
            try:
                ftp.delete(full)
            except ftplib.error_perm:
                remove_tree(ftp, full)
                ftp.rmd(full)
            removed.append(full)
    finally:
        ftp.quit()
    print("removed:" if removed else f"nothing matched {args.title_id}", *removed)
    return 0


def cmd_klog(args):
    with socket.create_connection((HOST, KLOG_PORT), timeout=5) as sock:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()
    return 0


def cmd_shot(args):
    if not SESSION:
        print("set P5_SESSION to an active Remote Play session id", file=sys.stderr)
        return 2
    with urllib.request.urlopen(f"{MANAGER}/api/remoteplay/sessions/{SESSION}/video.mjpeg",
                                timeout=10) as r:
        data = r.read(2_000_000)
    start = data.find(b"\xff\xd8")
    end = data.find(b"\xff\xd9", start)
    if start < 0 or end < 0:
        print("no frame in stream", file=sys.stderr)
        return 1
    with open(args.out, "wb") as fh:
        fh.write(data[start:end + 2])
    print(f"saved {args.out}")
    return 0


def require_yes(args):
    if not getattr(args, "yes", False):
        raise SystemExit("this deletes data on the console; repeat with --yes to confirm")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    p = sub.add_parser("payload")
    p.add_argument("path")
    p.add_argument("--timeout", type=float, default=20)
    f = sub.add_parser("ftp")
    fsub = f.add_subparsers(dest="action", required=True)
    fls = fsub.add_parser("ls")
    fls.add_argument("remote", nargs="?")
    fput = fsub.add_parser("put")
    fput.add_argument("local")
    fput.add_argument("remote")
    frm = fsub.add_parser("rm")
    frm.add_argument("remote")
    frm.add_argument("--dir", action="store_true")
    frm.add_argument("--yes", action="store_true")
    t = sub.add_parser("title")
    tsub = t.add_subparsers(dest="action", required=True)
    trm = tsub.add_parser("rm")
    trm.add_argument("title_id")
    trm.add_argument("--yes", action="store_true")
    k = sub.add_parser("klog")
    k.add_argument("--seconds", type=float, default=30)
    s = sub.add_parser("shot")
    s.add_argument("out")
    args = parser.parse_args(argv)
    handlers = {"status": cmd_status, "payload": cmd_payload, "ftp": cmd_ftp,
                "title": cmd_title, "klog": cmd_klog, "shot": cmd_shot}
    return handlers[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
