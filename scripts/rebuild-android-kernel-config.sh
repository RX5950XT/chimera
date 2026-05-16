#!/usr/bin/env bash
# Fix KCONFIG_CONFIG bug and rebuild with correct Hyper-V + Android config
set -euo pipefail

KERNEL_DIR="$HOME/build/wsl2-kernel"
OUT_DIR="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/android-kernel"
JOBS=$(nproc)

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }

cd "$KERNEL_DIR"

# Step 1: Apply correct config — start from WSL2 config-wsl into .config
info "Applying WSL2 base config + Chimera Android additions..."
cp Microsoft/config-wsl .config

# Append Android/Hyper-V additions directly to .config
cat >> .config << 'EOF'

# ---------- Chimera HCS/Android additions ----------

# AF_VSOCK — HvSocket guest side (port <-> GUID mapping)
CONFIG_VSOCKETS=y
CONFIG_VSOCKETS_LOOPBACK=y
CONFIG_VIRTIO_VSOCKETS=y
CONFIG_HYPERV_VSOCKETS=y

# virtio-gpu DRM (cuttlefish primary display driver)
CONFIG_DRM=y
CONFIG_DRM_VIRTIO_GPU=y

# virtio-input (keyboard/mouse/tablet from host HvSocket bridge)
CONFIG_VIRTIO_INPUT=y

# virtio-net (network)
CONFIG_VIRTIO_NET=y

# /dev/uinput (inject input events via uinput virtual device)
CONFIG_INPUT_UINPUT=y

# DRM framebuffer console (enables /dev/fb0 for display capture daemon)
CONFIG_DRM_FBDEV_EMULATION=y
CONFIG_FB=y
CONFIG_FRAMEBUFFER_CONSOLE=y

# Hyper-V synthetic display (hyperv_drm) — creates /dev/fb0 via DRM fbdev emulation
# Requires VideoMonitor device in HCS JSON to get the synthetic video adapter
CONFIG_DRM_HYPERV=y

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
EOF

# Step 2: Resolve any dependency conflicts
info "Running olddefconfig to resolve dependencies..."
make olddefconfig

# Step 3: Verify key options are present
info "Verifying key config options:"
for opt in CONFIG_HYPERVISOR_GUEST CONFIG_HYPERV CONFIG_VSOCKETS CONFIG_HYPERV_VSOCKETS CONFIG_INPUT_UINPUT CONFIG_ANDROID_BINDER_IPC; do
    val=$(grep "^${opt}" .config 2>/dev/null || echo "${opt} NOT FOUND")
    echo "  $val"
done

# Step 4: Rebuild kernel
info "Building kernel (${JOBS} jobs)..."
make -j"$JOBS" LOCALVERSION="-chimera-hcs" 2>&1 | tail -5

# Step 5: Copy output
mkdir -p "$OUT_DIR"
cp arch/x86/boot/bzImage "$OUT_DIR/bzImage"
cp System.map            "$OUT_DIR/System.map"
cp .config               "$OUT_DIR/kernel.config"

info "================================================================"
info "Kernel rebuilt: $OUT_DIR/bzImage ($(du -sh $OUT_DIR/bzImage | cut -f1))"
info "Config: $OUT_DIR/kernel.config"
info "================================================================"
