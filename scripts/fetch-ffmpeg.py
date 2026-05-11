#!/usr/bin/env python3
"""
Fetch FFmpeg Windows build for Chimera screen recording.

Downloads the latest release build from BtbN/FFmpeg-Builds and extracts
ffmpeg.exe to third_party/ffmpeg/ for bundling with the installer.

Usage:
    python scripts/fetch-ffmpeg.py
"""

import os
import sys
import urllib.request
import zipfile
import shutil

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(PROJECT_ROOT, "third_party", "ffmpeg")

# BtbN FFmpeg Windows builds (GPL-enabled, full feature set)
FFMPEG_URL = (
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
    "ffmpeg-master-latest-win64-gpl.zip"
)

ZIP_PATH = os.path.join(PROJECT_ROOT, "third_party", "ffmpeg-download.zip")


def download_file(url: str, dest: str):
    print(f"Downloading FFmpeg from {url} ...")
    print("(This is ~150 MB, may take a few minutes)")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=300) as response:
        total = int(response.headers.get("content-length", 0))
        downloaded = 0
        block_size = 1024 * 1024
        with open(dest, "wb") as f:
            while True:
                chunk = response.read(block_size)
                if not chunk:
                    break
                f.write(chunk)
                downloaded += len(chunk)
                if total > 0:
                    pct = downloaded * 100 // total
                    print(f"  {pct}% ({downloaded // (1024*1024)} MB / {total // (1024*1024)} MB)", end="\r")
    print(f"\nSaved to {dest}")


def extract_ffmpeg(zip_path: str, output_dir: str):
    print(f"Extracting ffmpeg.exe to {output_dir} ...")
    os.makedirs(output_dir, exist_ok=True)

    with zipfile.ZipFile(zip_path, "r") as zf:
        # The zip contains a folder like "ffmpeg-master-latest-win64-gpl/"
        # and inside that, "bin/ffmpeg.exe"
        for name in zf.namelist():
            if name.endswith("/bin/ffmpeg.exe"):
                # Extract just ffmpeg.exe
                data = zf.read(name)
                out_path = os.path.join(output_dir, "ffmpeg.exe")
                with open(out_path, "wb") as f:
                    f.write(data)
                print(f"Extracted: {out_path}")
                return True

    print("ERROR: ffmpeg.exe not found in archive")
    return False


def main():
    if os.path.exists(os.path.join(OUTPUT_DIR, "ffmpeg.exe")):
        print(f"FFmpeg already exists at {OUTPUT_DIR}")
        return 0

    try:
        download_file(FFMPEG_URL, ZIP_PATH)
        if extract_ffmpeg(ZIP_PATH, OUTPUT_DIR):
            print("FFmpeg fetched successfully!")
        else:
            return 1
    except Exception as e:
        print(f"ERROR: {e}")
        return 1
    finally:
        if os.path.exists(ZIP_PATH):
            os.remove(ZIP_PATH)
            print(f"Cleaned up {ZIP_PATH}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
