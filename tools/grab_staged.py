#!/usr/bin/env python3
"""grab_staged.py — fetch the live kernel log at chosen moments during one run.

The kernel's exported ring is about 130 s of log, and `oem stop` only hands back the first
256 KiB, so the only way to see the boot-side lines (Sahara, EE=, en1) and the later ones
(QRTR, TFTP, UIM/NAS) in the same run is to fetch while it runs, at several moments.

  python grab_staged.py t136 60 150 300        # waits 60 s after "now", then 150 s, then 300 s
  python grab_staged.py t136 --at-start        # first wait is measured from the moment you press enter

Fetches go to <cmi-out>/<name>_<seconds>.log.
"""
import os
import subprocess
import sys
import time

FB = r"D:\UotanToolbox\Bin\platform-tools\fastboot.exe"
OUT_DIR = r"D:\hakios-build\cmi-out"


def fb(*args, timeout=180):
    p = subprocess.run([FB] + list(args), capture_output=True, text=True, timeout=timeout)
    return (p.stdout + p.stderr).strip()


def fetch(path):
    fb("oem", "log", timeout=90)
    got = fb("get_staged", path, timeout=180)
    size = os.path.getsize(path) if os.path.exists(path) else 0
    print(f"  {os.path.basename(path)}: {size} bytes  {got.splitlines()[-1] if got else ''}")


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not argv:
        print(__doc__)
        return 1
    name = argv[0]
    marks = [int(x) for x in argv[1:]] or [60, 150, 300]

    start = time.time()
    print(f"marks (s from now): {marks}")
    for m in marks:
        delay = m - (time.time() - start)
        if delay > 0:
            time.sleep(delay)
        path = os.path.join(OUT_DIR, f"{name}_{m}.log")
        fetch(path)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
