#!/usr/bin/env python3
"""grab_log_root.py — fetch the whole kernel log, not the first 256 KB.

Why: `fastboot get_staged` hands back only 262143 bytes while the kernel returns ~1.5 MB
(the loader prints "kernel returned: log at ..., N bytes").  The loader also writes that same
staged log to the "logdump" partition, and this phone is rooted, so the log can be read back
whole with dd — no USB size limit, no loader change.

  python grab_log_root.py t133.log

Steps: oem stop -> wait for "returned" -> reboot to Android -> su dd logdump -> pull -> trim.
"""
import os
import re
import subprocess
import sys
import time

FB = r"D:\UotanToolbox\Bin\platform-tools\fastboot.exe"
FB2 = r"D:\hakios-build\pt\platform-tools\fastboot.exe"
ADB = r"D:\hakios-build\pt\platform-tools\adb.exe"
OUT_DIR = r"D:\hakios-build\cmi-out"


def run(cmd, timeout=180):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return (p.stdout + p.stderr).strip()


def fb(*args, timeout=180):
    return run([FB] + list(args), timeout)


def adb(*args, timeout=180):
    return run([ADB] + list(args), timeout)


def wait_for(kind, tries=40, delay=5):
    for _ in range(tries):
        if kind == "adb":
            if re.search(r"\bdevice\b", run([ADB, "devices"])):
                return True
        else:
            if "fastboot" in fb("devices"):
                return True
        time.sleep(delay)
    return False


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else time.strftime("t%H%M") + ".log"
    if not name.lower().endswith(".log"):
        name += ".log"
    path = name if os.path.isabs(name) else os.path.join(OUT_DIR, name)

    print("stop:", fb("oem", "stop").splitlines()[-1:])
    for _ in range(60):
        res = fb("getvar", "result", timeout=20)
        if "returned" in res:
            print("result:", res.splitlines()[0].strip())
            break
        time.sleep(5)

    # what the kernel said it handed over, for the record
    logmsg = fb("oem", "log")
    for line in logmsg.splitlines():
        if "kernel returned" in line:
            print("loader:", line.strip())

    print("rebooting to Android to read logdump with root ...")
    fb("reboot")
    if not wait_for("adb"):
        print("adb did not come up; cannot read logdump")
        return 1
    time.sleep(3)

    out = adb("shell", "su -c 'dd if=/dev/block/by-name/logdump of=/sdcard/klog.bin bs=1M'")
    print("dd:", out.splitlines()[-1] if out else "?")
    got = adb("pull", "/sdcard/klog.bin", path)
    print("pull:", got.splitlines()[-1] if got else "?")
    adb("shell", "su -c 'rm -f /sdcard/klog.bin'")

    if not os.path.exists(path):
        return 1
    data = open(path, "rb").read()
    text = data.decode("utf-8", "replace")
    head = text.find("== KERNEL LOG ==")
    tail = text.rfind("== END OF LOG ==")
    body = text[head:] if head >= 0 else text
    if tail > head > 0:
        body = text[head:tail]
    body = body.replace("\x00", "")
    open(path, "w", encoding="utf-8", newline="").write(text[:head] + body if head > 0 else body)
    print(f"\n{path}  {os.path.getsize(path)} bytes (logdump {len(data)} bytes)")

    stamps = re.findall(r"\[\s*(\d+\.\d+)\]", text)
    if stamps:
        print(f"timestamps: first={stamps[0]}s last={stamps[-1]}s  ({len(stamps)} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
