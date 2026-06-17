#!/usr/bin/env python3
"""
Manual screenrecord test: boots Chimera headless, then directly tests adb exec-out screenrecord.
Run after or alongside chimera-ui is already running.
"""
import os, sys, subprocess, time, json, pathlib, shutil

CHIMERA_ROOT = pathlib.Path(__file__).parent.parent
CONFIGS = CHIMERA_ROOT / "configs"
BUILD = CHIMERA_ROOT / "build" / "Release"

def main():
    sdk_cfg = json.load(open(CONFIGS / "android_sdk.json"))
    adb = pathlib.Path(sdk_cfg["sdk_root"]) / "platform-tools" / "adb.exe"
    ffmpeg = shutil.which("ffmpeg") or ""

    print(f"[INFO] adb: {adb}")
    print(f"[INFO] ffmpeg: {ffmpeg}")

    # Check what devices are connected
    result = subprocess.run([str(adb), "devices"], capture_output=True, text=True)
    print("[adb devices]\n" + result.stdout.strip())

    # Get the actual serial of the running emulator
    lines = result.stdout.strip().splitlines()
    devices = [l.split()[0] for l in lines[1:] if l.strip() and "device" in l]
    print(f"[INFO] connected devices: {devices}")
    if not devices:
        print("[FAIL] no connected devices - is the emulator running?")
        sys.exit(1)

    serial = devices[0]
    print(f"[INFO] using serial: {serial}")

    # Test 1: adb exec-out screenrecord for 5 seconds, count bytes
    print("\n[TEST 1] adb exec-out screenrecord (5s)")
    proc = subprocess.Popen(
        [str(adb), "-s", serial, "exec-out", "screenrecord",
         "--output-format=h264", "--size", "1920x1080", "--bit-rate", "24000000", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    start = time.time()
    total = 0
    deadline = start + 5
    while time.time() < deadline:
        chunk = proc.stdout.read(4096)
        if not chunk: break
        total += len(chunk)
    proc.kill(); proc.wait()
    stderr = proc.stderr.read().decode(errors='replace').strip()
    print(f"  bytes in 5s: {total}")
    if stderr: print(f"  adb stderr: {stderr[:300]}")

    if total == 0:
        print("  [FAIL] no H.264 data from screenrecord - headless emulator may not support exec-out screenrecord")
        # Try with adb shell instead
        print("\n[TEST 1b] trying adb shell screenrecord /dev/stdout")
        proc2 = subprocess.Popen(
            [str(adb), "-s", serial, "shell", "screenrecord",
             "--output-format=h264", "--size", "1920x1080", "--bit-rate", "24000000", "-"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        start = time.time()
        total2 = 0
        deadline = start + 5
        while time.time() < deadline:
            chunk = proc2.stdout.read(4096)
            if not chunk: break
            total2 += len(chunk)
        proc2.kill(); proc2.wait()
        stderr2 = proc2.stderr.read().decode(errors='replace').strip()
        print(f"  bytes in 5s (shell): {total2}")
        if stderr2: print(f"  stderr: {stderr2[:300]}")
        sys.exit(1)

    print(f"  [OK] {total} bytes ({total/1024:.1f} KB) - H.264 data flowing")

    if not ffmpeg:
        print("[SKIP] ffmpeg not found, skipping decode test")
        sys.exit(0)

    # Test 2: pipe to ffmpeg and count frames
    print(f"\n[TEST 2] adb | ffmpeg decode test (8s)")
    adb_proc = subprocess.Popen(
        [str(adb), "-s", serial, "exec-out", "screenrecord",
         "--output-format=h264", "--size", "1920x1080", "--bit-rate", "24000000", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    ff_proc = subprocess.Popen(
        [ffmpeg, "-hide_banner", "-loglevel", "error",
         "-fflags", "nobuffer", "-flags", "low_delay",
         "-probesize", "65536", "-analyzeduration", "0",
         "-f", "h264", "-i", "pipe:0",
         "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"],
        stdin=adb_proc.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    adb_proc.stdout.close()

    frame_size = 1920 * 1080 * 4
    buf = b""; frames = 0; total_bytes = 0
    deadline = time.time() + 8
    while time.time() < deadline:
        chunk = ff_proc.stdout.read(65536)
        if not chunk: break
        buf += chunk; total_bytes += len(chunk)
        while len(buf) >= frame_size:
            frames += 1; buf = buf[frame_size:]
            print(f"  frame {frames} at t={time.time()-start:.1f}s total_bytes={total_bytes}")

    adb_proc.kill(); ff_proc.kill()
    adb_proc.wait(); ff_proc.wait()
    ff_err = ff_proc.stderr.read().decode(errors='replace').strip()
    print(f"  frames: {frames}, bytes: {total_bytes}")
    if ff_err: print(f"  ffmpeg stderr: {ff_err[:300]}")

    if frames > 0:
        print("[PASS] Python pipe decoded frames - Qt pipe is the issue")
    else:
        print("[FAIL] Even Python pipe can't decode frames")

if __name__ == "__main__":
    main()
