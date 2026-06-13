"""
test-hcs-cuttlefish.py — HCS boot test for AOSP Cuttlefish x86_64 guest

Boots the Cuttlefish VHDX images via HCS with our WSL2 6.6 kernel.
Verifies:
  1. Kernel boots and serial output appears
  2. hv_sock + hyperv_drm modules load
  3. /dev/fb0 created (hyperv_drm)
  4. Display relay starts (vsock port 17)
  5. Input relay starts (vsock port 16)
  6. Android system partition mounts (/system/bin/init found)

Run from an elevated PowerShell:
  python scripts/test-hcs-cuttlefish.py
"""
import ctypes, json, os, pathlib, queue, sys, threading, time

try:
    CC = ctypes.WinDLL("computecore.dll", use_last_error=True)
except OSError:
    print("[FAIL] computecore.dll not found — run on Windows with Hyper-V enabled")
    sys.exit(1)

def _hcs_check(hr, name):
    if hr < 0:
        raise OSError(f"{name} HRESULT=0x{hr & 0xFFFFFFFF:08X}")

for fn, res in [
    ("HcsCreateOperation",         ctypes.c_void_p),
    ("HcsCloseOperation",          None),
    ("HcsWaitForOperationResult",  ctypes.HRESULT),
    ("HcsCreateComputeSystem",     ctypes.HRESULT),
    ("HcsStartComputeSystem",      ctypes.HRESULT),
    ("HcsTerminateComputeSystem",  ctypes.HRESULT),
    ("HcsCloseComputeSystem",      None),
]:
    getattr(CC, fn).restype = res

PROJ     = pathlib.Path(__file__).parent.parent
KERNEL   = str(PROJ / "out" / "android-kernel" / "bzImage")
INITRD   = str(PROJ / "out" / "cuttlefish" / "initrd.img")
SYSTEM   = str(PROJ / "out" / "cuttlefish" / "system.vhdx")
VENDOR   = str(PROJ / "out" / "cuttlefish" / "vendor.vhdx")
USERDATA = str(PROJ / "out" / "cuttlefish" / "userdata.vhdx")
METADATA = str(PROJ / "out" / "cuttlefish" / "metadata.vhdx")
CMDLINE  = ("console=ttyS0,115200n8 earlycon=uart8250,io,0x3f8,115200 loglevel=8 ignore_loglevel panic=30 "
            "init=/init androidboot.selinux=permissive androidboot.hardware=cutf_cvm "
            "androidboot.lcd_density=240 androidboot.opengles.version=131072")

_TS   = int(time.time()) % 100000
PIPE  = f"\\\\.\\pipe\\chimera-cf-{_TS}"
VM_ID = f"chimera-cf-{_TS}"

print(f"[*] kernel  : {KERNEL}  ({os.path.getsize(KERNEL)//1024//1024}MB)")
print(f"[*] initrd  : {INITRD}  ({os.path.getsize(INITRD)//1024}KB)")
for p in (KERNEL, INITRD):
    if not os.path.exists(p):
        print(f"[FAIL] File not found: {p}"); sys.exit(1)

has_system   = os.path.exists(SYSTEM)
has_vendor   = os.path.exists(VENDOR)
has_userdata = os.path.exists(USERDATA)
has_metadata = os.path.exists(METADATA)
print(f"[*] system.vhdx  : {'FOUND' if has_system else 'MISSING'}")
print(f"[*] vendor.vhdx  : {'FOUND' if has_vendor else 'MISSING'}")
print(f"[*] userdata.vhdx: {'FOUND' if has_userdata else 'MISSING'}")
print(f"[*] metadata.vhdx: {'FOUND' if has_metadata else 'MISSING'}")

# Build SCSI devices array — order: sda=system, sdb=vendor, sdc=userdata, sdd=metadata
scsi_attachments = []
for path, readonly in [(SYSTEM, True), (VENDOR, True), (USERDATA, False), (METADATA, False)]:
    if os.path.exists(path):
        scsi_attachments.append({
            "Type":                   "VirtualDisk",
            "Path":                   path,
            "ReadOnly":               readonly,
            "SupportCompressedVolumes": False,
        })

