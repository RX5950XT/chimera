#!/usr/bin/env python3
"""
Top-level build script for Project Chimera.

Usage:
    python scripts/build.py [--release|--debug] [--target <name>]
"""

import os
import sys
import subprocess
import argparse
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CHIMERA_ROOT = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(CHIMERA_ROOT, "build")


def run(cmd, cwd=None):
    print(f"> {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        print(f"Command failed with code {result.returncode}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Build Project Chimera")
    parser.add_argument("--release", action="store_true", help="Release build")
    parser.add_argument("--debug", action="store_true", help="Debug build")
    parser.add_argument("--target", default=None, help="Specific CMake target")
    parser.add_argument("--clean", action="store_true", help="Clean build directory")
    args = parser.parse_args()

    build_type = "Release" if args.release else ("Debug" if args.debug else "RelWithDebInfo")

    if args.clean and os.path.exists(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
        print("Cleaned build directory.")

    os.makedirs(BUILD_DIR, exist_ok=True)

    # Configure
    cmake_cmd = [
        "cmake",
        "-S", CHIMERA_ROOT,
        "-B", BUILD_DIR,
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-G", "Ninja",
    ]
    run(cmake_cmd)

    # Build
    build_cmd = ["cmake", "--build", BUILD_DIR, "-j"]
    if args.target:
        build_cmd.extend(["--target", args.target])
    run(build_cmd)

    print(f"\nBuild complete: {BUILD_DIR}")


if __name__ == "__main__":
    main()
