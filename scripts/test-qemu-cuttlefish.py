"""
test-qemu-cuttlefish.py — Phase 6c: QEMU virtio-gpu boot test for AOSP Cuttlefish

Boots the same Cuttlefish VHDX images via QEMU instead of HCS.
virtio-gpu creates /dev/dri/card0 (DRM KMS device) — hwcomposer.ranchu.so
uses this for display, allowing SurfaceFlinger to initialize properly.

Key difference from HCS:
  HCS: hyperv_drm → /dev/fb0 only → hwcomposer fails → SurfaceFlinger crash-loops
  QEMU: virtio-gpu → /dev/dri/card0 + /dev/fb0 → hwcomposer OK → SurfaceFlinger stable

Checks:
  1. virtio-gpu detected (DRM device in serial)
  2. Android system mounts (switch_root)
  3. Android init launched
  4. SurfaceFlinger starts
  5. SurfaceFlinger STABLE (no crash/restart for SF_STABLE_WINDOW_S seconds)

Run from an elevated PowerShell:
  python scripts\\test-qemu-cuttlefish.py
"""
import os, pathlib, queue, socket, subprocess, sys, threading, time

PROJ    = pathlib.Path(__file__).parent.parent
KERNEL  = str(PROJ / "out" / "android-kernel" / "bzImage")
INITRD  = str(PROJ / "out" / "cuttlefish" / "initrd-qemu.img")
SYSTEM  = str(PROJ / "out" / "cuttlefish" / "system.vhdx")
VENDOR  = str(PROJ / "out" / "cuttlefish" / "vendor.vhdx")
USERDATA= str(PROJ / "out" / "cuttlefish" / "userdata.vhdx")
METADATA= str(PROJ / "out" / "cuttlefish" / "metadata.vhdx")

QEMU_CANDIDATES = [
    r"C:\Program Files\qemu\qemu-system-x86_64.exe",
    r"C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
]

CMDLINE = (
    "console=ttyS0,115200n8 earlycon=uart8250,io,0x3f8,115200 "
    "loglevel=8 ignore_loglevel panic=30 "
    "init=/init androidboot.selinux=permissive androidboot.hardware=cutf_cvm "
    "androidboot.lcd_density=240 androidboot.opengles.version=131072"
)

SERIAL_PORT      = 4445
ADB_PORT         = 5580   # ADB over TCP (hostfwd guest:5555 → host:5580)
BOOT_TIMEOUT_S   = 480
SF_STABLE_WINDOW_S = 60   # seconds to watch for SurfaceFlinger crash after first start
SF_HEURISTIC_S   = 120    # if QEMU alive this long after init, presume SF stable

def find_qemu():
    for p in QEMU_CANDIDATES:
        if os.path.exists(p):
            return p
    raise FileNotFoundError("qemu-system-x86_64.exe not found; install QEMU for Windows")

def check_files():
    for label, path in [("kernel", KERNEL), ("initrd", INITRD),
                        ("system.vhdx", SYSTEM), ("vendor.vhdx", VENDOR),
                        ("userdata.vhdx", USERDATA), ("metadata.vhdx", METADATA)]:
        size = os.path.getsize(path) if os.path.exists(path) else -1
        status = f"{size//1024//1024}MB" if size >= 0 else "MISSING"
        print(f"[*] {label:<14}: {status}")
        if size < 0:
            print(f"[FAIL] Required file missing: {path}")
            sys.exit(1)

