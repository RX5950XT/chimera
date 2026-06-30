<#
.SYNOPSIS
    One-click Chimera launcher. Defaults to the stock SDK + gRPC display path,
    which renders the normal Android home/apps correctly. The experimental
    custom gfxstream "60fps" runtime is opt-in via -Fast.

.DESCRIPTION
    Double-click usage: run via start-chimera.cmd at the repo root, or
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1

    Default (via root start-chimera.cmd): custom gfxstream + GuestVulkan +
    interactive priority when available. Direct script usage without flags still
    defaults to stock SDK + gRPC for conservative diagnostics; pass -Fast for the
    production fast path.

    -Fast: custom gfxstream shared-texture runtime. Hits 1080p/60 for
    continuously-rendering content and uses a SwiftShader ES compositor path for
    normal Android UI so the home/apps remain visible.

.PARAMETER Fast
    Opt into the experimental custom gfxstream 60fps runtime (see caveat above).

.PARAMETER ConsolePort
    Android console base port. Defaults to 5554 (matches the host's adb serial
    wiring). The host honours this via CHIMERA_EMULATOR_CONSOLE_PORT, so a
    non-default port stays consistent end-to-end.

.PARAMETER Stock
    Force the stock SDK emulator + gRPC display path (this is also the default).

.PARAMETER RequireSharedTexture
    With -Fast, fail-closed: require the gfxstream shared-texture path (no gRPC
    fallback).

.PARAMETER SelfTest
    Boot headless, verify Android reaches an interactive 1920x1080 home,
    capture a screenshot, then shut down cleanly. Does not leave a UI open.

.PARAMETER AudioFirst
    Run the emulator at idle priority (EcoQoS) to protect host audio playback at
    the cost of interactive FPS. Sets CHIMERA_INTERACTIVE_PRIORITY=idle.

.PARAMETER InteractiveFirst
    Run the emulator at normal priority for the smoothest UI, accepting more host
    audio contention. Sets CHIMERA_INTERACTIVE_PRIORITY=normal.

.NOTES
    The default launch mode stays Stock (known-usable). Whether the daily default
    should flip to -Fast must be backed by scripts\verify-interactive-ui.ps1 data
    (Fast general UI is visible but composited via SwiftShader ES — not 60 FPS);
    that verifier is the authoritative daily-usability evidence, not the synthetic
    GL60 60fps proof in verify-true-1080p60.ps1.
#>
[CmdletBinding()]
param(
    [ValidateRange(5554, 5680)]
    [int]$ConsolePort = 5554,
    [switch]$Fast,
    [switch]$Stock,
    [switch]$RequireSharedTexture,
    [switch]$AudioFirst,
    [switch]$InteractiveFirst,
    [switch]$SelfTest,
    [ValidateRange(30, 600)]
    [int]$SelfTestBootSec = 200,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$AppExe   = Join-Path $RepoRoot "build\$Configuration\chimera-ui.exe"
$Adb      = Join-Path $RepoRoot "third_party\android-sdk\platform-tools\adb.exe"
$QtBin    = "C:\Qt\6.8.3\msvc2022_64\bin"
$Runtime  = Join-Path $RepoRoot "build\chimera-gfxstream-runtime"

if (-not (Test-Path -LiteralPath $AppExe -PathType Leaf)) {
    throw "chimera-ui.exe not found: $AppExe (build the project first)"
}
if (Test-Path -LiteralPath $QtBin -PathType Container) {
    $env:PATH = "$QtBin;$env:PATH"
}

# --- Display path selection ---------------------------------------------------
# Default = stock SDK + gRPC, which renders the normal Android home / apps
# correctly (verified). The custom gfxstream runtime is opt-in via -Fast: it
# keeps normal UI visible via SwiftShader ES composition while preserving the
# direct-VK shared-texture path for continuously-rendering content.
$customRequested = $Fast -and (-not $Stock)
$customAvailable =
    $customRequested -and
    (Test-Path -LiteralPath (Join-Path $Runtime "emulator.exe") -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $Runtime "lib64\libgfxstream_backend.dll") -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $Runtime "lib64\chimera-gfxstream-shared-texture.json") -PathType Leaf)

