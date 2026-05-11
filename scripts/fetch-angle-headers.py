#!/usr/bin/env python3
"""Download ANGLE headers from Google GitHub repository."""
import urllib.request
import os
import sys

BASE_URL = "https://raw.githubusercontent.com/google/angle/main"
HEADERS = [
    # EGL
    ("include/EGL/egl.h", "EGL/egl.h"),
    ("include/EGL/eglext.h", "EGL/eglext.h"),
    ("include/EGL/eglplatform.h", "EGL/eglplatform.h"),
    # GLES
    ("include/GLES3/gl3.h", "GLES3/gl3.h"),
    ("include/GLES3/gl3ext.h", "GLES3/gl3ext.h"),
    ("include/GLES2/gl2.h", "GLES2/gl2.h"),
    ("include/GLES2/gl2ext.h", "GLES2/gl2ext.h"),
    ("include/GLES2/gl2platform.h", "GLES2/gl2platform.h"),
    ("include/GLES3/gl3platform.h", "GLES3/gl3platform.h"),
    # KHR
    ("include/KHR/khrplatform.h", "KHR/khrplatform.h"),
]

def download():
    root = os.path.join(os.path.dirname(__file__), "..", "third_party", "angle")
    root = os.path.abspath(root)
    os.makedirs(root, exist_ok=True)

    ok = True
    for remote, local in HEADERS:
        url = f"{BASE_URL}/{remote}"
        path = os.path.join(root, local)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        try:
            print(f"Downloading {local} ...")
            urllib.request.urlretrieve(url, path)
        except Exception as e:
            print(f"FAILED {local}: {e}")
            ok = False

    if ok:
        print(f"ANGLE headers downloaded to {root}")
    else:
        print("Some downloads failed.")
        sys.exit(1)

if __name__ == "__main__":
    download()