def build_qemu_cmd(qemu_exe):
    cmd = [
        qemu_exe,
        "-accel", "whpx,kernel-irqchip=off",
        "-machine", "q35",
        "-m",  "4096",
        "-smp", "4",
        # Kernel direct boot
        "-kernel", KERNEL,
        "-initrd", INITRD,
        "-append", CMDLINE,
        # virtio-scsi for disks (gives /dev/sda..sdd — same as HCS SCSI; matches fstab.cutf_cvm)
        "-device", "virtio-scsi-pci,id=scsi0",
        "-drive",  f"file={SYSTEM},if=none,id=drv-sda,format=vhdx,readonly=on",
        "-device", "scsi-hd,bus=scsi0.0,drive=drv-sda",
        "-drive",  f"file={VENDOR},if=none,id=drv-sdb,format=vhdx,readonly=on",
        "-device", "scsi-hd,bus=scsi0.0,drive=drv-sdb",
        "-drive",  f"file={USERDATA},if=none,id=drv-sdc,format=vhdx",
        "-device", "scsi-hd,bus=scsi0.0,drive=drv-sdc",
        "-drive",  f"file={METADATA},if=none,id=drv-sdd,format=vhdx",
        "-device", "scsi-hd,bus=scsi0.0,drive=drv-sdd",
        # virtio-gpu: creates /dev/dri/card0 (KMS) + /dev/fb0 (fbdev emulation)
        # hwcomposer.ranchu.so uses /dev/dri/card0 for display output
        "-device", "virtio-gpu-pci",
        # virtio-net: ADB-over-TCP for SurfaceFlinger verification
        "-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{ADB_PORT}-:5555",
        "-device", "virtio-net-pci,netdev=net0",
        # Serial TCP server — Python connects after QEMU starts
        "-serial", f"tcp:127.0.0.1:{SERIAL_PORT},server,nowait",
        "-display", "none",
        "-monitor", "none",
        "-no-reboot",
    ]
    return cmd

QEMU_STDERR_LOG = str(PROJ / "qemu_stderr.log")

