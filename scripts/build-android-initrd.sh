#!/usr/bin/env bash
# =============================================================================
# build-android-initrd.sh — Phase 5d: Build initrd that boots AOSP Cuttlefish
#                            from VHDX block devices under HCS.
#
# This initrd:
#   1. Loads hv_sock.ko + hyperv_drm.ko (vsock + display)
#   2. Mounts /dev/sda (system.vhdx) at /system
#   3. Mounts /dev/sdb (vendor.vhdx) at /vendor
#   4. Mounts /dev/sdc (userdata.vhdx) at /data  or tmpfs fallback
#   5. Sets up /dev, /proc, /sys for Android
#   6. Launches chimera-display-relay + chimera-input-relay
#   7. Execs Android's /system/bin/init
#
# Prerequisites:
#   - out/android-kernel/ko/hv_sock.ko + hyperv_drm.ko
#   - out/cuttlefish/*.vhdx (from build-cuttlefish-vhdx.sh)
#   - scripts/guest/chimera-display-relay.c + chimera-input-relay.c
#
# Run in WSL2 Ubuntu-24.04:
#   bash scripts/build-android-initrd.sh
# =============================================================================
set -euo pipefail

PROJ_ROOT="/mnt/d/Workspace_cloud/Personal_Project/chimera"
OUT_DIR="$PROJ_ROOT/out/cuttlefish"
KO_DIR="$PROJ_ROOT/out/android-kernel/ko"
GUEST_SRC="$PROJ_ROOT/scripts/guest"
WORK="/tmp/chimera_android_initrd"
INITRD_OUT="$OUT_DIR/initrd.img"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }

# ── Step 1: Install build deps ────────────────────────────────────────────────
info "Installing build deps..."
sudo apt-get install -y --no-install-recommends musl-tools gcc cpio gzip 2>&1 | tail -3

# ── Step 2: Compile relay daemons (musl static) ───────────────────────────────
mkdir -p "$WORK"
KINCLUDE="-idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu"

info "Compiling chimera-display-relay..."
musl-gcc -static -O2 $KINCLUDE \
    -o "$WORK/chimera-display-relay" \
    "$GUEST_SRC/chimera-display-relay.c"

info "Compiling chimera-input-relay..."
musl-gcc -static -O2 $KINCLUDE \
    -o "$WORK/chimera-input-relay" \
    "$GUEST_SRC/chimera-input-relay.c"

info "Compiling dxg-enum (Phase 5f: dxgkrnl GPU-PV adapter test)..."
musl-gcc -static -O2 \
    -o "$WORK/dxg-enum" \
    "$GUEST_SRC/dxg-enum.c"

ls -lh "$WORK/chimera-display-relay" "$WORK/chimera-input-relay" "$WORK/dxg-enum"

# ── Step 3: Build initramfs tree ──────────────────────────────────────────────
info "Building initramfs tree..."
INITRD_TREE="$WORK/initrd"
rm -rf "$INITRD_TREE"
mkdir -p "$INITRD_TREE"/{bin,sbin,dev,proc,sys,run,tmp,lib/modules,newroot}

# Busybox
BUSYBOX_BIN="$INITRD_TREE/bin/busybox"
if [[ ! -f "$BUSYBOX_BIN" ]]; then
    info "Downloading busybox static..."
    curl -fsSL "https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox" \
         -o "$BUSYBOX_BIN"
    chmod +x "$BUSYBOX_BIN"
fi
for tool in sh ash mount umount ls cat echo sleep insmod modprobe mkdir mknod grep \
            dmesg switch_root chroot pivot_root ln cp mv rm chmod chown id \
            mountpoint tr find xargs sed awk; do
    ln -sf busybox "$INITRD_TREE/bin/$tool" 2>/dev/null || true
done

# Kernel modules
for ko in hv_sock.ko hyperv_drm.ko; do
    if [[ -f "$KO_DIR/$ko" ]]; then
        cp "$KO_DIR/$ko" "$INITRD_TREE/lib/modules/$ko"
        info "Module: $ko ($(du -sh "$KO_DIR/$ko" | cut -f1))"
    else
        warn "Missing: $KO_DIR/$ko — run patch-kernel-vsock-drm-modules.sh first"
    fi
done

# Relay daemons + dxg-enum
cp "$WORK/chimera-display-relay" "$INITRD_TREE/bin/"
cp "$WORK/chimera-input-relay"   "$INITRD_TREE/bin/"
cp "$WORK/dxg-enum"              "$INITRD_TREE/bin/"
chmod +x "$INITRD_TREE/bin/chimera-display-relay" "$INITRD_TREE/bin/chimera-input-relay" "$INITRD_TREE/bin/dxg-enum"

# ── Step 4: init script ───────────────────────────────────────────────────────
cat > "$INITRD_TREE/init" << 'INIT_EOF'
#!/bin/sh
# Phase 5d: Android boot initrd for HCS guest
# system.vhdx = full Android rootfs (contains /init -> /system/bin/init)
# vendor.vhdx = separate vendor partition (mounted at /newroot/vendor)
# userdata.vhdx = userdata (mounted at /newroot/data)

mount -t proc  proc  /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts && mount -t devpts devpts /dev/pts 2>/dev/null || true

echo "[chimera-android] init starting..."

# ── Load HyperV modules ───────────────────────────────────────────────────
echo "[chimera-android] Loading hv_sock..."
insmod /lib/modules/hv_sock.ko 2>/dev/null && \
    echo "[chimera-android] hv_sock loaded" || \
    echo "[chimera-android] hv_sock FAILED (may retry)"

sleep 2

echo "[chimera-android] Loading hyperv_drm..."
insmod /lib/modules/hyperv_drm.ko 2>/dev/null && \
    echo "[chimera-android] hyperv_drm loaded" || \
    echo "[chimera-android] hyperv_drm FAILED"

