#!/usr/bin/env bash
# =============================================================================
# build-qemu-initrd.sh — Phase 6c: Build initrd for QEMU virtio-gpu boot
#
# Much simpler than the HCS initrd:
#   - No HyperV modules needed (CONFIG_DRM_VIRTIO_GPU=y + CONFIG_SCSI_VIRTIO=y built-in)
#   - No relay daemons (QEMU handles display via VNC, input via PS/2 emulation)
#   - Just: mount disks via virtio-scsi (/dev/sda..sdd), inject local.prop, switch_root
#
# virtio-gpu creates /dev/dri/card0 + /dev/fb0 (DRM_FBDEV_EMULATION=y)
# Android's hwcomposer.ranchu.so uses /dev/dri/card0 for KMS display
# SurfaceFlinger uses SwiftShader (CPU EGL) for rendering
#
# Run in WSL2 Ubuntu-24.04:
#   bash scripts/build-qemu-initrd.sh
# =============================================================================
set -euo pipefail

PROJ_ROOT="/mnt/d/Workspace_cloud/Personal_Project/chimera"
OUT_DIR="$PROJ_ROOT/out/cuttlefish"
WORK="/tmp/chimera_qemu_initrd"
INITRD_OUT="$OUT_DIR/initrd-qemu.img"

GREEN='\033[0;32m'; NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $*"; }

# ── Step 1: Build initramfs tree ──────────────────────────────────────────────
info "Building QEMU initramfs tree..."
INITRD_TREE="$WORK/initrd"
rm -rf "$INITRD_TREE"
mkdir -p "$INITRD_TREE"/{bin,dev,proc,sys,run,tmp,newroot}

# Busybox
BUSYBOX_BIN="$INITRD_TREE/bin/busybox"
if [[ ! -f "$BUSYBOX_BIN" ]]; then
    info "Downloading busybox static..."
    curl -fsSL "https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox" \
         -o "$BUSYBOX_BIN"
    chmod +x "$BUSYBOX_BIN"
fi
for tool in sh ash mount umount ls cat echo sleep mkdir mknod grep dmesg \
            switch_root ln cp mv rm chmod readlink mountpoint tr head; do
    ln -sf busybox "$INITRD_TREE/bin/$tool" 2>/dev/null || true
done

# ── Step 2: init script ───────────────────────────────────────────────────────
cat > "$INITRD_TREE/init" << 'INIT_EOF'
#!/bin/sh
mount -t proc  proc  /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts && mount -t devpts devpts /dev/pts 2>/dev/null || true
mkdir -p /dev/dri  2>/dev/null || true

echo "[qemu-android] Phase 6c init — QEMU virtio-gpu boot"

# virtio-gpu is CONFIG_DRM_VIRTIO_GPU=y (built-in); auto-probed at PCI scan
# Wait for DRM device or /dev/fb0 (DRM_FBDEV_EMULATION=y)
i=0; while [ $i -lt 15 ]; do
    if [ -e /dev/dri/card0 ]; then
        echo "[qemu-android] /dev/dri/card0 ready (virtio-gpu DRM KMS)"
        break
    fi
    if [ -e /dev/fb0 ]; then
        echo "[qemu-android] /dev/fb0 ready (virtio-gpu fbdev emulation)"
        break
    fi
    sleep 1; i=$((i+1))
done
if [ -e /dev/dri/card0 ]; then
    echo "[qemu-android] virtio-gpu: DRM device OK"
elif [ -e /dev/fb0 ]; then
    echo "[qemu-android] virtio-gpu: fbdev fallback OK"
else
    echo "[qemu-android] WARNING: no virtio-gpu device found (check QEMU args)"
fi

# virtio-scsi gives /dev/sda, sdb, sdc, sdd — same naming as HCS SCSI
echo "[qemu-android] Waiting for virtio-scsi disks..."
i=0; while [ $i -lt 30 ]; do
    [ -b /dev/sda ] && break
    sleep 1; i=$((i+1))
done
[ -b /dev/sda ] || { echo "[qemu-android] ERROR: /dev/sda not found"; exec /bin/sh; }
echo "[qemu-android] Disks: $(ls /dev/sd* 2>/dev/null)"

