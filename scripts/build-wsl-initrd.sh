#!/usr/bin/env bash
# =============================================================================
# build-wsl-initrd.sh — Build minimal initramfs for WSL2 6.6.y kernel
#
# hv_sock (CONFIG_HYPERV_VSOCKETS=m) and hyperv_drm (CONFIG_DRM_HYPERV=m)
# are built as modules and loaded via insmod after VMBus channels are offered.
# vsock core (CONFIG_VSOCKETS=y) and uinput (CONFIG_INPUT_UINPUT=y) are built-in.
#
# Requires: out/android-kernel/ko/hv_sock.ko + hyperv_drm.ko (from patch-kernel-vsock-drm-modules.sh)
#
# Run in WSL2 Ubuntu-24.04:
#   bash scripts/build-wsl-initrd.sh
# =============================================================================
set -euo pipefail

PROJ_ROOT="/mnt/d/Workspace_cloud/Personal_Project/chimera"
OUT_DIR="$PROJ_ROOT/out/android-kernel"
GUEST_SRC="$PROJ_ROOT/scripts/guest"
WORK="/tmp/chimera_wsl_initrd_work"
INITRD_WORK="/tmp/chimera_wsl_initrd_final"

GREEN='\033[0;32m'; NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $*"; }

mkdir -p "$WORK" "$OUT_DIR"

# ── Step 1: Install build tools ───────────────────────────────────────────
info "Installing build deps..."
sudo apt-get install -y --no-install-recommends musl-tools gcc make cpio gzip 2>&1 | tail -3

# ── Step 2: Compile relay daemons (static musl) ───────────────────────────
KINCLUDE="-idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu"

info "Compiling chimera-input-relay..."
musl-gcc -static -O2 $KINCLUDE \
    -o "$WORK/chimera-input-relay" \
    "$GUEST_SRC/chimera-input-relay.c"

info "Compiling chimera-display-relay..."
musl-gcc -static -O2 $KINCLUDE \
    -o "$WORK/chimera-display-relay" \
    "$GUEST_SRC/chimera-display-relay.c"

info "Binary sizes:"
ls -lh "$WORK/chimera-input-relay" "$WORK/chimera-display-relay"

# ── Step 3: Build initramfs directory tree ────────────────────────────────
info "Building initramfs tree..."
rm -rf "$INITRD_WORK"
mkdir -p "$INITRD_WORK"/{bin,sbin,dev,proc,sys,run,tmp,lib/modules}

# Kernel modules (hv_sock + hyperv_drm built as =m)
KO_DIR="$OUT_DIR/ko"
for ko in hv_sock.ko hyperv_drm.ko; do
    if [[ -f "$KO_DIR/$ko" ]]; then
        cp "$KO_DIR/$ko" "$INITRD_WORK/lib/modules/$ko"
        info "Included module: $ko ($(du -sh "$KO_DIR/$ko" | cut -f1))"
    else
        info "WARNING: $KO_DIR/$ko not found — run patch-kernel-vsock-drm-modules.sh first"
    fi
done

# Busybox static binary
BUSYBOX_URL="https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox"
BUSYBOX_BIN="$INITRD_WORK/bin/busybox"
if [[ ! -f "$BUSYBOX_BIN" ]]; then
    info "Downloading busybox static..."
    curl -fsSL "$BUSYBOX_URL" -o "$BUSYBOX_BIN"
    chmod +x "$BUSYBOX_BIN"
fi
for tool in sh ash mount umount ls cat echo sleep modprobe insmod lsmod mkdir seq uname dmesg; do
    ln -sf busybox "$INITRD_WORK/bin/$tool" 2>/dev/null || true
done

# Relay daemons
cp "$WORK/chimera-input-relay"   "$INITRD_WORK/bin/"
cp "$WORK/chimera-display-relay" "$INITRD_WORK/bin/"
chmod +x "$INITRD_WORK/bin/chimera-input-relay" "$INITRD_WORK/bin/chimera-display-relay"

# ── Step 4: init script ───────────────────────────────────────────────────
# hv_sock and hyperv_drm are =m; load them after VMBus channels are offered.
# vsock core and uinput are built-in (=y).
cat > "$INITRD_WORK/init" << 'INIT_EOF'
#!/bin/sh
mount -t proc  proc  /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts && mount -t devpts devpts /dev/pts 2>/dev/null || true

echo "[chimera] WSL2 6.6 kernel guest init starting..."

# Load hv_sock module (HYPERV_VSOCKETS=m); vsock core is already built-in
echo "[chimera] Loading hv_sock module..."
insmod /lib/modules/hv_sock.ko 2>/dev/null && echo "[chimera] hv_sock loaded" || echo "[chimera] hv_sock load FAILED"

# Wait briefly for VMBus to offer the VideoMonitor channel, then load hyperv_drm
sleep 2
echo "[chimera] Loading hyperv_drm module..."
insmod /lib/modules/hyperv_drm.ko 2>/dev/null && echo "[chimera] hyperv_drm loaded" || echo "[chimera] hyperv_drm load FAILED"

# Wait for /dev/fb0
i=0
while [ $i -lt 15 ]; do
    [ -e /dev/fb0 ] && echo "[chimera] /dev/fb0 ready" && break
    sleep 1
    i=$((i+1))
done
[ -e /dev/fb0 ] || echo "[chimera] /dev/fb0 not found"

# /dev/uinput is built-in; confirm
[ -e /dev/uinput ] && echo "[chimera] /dev/uinput ready" || echo "[chimera] /dev/uinput not found"

# Start display relay (reads /dev/fb0, streams via vsock port 17)
if [ -x /bin/chimera-display-relay ]; then
    /bin/chimera-display-relay &
    echo "[chimera] Display relay started (PID $!)"
fi

# Start input relay (vsock port 16 -> /dev/uinput)
if [ -x /bin/chimera-input-relay ]; then
    /bin/chimera-input-relay &
    echo "[chimera] Input relay started (PID $!)"
fi

echo "[chimera] WSL2 guest ready. VSOCK ports 16 (input) + 17 (display) active."

while true; do sleep 60; done
INIT_EOF
chmod +x "$INITRD_WORK/init"

# ── Step 5: Pack cpio initramfs ───────────────────────────────────────────
info "Packing initramfs..."
(cd "$INITRD_WORK" && find . | cpio -H newc -o | gzip -9) > "$OUT_DIR/initrd.img"
info "initrd.img: $(du -sh "$OUT_DIR/initrd.img" | cut -f1)"
info "================================================================"
info "WSL2-compatible initrd build complete!"
info "kernel: out/android-kernel/bzImage"
info "initrd: out/android-kernel/initrd.img"
info "================================================================"
