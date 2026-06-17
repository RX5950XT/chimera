#!/usr/bin/env python3
"""
ADB H.264 pipeline diagnostic test.
Runs Chimera with CHIMERA_VIDEO_TRANSPORT=screenrecord for 120 seconds
and checks whether frames arrive from the ffmpeg decode pipeline.
"""

import os
import sys
import subprocess
import time
import json
import re
import signal
import pathlib
import tempfile

CHIMERA_ROOT = pathlib.Path(__file__).parent.parent
BUILD = CHIMERA_ROOT / "build" / "Release"
CONFIGS = CHIMERA_ROOT / "configs"


def load_json(path):
    with open(path) as f:
        return json.load(f)


def find_ffmpeg():
    """Find ffmpeg on PATH."""
    import shutil
    ff = shutil.which("ffmpeg")
    if ff:
        return ff
    # WinGet common location
    candidate = pathlib.Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Links" / "ffmpeg.exe"
    if candidate.exists():
        return str(candidate)
    return None


def main():
    sdk_cfg = load_json(CONFIGS / "android_sdk.json")
    instances_cfg = load_json(CONFIGS / "instances.json")

    adb_path = pathlib.Path(sdk_cfg["sdk_root"]) / "platform-tools" / "adb.exe"
    emulator_path = pathlib.Path(sdk_cfg["emulator"])
    avd_home = sdk_cfg.get("avd_home", str(CHIMERA_ROOT / "third_party" / "android-avd"))

    serial = instances_cfg["instances"][0].get("adbSerial", "emulator-5554")
    name = instances_cfg["instances"][0].get("name", "chimera_dev")

    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print("[FAIL] ffmpeg not found on PATH")
        sys.exit(1)
    print(f"[INFO] ffmpeg: {ffmpeg}")
    print(f"[INFO] adb:    {adb_path}")

    # --- Step 1: Quick manual pipeline test (adb | ffmpeg, 8 seconds) ---
    print("\n[STEP 1] Manual pipeline: adb exec-out screenrecord | ffmpeg -> BGRA frames")
    try:
        adb_proc = subprocess.Popen(
            [str(adb_path), "exec-out", "screenrecord",
             "--output-format=h264", "--size", "1920x1080",
             "--bit-rate", "24000000", "-"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ffmpeg_proc = subprocess.Popen(
            [ffmpeg,
             "-hide_banner", "-loglevel", "error",
             "-fflags", "nobuffer", "-flags", "low_delay",
             "-probesize", "65536", "-analyzeduration", "0",
             "-f", "h264", "-i", "pipe:0",
             "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"],
            stdin=adb_proc.stdout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        adb_proc.stdout.close()  # allow adb to send SIGPIPE when ffmpeg exits

        frame_size = 1920 * 1080 * 4  # 8,294,400 bytes
        buf = b""
        frames = 0
        start = time.time()
        deadline = start + 15  # 15 second test

        while time.time() < deadline:
            chunk = ffmpeg_proc.stdout.read(65536)
            if not chunk:
                break
            buf += chunk
            while len(buf) >= frame_size:
                frames += 1
                buf = buf[frame_size:]
                print(f"  [frame {frames}] received at t={time.time()-start:.1f}s")

        adb_proc.kill()
        ffmpeg_proc.kill()
        adb_proc.wait()
        ff_stderr = ffmpeg_proc.stderr.read().decode(errors='replace').strip()
        ffmpeg_proc.wait()

        if ff_stderr:
            print(f"  ffmpeg stderr: {ff_stderr}")
        print(f"  -> {frames} frames in {time.time()-start:.1f}s")
        if frames == 0:
            print("[WARN] Manual pipeline produced 0 frames — ffmpeg or adb issue")
        else:
            print("[OK] Manual pipeline works — Qt pipe integration is the suspect")
    except Exception as e:
        print(f"  [ERROR] {e}")

    # --- Step 2: Chimera integration test ---
    print("\n[STEP 2] Chimera integration with CHIMERA_VIDEO_TRANSPORT=screenrecord (90s)")
    log_file = pathlib.Path(tempfile.gettempdir()) / f"chimera_adbh264_test_{os.getpid()}.log"
    log_file.unlink(missing_ok=True)

    env = os.environ.copy()
    env["ANDROID_SDK_ROOT"] = sdk_cfg["sdk_root"]
    env["ANDROID_AVD_HOME"] = avd_home
    env["CHIMERA_LOG_PATH"] = str(log_file)
    env["CHIMERA_VIDEO_TRANSPORT"] = "screenrecord"
    env.pop("CHIMERA_GRPC_TRANSPORT", None)
    env.pop("CHIMERA_SHMEM_FRAME_NAME", None)
    env.pop("CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE", None)
    env.pop("CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE", None)

    chimera = subprocess.Popen(
        [str(BUILD / "chimera-ui.exe")],
        env=env,
        cwd=str(BUILD),
    )

    start = time.time()
    deadline_boot = start + 60   # wait up to 60s for ADB-H264 to start
    deadline_run = start + 120   # total 120s test

    adbh264_started = False
    first_frame_seen = False
    restart_count = 0
    ffmpeg_errors = []
    adb_errors = []
    frame_count_seen = False
    manual_test_done = False
    manual_test_frames = -1

    print(f"  Waiting for ADB-H264 start (up to 60s) then running 60s more...")

    while time.time() < deadline_run:
        time.sleep(2)

        if not log_file.exists():
            continue

        try:
            text = log_file.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue

        lines = text.splitlines()

        for line in lines:
            if "ADB-H264" in line and "screen capture stream" in line:
                if not adbh264_started:
                    adbh264_started = True
                    elapsed = time.time() - start
                    print(f"  [t={elapsed:.0f}s] ADB-H264 capture started")

            if "first frame received" in line and not first_frame_seen:
                first_frame_seen = True
                elapsed = time.time() - start
                print(f"  [t={elapsed:.0f}s] FIRST FRAME ARRIVED!")

            if "[AdbH264] ffmpeg:" in line and line not in ffmpeg_errors:
                ffmpeg_errors.append(line)
                print(f"  ffmpeg: {line.strip()}")

            if "[AdbH264] adb:" in line and line not in adb_errors:
                adb_errors.append(line)
                print(f"  adb: {line.strip()}")

            if "adb screenrecord exited" in line or "ffmpeg decoder exited" in line:
                restart_count += 1
                if restart_count <= 3:
                    print(f"  [restart #{restart_count}] {line.strip()}")

            if "CHIMERA_PERF" in line:
                m = re.search(r'stream=(\d+\.\d+)', line)
                if m and float(m.group(1)) > 0:
                    if not frame_count_seen:
                        frame_count_seen = True
                        print(f"  [PERF] non-zero stream FPS: {line.strip()}")

        # Once ADB-H264 has been running for 8s, run manual parallel test
        if adbh264_started and not manual_test_done and time.time() > (start + 65):
            manual_test_done = True
            print("\n  [PARALLEL TEST] Manual adb | ffmpeg pipeline while Chimera running...")
            try:
                adb_p = subprocess.Popen(
                    [str(adb_path), "-s", "emulator-5554", "exec-out",
                     "screenrecord", "--output-format=h264",
                     "--size", "1920x1080", "--bit-rate", "24000000", "-"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE
                )
                ff_p = subprocess.Popen(
                    [ffmpeg, "-hide_banner", "-loglevel", "error",
                     "-fflags", "nobuffer", "-flags", "low_delay",
                     "-probesize", "65536", "-analyzeduration", "0",
                     "-f", "h264", "-i", "pipe:0",
                     "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1"],
                    stdin=adb_p.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE
                )
                adb_p.stdout.close()
                frame_size = 1920 * 1080 * 4
                buf = b""
                mframes = 0
                mdeadline = time.time() + 10
                while time.time() < mdeadline:
                    chunk = ff_p.stdout.read(65536)
                    if not chunk:
                        break
                    buf += chunk
                    while len(buf) >= frame_size:
                        mframes += 1
                        buf = buf[frame_size:]
                adb_p.kill(); ff_p.kill()
                adb_p.wait(); ff_p.wait()
                ff_stderr = ff_p.stderr.read().decode(errors='replace').strip()
                manual_test_frames = mframes
                print(f"  -> manual: {mframes} frames in 10s, ffmpeg_err: {ff_stderr[:200] or 'none'}")
            except Exception as e:
                print(f"  -> manual test error: {e}")

        # Once ADB-H264 started, wait the extra 60s then stop
        if adbh264_started and time.time() > (deadline_boot + 60):
            break

    # Kill Chimera
    try:
        chimera.terminate()
        chimera.wait(timeout=10)
    except Exception:
        chimera.kill()

    # Kill stale emulator processes
    subprocess.run(["taskkill", "/F", "/IM", "emulator.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "qemu-system-x86_64.exe"], capture_output=True)
    time.sleep(2)

    print("\n[RESULTS]")
    print(f"  adbh264_started   = {adbh264_started}")
    print(f"  first_frame_seen  = {first_frame_seen}")
    print(f"  restart_count     = {restart_count}")
    print(f"  ffmpeg_errors     = {len(ffmpeg_errors)}")
    print(f"  adb_errors        = {len(adb_errors)}")
    print(f"  non_zero_stream   = {frame_count_seen}")
    print(f"  manual_frames     = {manual_test_frames}")

    if not adbh264_started:
        print("[FAIL] ADB-H264 never started (boot failed or env not applied)")
        sys.exit(1)
    if not first_frame_seen:
        print("[FAIL] No frames received from AdbH264 pipeline")
        if not ffmpeg_errors and not adb_errors:
            print("  -> No stderr from ffmpeg or adb: pipe silent, possible:")
            print("     (a) adb not connected to ffmpeg stdin via Qt OS-level pipe")
            print("     (b) ffmpeg throttled and starved before first keyframe")
        sys.exit(1)
    print("[PASS] ADB-H264 frame delivery confirmed")


if __name__ == "__main__":
    main()
