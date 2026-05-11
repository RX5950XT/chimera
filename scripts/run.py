#!/usr/bin/env python3
"""
Run Project Chimera — launch the Android emulator with WHPX acceleration.

Usage:
    python scripts/run.py [--avd chimera_pie64] [--gpu swiftshader_indirect|host] [--headless]
"""

import os
import sys
import json
import subprocess
import argparse
import signal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHIMERA_ROOT = os.path.dirname(SCRIPT_DIR)
CONFIG_PATH = os.path.join(CHIMERA_ROOT, "configs", "android_sdk.json")


def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)


def main():
    parser = argparse.ArgumentParser(description="Project Chimera Launcher")
    parser.add_argument("--avd", default=None, help="AVD name to launch")
    parser.add_argument("--gpu", default="swiftshader_indirect",
                        choices=["swiftshader_indirect", "host", "angle_indirect", "auto"],
                        help="GPU backend")
    parser.add_argument("--headless", action="store_true", help="Run without GUI window")
    parser.add_argument("--no-accel", action="store_true", help="Disable hardware acceleration")
    parser.add_argument("--verbose", action="store_true", help="Verbose emulator output")
    parser.add_argument("--ram", type=int, default=4096, help="Guest RAM in MB")
    parser.add_argument("--cpus", type=int, default=4, help="Guest CPU cores")
    parser.add_argument("--resolution", default="1920x1080", help="Guest resolution WxH")
    parser.add_argument("--adb-port", type=int, default=5555, help="ADB host port")
    args = parser.parse_args()

    cfg = load_config()
    emulator = cfg["emulator"]
    avd_name = args.avd or cfg["avd_name"]

    env = os.environ.copy()
    env["ANDROID_SDK_ROOT"] = cfg["sdk_root"]
    env["ANDROID_AVD_HOME"] = cfg["avd_home"]

    # Parse resolution
    try:
        w, h = map(int, args.resolution.split("x"))
    except ValueError:
        print("Invalid resolution format. Use WxH, e.g., 1920x1080")
        sys.exit(1)

    cmd = [emulator, "-avd", avd_name]

    if args.headless:
        cmd += ["-no-window"]

    if args.no_accel:
        cmd += ["-no-accel"]
    else:
        cmd += ["-accel", "on"]

    cmd += [
        "-gpu", args.gpu,
        "-memory", str(args.ram),
        "-cores", str(args.cpus),
        "-skin", f"{w}x{h}",
        "-no-snapshot",
        "-no-boot-anim",
    ]

    if args.verbose:
        cmd += ["-verbose"]

    # Port forwarding for ADB
    cmd += ["-ports", f"{args.adb_port},{args.adb_port + 1}"]

    print(f"[chimera] Launching emulator...")
    print(f"[chimera] Command: {' '.join(cmd)}")
    print(f"[chimera] ADB:     adb connect localhost:{args.adb_port}")
    print(f"[chimera] Press Ctrl+C to stop.\n")

    proc = subprocess.Popen(cmd, env=env)

    def signal_handler(sig, frame):
        print("\n[chimera] Stopping emulator...")
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        proc.wait()
    except KeyboardInterrupt:
        signal_handler(None, None)

    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