# Wait for /dev/fb0
i=0; while [ $i -lt 15 ]; do
    [ -e /dev/fb0 ] && echo "[chimera-android] /dev/fb0 ready" && break
    sleep 1; i=$((i+1))
done
[ -e /dev/fb0 ] || echo "[chimera-android] /dev/fb0 not found"

# Check for dxgkrnl GPU-PV device (built-in, appears if GPU-PV is configured)
i=0; while [ $i -lt 10 ]; do
    [ -e /dev/dxg ] && echo "[chimera-android] /dev/dxg ready" && break
    sleep 1; i=$((i+1))
done
if [ -e /dev/dxg ]; then
    echo "[chimera-android] /dev/dxg ready (dxgkrnl registered, VMBus GPU channel: pending)"
    # dxg-enum verifies if the VMBus GPU channel was actually offered by the hypervisor
    [ -x /bin/dxg-enum ] && /bin/dxg-enum || true
else
    echo "[chimera-android] /dev/dxg not found"
fi

# Start relay daemons now — they survive switch_root (statically linked processes)
[ -x /bin/chimera-display-relay ] && /bin/chimera-display-relay &
echo "[chimera-android] Display relay started"
[ -x /bin/chimera-input-relay ] && /bin/chimera-input-relay &
echo "[chimera-android] Input relay started"

# ── Wait for SCSI disks ───────────────────────────────────────────────────
echo "[chimera-android] Waiting for SCSI disks..."
i=0; while [ $i -lt 30 ]; do
    [ -b /dev/sda ] && break
    sleep 1; i=$((i+1))
done

if [ ! -b /dev/sda ]; then
    echo "[chimera-android] ERROR: /dev/sda not found"
    ls /dev/sd* /dev/vd* 2>/dev/null || true
    exec /bin/sh
fi
echo "[chimera-android] Disks: $(ls /dev/sd* 2>/dev/null)"

# ── Mount Android rootfs + partitions in /newroot ────────────────────────
mkdir -p /newroot
echo "[chimera-android] Mounting rootfs from /dev/sda → /newroot..."
mount -t ext4 -o ro,noatime /dev/sda /newroot 2>/dev/null || \
mount -t erofs -o ro,noatime /dev/sda /newroot 2>/dev/null || \
mount -o ro,noatime /dev/sda /newroot

if [ ! -e /newroot/init ] && [ ! -e /newroot/system ]; then
    echo "[chimera-android] ERROR: rootfs mount FAILED (no /newroot/init)"
    exec /bin/sh
fi
echo "[chimera-android] rootfs mounted OK"
echo "[chimera-android] rootfs: $(ls /newroot/ | head -5 | cat)"

# Vendor partition
if [ -b /dev/sdb ]; then
    mkdir -p /newroot/vendor
    mount -t ext4 -o ro,noatime /dev/sdb /newroot/vendor 2>/dev/null || \
    mount -o ro,noatime /dev/sdb /newroot/vendor 2>/dev/null || true
    mountpoint -q /newroot/vendor && echo "[chimera-android] vendor mounted" || \
        echo "[chimera-android] vendor mount failed (non-fatal)"
fi

# Userdata
mkdir -p /newroot/data
if [ -b /dev/sdc ]; then
    mount -t ext4 /dev/sdc /newroot/data 2>/dev/null || \
    mount -t tmpfs -o size=512m tmpfs /newroot/data
else
    mount -t tmpfs -o size=512m tmpfs /newroot/data
fi
echo "[chimera-android] data mounted"

# Additional tmpfs mounts Android init expects
mkdir -p /newroot/apex /newroot/config /newroot/metadata /newroot/mnt /newroot/tmp /newroot/debug_ramdisk /newroot/second_stage_resources
mount -t tmpfs tmpfs /newroot/apex     2>/dev/null || true
mount -t tmpfs tmpfs /newroot/config   2>/dev/null || true
mount -t tmpfs tmpfs /newroot/metadata 2>/dev/null || true
mount -t tmpfs -o size=256m tmpfs /newroot/tmp 2>/dev/null || true
mount -t tmpfs tmpfs /newroot/mnt      2>/dev/null || true

# selinux permissive
[ -f /sys/fs/selinux/enforce ] && echo 0 > /sys/fs/selinux/enforce 2>/dev/null || true

echo "[chimera-android] system init: $(ls /newroot/init 2>/dev/null || echo 'not found')"

if [ ! -e /newroot/init ]; then
    echo "[chimera-android] ERROR: /newroot/init not found"
    exec /bin/sh
fi

# Move essential mounts into newroot and hand off to Android
echo "[chimera-android] switch_root → /newroot /init..."
exec switch_root /newroot /init
INIT_EOF
chmod +x "$INITRD_TREE/init"

# ── Step 5: Pack initramfs ────────────────────────────────────────────────────
info "Packing initramfs..."
(cd "$INITRD_TREE" && find . | cpio -H newc -o | gzip -9) > "$INITRD_OUT"
info "initrd.img: $(du -sh "$INITRD_OUT" | cut -f1)"

info "================================================================"
info "Android initrd build complete: $INITRD_OUT"
info ""
info "Contents:"
info "  - hv_sock.ko + hyperv_drm.ko (HyperV vsock + display)"
info "  - chimera-display-relay (fb0 → vsock port 17)"
info "  - chimera-input-relay (vsock port 16 → uinput)"
info "  - init script: mounts /dev/sda=system, /dev/sdb=vendor, /dev/sdc=data"
info "  - boots /system/bin/init"
info ""
info "Next: python scripts/test-hcs-cuttlefish.py"
info "================================================================"
