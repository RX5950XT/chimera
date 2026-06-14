[CmdletBinding()]
param(
    [string]$ProxyRuntimeDir = "",
    [string]$LogPath = "",
    [int]$BootTimeoutSec = 180,
    [int]$FrameCollectionSec = 20,
    [string]$GpuMode = "host",
    [switch]$EnableFrameListener,
    [switch]$EnableVtableHook,
    [switch]$SkipAnalysis
)

$ErrorActionPreference = "Continue"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($ProxyRuntimeDir)) {
    $ProxyRuntimeDir = Join-Path $RepoRoot "build\chimera-gfxstream-proxy-runtime"
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $env:TEMP "chimera-proxy-smoke-$(Get-Date -Format 'yyyyMMdd-HHmmss').log"
}

$EmulatorExe = Join-Path $ProxyRuntimeDir "emulator.exe"
$AvdHome = Join-Path $RepoRoot "third_party\android-avd"
$SdkRoot = Join-Path $RepoRoot "third_party\android-sdk"
$Adb = Join-Path $SdkRoot "platform-tools\adb.exe"
$ProxyDll = Join-Path $ProxyRuntimeDir "lib64\libgfxstream_backend.dll"
$StockDll = Join-Path $ProxyRuntimeDir "lib64\libgfxstream_backend_stock.dll"
$AnalyzeScript = Join-Path $RepoRoot "scripts\analyze-gfxstream-proxy-log.ps1"

foreach ($f in @($EmulatorExe, $Adb, $ProxyDll, $StockDll, $AnalyzeScript)) {
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) {
        Write-Host "FATAL: Required file not found: $f"
        exit 1
    }
}

# Check ports via netstat (avoid NetworkInformation issues in non-interactive)
function Get-UsedPorts {
    $result = @()
    $netstat = netstat -ano 2>$null | Select-String "LISTENING"
    foreach ($port in @(5564, 5565, 8564)) {
        $pattern = ":$port\s"
        if ($netstat -match $pattern) { $result += $port }
    }
    return $result
}
$usedPorts = @(Get-UsedPorts)
if ($usedPorts.Count -gt 0) {
    Write-Host "FATAL: Ports already in use: $($usedPorts -join ',') — kill stale emulator/chimera processes first"
    exit 1
}

# Remove stale AVD lock files (they can be files OR directories)
$avdDir = Join-Path $AvdHome "chimera_dev.avd"
$lockFiles = @(Get-ChildItem -Path $avdDir -Filter "*.lock" -ErrorAction SilentlyContinue)
foreach ($lf in $lockFiles) {
    Write-Host "Removing stale AVD lock: $($lf.FullName)"
    try {
        if ($lf.PSIsContainer) {
            Remove-Item -LiteralPath $lf.FullName -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            Remove-Item -LiteralPath $lf.FullName -Force -ErrorAction SilentlyContinue
        }
    } catch {
        Write-Host "Could not remove lock (continuing): $($lf.FullName)"
    }
}

Write-Host "=== Chimera Proxy Smoke Test ==="
Write-Host "proxyRuntime=$ProxyRuntimeDir"
Write-Host "logPath=$LogPath"
Write-Host "gpuMode=$GpuMode frameListener=$EnableFrameListener vtableHook=$EnableVtableHook"
Write-Host ""

# Prepend lib64 and emulator dir to PATH
$proxyDir = (Resolve-Path -LiteralPath $ProxyRuntimeDir).Path
$lib64Dir = Join-Path $proxyDir "lib64"
$libDir = Join-Path $proxyDir "lib"
$origPath = $env:PATH
$env:PATH = "$lib64Dir;$libDir;$proxyDir;$origPath"

# Set environment for proxy DLL
$env:ANDROID_SDK_ROOT = $SdkRoot
$env:ANDROID_AVD_HOME = $AvdHome
$env:ANDROID_EMULATOR_HOME = $AvdHome
$env:CHIMERA_GFXSTREAM_PROXY_LOG = $LogPath
$env:CHIMERA_GFXSTREAM_PROXY_ADD_FRAME_LISTENER = if ($EnableFrameListener) { "1" } else { "0" }
$env:CHIMERA_GFXSTREAM_PROXY_HOOK_RENDERER_VTABLE = if ($EnableVtableHook) { "1" } else { "0" }
$env:CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB = "0"
$env:CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER = "0"