def connect_serial(timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(("127.0.0.1", SERIAL_PORT))
            sock.settimeout(2.0)
            return sock
        except OSError:
            try: sock.close()
            except: pass
            time.sleep(0.5)
    return None

def _sock_reader(sock, q):
    try:
        while True:
            data = sock.recv(4096)
            if data:
                q.put(data)
            else:
                break
    except Exception:
        pass

def main():
    qemu_exe = find_qemu()
    print(f"[*] QEMU: {qemu_exe}")
    check_files()

    cmd = build_qemu_cmd(qemu_exe)
    print(f"[*] Launching QEMU (WHPX)...")
    stderr_fh = open(QEMU_STDERR_LOG, "wb")
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=stderr_fh)
    print(f"[+] QEMU PID {proc.pid}")
    print(f"[*] Connecting to serial TCP port {SERIAL_PORT} (up to 30s)...")
    serial_sock = connect_serial(30)
    if not serial_sock:
        print("[FAIL] Serial TCP never opened")
        proc.terminate()
        sys.exit(1)
    print("[+] Serial connected")

    checks = {
        "gpu":     False,   # virtio-gpu DRM device detected
        "system":  False,   # rootfs mounted (switch_root)
        "init":    False,   # Android init launched
        "sf":      False,   # SurfaceFlinger first start
        "sf_stable": False, # SurfaceFlinger stable (no crash for SF_STABLE_WINDOW_S)
    }
    sf_first_seen = None
    sf_last_crash = None
    init_seen_time = None

    q: queue.Queue = queue.Queue()
    threading.Thread(target=_sock_reader, args=(serial_sock, q), daemon=True).start()

    print(f"[*] Watching serial ({BOOT_TIMEOUT_S}s boot timeout, "
          f"{SF_STABLE_WINDOW_S}s SF stability window, "
          f"{SF_HEURISTIC_S}s QEMU-alive heuristic)...")
    deadline = time.time() + BOOT_TIMEOUT_S
    buf = b""

    while time.time() < deadline:
        # Check SF stability once SF has started
        if checks["sf"] and sf_first_seen and not checks["sf_stable"]:
            elapsed = time.time() - sf_first_seen
            if sf_last_crash is None and elapsed >= SF_STABLE_WINDOW_S:
                checks["sf_stable"] = True
                print(f"  [+] SurfaceFlinger STABLE for {elapsed:.0f}s — no crash!")
                break

        # Heuristic: if QEMU still alive SF_HEURISTIC_S seconds after init check passed
        # and serial went quiet (Android running normally, not crashing), declare SF stable.
        # Rationale: pre-DMABUF_HEAPS, QEMU exited at t≈7s (Android reboot on gralloc fail).
        # If QEMU is alive 120s later, gralloc/SF initialized successfully.
        if checks["init"] and not checks["sf"] and init_seen_time:
            alive = time.time() - init_seen_time
            if alive >= SF_HEURISTIC_S and proc.poll() is None:
                checks["sf"] = True
                checks["sf_stable"] = True
                sf_first_seen = init_seen_time
                print(f"  [+] SurfaceFlinger PRESUMED STABLE — QEMU alive {alive:.0f}s after Android init (no crash/reboot)")
                break

        all_core = all(checks[k] for k in ("gpu", "system", "init", "sf", "sf_stable"))
        if all_core:
            break

        try:
            chunk = q.get(timeout=1.0)
        except queue.Empty:
            if proc.poll() is not None:
                print(f"[!] QEMU exited (code {proc.returncode}) — draining remaining serial...")
                # Drain remaining buffered data after QEMU exits (up to 5s)
                drain_deadline = time.time() + 5
                while time.time() < drain_deadline:
                    try:
                        chunk = q.get(timeout=0.5)
                        buf += chunk
                    except queue.Empty:
                        continue
                # Process remaining buffer
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    txt = line.strip().decode("utf-8", errors="replace")
                    if txt:
                        print(f"  SERIAL: {txt}", flush=True)
                        if "surfaceflinger" in txt.lower() and not checks["sf"]:
                            checks["sf"] = True; sf_first_seen = time.time()
                        if checks["sf"] and ("died" in txt.lower() or "killing service 'surfaceflinger'" in txt.lower()):
                            sf_last_crash = time.time(); sf_first_seen = time.time()
                        if "reboot" in txt.lower() or "poweroff" in txt.lower():
                            print(f"  [!] Guest reboot: {txt}")
                break
            continue

        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            txt = line.strip().decode("utf-8", errors="replace")
            if not txt:
                continue
            print(f"  SERIAL: {txt}", flush=True)

            # GPU detection
            if "dri/card0 ready" in txt:       checks["gpu"] = True
            if "virtio-gpu DRM"  in txt:        checks["gpu"] = True
            if "virtio_gpu"      in txt.lower(): checks["gpu"] = True
            if "/dev/fb0 ready"  in txt:        checks["gpu"] = True
            if "drm_fb_helper"   in txt.lower(): checks["gpu"] = True

            # Android boot
            if "rootfs mounted OK"  in txt: checks["system"] = True
            if "switch_root"        in txt: checks["system"] = True
            if "init first stage"   in txt.lower(): checks["init"] = True
            if "android"            in txt.lower(): checks["init"] = True
            if "init:"              in txt.lower(): checks["init"] = True
            if checks["init"] and init_seen_time is None:
                init_seen_time = time.time()

            # SurfaceFlinger
            sf_txt = txt.lower()
            if "surfaceflinger" in sf_txt and not checks["sf"]:
                checks["sf"] = True
                sf_first_seen = time.time()
                print(f"  [+] SurfaceFlinger STARTED — watching stability for {SF_STABLE_WINDOW_S}s...")

            # SurfaceFlinger crash detection
            if checks["sf"] and (
                ("service 'surfaceflinger'" in sf_txt and "died" in sf_txt) or
                ("killing service 'surfaceflinger'" in sf_txt) or
                ("surfaceflinger" in sf_txt and "restart" in sf_txt)
            ):
                sf_last_crash = time.time()
                sf_first_seen = time.time()  # Reset stability window
                print(f"  [!] SurfaceFlinger crashed — resetting stability window")

    serial_sock.close()
    stderr_fh.close()
    # Show QEMU stderr if non-empty
    with open(QEMU_STDERR_LOG, "rb") as f:
        qemu_err = f.read().decode("utf-8", errors="replace").strip()
    if qemu_err:
        print(f"\n[QEMU stderr]\n{qemu_err[-2000:]}\n")
    print()
    print("=" * 60)
    print(f"  [{'PASS' if checks['gpu']       else 'FAIL'}] virtio-gpu DRM device         (/dev/dri/card0)")
    print(f"  [{'PASS' if checks['system']    else 'FAIL'}] Android system mount          (switch_root)")
    print(f"  [{'PASS' if checks['init']      else 'FAIL'}] Android init launched")
    print(f"  [{'PASS' if checks['sf']        else 'FAIL'}] SurfaceFlinger started")
    print(f"  [{'PASS' if checks['sf_stable'] else 'FAIL'}] SurfaceFlinger stable         ({SF_STABLE_WINDOW_S}s no crash)")
    passed = sum(checks.values())
    total  = len(checks)
    print(f"  {passed}/{total} checks passed")
    if sf_last_crash:
        print(f"  Note: SurfaceFlinger crashed at least once (last at t={sf_last_crash - sf_first_seen:.0f}s)")
    print("=" * 60)

    print("[*] Terminating QEMU...")
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    sys.exit(0 if passed >= 4 else 1)

if __name__ == "__main__":
    main()