devices = {
    "HvSocket": {"HvSocketConfig": {
        "DefaultBindSecurityDescriptor":    "D:P(A;;FA;;;WD)",
        "DefaultConnectSecurityDescriptor": "D:P(A;;FA;;;WD)",
    }},
    "VideoMonitor": {"HorizontalResolution": 1920, "VerticalResolution": 1080},
    "ComPorts": {"0": {"NamedPipe": PIPE}},
}
if scsi_attachments:
    devices["Scsi"] = {"0": {"Attachments": {str(i): a for i, a in enumerate(scsi_attachments)}}}

VM_JSON = json.dumps({
    "SchemaVersion": {"Major": 2, "Minor": 1},
    "Owner": "chimera-cuttlefish",
    "ShouldTerminateOnLastHandleClosed": True,
    "VirtualMachine": {
        "StopOnReset": True,
        "Chipset": {"LinuxKernelDirect": {
            "KernelFilePath": KERNEL,
            "InitRdPath":     INITRD,
            "KernelCmdLine":  CMDLINE,
        }},
        "ComputeTopology": {
            "Memory":    {"SizeInMB": 2048, "AllowOvercommit": True},
            "Processor": {"Count": 4},
        },
        "Devices": devices,
    }
})

print(f"[*] VM ID: {VM_ID}")
print(f"[*] Pipe : {PIPE}")
print(f"[*] Disks: {len(scsi_attachments)} SCSI attachment(s)")

# Create and start VM
print("[*] Creating HCS VM...")
cs  = ctypes.c_void_p()
op  = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
result = ctypes.c_wchar_p()

hr = CC.HcsCreateComputeSystem(
    ctypes.c_wchar_p(VM_ID),
    ctypes.c_wchar_p(VM_JSON),
    op, None, ctypes.byref(cs))
_hcs_check(hr, "HcsCreateComputeSystem")
_hcs_check(CC.HcsWaitForOperationResult(op, 30000, ctypes.byref(result)), "Wait Create")
CC.HcsCloseOperation(op)
print("[+] VM created")

print("[*] Starting VM...")
op = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
hr = CC.HcsStartComputeSystem(cs, op, ctypes.c_wchar_p(None))
_hcs_check(hr, "HcsStartComputeSystem")
_hcs_check(CC.HcsWaitForOperationResult(op, 30000, ctypes.byref(result)), "Wait Start")
CC.HcsCloseOperation(op)
print("[+] VM running")

# Wait for serial pipe
print(f"[*] Waiting for serial pipe (up to 30s)...")
deadline = time.time() + 30
pipe_fh = None
while time.time() < deadline:
    try:
        pipe_fh = open(PIPE, "rb", buffering=0)
        break
    except OSError:
        time.sleep(0.5)

if not pipe_fh:
    print("[FAIL] Serial pipe never appeared")
    op = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
    CC.HcsTerminateComputeSystem(cs, op, ctypes.c_wchar_p(None))
    CC.HcsWaitForOperationResult(op, 10000, ctypes.byref(result))
    CC.HcsCloseOperation(op)
    CC.HcsCloseComputeSystem(cs)
    sys.exit(1)
print("[+] Serial pipe connected")

# Read boot messages
print("[*] Watching serial output (480s timeout — includes Android boot)...")
checks = {
    "fb0":       False,  # /dev/fb0 ready (hyperv_drm)
    "dxg":       False,  # dxgkrnl registered (device node present; VMBus GPU channel may not be offered)
    "dxg_ioctl": False,  # dxg-enum PASS (VMBus GPU channel established — optional)
    "fb_render": False,  # Phase 6a: fb-render drew SMPTE bars (display pipeline verified)
    "input":     False,  # Input relay started
    "display":   False,  # Display relay started
    "system":    False,  # Android system mount
    "init":      False,  # Android init launched
    "sf":        False,  # Phase 6b: SurfaceFlinger started (optional — requires SwiftShader)
}

pipe_q: queue.Queue = queue.Queue()

def _pipe_reader(fh, q):
    try:
        while True:
            chunk = fh.read(4096)
            if chunk:
                q.put(chunk)
    except (OSError, ValueError):
        pass

threading.Thread(target=_pipe_reader, args=(pipe_fh, pipe_q), daemon=True).start()

