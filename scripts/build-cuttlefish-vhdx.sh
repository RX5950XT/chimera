#!/usr/bin/env bash
# =============================================================================
# build-cuttlefish-vhdx.sh — Phase 5d: Download AOSP Cuttlefish x86_64 images
#                             and convert to VHDX for HCS GPU-PV guest.
#
# Run in WSL2 Ubuntu-24.04:
#   bash scripts/build-cuttlefish-vhdx.sh
#
# Outputs:
#   out/cuttlefish/system.vhdx   — Android system partition
#   out/cuttlefish/vendor.vhdx   — Android vendor partition
#   out/cuttlefish/userdata.vhdx — Android userdata partition (4 GiB)
#
# After this, run:
#   bash scripts/build-android-initrd.sh
#   python scripts/test-hcs-cuttlefish.py
# =============================================================================
set -euo pipefail

BUILD_ID="13281750"
TARGET="aosp_cf_x86_64_phone-trunk_staging-userdebug"
IMG_ZIP="aosp_cf_x86_64_phone-img-${BUILD_ID}.zip"
DOWNLOAD_URL="https://ci.android.com/builds/submitted/${BUILD_ID}/${TARGET}/latest/${IMG_ZIP}"

PROJ_ROOT="/mnt/d/Workspace_cloud/Personal_Project/chimera"
OUT_DIR="$PROJ_ROOT/out/cuttlefish"
EXTRACT_DIR="$OUT_DIR/extracted"
JOBS=$(nproc)

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }

mkdir -p "$OUT_DIR" "$EXTRACT_DIR"

# ── Step 1: Install tools ─────────────────────────────────────────────────────
info "Installing tools..."
sudo apt-get install -y --no-install-recommends \
    android-sdk-libsparse-utils \
    qemu-utils \
    unzip \
    curl \
    python3 \
    e2fsprogs \
    2>&1 | tail -3

# lpunpack — for Android dynamic partition (super.img) extraction
# Available in android-sdk-tools on newer Ubuntu
if ! command -v lpunpack &>/dev/null; then
    info "Installing lpunpack..."
    sudo apt-get install -y --no-install-recommends android-sdk-platform-tools 2>&1 | tail -3 || true
fi
# Fallback: download lpunpack binary from AOSP CI cvd-host_package
if ! command -v lpunpack &>/dev/null; then
    CVD_URL="https://ci.android.com/builds/submitted/${BUILD_ID}/${TARGET}/latest/cvd-host_package.tar.gz"
    CVD_TGZ="$OUT_DIR/cvd-host.tar.gz"
    if [[ ! -f "$CVD_TGZ" ]]; then
        info "Downloading cvd-host_package for lpunpack..."
        curl -fL "$CVD_URL" -o "$CVD_TGZ"
    fi
    mkdir -p "$OUT_DIR/cvd-host"
    tar -xf "$CVD_TGZ" -C "$OUT_DIR/cvd-host" --wildcards '*/lpunpack' 2>/dev/null || \
    tar -xf "$CVD_TGZ" -C "$OUT_DIR/cvd-host" bin/lpunpack 2>/dev/null || true
    LPUNPACK_BIN=$(find "$OUT_DIR/cvd-host" -name 'lpunpack' -type f 2>/dev/null | head -1)
    if [[ -n "$LPUNPACK_BIN" ]]; then
        chmod +x "$LPUNPACK_BIN"
        export PATH="$(dirname "$LPUNPACK_BIN"):$PATH"
        info "lpunpack found: $LPUNPACK_BIN"
    fi
fi

# ── Step 2: Download AOSP Cuttlefish img zip ──────────────────────────────────
if [[ ! -f "$OUT_DIR/$IMG_ZIP" ]]; then
    info "Fetching fresh signed GCS URL for AOSP Cuttlefish img zip (~1.1 GB)..."
    # ci.android.com serves HTML with pre-signed GCS URLs; Cache-Control: no-cache
    # forces a fresh response with a non-expired signed URL (valid ~5 minutes)
    VIEWER_URL="https://ci.android.com/builds/submitted/${BUILD_ID}/${TARGET}/latest/${IMG_ZIP}"
    SIGNED_URL=$(python3 - << 'PYEOF'
import urllib.request, ssl, re, time, sys
ctx = ssl.create_default_context()
url = sys.argv[1] if len(sys.argv) > 1 else ""
req = urllib.request.Request(url, headers={'Cache-Control': 'no-cache', 'Pragma': 'no-cache'})
resp = urllib.request.urlopen(req, timeout=30, context=ctx)
html = resp.read(30000).decode('utf-8', errors='replace')
now = int(time.time())
for m in re.findall(r'https://storage\.googleapis\.com[^"<>\s]+', html):
    clean = m.replace('\\u0026', '&').replace('\\/', '/')
    exp = re.search(r'Expires=(\d+)', clean)
    if exp and int(exp.group(1)) > now:
        print(clean)
        sys.exit(0)
sys.exit(1)
PYEOF
)
    if [[ -z "$SIGNED_URL" ]]; then
        echo "[ERROR] Could not get fresh signed URL — try again in a moment"
        exit 1
    fi
    info "Signed URL obtained ($(echo "$SIGNED_URL" | wc -c) chars)"
    info "Downloading..."
    curl -fL --progress-bar "$SIGNED_URL" -o "$OUT_DIR/$IMG_ZIP"
