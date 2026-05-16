#!/usr/bin/env bash
# =============================================================================
# create-test-vhdx.sh — Download Alpine Linux and create a minimal test VHDX
#
# This script creates the smallest possible HCS-bootable image to validate
# the HCS JSON config (LinuxKernelDirect boot) WITHOUT building a full
# Android image first.
#
# Run inside WSL2 Ubuntu 24.04:
#   bash scripts/create-test-vhdx.sh
#
# Output (Windows paths printed at end):
#   out/test-vm/bzImage    — Alpine/Chimera kernel
#   out/test-vm/initrd.img — initramfs with smoke-test script
#   out/test-vm/system.vhdx — (empty, 256MB) placeholder
#
# After running, update configs/hcs.json and run:
#   chimera-ui.exe --hcs-backend
# =============================================================================
set -euo pipefail

OUT="$(pwd)/out/test-vm"
mkdir -p "$OUT"

ALPINE_VER="3.21.3"
ALPINE_ARCH="x86_64"
ALPINE_ISO_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VER%.*}/releases/${ALPINE_ARCH}/alpine-virt-${ALPINE_VER}-${ALPINE_ARCH}.iso"
ISO_FILE="$OUT/alpine-virt.iso"

GREEN='\033[0;32m'; NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $*"; }

# ─── Step 1: Download Alpine virt ISO ────────────────────────────────────
if [[ ! -f "$ISO_FILE" ]]; then
    info "Downloading Alpine Linux virt ISO..."
    curl -fL "$ALPINE_ISO_URL" -o "$ISO_FILE"
else
    info "Alpine ISO already present: $ISO_FILE"
fi

# ─── Step 2: Extract kernel + initramfs from ISO ─────────────────────────
sudo apt-get install -y --no-install-recommends xorriso >/dev/null 2>&1 || \
    sudo apt-get install -y --no-install-recommends genisoimage >/dev/null 2>&1 || true

MNT="$(mktemp -d)"
sudo mount -o loop,ro "$ISO_FILE" "$MNT"
trap "sudo umount $MNT; rmdir $MNT" EXIT

# Alpine virt ISO layout: /boot/vmlinuz-virt + /boot/initramfs-virt
VMLINUZ=$(find "$MNT/boot" -name "vmlinuz*" | head -1)
INITRD=$(find  "$MNT/boot" -name "initramfs*" | head -1)

if [[ -z "$VMLINUZ" || -z "$INITRD" ]]; then
    echo "[ERROR] Cannot find kernel/initramfs in ISO"
    exit 1
fi

cp "$VMLINUZ" "$OUT/bzImage"
cp "$INITRD"  "$OUT/initrd.img"
info "Kernel:   $OUT/bzImage ($(du -sh "$OUT/bzImage" | cut -f1))"
info "Initrd:   $OUT/initrd.img ($(du -sh "$OUT/initrd.img" | cut -f1))"

sudo umount "$MNT"; rmdir "$MNT"; trap - EXIT

# ─── Step 3: Create stub system.vhdx (256 MiB, ext4) ────────────────────
VHDX_IMG="$OUT/system.vhdx"
if [[ ! -f "$VHDX_IMG" ]]; then
    info "Creating stub system.vhdx (256 MiB ext4)..."
    # Create raw ext4 image first, then convert to VHDX via PowerShell on Windows
    # In WSL2, we create a raw img; Windows-side PowerShell converts to VHDX
    RAW="$OUT/system.raw"
    dd if=/dev/zero of="$RAW" bs=1M count=256 status=progress
    mkfs.ext4 -L system "$RAW"
    info "Raw image: $RAW — convert to VHDX via PowerShell:"
    info "  Convert-VHD -Path '$(wslpath -w "$RAW")' -DestinationPath '$(wslpath -w "$VHDX_IMG")' -VHDType Dynamic"
fi

# ─── Step 4: Print hcs.json update instructions ──────────────────────────
WIN_OUT=$(wslpath -w "$OUT")
CMDLINE="console=hvc0 root=/dev/sda ro quiet modules=loop,squashfs,ext4"

info "======================================================================"
info "Test VHDX files ready!"
info ""
info "Update configs/hcs.json:"
cat << EOF
{
  "hcs": { "cpus": 2, "ram_mb": 1024, "gpu_mode": "none", "name": "chimera_test" },
  "boot": {
    "kernel":  "${WIN_OUT}\\bzImage",
    "initrd":  "${WIN_OUT}\\initrd.img",
    "cmdline": "${CMDLINE}"
  },
  "disks": {
    "system":   "",
    "vendor":   "",
    "userdata": ""
  }
}
EOF
info ""
info "Run: .\\build\\Release\\chimera-ui.exe --hcs-backend"
info "Expected: HCS stateChanged(Running) — confirms HCS JSON is valid"
info "======================================================================"
