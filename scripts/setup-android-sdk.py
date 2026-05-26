#!/usr/bin/env python3
"""
Setup Android SDK, Emulator, and System Image for Project Chimera.

Downloads the Android command line tools, then uses sdkmanager to fetch:
  - emulator (QEMU + WHPX)
  - platform-tools (adb, fastboot)
  - platforms;android-34
  - system-images;android-34;google_apis_playstore;x86_64

Then creates an AVD ready for Chimera to use.
"""

import os
import sys
import urllib.request
import zipfile
import subprocess
import shutil
import platform

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHIMERA_ROOT = os.path.dirname(SCRIPT_DIR)
SDK_DIR = os.path.join(CHIMERA_ROOT, "third_party", "android-sdk")
AVD_DIR = os.path.join(CHIMERA_ROOT, "third_party", "android-avd")

# Google command line tools (static URL, update periodically)
CMDTOOLS_URL = "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip"
CMDTOOLS_ZIP = os.path.join(SDK_DIR, "cmdtools.zip")

# Android API level and ABI
API_LEVEL = 34
ABI = "x86_64"
AVD_NAME = "chimera_dev"


def log(msg: str):
    print(f"[setup-sdk] {msg}")


def download(url: str, dest: str):
    if os.path.exists(dest):
        log(f"Already exists: {dest}")
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    log(f"Downloading {url} ...")
    try:
        urllib.request.urlretrieve(url, dest, reporthook=lambda b, bs, ts: (
            sys.stdout.write(f"\r  {b*bs//1024//1024} MB / {ts//1024//1024 if ts else '?'} MB")
            or sys.stdout.flush()
        ) if ts else None)
        sys.stdout.write("\n")
        log("Download complete.")
    except Exception as e:
        log(f"Download failed: {e}")
        sys.exit(1)


def ensure_cmdline_tools():
    """Ensure cmdline-tools/latest/bin/sdkmanager.bat exists."""
    cmdtools_latest = os.path.join(SDK_DIR, "cmdline-tools", "latest")
    sdkmanager = os.path.join(cmdtools_latest, "bin", "sdkmanager.bat")

    if os.path.exists(sdkmanager):
        log("sdkmanager found.")
        return sdkmanager

    # Download zip
    download(CMDTOOLS_URL, CMDTOOLS_ZIP)

    # Extract to temp dir first
    extract_tmp = os.path.join(SDK_DIR, "_cmdtools_extract")
    if os.path.exists(extract_tmp):
        shutil.rmtree(extract_tmp)
    os.makedirs(extract_tmp, exist_ok=True)

    log("Extracting command line tools...")
    with zipfile.ZipFile(CMDTOOLS_ZIP, "r") as z:
        z.extractall(extract_tmp)

    # Google zip extracts to cmdline-tools/ directly, but sdkmanager expects
    # cmdline-tools/latest/bin/. Move the extracted content accordingly.
    extracted_root = os.path.join(extract_tmp, "cmdline-tools")
    if not os.path.exists(extracted_root):
        log("Unexpected archive structure.")
        sys.exit(1)

    os.makedirs(cmdtools_latest, exist_ok=True)
    for item in os.listdir(extracted_root):
        src = os.path.join(extracted_root, item)
        dst = os.path.join(cmdtools_latest, item)
        if os.path.exists(dst):
            shutil.rmtree(dst) if os.path.isdir(dst) else os.remove(dst)
        shutil.move(src, dst)

    shutil.rmtree(extract_tmp)
    log(f"sdkmanager installed at {sdkmanager}")
    return sdkmanager


def run_sdkmanager(sdkmanager: str, args: list):
    """Run sdkmanager.bat with auto-accept licenses."""
    env = os.environ.copy()
    env["ANDROID_SDK_ROOT"] = SDK_DIR
    env["ANDROID_AVD_HOME"] = AVD_DIR

    cmd = [sdkmanager, "--sdk_root=" + SDK_DIR] + args
    log(f"> {' '.join(cmd)}")
    proc = subprocess.run(cmd, env=env, input="y\n" * 20, text=True)
    if proc.returncode != 0:
        log(f"sdkmanager exited with code {proc.returncode}")
        # Non-fatal: packages might already be installed


def main():
    log("Setting up Android SDK for Project Chimera...")
    os.makedirs(SDK_DIR, exist_ok=True)
    os.makedirs(AVD_DIR, exist_ok=True)

    sdkmanager = ensure_cmdline_tools()

    # Required packages
    packages = [
        "emulator",
        "platform-tools",
        f"platforms;android-{API_LEVEL}",
        f"system-images;android-{API_LEVEL};google_apis_playstore;{ABI}",
    ]

    log("Installing packages via sdkmanager (this may take a while)...")
    run_sdkmanager(sdkmanager, packages)

    # Find emulator binary
    emulator_exe = os.path.join(SDK_DIR, "emulator", "emulator.exe")
    if not os.path.exists(emulator_exe):
        log("WARNING: emulator.exe not found after sdkmanager. Checking alternate path...")
        alt = os.path.join(SDK_DIR, "emulator", "emulator.exe")
        if os.path.exists(alt):
            emulator_exe = alt
        else:
            log("ERROR: Could not locate emulator.exe")
            sys.exit(1)

    log(f"Emulator: {emulator_exe}")

    # Create AVD
    avdmanager = os.path.join(SDK_DIR, "cmdline-tools", "latest", "bin", "avdmanager.bat")
    if os.path.exists(avdmanager):
        log(f"Creating AVD '{AVD_NAME}'...")
        env = os.environ.copy()
        env["ANDROID_SDK_ROOT"] = SDK_DIR
        env["ANDROID_AVD_HOME"] = AVD_DIR
        subprocess.run(
            [avdmanager, "create", "avd", "--name", AVD_NAME,
             "--package", f"system-images;android-{API_LEVEL};google_apis_playstore;{ABI}",
             "--device", "pixel_5", "--force"],
            env=env, input="\n", text=True
        )
    else:
        log("avdmanager not found, skipping AVD creation.")

    # Write runtime config for Chimera
    config_path = os.path.join(CHIMERA_ROOT, "configs", "android_sdk.json")
    import json
    with open(config_path, "w") as f:
        json.dump({
            "sdk_root": SDK_DIR.replace("\\", "/"),
            "avd_home": AVD_DIR.replace("\\", "/"),
            "emulator": emulator_exe.replace("\\", "/"),
            "adb": os.path.join(SDK_DIR, "platform-tools", "adb.exe").replace("\\", "/"),
            "api_level": API_LEVEL,
            "abi": ABI,
            "avd_name": AVD_NAME
        }, f, indent=2)
    log(f"Config written to {config_path}")
    log("Setup complete!")


if __name__ == "__main__":
    main()
