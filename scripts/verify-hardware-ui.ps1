<#
.SYNOPSIS
    Verify whether the custom gfxstream runtime renders the NORMAL composited
    Android UI on hardware (host GLES via ANGLE/D3D11) instead of a black screen.

    STATUS (Session 88): ANGLE host-GLES can initialize and removes the
    SwiftShader "invalid version directive" shader error, but SurfaceFlinger
    later triggers an ANGLE libGLESv2.dll access violation during glDrawArrays
    (program 28/31; newer ANGLE reproduces it too). This verifier is therefore
    an ANGLE R&D harness, not a production pass gate.

.DESCRIPTION
    Boots chimera-ui with the custom gfxstream runtime and the experimental
    ANGLE host-GLES gate. The currently-supported production fix for normal UI
    is CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES (wired by start-chimera.ps1
    -Fast); this script intentionally tests the separate ANGLE/D3D11 compositor
    path so future R&D can tell whether the libGLESv2 draw crash is fixed.

    PASS criteria:
      - Android reaches boot_completed
      - host GLES adapter reports ANGLE (not SwiftShader)
      - zero "invalid version directive" shader compile errors
      - the composited home screenshot is non-black (>= MinHomeBytes)
#>
[CmdletBinding()]
param(
    [int]$BootTimeoutSec = 240,
    [int]$MinHomeBytes = 30000,
    [string]$Configuration = "Release",
    # Isolate ANGLE host GLES from the shared-texture Vulkan bridge: use the
    # stock gRPC display path on the custom runtime so we can tell whether ANGLE
    # hangs by itself or only when the D3D11 bridge is also active.
    [switch]$GrpcDisplay
)

$ErrorActionPreference = "Continue"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$AppExe   = Join-Path $RepoRoot "build\$Configuration\chimera-ui.exe"
$Adb      = Join-Path $RepoRoot "third_party\android-sdk\platform-tools\adb.exe"
$QtBin    = "C:\Qt\6.8.3\msvc2022_64\bin"
$Runtime  = Join-Path $RepoRoot "build\chimera-gfxstream-runtime\emulator.exe"
$AvdName  = "chimera_dev"
$Serial   = "emulator-5554"

$tmp = Join-Path $RepoRoot "tmp"
if (-not (Test-Path $tmp)) { New-Item -ItemType Directory -Path $tmp | Out-Null }
$msgLog    = Join-Path $tmp "verify-hardware-ui.msg.log"
$stdoutLog = Join-Path $tmp "verify-hardware-ui.out.log"
$stderrLog = Join-Path $tmp "verify-hardware-ui.err.log"
$shotPath  = Join-Path $tmp "verify-hardware-ui-home.png"
foreach ($f in @($msgLog, $stdoutLog, $stderrLog, $shotPath)) { if (Test-Path $f) { Remove-Item $f -Force } }

if (-not (Test-Path -LiteralPath $AppExe))  { throw "chimera-ui.exe not found: $AppExe" }
if (-not (Test-Path -LiteralPath $Runtime)) { throw "custom runtime not found: $Runtime" }
if (Test-Path -LiteralPath $QtBin) { $env:PATH = "$QtBin;$env:PATH" }

