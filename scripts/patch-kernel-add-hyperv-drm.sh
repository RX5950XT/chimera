#!/usr/bin/env bash
# Add CONFIG_DRM_HYPERV=y to already-built kernel config and do incremental rebuild
# Run AFTER rebuild-android-kernel-config.sh has completed once
set -euo pipefail

KERNEL_DIR="$HOME/build/wsl2-kernel"
OUT_DIR="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/android-kernel"
JOBS=$(nproc)

GREEN='\033[0;32m'; NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $*"; }

cd "$KERNEL_DIR"

# Verify DRM is already enabled (prerequisite for DRM_HYPERV)
grep "^CONFIG_DRM=y" .config || { echo "[FAIL] CONFIG_DRM=y not set — run full rebuild first"; exit 1; }

# Add DRM_HYPERV if not already present
if ! grep -q "^CONFIG_DRM_HYPERV=y" .config; then
    info "Adding CONFIG_DRM_HYPERV=y..."
    echo "CONFIG_DRM_HYPERV=y" >> .config
    make olddefconfig
else
    info "CONFIG_DRM_HYPERV=y already in .config"
fi

# Verify
grep "CONFIG_DRM_HYPERV" .config
grep "CONFIG_DXGKRNL" .config

# Incremental build (only hyperv_drm driver needs compiling)
info "Incremental rebuild (${JOBS} jobs)..."
make -j"$JOBS" LOCALVERSION="-chimera-hcs" 2>&1 | tail -10

# Update output
mkdir -p "$OUT_DIR"
cp arch/x86/boot/bzImage "$OUT_DIR/bzImage"
cp .config               "$OUT_DIR/kernel.config"

info "================================================================"
info "Kernel updated with hyperv_drm: $OUT_DIR/bzImage"
info "CONFIG_DRM_HYPERV=$(grep CONFIG_DRM_HYPERV .config)"
info "CONFIG_DXGKRNL=$(grep CONFIG_DXGKRNL .config)"
info "================================================================"