if ($customAvailable) {
    # CHIMERA_EMULATOR_PATH must be the emulator.exe file: the host derives the
    # runtime dir via parent_path() and execs this path directly. Pointing it at
    # the directory breaks both runtime detection and process launch.
    $env:CHIMERA_EMULATOR_PATH = Join-Path $Runtime "emulator.exe"
    $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = "1"
    # Keep the custom runtime's normal SurfaceFlinger UI on an ES shader path.
    # This avoids the headless HOST/core-profile shader mismatch while preserving
    # the direct-VK shared-texture path used by continuously-rendering content.
    $env:CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES = "1"
    # Route guest app HWUI through hardware Vulkan (skiavk on NVIDIA). chimera-ui
    # applies the skiavk runtime props + re-enables smooth animations after boot.
    # This is what makes general-UI scrolling/flinging feel ~60 instead of ~20.
    $env:CHIMERA_GUEST_VULKAN = "1"
    Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_ANGLE -ErrorAction SilentlyContinue
    if ($RequireSharedTexture) {
        $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = "1"
        Write-Host "display=gfxstream-shared-texture (-Fast, fail-closed; normal UI via GuestVulkan/skiavk)"
    } else {
        Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Write-Host "display=gfxstream-shared-texture (-Fast; normal UI via GuestVulkan/skiavk)"
    }
} else {
    Remove-Item Env:\CHIMERA_EMULATOR_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_ANGLE -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_GUEST_VULKAN -ErrorAction SilentlyContinue
    if ($customRequested) { Write-Host "display=stock-grpc (-Fast requested but custom runtime not found at $Runtime)" }
    else { Write-Host "display=stock-grpc (usable home/apps; use -Fast for the experimental 60fps runtime)" }
}

$env:CHIMERA_EMULATOR_CONSOLE_PORT = "$ConsolePort"
Remove-Item Env:\CHIMERA_QUICK_BOOT -ErrorAction SilentlyContinue  # default full boot

# --- Process-priority sugar (host audio <-> interactive FPS trade-off) --------
# These just export CHIMERA_INTERACTIVE_PRIORITY, which chimera-ui resolves.
# Default leaves it unset -> the built-in below_normal balance.
if ($AudioFirst -and $InteractiveFirst) {
    throw "-AudioFirst and -InteractiveFirst are mutually exclusive"
}
if ($AudioFirst) {
    $env:CHIMERA_INTERACTIVE_PRIORITY = "idle"
    Write-Host "priority=idle (-AudioFirst: protects host audio, lower interactive FPS)"
} elseif ($InteractiveFirst) {
    $env:CHIMERA_INTERACTIVE_PRIORITY = "normal"
    Write-Host "priority=normal (-InteractiveFirst: smoother UI, more host audio contention)"
} else {
    Remove-Item Env:\CHIMERA_INTERACTIVE_PRIORITY -ErrorAction SilentlyContinue
}

# --- Normal launch ------------------------------------------------------------
if (-not $SelfTest) {
    Write-Host "Launching Chimera (console port $ConsolePort)..."
    Start-Process -FilePath $AppExe -WorkingDirectory $RepoRoot | Out-Null
    Write-Host "Chimera started. Close its window to exit."
    return
}

# --- Self-test: boot, verify interactive home, screenshot, cleanup ------------
$Serial = "emulator-$ConsolePort"
$logPath = Join-Path $RepoRoot "tmp\start-chimera-selftest.log"
$shotPath = Join-Path $RepoRoot "tmp\start-chimera-home.png"
if (-not (Test-Path (Join-Path $RepoRoot "tmp"))) { New-Item -ItemType Directory -Path (Join-Path $RepoRoot "tmp") | Out-Null }
$env:CHIMERA_LOG_PATH = $logPath
if (Test-Path $logPath) { Remove-Item $logPath -Force }

