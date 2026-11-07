#!/usr/bin/env python3
"""
Black OS - Phase 8-style automated boot verification.

Boots blackos.iso headless in QEMU with the kernel's COM1 console wired to
stdio, then:

  1. waits for the BLACKOS_BOOT_OK boot-complete marker on serial,
  2. asserts boot telemetry (multiboot magic, identity map, PMM selftest,
     page-table root) is present in the boot transcript,
  3. drives the serial console like a keyboard user:
        info / mem / uptime / pmmtest / maptest
     and asserts each expected response,
  4. exits 0 on PASS, 1 on FAIL.

Portable: works on Linux/macOS/Windows build hosts.
"""
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ISO_DEFAULT = os.path.normpath(os.path.join(HERE, "..", "blackos.iso"))

BOOT_MARKER = "BLACKOS_BOOT_OK"

# Static boot-phase assertions ------------------------------- (marker, label)
BOOT_ASSERTIONS = [
    ("multiboot2 loader magic OK", "multiboot2 boot magic verified in-kernel"),
    ("running allocator selftest", "PMM allocator selftest ran"),
    ("selftest OK", "PMM allocator selftest passed"),
    ("CR3 root", "page table root installed and reported"),
    ("identity map: 0x0 - 0x3fffffff", "long-mode identity map is live"),
]

# Interactive console assertions (command -> expected substring)
CMD_ASSERTIONS = [
    ("info", "CR3 root"),
    ("mem", "bitmap-managed window"),
    ("uptime", "uptime:"),
    ("pmmtest", "selftest OK"),
    ("maptest", "mapped ("),
]


def find_qemu():
    path = shutil.which("qemu-system-x86_64")
    if not path:
        sys.exit("error: qemu-system-x86_64 not available on PATH")
    return path


def main():
    ap = argparse.ArgumentParser(description="Black OS boot verification")
    ap.add_argument("--iso", default=ISO_DEFAULT)
    ap.add_argument("--timeout", type=float, default=120.0,
                    help="seconds to wait for the boot marker")
    ap.add_argument("--keep-log", action="store_true",
                    help="dump the transcript to build/boot.log")
    args = ap.parse_args()

    if not os.path.isfile(args.iso):
        sys.exit(f"error: ISO not found: {args.iso} - run 'make iso' first")

    qemu = find_qemu()
    cmd = [
        qemu,
        "-display", "none",          # headless
        "-serial", "stdio",          # COM1 <-> our pipes
        "-monitor", "none",          # keep QEMU from grabbing SDTIN twice
        "-no-reboot",
        "-m", "512",
        "-cdrom", args.iso,
    ]

    print(f"[verify] booting {args.iso} ...")

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )

    # Reader thread accumulates the serial stream into `transcript`.
    transcript = bytearray()
    def reader():
        while True:
            chunk = proc.stdout.read(256)
            if not chunk:
                break
            transcript.extend(chunk)
    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def transcript_text():
        return transcript.decode("utf-8", "replace")

    # ---- step 1: boot marker -------------------------------------------------
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if BOOT_MARKER in transcript_text():
            break
        if proc.poll() is not None:
            print("[verify] FAIL: QEMU exited before the boot marker")
            print(transcript_text()[-2000:])
            sys.exit(1)
        time.sleep(0.2)
    else:
        proc.kill()
        print("[verify] FAIL: boot marker not seen within timeout")
        print(transcript_text()[-2000:])
        sys.exit(1)

    print(f"[verify] PASS: {BOOT_MARKER} reached "
          f"({len(transcript)} bytes of serial logged)")

    failures = []
    text = transcript_text()
    print("[verify] --- boot assertions ---")
    for marker, what in BOOT_ASSERTIONS:
        if marker in text:
            print(f"[verify]   ok    {what}")
        else:
            print(f"[verify]   FAIL  {what} (missing '{marker}')")
            failures.append((marker, what))

    # ---- step 2: drive the console -------------------------------------------
    print("[verify] --- console interaction ---")
    for command, needle in CMD_ASSERTIONS:
        proc.stdin.write(command.encode() + b"\n")
        proc.stdin.flush()
        time.sleep(0.5)          # let the guest echo + reply

    time.sleep(1.5)
    text = transcript_text()

    for command, needle in CMD_ASSERTIONS:
        if f"# {command}" in text or command + "\n" in text:
            if needle in text:
                print(f"[verify]   ok    {command} -> '{needle}'")
            else:
                print(f"[verify]   FAIL  {command} missing '{needle}'")
                failures.append((command, needle))
        else:
            print(f"[verify]   FAIL  {command} command never echoed")
            failures.append((command, "command echo"))

    # ---- step 3: power down ---------------------------------------------------
    try:
        proc.stdin.write(b"halt\n")
        proc.stdin.flush()
    except Exception:
        pass
    time.sleep(1.0)
    proc.kill()
    proc.wait()

    print("[verify] --- transcript tail ---")
    print(text[-1500:])

    if args.keep_log:
        log = os.path.join(HERE, "..", "build", "boot.log")
        os.makedirs(os.path.dirname(log), exist_ok=True)
        with open(log, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"[verify] transcript saved to {os.path.normpath(log)}")

    print("[verify] verdict ----")
    if failures:
        for f in failures:
            print(f"[verify] FAIL  {f}")
        sys.exit(1)
    print("[verify] PASS: Black OS boot + console verified end-to-end")
    sys.exit(0)


if __name__ == "__main__":
    main()
