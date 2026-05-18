#!/usr/bin/env bash
VHDX="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish/system.vhdx"
qemu-img convert -f vhdx -O raw "$VHDX" /tmp/sys.raw
file /tmp/sys.raw
mkdir -p /tmp/sysmnt
sudo mount -t ext4 -o ro,loop /tmp/sys.raw /tmp/sysmnt

echo "=== Root listing ==="
ls /tmp/sysmnt/

echo "=== /system listing (if exists) ==="
ls /tmp/sysmnt/system/ 2>&1 | head -10

echo "=== APEX preinstalled (system/apex/) ==="
ls /tmp/sysmnt/system/apex/ 2>&1 | head -30

echo "=== Direct apex search ==="
find /tmp/sysmnt -maxdepth 4 -name "*.apex" 2>/dev/null | head -20

echo "=== SwiftShader anywhere ==="
find /tmp/sysmnt -name "*swiftshader*" -o -name "*vk_swiftshader*" 2>/dev/null | head -10

echo "=== EGL anywhere (not in apex) ==="
find /tmp/sysmnt -name "libEGL_*" -not -path "*/apex/*" 2>/dev/null | head -10

echo "=== ANGLE EGL ==="
find /tmp/sysmnt -name "libEGL_angle*" 2>/dev/null | head -5

echo "=== All .so in top-level lib64 ==="
ls /tmp/sysmnt/lib64/egl/ 2>&1 || echo "no /lib64/egl"
ls /tmp/sysmnt/system/lib64/egl/ 2>&1 || echo "no system/lib64/egl"

sudo umount /tmp/sysmnt 2>&1 || true
rm -f /tmp/sys.raw
echo "Done"