function Kill-All {
    Get-Process -Name chimera-ui,emulator,qemu-system* -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

# adb writes to stderr for transient "device not found"; under StrictMode+Stop
# that aborts the loop. Wrap it so polling survives until the device appears.
function Invoke-AdbQuiet {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $Adb @Args 2>$null
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $old }
    [pscustomobject]@{ Code = $code; Out = (($out | Out-String).Trim()) }
}

$booted = $false
$resumed = "(unknown)"
$size = "(unknown)"
try {
    Write-Host "self-test: cleanup pre"
    Kill-All
    Start-Sleep -Seconds 2

    Write-Host "self-test: launch (serial $Serial)"
    $p = Start-Process -FilePath $AppExe -WorkingDirectory $RepoRoot -PassThru
    $deadline = (Get-Date).AddSeconds($SelfTestBootSec)
    while ((Get-Date) -lt $deadline) {
        if ($p.HasExited) { Write-Host "ui_exited_early code=$($p.ExitCode)"; break }
        $st = Invoke-AdbQuiet -Args @("-s", $Serial, "get-state")
        if ($st.Code -eq 0) {
            $bc = (Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "getprop", "sys.boot_completed")).Out
            if ($bc -eq "1") { $booted = $true; break }
        }
        Start-Sleep -Seconds 2
    }
    Write-Host "booted=$booted"

    if ($booted) {
        Start-Sleep -Seconds 6
        # Wake the screen + dismiss keyguard so screencap/composition is live.
        Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "input", "keyevent", "224") | Out-Null   # KEYCODE_WAKEUP
        Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "wm", "dismiss-keyguard") | Out-Null
        Start-Sleep -Seconds 1
        $size = (Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "wm", "size")).Out
        # Launch the Chimera home launcher and read the resumed activity.
        Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") | Out-Null
        Start-Sleep -Seconds 4
        $resumed = (Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "dumpsys window | grep -m1 mCurrentFocus")).Out
        Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "screencap", "-p", "/sdcard/chimera_home.png") | Out-Null
        Invoke-AdbQuiet -Args @("-s", $Serial, "pull", "/sdcard/chimera_home.png", $shotPath) | Out-Null
        Write-Host "wm_size=$size"
        Write-Host "resumed=$resumed"
        if (Test-Path $shotPath) { Write-Host "screenshot_bytes=$((Get-Item $shotPath).Length)" }

        # Interactivity proof: launch Settings, confirm it reaches the foreground.
        for ($i = 0; $i -lt 5; $i++) {
            Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "am", "start", "-n", "com.android.settings/.Settings") | Out-Null
            Start-Sleep -Seconds 2
            $afterFocus = (Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "dumpsys window | grep -m1 mCurrentFocus")).Out
            if ($afterFocus -match "settings") { break }
            Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "monkey", "-p", "com.android.settings", "1") | Out-Null
            Start-Sleep -Seconds 2
            $afterFocus = (Invoke-AdbQuiet -Args @("-s", $Serial, "shell", "dumpsys window | grep -m1 mCurrentFocus")).Out
            if ($afterFocus -match "settings") { break }
        }
        Write-Host "after_launch_focus=$afterFocus"
        if ($afterFocus -match "settings") { Write-Host "interactivity=ok" }
        else { throw "self-test FAILED: Settings did not reach foreground ('$afterFocus')" }
    }
}
finally {
    Write-Host "self-test: cleanup post"
    Kill-All
    Start-Sleep -Seconds 3
    $remain = @(Get-Process -Name chimera-ui,emulator,qemu-system* -ErrorAction SilentlyContinue)
    Write-Host "residual_processes=$($remain.Count)"
}

if (-not $booted) { throw "self-test FAILED: Android did not reach boot_completed on $Serial" }
if ($size -notmatch "1920x1080") { throw "self-test FAILED: unexpected display size '$size'" }
Write-Host "result=pass"
