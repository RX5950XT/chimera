#!/usr/bin/env bash
VHDX="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish/system.vhdx"
sudo modprobe nbd max_part=8 2>/dev/null || true
sudo qemu-nbd --connect=/dev/nbd0 -f vhdx "$VHDX" 2>&1
sleep 1

mkdir -p /tmp/sys_mnt
sudo mount -t ext4 -o ro /dev/nbd0 /tmp/sys_mnt 2>&1

echo "=== EGL libs in system ==="
ls /tmp/sys_mnt/lib64/egl/ 2>&1 || echo "no system/lib64/egl"

echo "=== Vulkan in system ==="
ls /tmp/sys_mnt/lib64/ 2>&1 | grep -i "vulkan\|swiftshader\|mesa\|llvm" || echo "none"

echo "=== Gralloc in system ==="
find /tmp/sys_mnt -name "gralloc*" 2>/dev/null | head -10

echo "=== Composer binary in system ==="
find /tmp/sys_mnt -name "*composer*" 2>/dev/null | head -10

echo "=== SwiftShader in system ==="
find /tmp/sys_mnt -name "*swiftshader*" 2>/dev/null | head -20

echo "=== lib64/hw in system ==="
ls /tmp/sys_mnt/lib64/hw/ 2>&1 | grep -i "gralloc\|hwc\|composer\|mesa\|swiftshader\|llvm" || echo "none found"

sudo umount /tmp/sys_mnt 2>&1 || true
sudo qemu-nbd --disconnect /dev/nbd0 2>&1
