#!/usr/bin/env python3
"""Run an N64 ROM in ares and read 32-bit globals over ares' GDB server.

usage: ares_read.py <rom.z64> <rom.elf> <symbol> [<symbol> ...]
         [--seconds N] [--until SYMBOL=VALUE] [--port P] [--ares PATH]
         [--dump PTR_SYMBOL LEN OUT]

Symbol addresses come from `mips64-elf-nm` on the ELF. ares is started with
its debug server enabled, left running for --seconds (with --until: until
that symbol holds VALUE, polled every POLL seconds, or at most --seconds;
each poll briefly halts the emulator, which the ROM does not see),
then each symbol is read with the GDB remote-protocol `m` packet and
printed as `symbol=0xXXXXXXXX`.
--dump reads the 32-bit pointer stored at PTR_SYMBOL and writes LEN bytes
from that address to OUT (e.g. the last framebuffer; see platform/n64).
ares is terminated afterwards. Exit status 1 if any read fails.
"""
import argparse
import os
import socket
import subprocess
import sys
import time

ARES = "/Applications/ares.app/Contents/MacOS/ares"
POLL = 2


def symbols(elf, names):
    nm = os.path.join(os.environ.get("N64_INST", os.path.expanduser("~/opt/libdragon")),
                      "bin", "mips64-elf-nm")
    out = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16) & 0xFFFFFFFF
    missing = [n for n in names if n not in table]
    if missing:
        sys.exit(f"symbols not found in {elf}: {', '.join(missing)}")
    return {n: table[n] for n in names}


class Gdb:
    def __init__(self, port, timeout):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.25)
        self.sock.settimeout(5)
        self.buf = b""
        # ares drops the connection unless the client opens with an ack.
        self.sock.sendall(b"+")
        self.command("qSupported:swbreak+;hwbreak+")

    def _recv_packet(self):
        while True:
            start = self.buf.find(b"$")
            end = self.buf.find(b"#", start + 1) if start >= 0 else -1
            if start >= 0 and end >= 0 and len(self.buf) >= end + 3:
                payload = self.buf[start + 1:end]
                self.buf = self.buf[end + 3:]
                self.sock.sendall(b"+")
                return payload.decode()
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("ares closed the GDB connection")
            self.buf += chunk

    def command(self, cmd):
        csum = sum(cmd.encode()) & 0xFF
        self.sock.sendall(f"${cmd}#{csum:02x}".encode())
        return self._recv_packet()

    def read32(self, addr):
        reply = self.command(f"m{addr:x},4")
        if len(reply) != 8 or reply.startswith("E"):
            raise RuntimeError(f"read at 0x{addr:08x} failed: {reply!r}")
        return int(reply, 16)  # N64 is big-endian; bytes arrive in memory order

    def read(self, addr, length, chunk=1024):
        out = bytearray()
        while len(out) < length:
            n = min(chunk, length - len(out))
            reply = self.command(f"m{addr + len(out):x},{n:x}")
            if len(reply) != 2 * n:
                raise RuntimeError(f"read at 0x{addr + len(out):08x} failed: {reply[:16]!r}")
            out += bytes.fromhex(reply)
        return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("elf")
    ap.add_argument("symbols", nargs="+")
    ap.add_argument("--seconds", type=float, default=5)
    ap.add_argument("--until", metavar="SYMBOL=VALUE")
    ap.add_argument("--port", type=int, default=9123)
    ap.add_argument("--ares", default=ARES)
    ap.add_argument("--dump", nargs=3, metavar=("PTR_SYMBOL", "LEN", "OUT"))
    args = ap.parse_args()

    until = args.until.split("=") if args.until else None
    names = args.symbols + ([args.dump[0]] if args.dump else []) + ([until[0]] if until else [])
    addrs = symbols(args.elf, names)
    proc = subprocess.Popen(
        [args.ares, "--no-file-prompt", "--system", "Nintendo 64",
         "--setting", "General/HomebrewMode=true",
         "--setting", "Input/Defocus=Allow",          # keep running unfocused
         "--setting", "DebugServer/Enabled=true",
         "--setting", f"DebugServer/Port={args.port}",
         "--setting", "DebugServer/UseIPv4=true",
         args.rom],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = True
    try:
        start = time.time()
        if until:
            # A GDB connection halts ares; closing it resumes. So each poll
            # is a short connection (the ROM's clock stops with it).
            want = int(until[1], 0)
            while time.time() - start < args.seconds:
                gdb = Gdb(args.port, timeout=30)
                gdb.command("?")
                if gdb.read32(addrs[until[0]]) == want:
                    break
                gdb.sock.close()
                time.sleep(POLL)
            else:
                gdb = Gdb(args.port, timeout=10)
                gdb.command("?")
            print(f"ares ran {time.time() - start:.0f} s", file=sys.stderr)
        else:
            time.sleep(args.seconds)
            gdb = Gdb(args.port, timeout=10)
            gdb.command("?")
        if args.dump:
            ptr = gdb.read32(addrs[args.dump[0]])
            with open(args.dump[2], "wb") as f:
                f.write(gdb.read(ptr, int(args.dump[1], 0)))
            print(f"dumped {args.dump[1]} bytes from 0x{ptr:08x} to {args.dump[2]}")
        for name, addr in addrs.items():
            if name not in args.symbols:
                continue
            try:
                print(f"{name}=0x{gdb.read32(addr):08x}")
            except Exception as e:  # report every symbol, then fail
                print(f"{name}: {e}", file=sys.stderr)
                ok = False
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
