#!/usr/bin/env bash
# =============================================================================
# build-android-kernel.sh — Build a Hyper-V + Android-ready kernel in WSL2
#
# Run inside WSL2 Ubuntu 24.04:
#   bash scripts/build-android-kernel.sh [--out /path/to/output]
#
# Output files:
#   out/bzImage       — bootable kernel for HCS LinuxKernelDirect boot
#   out/initrd.img    — minimal initramfs with busybox + hid-bridge
#
# The kernel is based on Microsoft's WSL2-Linux-Kernel which already includes:
#   - drivers/hv/dxgkrnl  (GPU-PV guest driver — the key for GPU acceleration)
#   - CONFIG_HYPERV*       (VMBus, synthetic devices)
# We add:
#   - CONFIG_DRM_VIRTIO_GPU  (virtio-gpu DRM driver for cuttlefish)
#   - CONFIG_VIRTIO_INPUT    (virtio-input for tablet/keyboard/mouse)
#   - CONFIG_VSOCKETS        (AF_VSOCK — HvSocket guest side)
#   - CONFIG_VIRTIO_VSOCKETS (virtio-vsock transport)
# =============================================================================
set -euo pipefail

OUT_DIR="${1:-$(pwd)/out/kernel}"
KERNEL_REPO="https://github.com/microsoft/WSL2-Linux-Kernel.git"
KERNEL_BRANCH="linux-msft-wsl-6.6.y"
KERNEL_DIR="$(pwd)/build/wsl2-kernel"
JOBS=$(nproc)

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()     { echo -e "${RED}[FAIL]${NC} $*"; exit 1; }

# ── Step 0: Prerequisites ─────────────────────────────────────────────────
info "Installing build dependencies..."
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    build-essential flex bison libssl-dev libelf-dev bc dwarves \
    python3 python3-pip git curl cpio kmod

# ── Step 1: Clone kernel ──────────────────────────────────────────────────
if [[ ! -d "$KERNEL_DIR/.git" ]]; then
    info "Cloning WSL2 kernel (shallow, branch $KERNEL_BRANCH)..."
    git clone --depth=1 --branch "$KERNEL_BRANCH" "$KERNEL_REPO" "$KERNEL_DIR"
else
    info "Kernel tree already present at $KERNEL_DIR — skipping clone"
fi

cd "$KERNEL_DIR"

# ── Step 2: Base config ───────────────────────────────────────────────────
info "Configuring kernel..."
cp Microsoft/config-wsl arch/x86/configs/chimera_defconfig

# Append Android/cuttlefish + GPU-PV required options
cat >> arch/x86/configs/chimera_defconfig << 'EOF'

# ---------- Chimera HCS/Android additions ----------

# AF_VSOCK — HvSocket guest side (port ↔ GUID mapping)
CONFIG_VSOCKETS=y
CONFIG_VSOCKETS_LOOPBACK=y
CONFIG_VIRTIO_VSOCKETS=y
CONFIG_HYPERV_VSOCKETS=y

# virtio-gpu DRM (cuttlefish primary display driver)
CONFIG_DRM=y
CONFIG_DRM_VIRTIO_GPU=y

# virtio-input (keyboard/mouse/tablet from host HvSocket bridge)
CONFIG_VIRTIO_INPUT=y

# virtio-snd (audio — Phase 5b)
CONFIG_SND_VIRTIO=m

# virtio-net (network — Phase 5b)
CONFIG_VIRTIO_NET=y

# /dev/uinput (needed by HID bridge daemon to inject input events)
CONFIG_INPUT_UINPUT=y

# DRM framebuffer console (enables /dev/fb0 for display capture daemon)
CONFIG_DRM_FBDEV_EMULATION=y
CONFIG_FB=y
CONFIG_FRAMEBUFFER_CONSOLE=y

# Android Binder IPC (required by Android userspace)
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDERFS=y