else
    info "Image zip already downloaded: $OUT_DIR/$IMG_ZIP"
fi

# ── Step 3: Extract images ────────────────────────────────────────────────────
info "Extracting img zip..."
unzip -o "$OUT_DIR/$IMG_ZIP" -d "$EXTRACT_DIR" 2>&1 | grep -E 'inflating|extracting|Archive' | tail -20
ls -lh "$EXTRACT_DIR/"*.img 2>/dev/null | head -20

# ── Step 4: Unpack super.img if present (dynamic partitions) ──────────────────
SUPER_IMG="$EXTRACT_DIR/super.img"
SYSTEM_IMG="$EXTRACT_DIR/system.img"
VENDOR_IMG="$EXTRACT_DIR/vendor.img"

if [[ -f "$SUPER_IMG" ]] && [[ ! -f "$SYSTEM_IMG" ]]; then
    info "Unpacking dynamic partitions from super.img..."
    mkdir -p "$EXTRACT_DIR/super-parts"

    # First desparse if needed
    if file "$SUPER_IMG" | grep -q "Android sparse"; then
        info "Desparsing super.img..."
        simg2img "$SUPER_IMG" "$EXTRACT_DIR/super_raw.img"
        SUPER_RAW="$EXTRACT_DIR/super_raw.img"
    else
        SUPER_RAW="$SUPER_IMG"
    fi

    if command -v lpunpack &>/dev/null; then
        lpunpack "$SUPER_RAW" "$EXTRACT_DIR/super-parts/"
        SYSTEM_IMG="$EXTRACT_DIR/super-parts/system_a.img"
        VENDOR_IMG="$EXTRACT_DIR/super-parts/vendor_a.img"
        [[ ! -f "$SYSTEM_IMG" ]] && SYSTEM_IMG="$EXTRACT_DIR/super-parts/system.img"
        [[ ! -f "$VENDOR_IMG" ]] && VENDOR_IMG="$EXTRACT_DIR/super-parts/vendor.img"
    else
        warn "lpunpack not found — trying to extract system from super.img offset"
        # Fallback: use qemu-img to detect partitions inside super.img
        qemu-img info "$SUPER_RAW" 2>&1 || true
        SYSTEM_IMG="$SUPER_RAW"  # boot from raw super (contains all partitions)
    fi
fi

# Desparse system/vendor images if they are Android sparse format
desparse_if_needed() {
    local img="$1"
    local out="$2"
    if file "$img" | grep -q "Android sparse"; then
        info "Desparsing $(basename "$img")..."
        simg2img "$img" "$out"
        echo "$out"
    else
        echo "$img"
    fi
}

SYSTEM_RAW=$(desparse_if_needed "$SYSTEM_IMG" "$EXTRACT_DIR/system_raw.img")
VENDOR_RAW=$(desparse_if_needed "$VENDOR_IMG" "$EXTRACT_DIR/vendor_raw.img")

# ── Step 5: Convert to VHDX ───────────────────────────────────────────────────
info "Converting system → system.vhdx..."
qemu-img convert -f raw -O vhdx -o subformat=dynamic "$SYSTEM_RAW" "$OUT_DIR/system.vhdx"
info "  system.vhdx: $(du -sh "$OUT_DIR/system.vhdx" | cut -f1)"

info "Converting vendor → vendor.vhdx..."
qemu-img convert -f raw -O vhdx -o subformat=dynamic "$VENDOR_RAW" "$OUT_DIR/vendor.vhdx"
info "  vendor.vhdx: $(du -sh "$OUT_DIR/vendor.vhdx" | cut -f1)"

# ── Step 6: Create userdata.vhdx (4 GiB ext4) ────────────────────────────────
USERDATA_IMG="$EXTRACT_DIR/userdata.img"
if [[ -f "$USERDATA_IMG" ]]; then
    info "Converting userdata → userdata.vhdx..."
    USERDATA_RAW=$(desparse_if_needed "$USERDATA_IMG" "$EXTRACT_DIR/userdata_raw.img")
    qemu-img convert -f raw -O vhdx -o subformat=dynamic "$USERDATA_RAW" "$OUT_DIR/userdata.vhdx"
else
    info "Creating blank userdata.vhdx (4 GiB ext4)..."
    USERDATA_RAW="$EXTRACT_DIR/userdata_blank.img"
    dd if=/dev/zero of="$USERDATA_RAW" bs=1M count=4096 status=progress
    mkfs.ext4 -L userdata -F "$USERDATA_RAW"
    qemu-img convert -f raw -O vhdx -o subformat=dynamic "$USERDATA_RAW" "$OUT_DIR/userdata.vhdx"
fi
info "  userdata.vhdx: $(du -sh "$OUT_DIR/userdata.vhdx" | cut -f1)"

# ── Step 7: Print results ─────────────────────────────────────────────────────
info "================================================================"
info "Phase 5d: VHDX conversion complete!"
info ""
info "Output files:"
ls -lh "$OUT_DIR/"*.vhdx
info ""
info "System partition type:"
file "$SYSTEM_RAW" || true
info ""
info "Next: run scripts/build-android-initrd.sh"
info "      then: python scripts/test-hcs-cuttlefish.py"
info "================================================================"
