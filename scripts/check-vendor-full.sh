#!/usr/bin/env bash
VHDX="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish/vendor.vhdx"
sudo modprobe nbd max_part=8 2>/dev/null || true
sudo qemu-nbd --connect=/dev/nbd0 -f vhdx "$VHDX" 2>&1
sleep 1

mkdir -p /tmp/vm
sudo mount -t ext4 -o ro /dev/nbd0 /tmp/vm 2>&1

echo "=== Top-level dirs ==="
ls /tmp/vm/ 2>&1

echo "=== bin/hw/ ==="
ls /tmp/vm/bin/hw/ 2>&1 | head -30

echo "=== lib64/hw/ (all) ==="
ls /tmp/vm/lib64/hw/ 2>&1 | head -40

echo "=== Any gralloc files ==="
find /tmp/vm -name "*gralloc*" 2>/dev/null | head -10

echo "=== Any composer binaries ==="
find /tmp/vm -name "*composer*" -type f 2>/dev/null | head -10

echo "=== Any ranchu ==="
find /tmp/vm -name "*ranchu*" 2>/dev/null | head -20

echo "=== etc/permissions ==="
ls /tmp/vm/etc/permissions/ 2>&1 | head -20

sudo umount /tmp/vm 2>&1 || true
sudo qemu-nbd --disconnect /dev/nbd0 2>&1