# Mount Android rootfs (system.vhdx = /dev/sda, ext4 ro)
mkdir -p /newroot
echo "[qemu-android] Mounting rootfs /dev/sda → /newroot..."
mount -t ext4 -o ro,noatime /dev/sda /newroot || { echo "ERROR: rootfs mount failed"; exec /bin/sh; }
[ -e /newroot/init ] || [ -e /newroot/system ] || { echo "ERROR: bad rootfs"; exec /bin/sh; }
echo "[qemu-android] rootfs mounted OK"

# Vendor (vendor.vhdx = /dev/sdb)
if [ -b /dev/sdb ]; then
    mkdir -p /newroot/vendor
    mount -t ext4 -o ro,noatime /dev/sdb /newroot/vendor 2>/dev/null || true
    mountpoint -q /newroot/vendor && echo "[qemu-android] vendor mounted" || \
        echo "[qemu-android] vendor mount failed (non-fatal)"
fi

# Userdata (userdata.vhdx = /dev/sdc)
mkdir -p /newroot/data
if [ -b /dev/sdc ]; then
    mount -t ext4 /dev/sdc /newroot/data 2>/dev/null || \
    mount -t tmpfs -o size=1g tmpfs /newroot/data
else
    mount -t tmpfs -o size=1g tmpfs /newroot/data
fi
echo "[qemu-android] data mounted"

# Metadata (metadata.vhdx = /dev/sdd — first_stage_mount target)
mkdir -p /newroot/metadata
if [ -b /dev/sdd ]; then
    mount -t ext4 /dev/sdd /newroot/metadata 2>/dev/null || \
    mount -t tmpfs tmpfs /newroot/metadata 2>/dev/null || true
else
    mount -t tmpfs tmpfs /newroot/metadata 2>/dev/null || true
fi
echo "[qemu-android] metadata mounted"

# Android init expects these tmpfs mounts (directories exist in system.vhdx ro)
mount -t tmpfs tmpfs /newroot/apex   2>/dev/null || true
mount -t tmpfs tmpfs /newroot/config 2>/dev/null || true
mount -t tmpfs tmpfs /newroot/mnt    2>/dev/null || true

# NOTE: Do NOT bind-mount /proc /sys /dev into newroot.
# Android first_stage_init mounts them itself; pre-mounting causes EBUSY.

# SELinux permissive
[ -f /sys/fs/selinux/enforce ] && echo 0 > /sys/fs/selinux/enforce 2>/dev/null || true

# Phase 6c: SwiftShader EGL + QEMU properties
mkdir -p /newroot/data
cat > /newroot/data/local.prop << 'PROP_EOF'
ro.hardware.egl=swiftshader
ro.kernel.qemu=1
ro.kernel.qemu.gles=1
androidboot.opengles.version=131072
debug.sf.nobootanimation=1
debug.hwui.renderer=skiagl
PROP_EOF
chmod 644 /newroot/data/local.prop
echo "[qemu-android] Phase 6c: SwiftShader/QEMU properties injected"

# Resolve init symlink
INIT_LINK=$(readlink /newroot/init 2>/dev/null || echo "")
if [ -n "$INIT_LINK" ]; then
    REAL_INIT="/newroot${INIT_LINK}"
    [ -f "$REAL_INIT" ] && echo "[qemu-android] init: /init → $INIT_LINK (OK)" || \
        { echo "ERROR: /init symlink target missing: $REAL_INIT"; exec /bin/sh; }
elif [ -f /newroot/init ]; then
    echo "[qemu-android] init: regular binary"
else
    echo "ERROR: /newroot/init not found"; exec /bin/sh
fi

echo "[qemu-android] switch_root → Android..."
exec switch_root /newroot /init
INIT_EOF
chmod +x "$INITRD_TREE/init"

# ── Step 3: Pack initramfs ────────────────────────────────────────────────────
info "Packing initramfs..."
mkdir -p "$(dirname "$INITRD_OUT")"
(cd "$INITRD_TREE" && find . | cpio -H newc -o | gzip -9) > "$INITRD_OUT"
info "initrd-qemu.img: $(du -sh "$INITRD_OUT" | cut -f1)"
info "Done → $INITRD_OUT"
