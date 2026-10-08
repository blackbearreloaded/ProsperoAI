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
  ps5ctl.py shell CMD [--yes]           run one command in shsrv (port 2323); needs shsrv running
  ps5ctl.py ps                          list processes through shsrv
  ps5ctl.py launch TITLE_ID [--yes]     start a title through shsrv (launch)

Hosts and ports come from the environment: PS5_HOST (<PS5-IP>), PS5_PAYLOAD_PORT (9021),
PS5_FTP_PORT (2120), PS5_KLOG_PORT (3232), P5_MANAGER (http://<PS5-IP>:3001),
P5_SESSION (Remote Play session id, needed for shot).

Deleting a title from /data/homebrew removes its files only. The console's title registration
is kept by the system until the title is deleted from the console UI, so re-install after a
delete may need that step too. Destructive commands refuse to run without --yes.
"""
import argparse
import ftplib
import io
import os
import pathlib
import socket
import sys
import time
import urllib.request

HOST = os.environ.get("PS5_HOST", "<PS5-IP>")
PAYLOAD_PORT = int(os.environ.get("PS5_PAYLOAD_PORT", "9021"))
FTP_PORT = int(os.environ.get("PS5_FTP_PORT", "2120"))
KLOG_PORT = int(os.environ.get("PS5_KLOG_PORT", "3232"))
SHSRV_PORT = int(os.environ.get("PS5_SHSRV_PORT", "2323"))
SHSRV_PROMPT = b"/$ "
MANAGER = os.environ.get("P5_MANAGER", "http://<PS5-IP>:3001").rstrip("/")
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
    except ftplib.all_errors as error:
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
        elif args.action == "size":
            ftp.voidcmd("TYPE I")
            print(ftp.size(args.remote))
        elif args.action == "chmod":
            print(ftp.sendcmd(f"SITE CHMOD {args.mode} {args.remote}"))
        elif args.action == "put":
            with open(args.local, "rb") as fh:
                ftp.storbinary(f"STOR {args.remote}", fh, blocksize=256 * 1024)
            print(f"uploaded {args.local} -> {args.remote}")
        elif args.action == "put-dir":
            root = pathlib.Path(args.local)
            if not root.is_dir():
                raise ValueError(f"not a directory: {root}")
            def ensure_directory(remote):
                current = ""
                for component in remote.strip("/").split("/"):
                    current += "/" + component
                    try:
                        ftp.mkd(current)
                    except ftplib.error_perm:
                        ftp.cwd(current)
            files = sorted((p for p in root.rglob("*") if p.is_file()),
                           key=lambda p: (p.name == "param.json", p.name == "eboot.bin", str(p)))
            for path in files:
                remote = args.remote.rstrip("/") + "/" + path.relative_to(root).as_posix()
                ensure_directory(remote.rsplit("/", 1)[0])
                with path.open("rb") as source:
                    ftp.storbinary(f"STOR {remote}", source, blocksize=256 * 1024)
                if path.name == "eboot.bin" or path.suffix == ".prx":
                    ftp.sendcmd(f"SITE CHMOD 755 {remote}")
                print(f"uploaded {path.relative_to(root)}", flush=True)
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
    session = args.session
    if not session:
        print("set P5_SESSION to an active Remote Play session id", file=sys.stderr)
        return 2
    with urllib.request.urlopen(f"{MANAGER}/api/remoteplay/sessions/{session}/video.mjpeg",
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


def shsrv(command, timeout=8.0):
    """Run one command in shsrv and return its output without the banner and prompt.

    shsrv is an interactive shell: the banner and prompt arrive first, then the output of the
    command, then the prompt again. Output is read until the prompt returns or the timeout ends.
    """
    with socket.create_connection((HOST, SHSRV_PORT), timeout=5) as sock:
        buf = b""
        end = time.monotonic() + timeout
        while SHSRV_PROMPT not in buf and time.monotonic() < end:
            try:
                buf += sock.recv(4096)
            except socket.timeout:
                break
        buf = b""
        sock.sendall(command.encode() + b"\n")
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
            if buf.rstrip().endswith(SHSRV_PROMPT.rstrip()):
                break
    text = buf.decode(errors="replace").replace("\r\n", "\n")
    if text.endswith("/$ "):
        text = text[:-3]
    return text.rstrip() + "\n"


def cmd_shell(args):
    if not args.cmd:
        raise SystemExit("empty command")
    if any(word in args.cmd.split() for word in ("rm", "rmdir", "kill", "launch", "mv", "pkg_install")):
        require_yes(args)
    sys.stdout.write(shsrv(args.cmd))
    return 0


def cmd_ps(_args):
    sys.stdout.write(shsrv("ps"))
    return 0


def cmd_launch(args):
    require_yes(args)
    sys.stdout.write(shsrv(f"launch {args.title_id}"))
    return 0


AGENT_PORT = int(os.environ.get("PS5_AGENT_PORT", "9111"))
SHADOW_PORT = int(os.environ.get("PS5_SHADOW_PORT", "10101"))


def agent_cmd(command):
    """One command to the resident ps5vkctl control payload (port 9111); returns its one-line reply."""
    with socket.create_connection((HOST, AGENT_PORT), timeout=30) as sock:
        sock.sendall(command.encode() + b"\n")
        out = b""
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                out += chunk
        except socket.timeout:
            pass
    return out.decode(errors="replace").strip()


def cmd_agent(args):
    print(agent_cmd(" ".join(args.words)))
    return 0


def cmd_cat(args):
    ftp = ftp_connect()
    try:
        out = []
        ftp.retrbinary(f"RETR {args.remote}", out.append)
        sys.stdout.write(b"".join(out).decode(errors="replace")[-args.tail:] if args.tail else b"".join(out).decode(errors="replace"))
    finally:
        ftp.quit()
    return 0


def shadow_api(method, route, body=None):
    """ShadowMountPlus HTTP API (api_bind_address/api_port in /data/shadowmount/config.ini)."""
    import json
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"http://{HOST}:{SHADOW_PORT}/api/v1{route}", data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode(errors="replace")


def cmd_shadow(args):
    if args.action == "info":
        code, text = shadow_api("POST", "/games/info", {"title_id": args.title_id})
    elif args.action == "version":
        code, text = shadow_api("GET", "/version")
    else:
        require_yes(args)
        code, text = shadow_api("POST", "/games/delete", {"title_id": args.title_id, "confirm": True})
    print(code, text.strip())
    return 0 if code < 400 else 1


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
    fsize = fsub.add_parser("size")
    fsize.add_argument("remote")
    fchmod = fsub.add_parser("chmod")
    fchmod.add_argument("mode", choices=("644", "755", "777"))
    fchmod.add_argument("remote")
    fput = fsub.add_parser("put")
    fput.add_argument("local")
    fput.add_argument("remote")
    fdir = fsub.add_parser("put-dir")
    fdir.add_argument("local")
    fdir.add_argument("remote")
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
    s.add_argument("--session", default=SESSION)
    s.add_argument("out")
    sh = sub.add_parser("shell")
    sh.add_argument("cmd")
    sh.add_argument("--yes", action="store_true")
    sub.add_parser("ps")
    la = sub.add_parser("launch")
    la.add_argument("title_id")
    la.add_argument("--yes", action="store_true")
    ag = sub.add_parser("agent", help="send one command to the ps5vkctl control payload on port 9111")
    ag.add_argument("words", nargs="+")
    ct = sub.add_parser("cat", help="print a file from the console (FTP)")
    ct.add_argument("remote")
    ct.add_argument("--tail", type=int, default=0, help="only the last N characters")
    sh2 = sub.add_parser("shadow", help="ShadowMountPlus API: info, version, delete (needs --yes)")
    sh2.add_argument("action", choices=["info", "version", "delete"])
    sh2.add_argument("title_id", nargs="?", default="")
    sh2.add_argument("--yes", action="store_true")
    args = parser.parse_args(argv)
    handlers = {"agent": cmd_agent, "cat": cmd_cat, "shadow": cmd_shadow, "status": cmd_status, "payload": cmd_payload, "ftp": cmd_ftp,
                "title": cmd_title, "klog": cmd_klog, "shot": cmd_shot,
                "shell": cmd_shell, "ps": cmd_ps, "launch": cmd_launch}
    return handlers[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
