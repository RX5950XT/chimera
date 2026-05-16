#!/usr/bin/env bash
# Convert hv_sock + hyperv_drm from built-in (=y) to modules (=m)
# so they can be loaded from initrd after VMBus channels are fully offered.
# Run after rebuild-android-kernel-config.sh has completed.
set -euo pipefail

KERNEL_DIR="$HOME/build/wsl2-kernel"
OUT_DIR="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/android-kernel"
MODS_DIR="$OUT_DIR/modules"
JOBS=$(nproc)

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }

cd "$KERNEL_DIR"

# Verify base config is present
grep "^CONFIG_VSOCKETS=y" .config || { echo "[FAIL] Run rebuild-android-kernel-config.sh first"; exit 1; }

info "Switching hv_sock + hyperv_drm to loadable modules..."
# Replace =y with =m for these two drivers; vsock core stays =y (built-in, needed at boot)
sed -i 's/^CONFIG_HYPERV_VSOCKETS=y$/CONFIG_HYPERV_VSOCKETS=m/' .config
sed -i 's/^CONFIG_DRM_HYPERV=y$/CONFIG_DRM_HYPERV=m/'           .config

make olddefconfig

info "Verifying config:"
grep "CONFIG_VSOCKETS\|CONFIG_HYPERV_VSOCKETS\|CONFIG_DRM_HYPERV\|CONFIG_DRM=" .config

info "Incremental kernel rebuild ($JOBS jobs)..."
# Full 'make modules' fails on rbd.ko BTF generation; build bzImage + specific modules instead
make -j"$JOBS" LOCALVERSION="-chimera-hcs" bzImage 2>&1 | tail -5

info "Building hv_sock.ko + hyperv_drm.ko modules..."
make -j"$JOBS" LOCALVERSION="-chimera-hcs" net/vmw_vsock/hv_sock.ko 2>&1 | tail -5
make -j"$JOBS" LOCALVERSION="-chimera-hcs" drivers/gpu/drm/hyperv/hyperv_drm.ko 2>&1 | tail -5

info "Copying output..."
mkdir -p "$OUT_DIR"
cp arch/x86/boot/bzImage "$OUT_DIR/bzImage"
cp .config               "$OUT_DIR/kernel.config"

# Copy built modules and bzImage to output
mkdir -p "$OUT_DIR/ko"
cp arch/x86/boot/bzImage              "$OUT_DIR/bzImage"
cp .config                            "$OUT_DIR/kernel.config"
cp net/vmw_vsock/hv_sock.ko           "$OUT_DIR/ko/hv_sock.ko"
cp drivers/gpu/drm/hyperv/hyperv_drm.ko "$OUT_DIR/ko/hyperv_drm.ko"

info "Module paths:"
echo "  hv_sock    : $OUT_DIR/ko/hv_sock.ko"
echo "  hyperv_drm : $OUT_DIR/ko/hyperv_drm.ko"

info "================================================================"
info "Module build complete!"
info "Next: run build-wsl-initrd.sh to include these modules in initrd"
info "================================================================"
