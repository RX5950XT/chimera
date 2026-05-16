#!/usr/bin/env python3
"""
project-gpu-driver.py — Copy host GPU driver DLLs into a guest VHDX.

GPU-PV requires the host GPU driver's user-mode DLLs to be present inside
the guest at /vendor/lib64/ so the guest dxgkrnl driver can load them.
This script:
  1. Enumerates host GPUs via WMI / SetupAPI
  2. Finds the driver's DriverStore FileRepository path
  3. Copies the required DLLs into the mounted guest VHDX

Usage:
  python scripts/project-gpu-driver.py --vhdx path\\to\\userdata.vhdx
  python scripts/project-gpu-driver.py --dry-run   (list DLLs only)

Requirements: Windows 11, Hyper-V enabled, run as Administrator
"""
import argparse
import os
import sys
import shutil
import subprocess
import ctypes
from pathlib import Path

# ─────────────────────────────────────────────────────────────────────────────
# GPU driver DLL names that GPU-PV requires in the guest
# (matches what WSA/WSL2 projects into /vendor/lib64/)
# ─────────────────────────────────────────────────────────────────────────────
REQUIRED_DLLS = {
    "nvidia": [
        "nvwgf2umx.dll",      # NVIDIA D3D12 UMD
        "nvwgf2um.dll",
        "nvcuda.dll",
        "nvopencl.dll",
    ],
    "amd": [
        "amdxc64.dll",        # AMD D3D12 UMD
        "amdxc32.dll",
        "atidxx64.dll",
        "atidxx32.dll",
    ],
    "intel": [
        "igdrcl64.dll",       # Intel OpenCL
        "igd10iumd64.dll",    # Intel D3D10 UMD
        "igd12umd64.dll",     # Intel D3D12 UMD
        "intelocl64.dll",
    ],
}

DRIVER_STORE = Path(r"C:\Windows\System32\DriverStore\FileRepository")


def find_gpu_vendor() -> str:
    """Return 'nvidia', 'amd', or 'intel' based on installed display adapters."""
    try:
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command",
             "Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name"],
            text=True, stderr=subprocess.DEVNULL
        )
        name = out.lower()
        if "nvidia" in name:   return "nvidia"
        if "amd" in name or "radeon" in name: return "amd"
        if "intel" in name:    return "intel"
    except Exception as e:
        print(f"[WARN] WMI query failed: {e}")
    return "unknown"


def find_driver_store_path(vendor: str) -> Path | None:
    """Find the driver FileRepository folder that contains the UMD DLL."""
    patterns = {
        "nvidia": "nv*",
        "amd":    "u0*",   # AMD driver packages start with u0
        "intel":  "ki*",   # Intel GPU packages often start with ki or iigd
    }
    pattern = patterns.get(vendor, "*")
    candidates = sorted(DRIVER_STORE.glob(pattern), key=lambda p: p.stat().st_mtime, reverse=True)
    for folder in candidates:
        dll_names = {p.name.lower() for p in folder.glob("*.dll")}
        required = {d.lower() for d in REQUIRED_DLLS.get(vendor, [])}
        if required & dll_names:
            return folder
    # Fallback: search all packages
    for folder in sorted(DRIVER_STORE.iterdir(), key=lambda p: p.stat().st_mtime, reverse=True):
        dll_names = {p.name.lower() for p in folder.glob("*.dll")}
        required = {d.lower() for d in REQUIRED_DLLS.get(vendor, [])}
        if required & dll_names:
            return folder
    return None


def mount_vhdx(vhdx_path: Path) -> Path | None:
    """Mount VHDX using diskpart / PowerShell and return the mount point."""
    script = f"""
    $vhd = Mount-VHD -Path '{vhdx_path}' -Passthru -NoDriveLetter -ErrorAction Stop
    $disk = $vhd | Get-Disk
    $part = $disk | Get-Partition | Where-Object {{$_.Type -ne 'Reserved'}} | Select-Object -First 1
    $letter = [char](68..90 | Where-Object {{-not (Test-Path "$([char]$_):\\")}} | Select-Object -First 1)
    Add-PartitionAccessPath -DiskNumber $disk.Number -PartitionNumber $part.PartitionNumber `
        -AccessPath "$letter\:" -ErrorAction SilentlyContinue
    Write-Host $letter
    """
    try:
        result = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", script],
            text=True, stderr=subprocess.DEVNULL
        ).strip()
        if result:
            return Path(f"{result}:\\")
    except Exception as e:
        print(f"[ERROR] Mount-VHD failed: {e}")
    return None


def unmount_vhdx(vhdx_path: Path) -> None:
    subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         f"Dismount-VHD -Path '{vhdx_path}' -ErrorAction SilentlyContinue"],
        check=False
    )


def project_dlls(src_dir: Path, dest_dir: Path, vendor: str, dry_run: bool) -> int:
    """Copy required DLLs from src_dir into dest_dir. Returns count copied."""
    dest_dir.mkdir(parents=True, exist_ok=True)
    count = 0
    dlls = REQUIRED_DLLS.get(vendor, [])
    for dll_name in dlls:
        src = src_dir / dll_name
        if not src.exists():
            # Case-insensitive search
            matches = list(src_dir.glob(f"*{dll_name}*"))
            if not matches:
                print(f"  [SKIP] {dll_name} not found in driver store")
                continue
            src = matches[0]
        dst = dest_dir / dll_name
        print(f"  {'[DRY]' if dry_run else '[COPY]'} {src.name} -> {dst}")
        if not dry_run:
            shutil.copy2(src, dst)
        count += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description="Project GPU drivers into guest VHDX")
    parser.add_argument("--vhdx",    help="Path to userdata.vhdx")
    parser.add_argument("--dry-run", action="store_true", help="List DLLs without copying")
    parser.add_argument("--vendor",  choices=["nvidia", "amd", "intel"],
                        help="Override GPU vendor detection")
    args = parser.parse_args()

    if not args.dry_run and not args.vhdx:
        parser.error("--vhdx required unless --dry-run")

    # Check admin
    if not args.dry_run:
        try:
            if not ctypes.windll.shell32.IsUserAnAdmin():
                print("[ERROR] Must run as Administrator to mount VHDX")
                return 1
        except AttributeError:
            pass  # non-Windows CI

    vendor = args.vendor or find_gpu_vendor()
    print(f"GPU vendor: {vendor}")

    driver_path = find_driver_store_path(vendor)
    if not driver_path:
        print(f"[ERROR] No matching driver store package found for {vendor}")
        return 1
    print(f"Driver store: {driver_path}")

    if args.dry_run:
        print("\nDLLs to project:")
        project_dlls(driver_path, Path("/dev/null"), vendor, dry_run=True)
        return 0

    vhdx = Path(args.vhdx)
    if not vhdx.exists():
        print(f"[ERROR] VHDX not found: {vhdx}")
        return 1

    print(f"Mounting {vhdx}...")
    mount_point = mount_vhdx(vhdx)
    if not mount_point:
        print("[ERROR] Failed to mount VHDX — ensure Hyper-V is enabled and run as Admin")
        return 1

    try:
        dest = mount_point / "vendor" / "lib64" / "gpu"
        print(f"Projecting to {dest}")
        n = project_dlls(driver_path, dest, vendor, dry_run=False)
        print(f"\nCopied {n} DLL(s) to guest VHDX")
    finally:
        print(f"Unmounting {vhdx}...")
        unmount_vhdx(vhdx)

    print("Done. GPU-PV driver projection complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
