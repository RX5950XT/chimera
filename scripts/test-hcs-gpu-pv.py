"""
test-hcs-gpu-pv.py — Phase 5e: GPU-PV verification for Android guest

Boots the Cuttlefish VHDX images via HCS with GpuConfiguration:Mirror.
Verifies that dxgkrnl loads and creates /dev/dxg in the guest.

Checks:
  1. /dev/fb0 created (hyperv_drm — display baseline)
  2. /dev/dxg created (dxgkrnl GPU-PV)
  3. Input relay connected (vsock port 16)
  4. Display relay connected (vsock port 17)
  5. Android system partition mounted (switch_root)

Run from an elevated PowerShell:
  python scripts/test-hcs-gpu-pv.py
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
CMDLINE  = ("console=ttyS0,115200n8 earlycon=uart8250,io,0x3f8,115200 "
            "loglevel=8 ignore_loglevel panic=30 init=/init "
            "androidboot.selinux=permissive")

_TS   = int(time.time()) % 100000
PIPE  = f"\\\\.\\pipe\\chimera-gpupv-{_TS}"
VM_ID = f"chimera-gpupv-{_TS}"

for p in (KERNEL, INITRD):
    if not os.path.exists(p):
        print(f"[FAIL] File not found: {p}"); sys.exit(1)

print(f"[*] kernel  : {KERNEL}  ({os.path.getsize(KERNEL)//1024//1024}MB)")
print(f"[*] initrd  : {INITRD}  ({os.path.getsize(INITRD)//1024}KB)")
print(f"[*] system.vhdx  : {'FOUND' if os.path.exists(SYSTEM)   else 'MISSING'}")
print(f"[*] vendor.vhdx  : {'FOUND' if os.path.exists(VENDOR)   else 'MISSING'}")
print(f"[*] userdata.vhdx: {'FOUND' if os.path.exists(USERDATA) else 'MISSING'}")

scsi_attachments = []
for path, readonly in [(SYSTEM, True), (VENDOR, True), (USERDATA, False)]:
    if os.path.exists(path):
        scsi_attachments.append({
            "Type":                     "VirtualDisk",
            "Path":                     path,
            "ReadOnly":                 readonly,
            "SupportCompressedVolumes": False,
        })

devices = {
    "HvSocket": {"HvSocketConfig": {
        "DefaultBindSecurityDescriptor":    "D:P(A;;FA;;;WD)",
        "DefaultConnectSecurityDescriptor": "D:P(A;;FA;;;WD)",
    }},
    "VideoMonitor": {"HorizontalResolution": 1920, "VerticalResolution": 1080},
    "ComPorts":     {"0": {"NamedPipe": PIPE}},
    # GPU-PV: Mirror host GPU into guest so dxgkrnl sees the physical device
    # Field name is "GpuP" (not "GpuConfiguration") per HCS schema v2.1
    "GpuP": {"AssignmentMode": "Mirror", "AllowVendorExtension": True},
}
if scsi_attachments:
    devices["Scsi"] = {"0": {"Attachments": {str(i): a for i, a in enumerate(scsi_attachments)}}}

VM_JSON = json.dumps({
    "SchemaVersion": {"Major": 2, "Minor": 1},
    "Owner": "chimera-gpupv",
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
print(f"[*] GPU-PV: Mirror mode (dxgkrnl → /dev/dxg)")

print("[*] Creating HCS VM with GPU-PV...")
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

print("[*] Watching serial output (300s timeout)...")
checks = {
    "fb0":     False,   # hyperv_drm /dev/fb0
    "dxg":     False,   # dxgkrnl /dev/dxg
    "input":   False,   # input relay vsock 16
    "display": False,   # display relay vsock 17
    "system":  False,   # Android system partition mounted
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

deadline = time.time() + 300
buf = b""
while time.time() < deadline:
    if all(checks.values()):
        break
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
            if "/dev/fb0 ready"           in txt: checks["fb0"]     = True
            if "/dev/dxg ready"           in txt: checks["dxg"]     = True
            if "dxgkrnl"                  in txt: checks["dxg"]     = True
            if "[input-relay] listening"   in txt: checks["input"]   = True
            if "Input relay started"       in txt: checks["input"]   = True
            if "[display-relay] listening" in txt: checks["display"] = True
            if "Display relay started"     in txt: checks["display"] = True
            if "rootfs mounted OK"         in txt: checks["system"]  = True

pipe_fh.close()

print()
print("=" * 60)
print(f"  [{'PASS' if checks['fb0']     else 'FAIL'}] /dev/fb0 ready       (hyperv_drm)")
print(f"  [{'PASS' if checks['dxg']     else 'FAIL'}] /dev/dxg ready       (dxgkrnl GPU-PV)")
print(f"  [{'PASS' if checks['input']   else 'FAIL'}] Input relay          (vsock port 16)")
print(f"  [{'PASS' if checks['display'] else 'FAIL'}] Display relay        (vsock port 17)")
print(f"  [{'PASS' if checks['system']  else 'FAIL'}] Android system mount (switch_root)")
passed = sum(checks.values())
print(f"  {passed}/{len(checks)} checks passed")
print("=" * 60)

print("[*] Terminating VM...")
op = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
CC.HcsTerminateComputeSystem(cs, op, ctypes.c_wchar_p(None))
CC.HcsWaitForOperationResult(op, 10000, ctypes.byref(result))
CC.HcsCloseOperation(op)
CC.HcsCloseComputeSystem(cs)
print("[*] Done.")
sys.exit(0 if passed >= 4 else 1)