# tmpfs / overlayfs (Android rootfs)
CONFIG_TMPFS=y
CONFIG_OVERLAY_FS=y

# squashfs (for Android system image mounting)
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_XZ=y

# ext4 (userdata partition)
CONFIG_EXT4_FS=y

# Disable unused debug/tracing to reduce size
CONFIG_DEBUG_INFO=n
CONFIG_DEBUG_KERNEL=n
EOF

make KCONFIG_CONFIG=arch/x86/configs/chimera_defconfig defconfig

# Ensure dxgkrnl (GPU-PV driver) is enabled — already in WSL2 config
if ! grep -q "CONFIG_DXGKRNL=y" .config; then
    warn "CONFIG_DXGKRNL not set — enabling..."
    echo "CONFIG_DXGKRNL=y" >> .config
    make olddefconfig
fi

info "Kernel config summary:"
grep -E "CONFIG_(DXGKRNL|VSOCKETS|VIRTIO|DRM_VIRTIO|HYPERV)" .config | grep "=y" | head -20

# ── Step 3: Build ─────────────────────────────────────────────────────────
info "Building kernel (${JOBS} jobs)..."
make -j"$JOBS" LOCALVERSION="-chimera-hcs"

# ── Step 4: Copy output ───────────────────────────────────────────────────
mkdir -p "$OUT_DIR"
cp arch/x86/boot/bzImage       "$OUT_DIR/bzImage"
cp System.map                  "$OUT_DIR/System.map"
cp .config                     "$OUT_DIR/kernel.config"
info "Kernel built: $OUT_DIR/bzImage"

# ── Step 5: Minimal initramfs ─────────────────────────────────────────────
# The initramfs mounts system/vendor/userdata VHDX partitions and exec's
# Android init. For a smoke-test this just drops to a busybox shell.
info "Building minimal initramfs..."
INITRD_WORK="$(pwd)/build/initrd_work"
rm -rf "$INITRD_WORK"
mkdir -p "$INITRD_WORK"/{bin,dev,proc,sys,sysroot}

# Install busybox (static) as /bin/sh
BUSYBOX_URL="https://www.busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox"
if ! command -v busybox &>/dev/null; then
    curl -fsSL "$BUSYBOX_URL" -o "$INITRD_WORK/bin/busybox"
    chmod +x "$INITRD_WORK/bin/busybox"
fi
for tool in sh mount pivot_root switch_root; do
    ln -sf busybox "$INITRD_WORK/bin/$tool" 2>/dev/null || true
done

# init script
cat > "$INITRD_WORK/init" << 'INIT_EOF'
#!/bin/sh
mount -t proc  proc  /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mdev -s

# Chimera smoke test: report kernel version over hvc0 console
echo "Chimera HCS kernel boot OK: $(uname -r)" > /dev/hvc0 2>/dev/null || true
echo "Chimera HCS kernel boot OK: $(uname -r)"

# In production: mount /dev/sda (system.vhdx) and exec Android init
# exec switch_root /sysroot /system/bin/init
exec sh
INIT_EOF
chmod +x "$INITRD_WORK/init"

# Pack initramfs
(cd "$INITRD_WORK" && find . | cpio -H newc -o | gzip) > "$OUT_DIR/initrd.img"
info "Initramfs: $OUT_DIR/initrd.img ($(du -sh "$OUT_DIR/initrd.img" | cut -f1))"

# ── Step 6: Update hcs.json ───────────────────────────────────────────────
# Convert WSL2 path to Windows path for hcs.json
WIN_OUT=$(wslpath -w "$OUT_DIR")
info "==================================================================="
info "Build complete!"
info ""
info "Update configs/hcs.json with these paths:"
info "  boot.kernel:  ${WIN_OUT}\\bzImage"
info "  boot.initrd:  ${WIN_OUT}\\initrd.img"
info "  boot.cmdline: console=hvc0 quiet"
info ""
info "Then run: chimera-ui.exe --hcs-backend"
info "==================================================================="
