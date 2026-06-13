"""
test-hcs-wsl2-kernel.py — Standalone HCS boot verification for WSL2 6.6 kernel

Run from an elevated (Administrator) PowerShell or Command Prompt:
  python scripts/test-hcs-wsl2-kernel.py

Tests:
  1. Creates + starts HCS VM with out/android-kernel/bzImage + out/android-kernel/initrd.img
  2. Reads serial pipe for [chimera] boot messages
  3. Verifies /dev/fb0, input relay, display relay all start successfully
"""
import ctypes, json, os, pathlib, queue, sys, threading, time

# ── HCS API ──────────────────────────────────────────────────────────────────
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
    ("HcsCloseOperation",          None),           # void
    ("HcsWaitForOperationResult",  ctypes.HRESULT),
    ("HcsCreateComputeSystem",     ctypes.HRESULT),
    ("HcsStartComputeSystem",      ctypes.HRESULT),
    ("HcsTerminateComputeSystem",  ctypes.HRESULT),
    ("HcsCloseComputeSystem",      None),           # void
]:
    getattr(CC, fn).restype = res

# ── Config ───────────────────────────────────────────────────────────────────
PROJ = pathlib.Path(__file__).parent.parent
KERNEL  = str(PROJ / "out" / "android-kernel" / "bzImage")
INITRD  = str(PROJ / "out" / "android-kernel" / "initrd.img")
CMDLINE = "console=ttyS0,115200n8 earlycon=uart8250,io,0x3f8,115200 loglevel=8 ignore_loglevel panic=30 init=/init"
_TS     = int(time.time()) % 100000
PIPE    = f"\\\\.\\pipe\\chimera-serial-{_TS}"   # unique pipe per run
VM_ID   = f"chimera-wsl2-test-{_TS}"             # unique VM ID per run

print(f"[*] kernel  : {KERNEL}  ({os.path.getsize(KERNEL)//1024//1024}MB)")
print(f"[*] initrd  : {INITRD}  ({os.path.getsize(INITRD)//1024}KB)")
for p in (KERNEL, INITRD):
    if not os.path.exists(p):
        print(f"[FAIL] File not found: {p}"); sys.exit(1)

# ── HCS JSON ─────────────────────────────────────────────────────────────────
VM_JSON = json.dumps({
    "SchemaVersion": {"Major": 2, "Minor": 1},
    "Owner": "chimera-test",
    "ShouldTerminateOnLastHandleClosed": True,
    "VirtualMachine": {
        "StopOnReset": True,
        "Chipset": {"LinuxKernelDirect": {
            "KernelFilePath": KERNEL,
            "InitRdPath":     INITRD,
            "KernelCmdLine":  CMDLINE,
        }},
        "ComputeTopology": {
            "Memory":    {"SizeInMB": 1024, "AllowOvercommit": True},
            "Processor": {"Count": 2},
        },
        "Devices": {
            "HvSocket": {"HvSocketConfig": {
                "DefaultBindSecurityDescriptor":    "D:P(A;;FA;;;WD)",
                "DefaultConnectSecurityDescriptor": "D:P(A;;FA;;;WD)",
            }},
            "VideoMonitor": {"HorizontalResolution": 1920, "VerticalResolution": 1080},
            "ComPorts": {"0": {"NamedPipe": PIPE}},
        },
    }
})

# ── Create + Start VM ─────────────────────────────────────────────────────────
print("[*] Creating HCS VM...")
cs  = ctypes.c_void_p()
op  = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
result = ctypes.c_wchar_p()

hr = CC.HcsCreateComputeSystem(
    ctypes.c_wchar_p(VM_ID),
    ctypes.c_wchar_p(VM_JSON),
    op,
    None,             # SecurityDescriptor (NULL = default)
    ctypes.byref(cs))
_hcs_check(hr, "HcsCreateComputeSystem")
_hcs_check(CC.HcsWaitForOperationResult(op, 30000, ctypes.byref(result)), "Wait Create")
CC.HcsCloseOperation(op)
print(f"[+] VM created (state=Stopped)")

print("[*] Starting VM...")
op = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
hr = CC.HcsStartComputeSystem(cs, op, ctypes.c_wchar_p(None))
_hcs_check(hr, "HcsStartComputeSystem")
_hcs_check(CC.HcsWaitForOperationResult(op, 30000, ctypes.byref(result)), "Wait Start")
CC.HcsCloseOperation(op)
print("[+] VM running")

# ── Serial pipe ───────────────────────────────────────────────────────────────
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
    print("[FAIL] Serial pipe never appeared — kernel may not have booted")
    CC.HcsTerminateComputeSystem(cs, ctypes.c_void_p(CC.HcsCreateOperation(None, None)), ctypes.c_wchar_p(None))
    sys.exit(1)
print("[+] Serial pipe connected")

# ── Read boot messages (background thread avoids blocking deadline check) ────
print("[*] Waiting for [chimera] boot messages (180s timeout)...")
got_fb0 = got_input = got_display = False
pipe_q: queue.Queue = queue.Queue()

def _pipe_reader(fh, q):
    try:
        while True:
            chunk = fh.read(4096)
            if chunk:
                q.put(chunk)
    except (OSError, ValueError):
        pass

reader = threading.Thread(target=_pipe_reader, args=(pipe_fh, pipe_q), daemon=True)
reader.start()

deadline = time.time() + 180
buf = b""
while time.time() < deadline:
    if got_fb0 and got_input and got_display:
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
            if "/dev/fb0 ready"        in txt: got_fb0     = True
            if "Input relay started"   in txt: got_input   = True
            if "Display relay started" in txt: got_display = True

pipe_fh.close()

# ── Results ───────────────────────────────────────────────────────────────────
print()
print("=" * 50)
print(f"  [{'PASS' if got_fb0     else 'FAIL'}] /dev/fb0 ready (hyperv_drm built-in)")
print(f"  [{'PASS' if got_input   else 'FAIL'}] Input relay connected (vsock port 16)")
print(f"  [{'PASS' if got_display else 'FAIL'}] Display relay connected (vsock port 17)")
passed = sum([got_fb0, got_input, got_display])
print(f"  {passed}/3 checks passed")
print("=" * 50)

# Cleanup
print("[*] Terminating VM...")
op = ctypes.c_void_p(CC.HcsCreateOperation(None, None))
CC.HcsTerminateComputeSystem(cs, op, ctypes.c_wchar_p(None))
CC.HcsWaitForOperationResult(op, 10000, ctypes.byref(result))
CC.HcsCloseOperation(op)
CC.HcsCloseComputeSystem(cs)
print("[*] Done.")
sys.exit(0 if passed == 3 else 1)
