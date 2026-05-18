param([int]$TimeoutSec = 30)
$base = "D:\Workspace_cloud\Personal_Project\chimera"
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$serial = "$base\qemu-vnc-test.log"
Remove-Item $serial -ErrorAction SilentlyContinue

$cmdline = "console=ttyS0,115200n8 earlycon=uart8250,io,0x3f8,115200 loglevel=8 ignore_loglevel panic=30 init=/init androidboot.selinux=permissive androidboot.hardware=cutf_cvm androidboot.lcd_density=240 androidboot.opengles.version=131072"

$argStr = "-accel whpx,kernel-irqchip=off -machine q35 -smp 4 -m 4096" +
          " -vga none" +
          " -device virtio-gpu-pci,xres=1280,yres=720" +
          " -kernel `"$base\out\android-kernel\bzImage`"" +
          " -initrd `"$base\out\cuttlefish\initrd-qemu.img`"" +
          " -append `"$cmdline`"" +
          " -device virtio-scsi-pci,id=scsi0" +
          " -drive file=`"$base\out\cuttlefish\system.vhdx`",if=none,id=drv-sda,format=vhdx,readonly=on" +
          " -device scsi-hd,bus=scsi0.0,drive=drv-sda" +
          " -drive file=`"$base\out\cuttlefish\vendor.vhdx`",if=none,id=drv-sdb,format=vhdx,readonly=on" +
          " -device scsi-hd,bus=scsi0.0,drive=drv-sdb" +
          " -drive file=`"$base\out\cuttlefish\userdata.qcow2`",if=none,id=drv-sdc,format=qcow2,cache=writethrough" +
          " -device scsi-hd,bus=scsi0.0,drive=drv-sdc" +
          " -drive file=`"$base\out\cuttlefish\metadata.vhdx`",if=none,id=drv-sdd,format=vhdx,cache=writethrough" +
          " -device scsi-hd,bus=scsi0.0,drive=drv-sdd" +
          " -netdev user,id=net0,hostfwd=tcp:127.0.0.1:5561-:5555" +
          " -device virtio-net-pci,netdev=net0" +
          " -serial file:`"$serial`"" +
          " -qmp tcp:127.0.0.1:4449,server=on,wait=off" +
          " -vnc 127.0.0.1:1"  # port 5901

Write-Host "[vnc-test] Starting QEMU..."
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $qemu
$psi.Arguments = $argStr
$psi.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Host "[vnc-test] QEMU PID: $($proc.Id)"

# Wait for fb-render to run (should happen within 10s)
$captured = $false
for ($i = 1; $i -le $TimeoutSec; $i++) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) { Write-Host "[vnc-test] QEMU exited at t=$i"; break }

    # Check serial for fb-render completion
    if (Test-Path $serial) {
        $lines = Get-Content $serial -ErrorAction SilentlyContinue
        $fbDone = $lines | Where-Object { $_ -match "fb-render.*done|color bars drawn" }
        if ($fbDone) {
            Write-Host "[vnc-test] t=$i fb-render completed:"
            $fbDone | ForEach-Object { Write-Host "  $_" }

            # Capture VNC screenshot using qemu-img
            Start-Sleep -Milliseconds 500
            Write-Host "[vnc-test] Capturing VNC screenshot..."

            # Use QMP screendump
            $qmpScript = @"
{"execute": "qmp_capabilities"}
{"execute": "screendump", "arguments": {"filename": "$($base.Replace('\','\\'))\\qemu_vnc_test.ppm"}}
{"execute": "quit"}
"@
            try {
                $tcp = New-Object System.Net.Sockets.TcpClient("127.0.0.1", 4449)
                $stream = $tcp.GetStream()
                Start-Sleep -Milliseconds 200
                $reader = New-Object System.IO.StreamReader($stream)
                $writer = New-Object System.IO.StreamWriter($stream)
                $writer.AutoFlush = $true

                # Read greeting
                Start-Sleep -Milliseconds 100
                $greeting = ""
                try { while ($stream.DataAvailable) { $greeting += [char]$stream.ReadByte() } } catch {}

                # Send commands
                $writer.WriteLine('{"execute": "qmp_capabilities"}')
                Start-Sleep -Milliseconds 200
                $writer.WriteLine('{"execute": "screendump", "arguments": {"filename": "' + "$($base.Replace('\','\\\\'))\\\\qemu_vnc_test.ppm" + '"}}')
                Start-Sleep -Milliseconds 500

                $resp = ""
                try { while ($stream.DataAvailable) { $resp += [char]$stream.ReadByte() } } catch {}
                Write-Host "[vnc-test] QMP response: $resp"
                $tcp.Close()
                $captured = $true
            } catch {
                Write-Host "[vnc-test] QMP error: $_"
            }
            break
        }
    }

    $serialCount = if (Test-Path $serial) { (Get-Content $serial -ErrorAction SilentlyContinue).Count } else { 0 }
    Write-Host "[vnc-test] t=${i}s serial=$serialCount qemu=$(if ($proc.HasExited) {'exited'} else {'running'})"
}

Write-Host "[vnc-test] Stopping QEMU..."
if (-not $proc.HasExited) { $proc.Kill() }

# Analyze screenshot if captured
$ppm = "$base\qemu_vnc_test.ppm"
if (Test-Path $ppm) {
    $size = (Get-Item $ppm).Length
    Write-Host "[vnc-test] Screenshot: $ppm ($size bytes)"
    # Count non-black pixels in PPM
    $bytes = [System.IO.File]::ReadAllBytes($ppm)
    $nonzero = ($bytes | Where-Object { $_ -ne 0 }).Count
    $total = $bytes.Length
    Write-Host "[vnc-test] Non-zero bytes: $nonzero / $total ($([math]::Round($nonzero*100/$total, 1))%)"
    if ($nonzero -gt 1000) {
        Write-Host "[vnc-test] DISPLAY ACTIVE - VNC shows non-black content!"
    } else {
        Write-Host "[vnc-test] Display is black"
    }
} else {
    Write-Host "[vnc-test] No screenshot captured"
}