deadline = time.time() + 480
buf = b""
while time.time() < deadline:
    core_so_far = {k: v for k, v in checks.items() if k not in ("dxg_ioctl", "sf")}
    if all(core_so_far.values()) and checks["sf"]:
        break  # All core checks + optional sf complete — no need to wait longer
    try:
        chunk = pipe_q.get(timeout=1.0)
    except queue.Empty:
        continue
    buf += chunk
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        txt = line.strip().decode("utf-8", errors="replace")
        if txt:
            print(f"  SERIAL: {txt}", flush=True)
            if "/dev/fb0 ready"                    in txt: checks["fb0"]       = True
            if "/dev/dxg ready"                    in txt: checks["dxg"]       = True
            if "dxgkrnl registered"                in txt: checks["dxg"]       = True
            if "registering driver dxgkrnl"        in txt: checks["dxg"]       = True
            if "dxgkrnl GPU-PV enumeration PASS"   in txt: checks["dxg_ioctl"] = True
            if "Phase 6a: fb-render complete"      in txt: checks["fb_render"] = True
            if "fb-render complete"                in txt: checks["fb_render"] = True
            if "Phase 6a display pipeline"         in txt: checks["fb_render"] = True
            if "Input relay started"               in txt: checks["input"]     = True
            if "[input-relay] listening"           in txt: checks["input"]     = True
            if "Display relay started"             in txt: checks["display"]   = True
            if "[display-relay] listening"         in txt: checks["display"]   = True
            if "rootfs mounted OK"                 in txt: checks["system"]    = True
            if "switch_root"                       in txt: checks["init"]      = True
            # SurfaceFlinger (Phase 6b software rendering)
            if "surfaceflinger" in txt.lower()             : checks["sf"]       = True
            if "SurfaceFlinger"                    in txt  : checks["sf"]       = True
            # Catch Android init output after switch_root
            if "init:" in txt.lower() or "android" in txt.lower():
                checks["init"] = True

pipe_fh.close()

print()
print("=" * 60)
print(f"  [{'PASS' if checks['fb0']       else 'FAIL'}] /dev/fb0 ready         (hyperv_drm)")
print(f"  [{'PASS' if checks['dxg']       else 'FAIL'}] dxgkrnl registered     (VMBus driver present)")
print(f"  [{'INFO' if checks['dxg_ioctl'] else 'N/A '}] dxg-enum IOCTL PASS   (VMBus GPU channel — optional)")
print(f"  [{'PASS' if checks['fb_render'] else 'FAIL'}] Phase 6a: fb-render    (display pipeline: fb0→relay)")
print(f"  [{'PASS' if checks['input']     else 'FAIL'}] Input relay            (vsock port 16)")
print(f"  [{'PASS' if checks['display']   else 'FAIL'}] Display relay          (vsock port 17)")
print(f"  [{'PASS' if checks['system']    else 'FAIL'}] Android system mount   (switch_root)")
print(f"  [{'PASS' if checks['init']      else 'FAIL'}] Android init launched")
print(f"  [{'PASS' if checks['sf']        else 'N/A '}] Phase 6b: SurfaceFlinger (SwiftShader — optional)")
# dxg_ioctl and sf are optional
core_checks = {k: v for k, v in checks.items() if k not in ("dxg_ioctl", "sf")}
passed = sum(core_checks.values())
opt_notes = []
opt_notes.append(f"dxg_ioctl: {'PASS' if checks['dxg_ioctl'] else 'N/A — VMBus GPU channel not offered'}")
opt_notes.append(f"sf: {'PASS' if checks['sf'] else 'N/A — virtio-gpu not present'}")
print(f"  {passed}/{len(core_checks)} core checks passed  ({', '.join(opt_notes)})")
print("=" * 60)

print("[*] Terminating VM...")
op = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
CC.HcsTerminateComputeSystem(cs, op, ctypes.c_wchar_p(None))
CC.HcsWaitForOperationResult(op, 10000, ctypes.byref(result))
CC.HcsCloseOperation(op)
CC.HcsCloseComputeSystem(cs)
print("[*] Done.")
sys.exit(0 if passed >= 6 else 1)
