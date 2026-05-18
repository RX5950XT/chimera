#!/usr/bin/env bash
VHDX="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish/system.vhdx"
echo "[1] Converting VHDX to raw (4GB -> /tmp/sys.raw)..."
qemu-img convert -f vhdx -O raw "$VHDX" /tmp/sys.raw
echo "[2] File type check:"
file /tmp/sys.raw
echo "[3] Mount:"
mkdir -p /tmp/sysmnt
sudo mount -t ext4 -o ro,loop /tmp/sys.raw /tmp/sysmnt
echo "[4] Top-level dirs:"
ls /tmp/sysmnt/ 2>&1 | head -10
echo "[5] APEX modules (look for swiftshader/gles/angle):"
ls /tmp/sysmnt/system/apex/ 2>&1 | grep -iE "swift|gles|angle|mesa|llvm|opengl|egl" | head -20
echo "[6] SwiftShader search:"
find /tmp/sysmnt -name "*swiftshader*" -o -name "*swift_shader*" 2>/dev/null | head -20
echo "[7] EGL in system:"
find /tmp/sysmnt -path "*/egl/libEGL*" 2>/dev/null | head -10
echo "[8] Cleanup"
sudo umount /tmp/sysmnt 2>&1 || true
rm -f /tmp/sys.raw
echo "Done"
