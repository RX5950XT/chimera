<#
.SYNOPSIS
    Chimera launcher. Root start-chimera.cmd defaults to the fastest usable
    path; direct script usage without flags stays on stock SDK + gRPC for
    conservative diagnostics.

.DESCRIPTION
    Double-click usage: run via start-chimera.cmd at the repo root. That wrapper
    passes -Fast -InteractiveFirst, selecting custom gfxstream + GuestVulkan when
    the runtime is available.

    Direct script usage without flags:
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1
    uses stock SDK + gRPC. Pass -Fast for the production fast path.

    -Fast: custom gfxstream shared-texture runtime. Hits 1080p/60 for
    continuously-rendering content; normal Android UI renders on the safe ES
    compositor path. CHIMERA_GUEST_VULKAN=1 only enables the guest Vulkan FEATURE
    (Vulkan apps/games reach the host NVIDIA GPU) — there is no skiavk UI switch:
    on this user-build image it cannot complete (no root) and a half-applied
    state blanks app windows (the old "-Fast boots to a black center" bug).
    General UI 60 is not the gate; scripts\verify-interactive-ui.ps1 is the
    daily-usability evidence.

.PARAMETER Fast
    Opt into the custom gfxstream fast runtime (see caveat above).

.PARAMETER ConsolePort
    Android console base port. Direct launch defaults to 5554 (matches the host's
    adb serial wiring). SelfTest without an explicit ConsolePort auto-picks a free
    even console/ADB pair. The host honours this via CHIMERA_EMULATOR_CONSOLE_PORT,
    so a non-default port stays consistent end-to-end.

.PARAMETER Stock
    Force the stock SDK emulator + gRPC display path.

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
    GL60/verify-true-1080p60 proves the continuous-rendering 60 FPS path.
    Daily normal-UI usability is tracked by scripts\verify-interactive-ui.ps1;
    do not treat adb swipe results or GL60 as a general-UI 60 claim.