function Adb { param([string[]]$A) $o = & $Adb @A 2>$null; return (($o | Out-String).Trim()) }
function Kill-All {
    Get-Process -Name chimera-ui, emulator, qemu-system* -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

$savedEnv = @{
    CHIMERA_GFXSTREAM_HEADLESS_ANGLE = $env:CHIMERA_GFXSTREAM_HEADLESS_ANGLE
    CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES = $env:CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES
    CHIMERA_EMULATOR_PATH = $env:CHIMERA_EMULATOR_PATH
    CHIMERA_LOG_PATH = $env:CHIMERA_LOG_PATH
    CHIMERA_QUICK_BOOT = $env:CHIMERA_QUICK_BOOT
    CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE
    CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE
    CHIMERA_GPU_MODE = $env:CHIMERA_GPU_MODE
    CHIMERA_EMULATOR_CONSOLE_PORT = $env:CHIMERA_EMULATOR_CONSOLE_PORT
}
function Restore-Env {
    foreach ($key in $script:savedEnv.Keys) {
        if ($script:savedEnv[$key] -eq $null) { Remove-Item "Env:\$key" -ErrorAction SilentlyContinue }
        else { Set-Item "Env:\$key" $script:savedEnv[$key] }
    }
}

# --- env: custom gfxstream runtime + ANGLE host-GLES fallback (gated patch in
# emugl_config.cpp). The emulator child inherits this.
$env:CHIMERA_GFXSTREAM_HEADLESS_ANGLE = "1"
Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES -ErrorAction SilentlyContinue
$env:CHIMERA_EMULATOR_PATH = (Resolve-Path -LiteralPath $Runtime).Path
$env:CHIMERA_LOG_PATH = $msgLog
$env:CHIMERA_QUICK_BOOT = "0"
$runtimeArg = "--gfxstream-shared-texture"
if ($GrpcDisplay) {
    # ANGLE in isolation: stock gRPC display, no shared-texture Vulkan bridge.
    Remove-Item Env:\CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
    $runtimeArg = ""
} else {
    $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = "1"
    $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = "1"
}
Remove-Item Env:\CHIMERA_GPU_MODE -ErrorAction SilentlyContinue   # rely on the emugl_config fix
Remove-Item Env:\CHIMERA_EMULATOR_CONSOLE_PORT -ErrorAction SilentlyContinue

$booted = $false
$homeBytes = 0
$adapter = "(unknown)"
$shaderErrs = -1
$borrowOk = 0
$postDirect = 0

try {
    Write-Host "verify-hardware-ui: cleanup pre"
    Kill-All; Start-Sleep -Seconds 2

    Write-Host "verify-hardware-ui: launch custom runtime (ANGLE host GLES; GrpcDisplay=$GrpcDisplay)"
    $appArgs = if ([string]::IsNullOrWhiteSpace($runtimeArg)) { "" } else { " $runtimeArg" }
    $cmdLine = "`"$AppExe`"$appArgs 1>`"$stdoutLog`" 2>`"$stderrLog`""
    $proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/d /s /c `"$cmdLine`"" `
        -WorkingDirectory $RepoRoot -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    if ($proc.HasExited) {
        throw "chimera-ui exited early (code $($proc.ExitCode)): $(Get-Content $stderrLog -Raw -ErrorAction SilentlyContinue)"
    }

    $deadline = (Get-Date).AddSeconds($BootTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { Write-Host "ui_exited_early code=$($proc.ExitCode)"; break }
        if ((Adb @("-s", $Serial, "get-state")) -eq "device") {
            if ((Adb @("-s", $Serial, "shell", "getprop", "sys.boot_completed")) -eq "1") { $booted = $true; break }
        }
        Start-Sleep -Seconds 3
    }
    Write-Host "booted=$booted"

    if ($booted) {
        Start-Sleep -Seconds 6
        Adb @("-s", $Serial, "shell", "input", "keyevent", "224") | Out-Null   # WAKEUP
        Adb @("-s", $Serial, "shell", "wm", "dismiss-keyguard") | Out-Null
        Adb @("-s", $Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") | Out-Null
        Start-Sleep -Seconds 6
        Adb @("-s", $Serial, "shell", "screencap", "-p", "/sdcard/chimera_home.png") | Out-Null
        Adb @("-s", $Serial, "pull", "/sdcard/chimera_home.png", $shotPath) | Out-Null
        if (Test-Path $shotPath) { $homeBytes = (Get-Item $shotPath).Length }
    }
}
finally {
    Write-Host "verify-hardware-ui: cleanup post"
    Kill-All; Start-Sleep -Seconds 3
    $remain = @(Get-Process -Name chimera-ui, emulator, qemu-system* -ErrorAction SilentlyContinue).Count

    # --- log analysis (emulator stdout carries the gfxstream host GL adapter + shaders)
    $logText = ""
    foreach ($f in @($stdoutLog, $stderrLog, $msgLog)) {
        if (Test-Path $f) { $logText += (Get-Content $f -Raw -ErrorAction SilentlyContinue) + "`n" }
    }
    # The host GLES translator line is specific: "...OpenGL ES Translator (Google <X>)".
    # Match that, not a bare "SwiftShader" (which also names the Vulkan ICD DLL).
    if ($logText -match "Translator \(Google ANGLE\)" -or $logText -match "ANGLE \(") { $adapter = "ANGLE" }
    elseif ($logText -match "Translator \(Google SwiftShader\)") { $adapter = "SwiftShader-translator" }
    $shaderErrs = ([regex]::Matches($logText, "invalid version directive")).Count
    $borrowOk  = ([regex]::Matches($logText, "borrowForDisplay\(kVk\).*success=1")).Count
    $postDirect = ([regex]::Matches($logText, "postFrameDirectGpu")).Count

    Write-Host "residual_processes=$remain"
    Write-Host "home_bytes=$homeBytes"
    Write-Host "host_gles_adapter=$adapter"
    Write-Host "shader_version_errors=$shaderErrs"
    Write-Host "borrowForDisplay_kvk_ok=$borrowOk"
    Write-Host "postFrameDirectGpu=$postDirect"
    Restore-Env
}

if (-not $booted) { throw "FAIL: Android did not reach boot_completed" }
if ($adapter -ne "ANGLE") { throw "FAIL: host GLES adapter did not report ANGLE (actual: $adapter)" }
if ($shaderErrs -gt 0) { throw "FAIL: $shaderErrs compositor shader version errors (normal UI would be black)" }
if ($homeBytes -lt $MinHomeBytes) { throw "FAIL: home screenshot $homeBytes B < $MinHomeBytes B (likely black)" }
Write-Host "result=pass"
