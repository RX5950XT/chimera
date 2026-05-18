#!/usr/bin/env bash
set -euo pipefail
OUT="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish"
TMP_RAW="/tmp/userdata_chimera.raw"
TMP_QCOW="/tmp/userdata_chimera.qcow2"

echo "[INFO] Creating fresh 4GB ext4..."
dd if=/dev/zero bs=1M count=4096 status=progress > "$TMP_RAW"
mke2fs -t ext4 -F "$TMP_RAW" 2>&1 | tail -4

echo "[INFO] Converting to qcow2..."
qemu-img convert -f raw -O qcow2 "$TMP_RAW" "$TMP_QCOW"
ls -lh "$TMP_QCOW"

echo "[INFO] Copying to Windows..."
cp -f "$TMP_QCOW" "$OUT/userdata.qcow2"
ls -lh "$OUT/userdata.qcow2"

rm -f "$TMP_RAW" "$TMP_QCOW"
echo "[INFO] Done — userdata.qcow2 reset to fresh state"
