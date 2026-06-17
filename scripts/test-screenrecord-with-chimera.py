#!/usr/bin/env python3
"""
Boot Chimera (emulator), wait for boot, then run screenrecord manual test.
"""
import os, sys, subprocess, time, json, pathlib, shutil, tempfile

CHIMERA_ROOT = pathlib.Path(__file__).parent.parent
CONFIGS = CHIMERA_ROOT / "configs"
BUILD = CHIMERA_ROOT / "build" / "Release"

def main():
    sdk_cfg = json.load(open(CONFIGS / "android_sdk.json"))
    instances_cfg = json.load(open(CONFIGS / "instances.json"))
    adb = pathlib.Path(sdk_cfg["sdk_root"]) / "platform-tools" / "adb.exe"
    avd_home = sdk_cfg.get("avd_home", str(CHIMERA_ROOT / "third_party" / "android-avd"))
    ffmpeg = shutil.which("ffmpeg") or ""

    log_file = pathlib.Path(tempfile.gettempdir()) / f"chimera_screenrecord_{os.getpid()}.log"

    env = os.environ.copy()
    env["ANDROID_SDK_ROOT"] = sdk_cfg["sdk_root"]
    env["ANDROID_AVD_HOME"] = avd_home
    env["CHIMERA_LOG_PATH"] = str(log_file)
    # DO NOT set CHIMERA_VIDEO_TRANSPORT - use default gRPC to avoid ADB-H264 competing
    env.pop("CHIMERA_VIDEO_TRANSPORT", None)
    env.pop("CHIMERA_GRPC_TRANSPORT", None)

    print("[INFO] Starting Chimera (default gRPC display)...")
    chimera = subprocess.Popen([str(BUILD / "chimera-ui.exe")], env=env, cwd=str(BUILD))

    # Wait for boot (look for "boot_completed" or "Guest first-boot setup" in log)
    print("[INFO] Waiting for Android boot (up to 90s)...")
    boot_ready = False
    for _ in range(90):
        time.sleep(1)
        if log_file.exists():
            text = log_file.read_text(encoding='utf-8', errors='replace')
            if "Guest first-boot setup applied" in text or "CHIMERA_PERF guest" in text:
                # PERF means boot is complete and emulator is running
                if "CHIMERA_PERF" in text:
                    boot_ready = True
                    print("[INFO] Boot detected (PERF samples appearing)")
                    break

    if not boot_ready:
        print("[WARN] Boot not confirmed after 90s, proceeding anyway")

    # Wait for adb device to come online (not just offline)
    print("[INFO] Waiting for ADB device online (up to 30s)...")
    devices = []
    for i in range(30):
        time.sleep(1)
        result = subprocess.run([str(adb), "devices"], capture_output=True, text=True, timeout=10)
        lines = result.stdout.strip().splitlines()
        all_devs = [(l.split()[0], l.split()[1] if len(l.split()) > 1 else "")
                    for l in lines[1:] if l.strip() and "\t" in l]
        online = [d for d, s in all_devs if s == "device"]
        if online:
            devices = online
            print(f"[adb devices]\n{result.stdout.strip()}")
            break
        if i % 5 == 4:
            print(f"  still waiting... ({result.stdout.strip().replace(chr(10), ' ')})")

    print(f"[INFO] connected devices: {devices}")

    if not devices:
        # Check what adb devices shows
        result = subprocess.run([str(adb), "devices"], capture_output=True, text=True, timeout=10)
        print(f"[FAIL] no online devices after 30s\n{result.stdout.strip()}")
        chimera.kill(); chimera.wait()
        sys.exit(1)

    serial = devices[0]
    print(f"[INFO] using serial: {serial}")

    # Test: adb exec-out screenrecord for 5 seconds
    print(f"\n[TEST] adb exec-out screenrecord --output-format=h264 (5s)")
    proc = subprocess.Popen(
        [str(adb), "-s", serial, "exec-out", "screenrecord",
         "--output-format=h264", "--size", "1920x1080", "--bit-rate", "24000000", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    total = 0
    deadline = time.time() + 5
    while time.time() < deadline:
        chunk = proc.stdout.read(4096)
        if not chunk: break
        total += len(chunk)
    proc.kill(); proc.wait()
    stderr = proc.stderr.read().decode(errors='replace').strip()
    print(f"  bytes in 5s: {total}")
    if stderr: print(f"  adb stderr: {stderr[:500]}")

    if total > 0 and ffmpeg:
        print(f"\n[TEST2] pipe to ffmpeg (8s)")
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
        buf = b""; frames = 0; tbytes = 0
        start = time.time(); deadline = start + 8
        while time.time() < deadline:
            chunk = ff_proc.stdout.read(65536)
            if not chunk: break
            buf += chunk; tbytes += len(chunk)
            while len(buf) >= frame_size:
                frames += 1; buf = buf[frame_size:]
                print(f"  frame {frames} at t={time.time()-start:.1f}s")
        adb_proc.kill(); ff_proc.kill()
        adb_proc.wait(); ff_proc.wait()
        ff_err = ff_proc.stderr.read().decode(errors='replace').strip()
        print(f"  frames: {frames}, bytes: {tbytes}")
        if ff_err: print(f"  ffmpeg: {ff_err[:300]}")
        if frames > 0:
            print(f"[PASS] Python pipe: {frames} frames in 8s - Qt setStandardOutputProcess is the bug")
        else:
            print(f"[FAIL] Python pipe: 0 frames - screenrecord not producing decodable H.264")
    elif total == 0:
        print("[FAIL] screenrecord produced no data - not supported in headless mode?")

    # Kill chimera
    chimera.terminate()
    try: chimera.wait(timeout=10)
    except: chimera.kill()
    subprocess.run(["taskkill", "/F", "/IM", "emulator.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "adb.exe"], capture_output=True)

if __name__ == "__main__":
    main()
