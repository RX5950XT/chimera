#!/usr/bin/env bash
# =============================================================================
# build-initramfs.sh — Build initramfs with Chimera HvSocket relay daemons
#
# Packages:
#   - busybox (static) as the shell/init base
#   - chimera-input-relay (AF_VSOCK port 16 → /dev/uinput)
#   - chimera-display-relay (AF_VSOCK port 17 ← /dev/fb0)
#   - Azure kernel modules: vsock, hv_sock
#
# Outputs:
#   out/hyperv-kernel/initrd.img
#
# Run in WSL2 Ubuntu-24.04:
#   bash scripts/build-initramfs.sh
# =============================================================================
set -euo pipefail

PROJ_ROOT="/mnt/d/Workspace_cloud/Personal_Project/chimera"
OUT_DIR="$PROJ_ROOT/out/hyperv-kernel"
GUEST_SRC="$PROJ_ROOT/scripts/guest"
WORK="$PROJ_ROOT/build/initramfs_work"
MODULES_DEB="/tmp/linux-modules-6.11.0-1018-azure_6.11.0-1018.18~24.04.1_amd64.deb"
MODULES_DIR="/tmp/hv-modules"
KERNEL_VER="6.11.0-1018-azure"

GREEN='\033[0;32m'; NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $*"; }

mkdir -p "$OUT_DIR" "$WORK"

# ── Step 1: Install build tools ───────────────────────────────────────────
info "Installing musl-tools and build deps..."
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

# ── Step 3: Download modules deb if needed ────────────────────────────────
if [[ ! -f "$MODULES_DEB" ]]; then
    info "Downloading azure kernel modules deb..."
    (cd /tmp && apt-get download linux-modules-${KERNEL_VER} 2>&1 | tail -3)
fi

# Extract modules
if [[ ! -d "$MODULES_DIR/lib/modules" ]]; then
    info "Extracting kernel modules..."
    dpkg-deb -x "$MODULES_DEB" "$MODULES_DIR/"
fi

# ── Step 4: Build initramfs directory tree ────────────────────────────────
info "Building initramfs tree..."
INITRD_WORK="$PROJ_ROOT/build/initrd_final"
rm -rf "$INITRD_WORK"
mkdir -p "$INITRD_WORK"/{bin,sbin,dev,proc,sys,run,tmp,lib,lib/modules}

# Busybox static binary
BUSYBOX_URL="https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox"
BUSYBOX_BIN="$INITRD_WORK/bin/busybox"
if [[ ! -f "$BUSYBOX_BIN" ]]; then
    info "Downloading busybox static..."
    curl -fsSL "$BUSYBOX_URL" -o "$BUSYBOX_BIN"
    chmod +x "$BUSYBOX_BIN"
fi
# Symlink common tools
for tool in sh ash mount umount ls cat echo sleep modprobe insmod lsmod; do
    ln -sf busybox "$INITRD_WORK/bin/$tool" 2>/dev/null || true
done

# Relay daemons
cp "$WORK/chimera-input-relay"   "$INITRD_WORK/bin/"
cp "$WORK/chimera-display-relay" "$INITRD_WORK/bin/"
chmod +x "$INITRD_WORK/bin/chimera-input-relay" "$INITRD_WORK/bin/chimera-display-relay"

# Copy vsock + hv_sock modules (decompress .ko.zst)
install_module() {
    local name="$1"
    local src
    # Search base modules, extra modules, and system-installed modules (fallback)
    src=$(find "$MODULES_DIR" "$MODULES_DIR-extra" \
              /lib/modules/"$KERNEL_VER" \
              -name "${name}.ko.zst" 2>/dev/null | head -1)
    if [[ -z "$src" ]]; then
        echo "[WARN] Module not found: $name"
        return 0
    fi
    zstd -d "$src" -o "$INITRD_WORK/lib/modules/${name}.ko" --force 2>/dev/null || \
        cp "$src" "$INITRD_WORK/lib/modules/${name}.ko.zst"
    echo "  [+] $name"
}

info "Installing kernel modules..."
install_module vsock
install_module hv_sock
install_module vsock_loopback
# hyperv_drm: DRM driver for Hyper-V synthetic video (VideoMonitor HCS device).
# Azure kernel has CONFIG_DRM_FBDEV_EMULATION=y so loading this creates /dev/fb0.
install_module hyperv_drm

