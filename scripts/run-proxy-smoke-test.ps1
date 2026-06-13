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
    foreach ($port in @(5554, 5555, 8554)) {
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
    "-ports", "5554,5555",
    "-grpc", "8554",
    "-idle-grpc-timeout", "300"
)

Write-Host "Starting emulator headless with proxy DLL..."
$emulatorProc = $null

try {
    # Use Start-Process for reliable non-interactive launch
    $spArgs = @{
        FilePath = $EmulatorExe
        ArgumentList = $emulatorArgs
        WorkingDirectory = $proxyDir
        PassThru = $true
        WindowStyle = "Hidden"
    }
    $emulatorProc = Start-Process @spArgs
    if ($null -eq $emulatorProc) {
        Write-Host "FATAL: Failed to start emulator process"
        exit 1
    }
    Write-Host "emulator_pid=$($emulatorProc.Id)"

    # Lower process priority to minimize host interference
    try {
        $emulatorProc.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
    } catch {
        Write-Warning "Could not lower emulator priority: $_"
    }

    # Wait for ADB to see the device first (up to 60s)
    Write-Host "Waiting for ADB device (timeout=60s)..."
    $adbSerial = "emulator-5554"
    $adbConnected = $false
    $adbDeadline = (Get-Date).AddSeconds(60)

    while ((Get-Date) -lt $adbDeadline) {
        if ($emulatorProc.HasExited) {
            Write-Host "Emulator exited early (code=$($emulatorProc.ExitCode)) — analyzing partial log"
            break
        }
        $devices = & $Adb devices 2>&1
        if ($devices -match "$adbSerial\s+device") {
            $adbConnected = $true
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

    # Restore PATH and clean env
    $env:PATH = $origPath
    $env:CHIMERA_GFXSTREAM_PROXY_LOG = ""
    $env:CHIMERA_GFXSTREAM_PROXY_ADD_FRAME_LISTENER = ""
    $env:CHIMERA_GFXSTREAM_PROXY_HOOK_RENDERER_VTABLE = ""
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
