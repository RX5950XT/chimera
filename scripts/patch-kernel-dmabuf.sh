#!/usr/bin/env bash
# Patch DMABUF_HEAPS into existing kernel .config and do incremental rebuild
set -euo pipefail

KERNEL_DIR="/home/rx595/build/wsl2-kernel"
OUT_DIR="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/android-kernel"
JOBS=$(nproc)

GREEN='\033[0;32m'; NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $*"; }

info "Kernel dir: $KERNEL_DIR"
info "JOBS: $JOBS"

# Patch .config — remove any DMABUF_HEAP lines then add correct values
KCONFIG="$KERNEL_DIR/.config"
info "Patching $KCONFIG ..."
sed -i "/CONFIG_DMABUF_HEAP/d" "$KCONFIG"
echo "" >> "$KCONFIG"
echo "CONFIG_DMABUF_HEAPS=y" >> "$KCONFIG"
echo "CONFIG_DMABUF_HEAPS_SYSTEM=y" >> "$KCONFIG"

info "DMABUF config after patch:"
grep -E "DMABUF_HEAP" "$KCONFIG"

# Resolve dependencies
info "Running make olddefconfig..."
make -C "$KERNEL_DIR" olddefconfig 2>&1 | tail -5

info "DMABUF config after olddefconfig:"
grep -E "DMABUF_HEAP" "$KCONFIG"

# Build only bzImage — DMABUF_HEAPS=y is built-in, no modules needed
# Using `bzImage` target skips module BTF processing (avoids pre-existing rbd.ko pahole error)
info "Building bzImage only (${JOBS} jobs)..."
make -C "$KERNEL_DIR" -j"$JOBS" LOCALVERSION="-chimera-hcs" bzImage 2>&1 | tail -15

# Copy output
info "Copying output..."
mkdir -p "$OUT_DIR"
cp "$KERNEL_DIR/arch/x86/boot/bzImage" "$OUT_DIR/bzImage"
cp "$KERNEL_DIR/System.map"            "$OUT_DIR/System.map"
cp "$KERNEL_DIR/.config"               "$OUT_DIR/kernel.config"

info "============================================"
info "bzImage: $(du -sh $OUT_DIR/bzImage | cut -f1)"
info "DMABUF in final kernel.config:"
grep -E "DMABUF_HEAP" "$OUT_DIR/kernel.config"
info "============================================"
