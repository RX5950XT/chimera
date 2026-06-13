param(
    [string]$Distro = "Ubuntu-24.04",
    [string]$QemuDir = "src\virtualization\qemu",
    [string]$OutDir = "build\chimera-emugl-runtime",
    [string]$AospPrebuiltsDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$qemuPath = Resolve-Path -LiteralPath (Join-Path $repoRoot $QemuDir)
$outPath = Join-Path $repoRoot $OutDir

function Convert-ToWslPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^([A-Za-z]):\\(.*)$') {
        throw "Only drive-letter paths are supported: $full"
    }
    $drive = $Matches[1].ToLowerInvariant()
    $rest = $Matches[2] -replace '\\', '/'
    return "/mnt/$drive/$rest"
}

$wslQemu = Convert-ToWslPath $qemuPath.ProviderPath
$wslOut = Convert-ToWslPath $outPath
$wslPrebuilts = ""
if (![string]::IsNullOrWhiteSpace($AospPrebuiltsDir)) {
    $prebuiltsPath = Resolve-Path -LiteralPath $AospPrebuiltsDir
    $wslPrebuilts = Convert-ToWslPath $prebuiltsPath.ProviderPath
}

if ([string]::IsNullOrWhiteSpace($wslQemu) -or [string]::IsNullOrWhiteSpace($wslOut)) {
    throw "Failed to translate paths for WSL distro $Distro"
}

$script = @'
set -euo pipefail

QEMU_SRC="$1"
OUT_DIR="$2"
AOSP_PREBUILTS="${3:-}"
WORK_DIR="/tmp/chimera-qemu-emugl-build"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  echo "missing x86_64-w64-mingw32-gcc; install mingw-w64 in WSL" >&2
  exit 2
fi

CONFIGURE_FLAGS=()
USE_SYSTEM_MINGW=0
if [ -z "$AOSP_PREBUILTS" ]; then
  AOSP_PREBUILTS="$(cd "$QEMU_SRC" && cd ../.. && pwd)/prebuilts"
fi
if [ -d "$AOSP_PREBUILTS/gcc" ] || [ -d "$AOSP_PREBUILTS/clang" ]; then
  CONFIGURE_FLAGS+=(--aosp-prebuilts-dir="$AOSP_PREBUILTS")
else
  echo "AOSP prebuilts not found under $AOSP_PREBUILTS; using system gcc/mingw and disabling pc-bios copy" >&2
  CONFIGURE_FLAGS+=(--cc=gcc --no-pcbios)
  USE_SYSTEM_MINGW=1
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
rsync -a --delete --exclude 'objs*' "$QEMU_SRC"/ "$WORK_DIR"/
cd "$WORK_DIR"
grep -Il $'\r' -r . | xargs -r sed -i 's/\r$//'

if [ "$USE_SYSTEM_MINGW" = "1" ]; then
  python3 - <<'PY'
from pathlib import Path

configure = Path("android-configure.sh")
text = configure.read_text()
needle = "    HOST_OS=windows\n    HOST_TAG=$HOST_OS-$HOST_ARCH\n"
replacement = """    HOST_OS=windows
    HOST_ARCH=x86_64
    BUILD_ARCH=x86_64
    HOST_TAG=$HOST_OS-$HOST_ARCH
    FORCE_32BIT=no
    CFLAGS=$(printf %s "$CFLAGS" | sed -e 's#-m32##g' -e 's#-Wa,--32##g')
    LDFLAGS=$(printf %s "$LDFLAGS" | sed -e 's#-m32##g')
"""
if needle not in text:
    raise SystemExit("android-configure.sh MinGW host block changed; cannot apply Chimera 64-bit patch")
text = text.replace(needle, replacement, 1)
configure.write_text(text)

makefile = Path("Makefile.android")
text = makefile.read_text()
needle = "EMULATOR_BUILD_32BITS := $(strip $(filter windows linux,$(HOST_OS)))"
replacement = "EMULATOR_BUILD_32BITS := $(strip $(filter linux,$(HOST_OS)))"
if needle not in text:
    raise SystemExit("Makefile.android 32-bit build rule changed; cannot apply Chimera 64-bit patch")
text = text.replace(needle, replacement, 1)
text = text.replace("MY_CFLAGS := -g -falign-functions=0",
                    "MY_CFLAGS := -g -falign-functions=0 -fcommon", 1)
text = text.replace("include $(LOCAL_PATH)/Makefile.tests",
                    "# Chimera: tests skipped for custom runtime build", 1)
makefile.write_text(text)

emugl = Path("distrib/android-emugl/Android.mk")
text = emugl.read_text()
needle = "# Required by our units test.\ninclude $(EMUGL_PATH)/googletest.mk\n\n"
if needle not in text:
    raise SystemExit("android-emugl Android.mk googletest include changed; cannot apply Chimera no-tests patch")
emugl.write_text(text.replace(needle, "", 1))

emugen = Path("distrib/android-emugl/host/tools/emugen/Android.mk")
text = emugen.read_text()
needle = """
$(call emugl-begin-host-executable,emugen_unittests)
LOCAL_SRC_FILES := \\
    Parser.cpp \\
    Parser_unittest.cpp
LOCAL_HOST_BUILD := true
$(call emugl-import,libemugl_gtest_host)
$(call emugl-end-module)
"""
if needle not in text:
    raise SystemExit("emugen Android.mk unittest block changed; cannot apply Chimera no-tests patch")
emugen.write_text(text.replace(needle, "", 1))