# D3D11 shared texture producer: provide named objects so chimera_proxy_try_publish_d3d11_frame
# knows where to publish the shared texture metadata and frame-ready event.
$proxyTestSuffix = "ProxySmoke_$(Get-Date -Format 'HHmmss')"
$env:CHIMERA_D3D11_TEXTURE_METADATA = "Local\ChimeraProxyD3D11Meta_$proxyTestSuffix"
$env:CHIMERA_D3D11_TEXTURE_NAME     = "Local\ChimeraProxyD3D11Tex_$proxyTestSuffix"
$env:CHIMERA_D3D11_TEXTURE_EVENT    = "Local\ChimeraProxyD3D11Evt_$proxyTestSuffix"
Write-Host "d3d11_meta=$($env:CHIMERA_D3D11_TEXTURE_METADATA)"

$emulatorArgs = @(
    "-avd", "chimera_dev",
    "-no-window",
    "-accel", "on",
    "-gpu", $GpuMode,
    "-memory", "2048",
    "-cores", "2",
    "-no-skin",
    "-window-size", "1920x1080",
    "-fixed-scale",
    "-vsync-rate", "60",
    "-netfast",
    "-no-snapstorage",
    "-no-snapshot",
    "-no-snapshot-load",
    "-no-snapshot-save",
    "-no-boot-anim",
    "-no-audio",
    "-no-metrics",
    "-crash-report-mode", "never",
    "-ports", "5564,5565",
    "-grpc", "8564",
    "-idle-grpc-timeout", "300"
)

Write-Host "Starting emulator headless with proxy DLL..."
$emulatorProc = $null

