#!/usr/bin/env bash
VHDX="/mnt/d/Workspace_cloud/Personal_Project/chimera/out/cuttlefish/vendor.vhdx"
sudo modprobe nbd max_part=8 2>/dev/null || true
sudo qemu-nbd --connect=/dev/nbd0 -f vhdx "$VHDX" 2>&1
sleep 1

# Check for ext4 superblock at various offsets
echo "--- Searching for ext4 superblock ---"
for offset in 0 512 1024 2048 4096 8192 65536 1048576; do
    magic=$(sudo dd if=/dev/nbd0 bs=1 skip=$((offset+1080)) count=2 2>/dev/null | xxd 2>/dev/null)
    echo "offset $offset+1080: $magic"
    if echo "$magic" | grep -q "53 ef"; then
        echo "  *** EXT4 SUPERBLOCK FOUND AT OFFSET $offset ***"
        sudo mount -t ext4 -o ro,loop,offset=$offset /dev/nbd0 /tmp/vm 2>&1
        echo "EGL libs:"
        ls /tmp/vm/lib64/egl/ 2>&1
        sudo umount /tmp/vm 2>&1 || true
        break
    fi
done

sudo qemu-nbd --disconnect /dev/nbd0 2>&1