common = Path("distrib/android-emugl/shared/emugl/common/Android.mk")
text = common.read_text()
start = text.find("### emugl_common_unittests")
if start == -1:
    raise SystemExit("shared emugl common unittest block changed; cannot apply Chimera no-tests patch")
common.write_text(text[:start].rstrip() + "\n")

osdep = Path("include/qemu/osdep.h")
text = osdep.read_text()
needle = """#ifndef always_inline
#if !((__GNUC__ < 3) || defined(__APPLE__))
#ifdef __OPTIMIZE__
#undef inline
#define inline __attribute__ (( always_inline )) __inline__
#endif
#endif
#else
#undef inline
#define inline always_inline
#endif
"""
replacement = """#ifndef __cplusplus
#ifndef always_inline
#if !((__GNUC__ < 3) || defined(__APPLE__))
#ifdef __OPTIMIZE__
#undef inline
#define inline __attribute__ (( always_inline )) __inline__
#endif
#endif
#else
#undef inline
#define inline always_inline
#endif
#endif
"""
if needle not in text:
    raise SystemExit("qemu osdep inline block changed; cannot apply C++ compatibility patch")
osdep.write_text(text.replace(needle, replacement, 1))

gen_hw = Path("android/tools/gen-hw-config.py")
text = gen_hw.read_text().replace('open(targetFile,"wb")', 'open(targetFile,"w")', 1)
gen_hw.write_text(text)
PY

  python3 -m lib2to3 -w \
    scripts/qapi-types.py \
    scripts/qapi-visit.py \
    scripts/qapi-commands.py \
    scripts/qapi.py \
    scripts/ordereddict.py \
    android/tools/gen-hw-config.py \
    android/scripts/gen-entries.py >/dev/null
fi

echo "Configuring Chimera EmuGL runtime."
bash ./android-configure.sh --mingw --no-tests --out-dir=objs-chimera-emugl \
  "${CONFIGURE_FLAGS[@]}"

if [ "$USE_SYSTEM_MINGW" = "1" ]; then
  python3 - <<'PY'
from pathlib import Path
config = Path("objs-chimera-emugl/config.make")
text = config.read_text()
text = text.replace("HOST_CXX    := /usr/bin/x86_64-w64-mingw32-gcc",
                    "HOST_CXX    := /usr/bin/x86_64-w64-mingw32-g++")
config.write_text(text)
PY
fi

echo "Building Chimera classic x86 emulator runtime."
make -j"$(nproc)" OBJS_DIR=objs-chimera-emugl \
  objs-chimera-emugl/emulator64-x86.exe

echo "Building 64-bit EmuGL shared texture runtime DLLs."
make -j"$(nproc)" OBJS_DIR=objs-chimera-emugl \
  objs-chimera-emugl/lib64/lib64OpenglRender.dll \
  objs-chimera-emugl/lib64/lib64EGL_translator.dll \
  objs-chimera-emugl/lib64/lib64GLES_CM_translator.dll \
  objs-chimera-emugl/lib64/lib64GLES_V2_translator.dll

mkdir -p "$OUT_DIR/lib64"
cp -a objs-chimera-emugl/emulator64-x86.exe "$OUT_DIR"/
cp -a objs-chimera-emugl/emulator64-x86.exe "$OUT_DIR/emulator.exe"
for dll in libstdc++-6.dll libgcc_s_seh-1.dll; do
  found="$(x86_64-w64-mingw32-g++ -print-file-name="$dll")"
  if [ -z "$found" ] || [ "$found" = "$dll" ] || [ ! -f "$found" ]; then
    echo "required MinGW runtime DLL not found: $dll" >&2
    exit 4
  fi
  cp -a "$found" "$OUT_DIR/"
done
if [ -f /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll ]; then
  cp -a /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll "$OUT_DIR/"
fi
for dll in lib64OpenglRender.dll lib64EGL_translator.dll lib64GLES_CM_translator.dll lib64GLES_V2_translator.dll; do
  found="$(find objs-chimera-emugl -name "$dll" -print -quit)"
  if [ -z "$found" ]; then
    echo "build completed but required runtime DLL was not produced: $dll" >&2
    exit 4
  fi
  cp -a "$found" "$OUT_DIR/lib64/"
done
if [ -d objs-chimera-emugl/lib64/gles_mesa ]; then
  cp -a objs-chimera-emugl/lib64/gles_mesa "$OUT_DIR/lib64/"
fi
if [ -d objs-chimera-emugl/qemu ]; then
  cp -a objs-chimera-emugl/qemu "$OUT_DIR/"
fi
'@

New-Item -ItemType Directory -Force -Path $outPath | Out-Null
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script))
wsl -d $Distro -- bash -lc "echo $encoded | base64 -d > /tmp/chimera-build-emugl-runtime.sh && bash /tmp/chimera-build-emugl-runtime.sh '$wslQemu' '$wslOut' '$wslPrebuilts'"
$wslExit = $LASTEXITCODE
if ($wslExit -ne 0) {
    exit $wslExit
}

if (Test-Path -LiteralPath (Join-Path $outPath "emulator.exe")) {
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "write-chimera-emugl-runtime-manifest.ps1") `
        -RuntimeDir $outPath
    $manifestExit = $LASTEXITCODE
    if ($manifestExit -ne 0) {
        exit $manifestExit
    }
} else {
    Write-Host "Built Chimera EmuGL DLLs, but emulator.exe is absent; runtime manifest skipped."
}