#>
[CmdletBinding()]
param(
    # 0 = autoselect a free console/ADB port pair.
    [ValidateRange(0, 5680)]
    [int]$ConsolePort = 5554,
    [switch]$Fast,
    [switch]$Stock,
    [switch]$RequireSharedTexture,
    [switch]$AudioFirst,
    [switch]$InteractiveFirst,
    [switch]$SelfTest,
    # Quick Boot (AVD default_boot snapshot) is the launch default: cold boot the
    # first time, save on clean exit, then ~10s resumes. -NoQuick forces a full
    # cold boot (and disables the exit-time snapshot save).
    [switch]$NoQuick,
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
$AvdName  = "chimera_dev"
$AvdDir   = Join-Path $RepoRoot "third_party\android-avd\$AvdName.avd"

. (Join-Path $PSScriptRoot "ChimeraVerifyCommon.ps1")

$explicitConsolePort = $PSBoundParameters.ContainsKey('ConsolePort')
if (-not $explicitConsolePort) {
    # Auto-pick a verified-free console/ADB/gRPC port set. The old fixed 5554/8554
    # default let a leftover emulator still holding gRPC 8554 hijack the input
    # channel: the new instance rendered a live picture via the shared texture, but
    # every touch POST to 127.0.0.1:8554 hit the stale listener, so nothing was
    # clickable. Selecting a free set makes launches collision-proof (and lets a
    # second instance start cleanly instead of silently losing input).
    $ConsolePort = Resolve-EmulatorConsolePort -ConsolePort 0
} else {
    $ConsolePort = Resolve-EmulatorConsolePort -ConsolePort $ConsolePort
}

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

# Fail-closed: -RequireSharedTexture must never silently fall back to stock gRPC.
if ($customRequested -and $RequireSharedTexture -and -not $customAvailable) {
    throw "-RequireSharedTexture: custom gfxstream runtime not found at $Runtime (fail-closed; refusing stock gRPC fallback)"
}

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
    # Guest Vulkan FEATURE only: Vulkan apps/games reach the host NVIDIA GPU, and
    # chimera-ui keeps normal Android animations on. NOT a skiavk UI switch — that
    # cannot work on this user-build image (no root for the framework restart) and
    # half-applying it blanks app windows.
    $env:CHIMERA_GUEST_VULKAN = "1"
    Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_ANGLE -ErrorAction SilentlyContinue
    if ($RequireSharedTexture) {
        $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = "1"
        Write-Host "display=gfxstream-shared-texture (-Fast, fail-closed; guest Vulkan feature on)"
    } else {
        Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Write-Host "display=gfxstream-shared-texture (-Fast; guest Vulkan feature on)"
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
# Quick Boot default (S112): AVD default_boot snapshot — cold boot when absent,
# save on clean exit (graceful "adb emu kill" in VirtualMachine::stop()), ~10s
# resume afterwards. Never a named "-snapshot" load (that would revert guest
# data on load). -NoQuick opts back into a full cold boot. SelfTest keeps full
# boot so its boot-time numbers stay comparable across sessions.
if ($NoQuick -or $SelfTest) {
    Remove-Item Env:\CHIMERA_QUICK_BOOT -ErrorAction SilentlyContinue  # full cold boot
} else {
    $env:CHIMERA_QUICK_BOOT = "1"
    Write-Host "boot=quick (AVD default_boot; first launch cold-boots then saves on exit; -NoQuick for full boot)"
}

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
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
    Start-Sleep -Seconds 2

    Write-Host "self-test: launch (serial $Serial)"
    $launchAt = Get-Date
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
    if ($booted) { Write-Host ("boot_seconds={0}" -f [int]((Get-Date) - $launchAt).TotalSeconds) }

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
        Write-Host "wm_size=$size"
        Write-Host "resumed=$resumed"

        # Content gate: the home frame must be real pixels, not a blank/black
        # surface. dumpsys-focus alone let the skiavk half-applied blank-screen
        # bug pass this self-test; assert bytes/nonblack/luma like the verifiers.
        $homeStats = $null
        $gateDeadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $gateDeadline) {
            try { $homeStats = Get-AdbScreenshotStats -Name "selftest_home" } catch { $homeStats = $null }
            if ($null -ne $homeStats -and $homeStats.Bytes -ge 20000 -and
                $homeStats.NonBlackPercent -ge 10.0 -and $homeStats.LumaSpread -ge 40) { break }
            Start-Sleep -Seconds 2
        }
        if ($null -eq $homeStats) { throw "self-test FAILED: could not capture home screenshot" }
        Copy-Item -LiteralPath $homeStats.Path -Destination $shotPath -Force
        Write-Host "screenshot_bytes=$($homeStats.Bytes)"
        Write-Host ("screenshot_nonblack_pct={0:N1}" -f $homeStats.NonBlackPercent)
        Write-Host "screenshot_luma_spread=$($homeStats.LumaSpread)"
        Write-Host ("visible_home_seconds={0}" -f [int]((Get-Date) - $launchAt).TotalSeconds)
        if ($homeStats.Bytes -lt 20000 -or $homeStats.NonBlackPercent -lt 10.0 -or $homeStats.LumaSpread -lt 40) {
            throw "self-test FAILED: home frame looks blank/black (bytes=$($homeStats.Bytes) nonblack=$([math]::Round($homeStats.NonBlackPercent,1))% spread=$($homeStats.LumaSpread))"
        }

        # HOST WINDOW gate: the ADB screencap above only proves the GUEST rendered.
        # The shared-texture window stayed black for 15 sessions while every
        # guest-side/counter gate passed — assert the pixels the user actually sees.
        $hostStats = $null
        $hostGateDeadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $hostGateDeadline) {
            try { $hostStats = Get-HostWindowPixelStats -Name "selftest_hostwindow" } catch { $hostStats = $null }
            if ($null -ne $hostStats -and $hostStats.NonBlackPercent -ge 5.0) { break }
            Start-Sleep -Seconds 2
        }
        if ($null -eq $hostStats) { throw "self-test FAILED: could not capture host window" }
        Write-Host ("host_window_nonblack_pct={0:N1}" -f $hostStats.NonBlackPercent)
        Write-Host "host_window_luma_spread=$($hostStats.LumaSpread)"
        if ($hostStats.NonBlackPercent -lt 5.0) {
            throw "self-test FAILED: host window shows a black guest display (nonblack=$([math]::Round($hostStats.NonBlackPercent,1))% spread=$($hostStats.LumaSpread)); guest rendered but frames are not visible on the host"
        }

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
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Start-Sleep -Seconds 3
    $remain = @(Get-ChimeraProcesses)
    Write-Host "residual_processes=$($remain.Count)"
}

if (-not $booted) { throw "self-test FAILED: Android did not reach boot_completed on $Serial" }
if ($size -notmatch "1920x1080") { throw "self-test FAILED: unexpected display size '$size'" }
Write-Host "result=pass"
