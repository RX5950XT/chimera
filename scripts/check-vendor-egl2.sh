#!/usr/bin/env bash
VHDX="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish/vendor.vhdx"
sudo modprobe nbd max_part=8 2>/dev/null || true
sudo qemu-nbd --connect=/dev/nbd0 -f vhdx "$VHDX" 2>&1
sleep 1

mkdir -p /tmp/vm
sudo mount -t ext4 -o ro /dev/nbd0 /tmp/vm 2>&1

echo "=== EGL libs ==="
ls /tmp/vm/lib64/egl/ 2>&1 || echo "no lib64/egl"
ls /tmp/vm/lib/egl/ 2>&1 || echo "no lib/egl"

echo "=== Vulkan ==="
ls /tmp/vm/lib64/ 2>&1 | grep -i "vulkan\|swiftshader\|mesa\|llvm" || echo "none"

echo "=== Composer ==="
ls /tmp/vm/bin/hw/ 2>&1 | grep -i "composer\|hwc" || echo "none"

echo "=== Gralloc ==="
ls /tmp/vm/lib64/hw/ 2>&1 | grep -i "gralloc\|alloc" || echo "none"

echo "=== Swiftshader check ==="
find /tmp/vm -name "*swiftshader*" 2>/dev/null | head -20

sudo umount /tmp/vm 2>&1 || true
sudo qemu-nbd --disconnect /dev/nbd0 2>&1
