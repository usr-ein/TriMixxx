#!/usr/bin/env python3
"""Run commands on an emulated deck over its serial console, without ssh.

pi-qemu gives the Pi a console on the mini UART (the S3 has the PL011):
<run dir>/console.sock, a getty on ttyS0, every byte also in console.log.
This logs in as sam1902 if it has to (password from image/secrets.env, or
SAM1902_PASSWORD), runs each COMMAND in turn and prints its output.

    console.py RUN_DIR/console.sock 'ip -br a' 'systemctl --failed'

For a person: `nc -U RUN_DIR/console.sock` is the console itself.
"""
import os
import re
import socket
import sys
import time
from pathlib import Path


def password():
    if os.environ.get("SAM1902_PASSWORD"):
        return os.environ["SAM1902_PASSWORD"]
    secrets = Path(__file__).resolve().parent / "image" / "secrets.env"
    for line in secrets.read_text().splitlines():
        if line.startswith("SAM1902_PASSWORD="):
            return line.split("=", 1)[1].strip().strip("'\"")
    sys.exit("console.py: no SAM1902_PASSWORD (image/secrets.env)")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    s = socket.socket(socket.AF_UNIX)
    s.connect(sys.argv[1])
    s.settimeout(0.3)

    def read_until(pattern, timeout):
        buf, end = "", time.time() + timeout
        while time.time() < end:
            try:
                data = s.recv(65536)
            except socket.timeout:
                data = b""
            buf += data.decode(errors="replace")
            if re.search(pattern, buf):
                return buf
        return buf

    prompt = r"\$ $"
    s.send(b"\n")
    seen = read_until(r"login: $|" + prompt, 10)
    if "login:" in seen:
        s.send(b"sam1902\n")
        read_until("assword", 10)
        s.send(password().encode() + b"\n")
        if not re.search(prompt, read_until(prompt, 20)):
            sys.exit("console.py: login failed")
    s.send(b"export TERM=dumb PAGER=cat SYSTEMD_PAGER= SYSTEMD_COLORS=0; stty -echo cols 250\n")
    read_until(prompt, 5)
    for cmd in sys.argv[2:]:
        mark = f"__console_done_{time.time_ns()}__"
        s.send(f"{cmd}; echo {mark} $?\n".encode())
        out = read_until(mark + r" \d+", 600)
        body, _, status = out.partition(mark)
        print(re.sub(r"\x1b\[[0-9;]*m", "", body).replace("\r", "").strip())
        code = re.match(r" (\d+)", status)
        if code and code.group(1) != "0":
            print(f"[exit {code.group(1)}]")
        read_until(prompt, 2)


if __name__ == "__main__":
    main()