try {
    # Redirect emulator stdout/stderr to files so pipe buffers never block
    # (blocked stdout in non-interactive PowerShell can cause gRPC server to stall)
    $emulatorStdout = Join-Path $env:TEMP "chimera-emu-out-$PID.log"
    $emulatorStderr = Join-Path $env:TEMP "chimera-emu-err-$PID.log"
    $spArgs = @{
        FilePath = $EmulatorExe
        ArgumentList = $emulatorArgs
        WorkingDirectory = $proxyDir
        PassThru = $true
        WindowStyle = "Hidden"
        RedirectStandardOutput = $emulatorStdout
        RedirectStandardError  = $emulatorStderr
    }
    $emulatorProc = Start-Process @spArgs
    if ($null -eq $emulatorProc) {
        Write-Host "FATAL: Failed to start emulator process"
        exit 1
    }
    Write-Host "emulator_pid=$($emulatorProc.Id)"

    # Note: do NOT lower emulator priority in smoke test — BelowNormal starves CPU readback
    # causing gRPC getScreenshot to time out (>30s). The smoke test has no audio to protect.

    # Wait for ADB to see the device first (up to 60s)
    # Use adb connect for custom ports: ADB auto-scan covers 5554-5584; 5564 is in range
    # but explicit connect ensures discovery when ADB server missed the window.
    Write-Host "Waiting for ADB device (timeout=60s)..."
    $adbSerial = "emulator-5564"
    $adbConnected = $false
    $adbDeadline = (Get-Date).AddSeconds(60)

    while ((Get-Date) -lt $adbDeadline) {
        if ($emulatorProc.HasExited) {
            Write-Host "Emulator exited early (code=$($emulatorProc.ExitCode)) — analyzing partial log"
            break
        }
        # Attempt explicit connect (harmless if already connected)
        & $Adb connect "127.0.0.1:5565" 2>$null | Out-Null
        $devices = & $Adb devices 2>&1
        if ($devices -match "$adbSerial\s+device" -or $devices -match "127.0.0.1:5565\s+device") {
            $adbConnected = $true
            $adbSerial = if ($devices -match "127.0.0.1:5565\s+device") { "127.0.0.1:5565" } else { $adbSerial }
            Write-Host "adb_device=$adbSerial connected at $(Get-Date -Format 'HH:mm:ss')"
            break
        }
        Start-Sleep -Seconds 3
    }

    # Wait for boot_completed
    Write-Host "Waiting for boot_completed (timeout=${BootTimeoutSec}s)..."
    $bootCompleted = $false
    $bootDeadline = (Get-Date).AddSeconds($BootTimeoutSec)

    while ((Get-Date) -lt $bootDeadline) {
        if ($emulatorProc.HasExited) {
            Write-Host "Emulator exited (code=$($emulatorProc.ExitCode)) before boot"
            break
        }
        $bootProp = & $Adb -s $adbSerial shell getprop sys.boot_completed 2>&1
        if ($bootProp -match "^1") {
            $bootCompleted = $true
            Write-Host "boot_completed=1 at $(Get-Date -Format 'HH:mm:ss')"
            break
        }
        Start-Sleep -Seconds 5
    }

    if (-not $bootCompleted) {
        Write-Host "Boot did not complete in ${BootTimeoutSec}s — partial log will be analyzed"
    } else {
        # Drive getScreenshot via gRPC (HTTP/2 binary) to trigger hooked_renderer_get_screenshot.
        # Binary gRPC frame: [0x00][4-byte BE length][protobuf payload]
        # ImageFormat proto: field1(format=RGBA8888=1), field3(width=1920), field4(height=1080)
        #   08 01  18 80 0F  20 B8 08
        Write-Host "Driving gRPC getScreenshot to trigger vtable hook..."
        # Use pwsh (PowerShell 7 / .NET 5+) for HTTP/2 binary gRPC.
        # Write the caller script to a temp file to avoid quoting issues.
        # Use Python grpcio for reliable h2c gRPC (httpx/pwsh both reject h2c plaintext)
        $grpcHelperScript = Join-Path $env:TEMP "chimera_grpc_helper_$PID.py"
        Set-Content -LiteralPath $grpcHelperScript -Encoding UTF8 -Value @(
            'import sys, time, socket, grpc',
            'port = 8564',
            '# TCP probe',
            'try:',
            '    s = socket.create_connection(("127.0.0.1", port), timeout=3)',
            '    s.close(); print("port8564=open")',
            'except Exception as e:',
            '    print(f"port8564=closed:{e}"); sys.exit(1)',
            '# Build ImageFormat proto: format=RGBA8888(1), width=1920, height=1080',
            'def varint(n):',
            '    b = b""',
            '    while n >= 0x80:',
            '        b += bytes([(n & 0x7F) | 0x80]); n >>= 7',
            '    return b + bytes([n])',
            'def field(num, val): return varint((num << 3)) + varint(val)',
            'payload = field(1, 1) + field(3, 1920) + field(4, 1080)',
            '# Call via grpc insecure channel (h2c native), with retries',
            'last_err = None',
            'for attempt in range(5):',
            '    if attempt > 0: time.sleep(2)',
            '    try:',
            '        ch = grpc.insecure_channel("localhost:8564", options=[',
            '            ("grpc.max_receive_message_length", 64*1024*1024),',
            '            ("grpc.max_send_message_length", 64*1024*1024)])',
            '        stub = ch.unary_unary(',
            '            "/android.emulation.control.EmulatorController/getScreenshot",',
            '            request_serializer=lambda x: x,',
            '            response_deserializer=lambda x: x)',
            '        resp = stub(payload, timeout=30)',
            '        print(f"grpc_ok attempt={attempt} bytes={len(resp)}")',
            '        ch.close()',
            '        last_err = None',
            '        break',
            '    except grpc.RpcError as e:',
            '        last_err = f"attempt={attempt} {e.code().name}:{e.details()[:80]}"',
            '        print(f"grpc_retry {last_err}")',
            '        try: ch.close()',
            '        except: pass',
            '    except Exception as e:',
            '        last_err = f"attempt={attempt} {type(e).__name__}:{str(e)[:80]}"',
            '        print(f"grpc_retry {last_err}")',
            '        try: ch.close()',
            '        except: pass',
            'if last_err: print(f"grpc_fail_final:{last_err}")'
        )
        try {
            $result = python $grpcHelperScript 2>&1
            Write-Host "grpc_getScreenshot=$($result -join ' ')"
        } finally {
            Remove-Item $grpcHelperScript -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Seconds 2  # allow proxy log to flush

        Write-Host "Collecting frame signals for ${FrameCollectionSec}s..."
        $frameDeadline = (Get-Date).AddSeconds($FrameCollectionSec)
        while ((Get-Date) -lt $frameDeadline) {
            if ($emulatorProc.HasExited) { break }
            Start-Sleep -Seconds 2
        }
        Write-Host "Collection complete."
    }

} finally {
    # Kill emulator and all children
    if ($null -ne $emulatorProc -and -not $emulatorProc.HasExited) {
        Write-Host "Killing emulator PID=$($emulatorProc.Id)..."
        try { & taskkill /F /T /PID $emulatorProc.Id 2>$null | Out-Null } catch {}
        try { $emulatorProc.Kill() } catch {}
    }

    # Wait for processes to die
    Start-Sleep -Seconds 4

    # Verify no residual emulator/qemu processes
    $residuals = @(Get-Process -Name "emulator","qemu-system*" -ErrorAction SilentlyContinue)
    if ($residuals.Count -gt 0) {
        Write-Host "WARNING: Residual processes found:"
        foreach ($rp in $residuals) {
            Write-Host "  PID=$($rp.Id) Name=$($rp.ProcessName)"
            try { $rp.Kill() } catch {}
        }
    } else {
        Write-Host "no_residual_processes=OK"
    }

    # Show last lines of emulator stderr for diagnostics (always show, not just on success)
    if (Test-Path -LiteralPath $emulatorStderr -PathType Leaf) {
        $emuLog = Get-Content -LiteralPath $emulatorStderr -Tail 20 -ErrorAction SilentlyContinue
        if ($emuLog) {
            Write-Host "=== emulator_stderr_tail ==="
            $emuLog | ForEach-Object { Write-Host "  $_" }
        } else {
            Write-Host "emulator_stderr=empty"
        }
        Remove-Item $emulatorStderr -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host "emulator_stderr=not_found path=$emulatorStderr"
    }
    if (Test-Path -LiteralPath $emulatorStdout -PathType Leaf) {
        $outLog = Get-Content -LiteralPath $emulatorStdout -Tail 60 -ErrorAction SilentlyContinue
        if ($outLog) { Write-Host "=== emulator_stdout_tail ==="; $outLog | ForEach-Object { Write-Host "  $_" } }
        Remove-Item $emulatorStdout -Force -ErrorAction SilentlyContinue
    }

    # Restore PATH and clean env
    $env:PATH = $origPath
    $env:CHIMERA_GFXSTREAM_PROXY_LOG = ""
    $env:CHIMERA_GFXSTREAM_PROXY_ADD_FRAME_LISTENER = ""
    $env:CHIMERA_GFXSTREAM_PROXY_HOOK_RENDERER_VTABLE = ""
    $env:CHIMERA_D3D11_TEXTURE_METADATA = ""
    $env:CHIMERA_D3D11_TEXTURE_NAME = ""
    $env:CHIMERA_D3D11_TEXTURE_EVENT = ""
}

Write-Host ""
Write-Host "=== Proxy Log Summary ==="
Write-Host "logPath=$LogPath"

if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
    $logContent = Get-Content -LiteralPath $LogPath -Raw -ErrorAction SilentlyContinue
    if ($logContent) {
        $logLines = ($logContent -split "`n").Count
        Write-Host "logSize=$($logContent.Length) logLines=$logLines"
    } else {
        Write-Host "WARNING: Proxy log file is empty — proxy DLL may not have written to it"
    }

    if (-not $SkipAnalysis) {
        Write-Host ""
        Write-Host "=== Analysis ==="
        try {
            & $AnalyzeScript -LogPath $LogPath
            Write-Host "analysis=PASS"
        } catch {
            Write-Host "analysis=FAIL: $_"
        }
    }
} else {
    Write-Host "WARNING: No proxy log was written at: $LogPath"
    Write-Host "Check that CHIMERA_GFXSTREAM_PROXY_LOG is read by the proxy DLL"
}