# uinput — try extra modules package
UINPUT_KO=$(find "$MODULES_DIR" -name "uinput.ko*" 2>/dev/null | head -1)
if [[ -z "$UINPUT_KO" ]]; then
    # Try modules-extra
    EXTRA_DEB="/tmp/linux-modules-extra-${KERNEL_VER}_*amd64.deb"
    if ! ls $EXTRA_DEB 2>/dev/null | head -1 | grep -q .; then
        info "Downloading linux-modules-extra for uinput..."
        (cd /tmp && apt-get download linux-modules-extra-${KERNEL_VER} 2>&1 | tail -3)
    fi
    EXTRA_DEB_FILE=$(ls /tmp/linux-modules-extra-${KERNEL_VER}*.deb 2>/dev/null | head -1)
    if [[ -n "$EXTRA_DEB_FILE" ]]; then
        dpkg-deb -x "$EXTRA_DEB_FILE" "$MODULES_DIR-extra/" 2>/dev/null || true
        UINPUT_KO=$(find "$MODULES_DIR-extra" -name "uinput.ko*" 2>/dev/null | head -1)
    fi
fi

if [[ -n "$UINPUT_KO" ]]; then
    if [[ "$UINPUT_KO" == *.zst ]]; then
        zstd -d "$UINPUT_KO" -o "$INITRD_WORK/lib/modules/uinput.ko" --force 2>/dev/null
    else
        cp "$UINPUT_KO" "$INITRD_WORK/lib/modules/uinput.ko"
    fi
    echo "  [+] uinput"
else
    echo "[WARN] uinput module not found — input relay may not inject events"
fi

# ── Step 5: init script ───────────────────────────────────────────────────
cat > "$INITRD_WORK/init" << 'INIT_EOF'
#!/bin/sh
mount -t proc  proc  /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts && mount -t devpts devpts /dev/pts 2>/dev/null || true

echo "Chimera HCS guest init starting..."
echo "Kernel: $(uname -r)"

# Load HvSocket modules (uinput is CONFIG_INPUT_UINPUT=y, built-in — no insmod needed)
for mod in vsock hv_sock hyperv_drm; do
    if [ -f "/lib/modules/${mod}.ko" ]; then
        insmod "/lib/modules/${mod}.ko" 2>/dev/null && echo "  [+] ${mod}" || echo "  [!] ${mod} failed"
    else
        modprobe "$mod" 2>/dev/null && echo "  [+] ${mod} (via modprobe)" || echo "  [-] ${mod} not found"
    fi
done
# Wait for /dev/fb0 to appear (hyperv_drm + VideoMonitor = /dev/fb0 via DRM fbdev emulation)
for i in $(seq 1 10); do
    [ -e /dev/fb0 ] && echo "  [+] /dev/fb0 ready" && break
    sleep 1
done
[ -e /dev/fb0 ] || echo "  [-] /dev/fb0 not found (display relay will skip fb0)"

# Start relay daemons
if [ -x /bin/chimera-input-relay ]; then
    /bin/chimera-input-relay &
    echo "Input relay started (PID $!)"
fi
if [ -x /bin/chimera-display-relay ]; then
    /bin/chimera-display-relay &
    echo "Display relay started (PID $!)"
fi

echo "Chimera HCS guest ready. VSOCK ports 16 (input) + 17 (display) active."

# Keep running
while true; do sleep 60; done
INIT_EOF
chmod +x "$INITRD_WORK/init"

# ── Step 6: Pack cpio initramfs ───────────────────────────────────────────
info "Packing initramfs..."
(cd "$INITRD_WORK" && find . | cpio -H newc -o | gzip -9) > "$OUT_DIR/initrd.img"
info "initrd.img: $(du -sh "$OUT_DIR/initrd.img" | cut -f1)"

# ── Step 7: Print summary ─────────────────────────────────────────────────
WIN_OUT=$(wslpath -w "$OUT_DIR")
info "================================================================"
info "Initramfs build complete!"
info ""
info "configs/hcs.json paths:"
info "  kernel: ${WIN_OUT}\\bzImage"
info "  initrd: ${WIN_OUT}\\initrd.img"
info "  cmdline: console=hvc0 quiet"
info ""
info "Run: chimera-ui.exe --hcs-backend"
info "Expected: HvSocket input/display connections succeed"
info "================================================================"
