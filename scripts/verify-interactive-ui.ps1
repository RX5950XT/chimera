<#
.SYNOPSIS
    Honest interactive-UI verifier for Project Chimera.

.DESCRIPTION
    Measures REAL Android UI interaction (Home -> Settings -> sustained scroll ->
    app switch) in a visible Chimera window — NOT a synthetic continuous-render
    GL app. It classifies which display path actually wired up, reports per-segment
    guest/stream/render/dup FPS with the bottleneck channel, and samples the
    emulator/qemu process tree priority/CPU plus chimera-ui helper-spawn churn as
    a host-audio-contention proxy.

    Honesty contract (this script can never print a "60" claim for general UI):
      Stock : proves the stock gRPC path delivers live 1920x1080 frames during
              interaction. result=baseline, exit 0. Never a 60 claim.
      Fast  : only result=pass-gpu-direct-60 when path=gpu-direct AND sustained
              effective >= MinSustainedEffectiveFps AND dup small. Otherwise
              result=fast-ui-visible-not-60 with a root-cause classification and a
              non-zero exit (unless -AllowBaseline).

    General SurfaceFlinger UI at 60 FPS is out of scope: it requires gfxstream
    compositor R&D (direct-VK covering SurfaceFlinger composition). This verifier
    measures and classifies; it does not claim that milestone.

.NOTES
    Reuses scripts/ChimeraVerifyCommon.ps1 for port picking, adb, the screenshot
    visible-frame gate, perf parsing, process cleanup, and host-window foreground.
#>
[CmdletBinding()]
param(
    [ValidateSet("Stock", "Fast")]
    [string]$Mode = "Stock",

    # Measured sustained-scroll window. Keep >= 20s so the 5s CHIMERA_PERF cadence
    # yields enough steady-state samples.
    [ValidateRange(20, 300)]
    [int]$MeasureSeconds = 30,

    # Skip the app cold-start / first-frame transient before the measured window.
    [ValidateRange(0, 120)]
    [int]$WarmupSeconds = 8,

    # adb input swipe loop period during sustained scroll.
    [ValidateRange(8, 250)]
    [int]$SwipeCadenceMs = 32,

    # Developer-observable mode: boot, gate, then idle while printing live
    # CHIMERA_PERF / telemetry so a human can drive the host window by hand and
    # listen for audio stutter. Never gates, always exit 0.
    [switch]$Observe,
    [ValidateRange(10, 1200)]
    [int]$ObserveSeconds = 120,

    # Optional process-priority override for the run (idle|below_normal|normal).
    # Empty = inherit the chimera-ui built-in default (below_normal). Use this to
    # measure the audio<->FPS trade-off.
    [ValidateSet("", "idle", "below_normal", "normal")]
    [string]$Priority = "",

    [ValidateRange(10, 600)]
    [int]$BootTimeoutSec = 200,

    # 0 = autoselect a free console/ADB port pair.
    [ValidateRange(0, 5680)]
    [int]$ConsolePort = 0,

    # Mode-default thresholds applied when left at the -1 sentinel.
    [double]$MinSustainedEffectiveFps = -1,
    [double]$MinSustainedStreamFps = -1,
    [double]$MinUniqueContentFps = -1,

    # Fast: pass as a documented baseline (exit 0) instead of failing when the
    # general-UI path is visible-but-not-60.
    [switch]$AllowBaseline,

    # Session 91 experiment: after boot, route the guest UI through Vulkan on the
    # host NVIDIA GPU — SurfaceFlinger RenderEngine + app HWUI both skiavk (via runtime
    # setprop + framework restart). Requires CHIMERA_GUEST_VULKAN=1 in the environment
    # so chimera-ui launches the emulator with "-feature Vulkan". Measures whether
    # hardware-Vulkan UI rendering beats the default skiagl->SwiftShader path.
    [switch]$GuestVulkan,

    [ValidateRange(1, 7680)][int]$MinWidth = 1920,
    [ValidateRange(1, 4320)][int]$MinHeight = 1080,

    [string]$Configuration = "Release",
    [string]$AvdName = "chimera_dev",
    [string]$RuntimePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$AppExe = Join-Path $RepoRoot "build\$Configuration\chimera-ui.exe"
$Adb = Join-Path $RepoRoot "third_party\android-sdk\platform-tools\adb.exe"
$AvdDir = Join-Path $RepoRoot "third_party\android-avd\$AvdName.avd"
$DefaultRuntime = Join-Path $RepoRoot "build\chimera-gfxstream-runtime\emulator.exe"
$ResolvedRuntime = if ([string]::IsNullOrWhiteSpace($RuntimePath)) { $DefaultRuntime } else { $RuntimePath }
$QtBin = "C:\Qt\6.8.3\msvc2022_64\bin"

. (Join-Path $PSScriptRoot "ChimeraVerifyCommon.ps1")

$script:Serial = "emulator-5554"

# Mode-default thresholds. Stock floors are liveness checks (prove frames flow),
# NOT quality gates — Stock reports the real number and passes as baseline.
if ($Mode -eq "Stock") {
    if ($MinSustainedEffectiveFps -lt 0) { $MinSustainedEffectiveFps = 2.0 }
    if ($MinSustainedStreamFps -lt 0) { $MinSustainedStreamFps = 3.0 }
    if ($MinUniqueContentFps -lt 0) { $MinUniqueContentFps = 1.0 }
} else {
    if ($MinSustainedEffectiveFps -lt 0) { $MinSustainedEffectiveFps = 55.0 }
    if ($MinSustainedStreamFps -lt 0) { $MinSustainedStreamFps = 30.0 }
    if ($MinUniqueContentFps -lt 0) { $MinUniqueContentFps = 20.0 }
}

# --- Telemetry accumulators (host-audio contention proxy) -----------------
# Kept deliberately lightweight: per-PID Get-Process reads (no WMI) on a 2s
# cadence, with the costly CIM adb-children scan throttled to ~4s, so the
# verifier does not perturb the capture pipeline it is measuring.
$script:telemPrevCpu = @{}
$script:telemCpuPctSamples = New-Object System.Collections.Generic.List[double]
$script:telemPriority = @{}
$script:telemHelperPids = New-Object System.Collections.Generic.HashSet[int]
$script:telemChimeraUiPid = 0
$script:telemGuestPids = @()
$script:telemLastChurn = [datetime]::MinValue
$script:telemLastPidRefresh = [datetime]::MinValue

function Get-ChimeraUiProcess {
    @(Get-ChimeraProcesses | Where-Object { $_.ProcessName -eq 'chimera-ui' }) | Select-Object -First 1
}

function Update-GuestPids {
    # Cheap in-process enumeration (no WMI/CIM): emulator + qemu tree.
    $script:telemGuestPids = @(
        Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -eq 'emulator' -or $_.ProcessName -like 'qemu-system*' } |
            ForEach-Object { [int]$_.Id }
    )
}

function Sample-Telemetry {
    $now = Get-Date
    if (($now - $script:telemLastPidRefresh).TotalSeconds -ge 6) {
        Update-GuestPids
        $script:telemLastPidRefresh = $now
    }
    $sumPct = 0.0
    $hadPrev = $false
    foreach ($gpid in $script:telemGuestPids) {
        $p = Get-Process -Id $gpid -ErrorAction SilentlyContinue
        if ($null -eq $p) { continue }
        try { $cur = $p.TotalProcessorTime.TotalSeconds } catch { continue }
        if ($script:telemPrevCpu.ContainsKey($gpid)) {
            $prev = $script:telemPrevCpu[$gpid]
            $dt = ($now - $prev.Time).TotalSeconds
            if ($dt -gt 0) {
                $sumPct += 100.0 * ($cur - $prev.Total) / ($dt * [Environment]::ProcessorCount)
                $hadPrev = $true
            }
        }
        $script:telemPrevCpu[$gpid] = @{ Time = $now; Total = $cur }
        try { $script:telemPriority[[string]$p.PriorityClass] = $true } catch {}
    }
    if ($hadPrev) { $script:telemCpuPctSamples.Add($sumPct) }

    if ($script:telemChimeraUiPid -gt 0 -and ($now - $script:telemLastChurn).TotalSeconds -ge 4) {
        $script:telemLastChurn = $now
        $kids = @(Get-CimInstance Win32_Process -Filter "Name='adb.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.ParentProcessId -eq $script:telemChimeraUiPid })
        foreach ($k in $kids) { [void]$script:telemHelperPids.Add([int]$k.ProcessId) }
    }
}

# --- Mode -> launch environment (mirrors start-chimera.ps1) ----------------
$TouchedEnv = @(
    'CHIMERA_EMULATOR_PATH', 'CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE',
    'CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE', 'CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE',
    'CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE', 'CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES',
    'CHIMERA_GFXSTREAM_HEADLESS_ANGLE', 'CHIMERA_LOG_PATH', 'CHIMERA_QUICK_BOOT',
    'CHIMERA_EMULATOR_CONSOLE_PORT', 'CHIMERA_INTERACTIVE_PRIORITY',
    'CHIMERA_GRPC_TRANSPORT', 'CHIMERA_VIDEO_TRANSPORT'
)
$SavedEnv = @{}
foreach ($name in $TouchedEnv) { $SavedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

function Set-ProcessEnv {
    param([string]$Name, [string]$Value)
    if ($null -eq $Value) { Remove-Item "Env:\$Name" -ErrorAction SilentlyContinue }
    else { Set-Item "Env:\$Name" -Value $Value }
}

function Set-ChimeraModeEnv {
    param([string]$Mode)
    # Clear shared-texture vars first so a stale value cannot leak across modes.
    foreach ($v in 'CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE', 'CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE',
                   'CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE', 'CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE',
                   'CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES', 'CHIMERA_GFXSTREAM_HEADLESS_ANGLE',
                   'CHIMERA_EMULATOR_PATH', 'CHIMERA_GRPC_TRANSPORT', 'CHIMERA_VIDEO_TRANSPORT') {
        Remove-Item "Env:\$v" -ErrorAction SilentlyContinue
    }
    if ($Mode -eq "Fast") {
        $runtimeDir = Split-Path -Parent $ResolvedRuntime
        $dll = Join-Path $runtimeDir "lib64\libgfxstream_backend.dll"
        if (-not (Test-Path -LiteralPath $ResolvedRuntime -PathType Leaf) -or -not (Test-Path -LiteralPath $dll -PathType Leaf)) {
            throw "Fast mode requires the custom gfxstream runtime; missing $ResolvedRuntime or lib64\libgfxstream_backend.dll. Build it with scripts/build-chimera-gfxstream-runtime.ps1 or run -Mode Stock."
        }
        $env:CHIMERA_EMULATOR_PATH = (Resolve-Path -LiteralPath $ResolvedRuntime).Path
        $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = "1"
        # Keep general Android UI visible via the SwiftShader ES compositor path.
        $env:CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES = "1"
        # Deliberately NOT setting CHIMERA_REQUIRE_* — fail-closed would kill the
        # run before we can observe and classify the real (composited) UI path.
        return "--gfxstream-shared-texture"
    }
    return ""
}

# --- Logging targets -------------------------------------------------------
$logDir = Join-Path $RepoRoot "tmp"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$tag = $Mode.ToLower()
$stderrLog = Join-Path $logDir "chimera-interactive-$tag.err"
$stdoutLog = Join-Path $logDir "chimera-interactive-$tag.out"
$script:messageLog = Join-Path $logDir "chimera-interactive-$tag.log"
Remove-Item -LiteralPath $stderrLog, $stdoutLog, $script:messageLog -Force -ErrorAction SilentlyContinue

function Get-PerfSampleCount {
    return @(Get-PerfSamples -LogText ([string](Read-LogText -Path $script:messageLog))).Count
}

function Get-DisplayPathLine {
    param([string]$LogText)
    $m = [regex]::Match($LogText, 'CHIMERA_DISPLAY[^\r\n]*')
    if ($m.Success) { return $m.Value }
    return ""
}

function Get-InteractiveDisplayPath {
    param([string]$LogText)
    # Prefer the authoritative CHIMERA_DISPLAY path token; refine with post markers.
    $line = Get-DisplayPathLine -LogText $LogText
    $base = "unknown"
    if ($line -match 'path=(\S+)') { $base = $Matches[1] }
    elseif ($LogText -match 'Shared D3D11 texture display capture started') { $base = 'gfxstream-shared-texture' }
    elseif ($LogText -match 'Shared-memory display capture started') { $base = 'shmem-cpu-readback' }
    elseif ($LogText -match 'Starting .+ screen capture stream') { $base = 'grpc-unary' }
    elseif ($LogText -match 'ADB raw screen capture fallback started') { $base = 'adb-raw' }

    if ($base -eq 'gfxstream-shared-texture' -or $base -eq 'window-d3d11') {
        # Count actual per-frame markers; "GPU-direct D3D11 import OK" is one-time
        # init, not proof that frames take the direct path.
        $hasCpu = $LogText -match 'postFrameCpu|GL readback fallback|UpdateSubresource'
        $hasDirect = $LogText -match 'postFrameDirectGpu'
        if ($hasCpu -and $hasDirect) { return 'gpu-mixed' }
        if ($hasCpu) { return 'gpu-shared-cpu-composited' }
        if ($hasDirect) { return 'gpu-direct' }
        return 'gpu-shared-unknown'
    }
    return $base
}

function Measure-Segment {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][object[]]$Samples,
        [Parameter(Mandatory = $true)][int]$StartIndex,
        [Parameter(Mandatory = $true)][int]$EndIndex
    )
    $slice = @()
    if ($EndIndex -gt $StartIndex) {
        $slice = @($Samples[$StartIndex..($EndIndex - 1)])
    }
    if ($slice.Count -eq 0) {
        return [pscustomobject]@{
            Name = $Name; Count = 0; GuestAvg = 0; GuestMax = 0; StreamAvg = 0;
            RenderAvg = 0; EffAvg = 0; EffMin = 0; DupMax = 0; Bottleneck = "n/a"
        }
    }
    $guestAvg = ($slice | Measure-Object -Property Guest -Average).Average
    $guestMax = ($slice | Measure-Object -Property Guest -Maximum).Maximum
    $streamAvg = ($slice | Measure-Object -Property Stream -Average).Average
    $renderAvg = ($slice | Measure-Object -Property Render -Average).Average
    $effAvg = ($slice | Measure-Object -Property Effective -Average).Average
    $effMin = ($slice | Measure-Object -Property Effective -Minimum).Minimum
    $dupMax = ($slice | Measure-Object -Property DuplicatePercent -Maximum).Maximum
    $channels = @(
        @{ N = 'guest'; V = $guestAvg },
        @{ N = 'stream'; V = $streamAvg },
        @{ N = 'render'; V = $renderAvg }
    )
    $bottleneck = ($channels | Sort-Object { $_.V } | Select-Object -First 1).N
    return [pscustomobject]@{
        Name = $Name; Count = $slice.Count; GuestAvg = $guestAvg; GuestMax = $guestMax;
        StreamAvg = $streamAvg; RenderAvg = $renderAvg; EffAvg = $effAvg; EffMin = $effMin;
        DupMax = $dupMax; Bottleneck = $bottleneck
    }
}

# --- Main ------------------------------------------------------------------
Require-File -Path $AppExe -Name "chimera-ui.exe"
Require-File -Path $Adb -Name "adb.exe"

if ($ConsolePort -eq 0) {
    $ConsolePort = Get-FreeEmulatorConsolePort
}
$script:Serial = "emulator-$ConsolePort"
Write-Host "mode=$Mode"
Write-Host "selected_console_port=$ConsolePort"
Write-Host "serial=$script:Serial"

$env:PATH = "$QtBin;$env:PATH"

try {
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks

    $runtimeArg = Set-ChimeraModeEnv -Mode $Mode
    $env:CHIMERA_QUICK_BOOT = "0"
    $env:CHIMERA_LOG_PATH = $script:messageLog
    $env:CHIMERA_EMULATOR_CONSOLE_PORT = "$ConsolePort"
    if ([string]::IsNullOrEmpty($Priority)) {
        Remove-Item Env:\CHIMERA_INTERACTIVE_PRIORITY -ErrorAction SilentlyContinue
    } else {
        $env:CHIMERA_INTERACTIVE_PRIORITY = $Priority
        Write-Host "priority_override=$Priority"
    }

    $appArgs = if ([string]::IsNullOrWhiteSpace($runtimeArg)) { "" } else { " $runtimeArg" }
    $cmdLine = "$(Quote-CmdArgument -Value $AppExe)$appArgs 1>$(Quote-CmdArgument -Value $stdoutLog) 2>$(Quote-CmdArgument -Value $stderrLog)"
    $process = Start-Process -FilePath "cmd.exe" -ArgumentList "/d /s /c `"$cmdLine`"" `
        -WorkingDirectory $RepoRoot -PassThru

    Start-Sleep -Seconds 2
    if ($process.HasExited) {
        throw "chimera-ui exited early with code $($process.ExitCode): $(Read-LogText -Path $stderrLog)"
    }

    Wait-AndroidBoot -TimeoutSec $BootTimeoutSec -AppProcess $process
    Assert-AndroidDisplayFloor
    $ui = Get-ChimeraUiProcess
    if ($null -ne $ui) { $script:telemChimeraUiPid = [int]$ui.Id }
    Update-GuestPids

    # --- Visible Home gate ---
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") -IgnoreExit | Out-Null
    Wait-VisiblePackageFrame -Name "home" -PackageRegex "com\.chimera\.launcher" -TimeoutSec 30 `
        -Relaunch { Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") -IgnoreExit | Out-Null }

    if ($GuestVulkan) {
        Write-Host "guest_vulkan=1 — routing SurfaceFlinger + HWUI through Vulkan (skiavk) on NVIDIA"
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "setprop", "debug.renderengine.backend", "skiavk") -IgnoreExit | Out-Null
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "setprop", "debug.hwui.renderer", "skiavk") -IgnoreExit | Out-Null
        # Full framework restart so SF picks up the Vulkan RenderEngine and every app's
        # HWUI re-inits on Vulkan. boot_completed re-fires.
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "stop") -IgnoreExit | Out-Null
        Start-Sleep -Seconds 2
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "start") -IgnoreExit | Out-Null
        Wait-AndroidBoot -TimeoutSec $BootTimeoutSec -AppProcess $process
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") -IgnoreExit | Out-Null
        Wait-VisiblePackageFrame -Name "home-vk" -PackageRegex "com\.chimera\.launcher" -TimeoutSec 30 `
            -Relaunch { Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") -IgnoreExit | Out-Null }
        $hwuiProp = (Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "getprop", "debug.hwui.renderer") | Out-String).Trim()
        Write-Host "guest_vulkan_hwui_prop=$hwuiProp"
    }

    if ($Observe) {
        Write-Host "OBSERVE mode: drive the host window by hand; live telemetry below."
        Ensure-HostWindowVisible -Process $process
        $deadline = (Get-Date).AddSeconds($ObserveSeconds)
        while ((Get-Date) -lt $deadline) {
            if ($process.HasExited) { break }
            Ensure-HostWindowVisible -Process $process
            Sample-Telemetry
            $samples = @(Get-PerfSamples -LogText ([string](Read-LogText -Path $script:messageLog)))
            $latest = if ($samples.Count -gt 0) { $samples[$samples.Count - 1] } else { $null }
            if ($null -ne $latest) {
                Write-Host ("OBS perf guest={0:N1} stream={1:N1} render={2:N1} eff={3:N1} dup={4:N1}" -f `
                    $latest.Guest, $latest.Stream, $latest.Render, $latest.Effective, $latest.DuplicatePercent)
            }
            Start-Sleep -Seconds 2
        }
        Write-Host "result=observe"
        exit 0
    }

    # --- Segmented real interaction ---
    $boundIdleStart = Get-PerfSampleCount

    # SEG idle-home (NOT gated; push-based idle is expected to be low)
    $idleSettle = [Math]::Max(4, $WarmupSeconds)
    $t = (Get-Date).AddSeconds($idleSettle)
    while ((Get-Date) -lt $t) { Ensure-HostWindowVisible -Process $process; Sample-Telemetry; Start-Sleep -Seconds 1 }
    $boundIdleEnd = Get-PerfSampleCount

    # SEG settings-open
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.android.settings/.Settings") -IgnoreExit | Out-Null
    Wait-VisiblePackageFrame -Name "settings" `
        -PackageRegex "com\.android\.settings/(?:\.Settings|com\.android\.settings\.Settings)" -TimeoutSec 30 `
        -Relaunch { Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.android.settings/.Settings") -IgnoreExit | Out-Null }

    # warmup before the measured window
    Start-Sleep -Seconds $WarmupSeconds
    Ensure-HostWindowVisible -Process $process
    $boundScrollStart = Get-PerfSampleCount
    Write-Host "warmup_samples_skipped=$boundScrollStart"

    # SEG sustained-scroll (THE gated segment)
    $deadline = (Get-Date).AddSeconds($MeasureSeconds)
    $lastForeground = Get-Date
    $lastTelemetry = Get-Date
    $up = $true
    while ((Get-Date) -lt $deadline) {
        if ($up) {
            Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "input", "swipe", "960", "900", "960", "200", "120") -IgnoreExit | Out-Null
        } else {
            Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "input", "swipe", "960", "200", "960", "900", "120") -IgnoreExit | Out-Null
        }
        $up = -not $up
        $now = Get-Date
        if (($now - $lastTelemetry).TotalMilliseconds -ge 2000) { Sample-Telemetry; $lastTelemetry = $now }
        if (($now - $lastForeground).TotalSeconds -ge 5) { Ensure-HostWindowVisible -Process $process; $lastForeground = $now }
        Start-Sleep -Milliseconds $SwipeCadenceMs
    }
    $boundScrollEnd = Get-PerfSampleCount

    # SEG app-switch (exercise transitions; not separately gated)
    foreach ($i in 1..3) {
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 700
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.android.settings/.Settings") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 700
        Sample-Telemetry
    }

    Start-Sleep -Seconds 6
    $logText = [string](Read-LogText -Path $script:messageLog)
    if ([string]::IsNullOrWhiteSpace($logText)) { $logText = [string](Read-LogText -Path $stderrLog) }

    $allSamples = @(Get-PerfSamples -LogText $logText)
    $displayPath = Get-InteractiveDisplayPath -LogText $logText
    $displayLine = Get-DisplayPathLine -LogText $logText

    $idleSeg = Measure-Segment -Name "idle-home" -Samples $allSamples -StartIndex $boundIdleStart -EndIndex $boundIdleEnd
    $scrollSeg = Measure-Segment -Name "sustained-scroll" -Samples $allSamples -StartIndex $boundScrollStart -EndIndex $boundScrollEnd

    # --- Telemetry summary ---
    $cpuAvg = if ($script:telemCpuPctSamples.Count -gt 0) { ($script:telemCpuPctSamples | Measure-Object -Average).Average } else { 0 }
    $cpuMax = if ($script:telemCpuPctSamples.Count -gt 0) { ($script:telemCpuPctSamples | Measure-Object -Maximum).Maximum } else { 0 }
    $prioSet = ($script:telemPriority.Keys | Sort-Object) -join ','
    if ([string]::IsNullOrEmpty($prioSet)) { $prioSet = "unknown" }
    $helperSpawns = $script:telemHelperPids.Count

    # --- Report (machine-greppable) ---
    if (-not [string]::IsNullOrEmpty($displayLine)) { Write-Host $displayLine }
    Write-Host ("CHIMERA_INT path={0} mode={1} seg=idle-home guestAvg={2:N1} streamAvg={3:N1} renderAvg={4:N1} effAvg={5:N1} samples={6}" -f `
        $displayPath, $Mode, $idleSeg.GuestAvg, $idleSeg.StreamAvg, $idleSeg.RenderAvg, $idleSeg.EffAvg, $idleSeg.Count)
    Write-Host ("CHIMERA_INT path={0} mode={1} seg=sustained-scroll guestFps={2:N1} streamFps={3:N1} renderFps={4:N1} effFps={5:N1} effMin={6:N1} dupPct={7:N1} bottleneck={8} samples={9}" -f `
        $displayPath, $Mode, $scrollSeg.GuestMax, $scrollSeg.StreamAvg, $scrollSeg.RenderAvg, $scrollSeg.EffAvg, $scrollSeg.EffMin, $scrollSeg.DupMax, $scrollSeg.Bottleneck, $scrollSeg.Count)
    Write-Host ("CHIMERA_INT_PRIO qemuPriority={0} qemuCpuPctAvg={1:N1} qemuCpuPctMax={2:N1} helperSpawns={3} priorityOverride={4}" -f `
        $prioSet, $cpuAvg, $cpuMax, $helperSpawns, ($(if ([string]::IsNullOrEmpty($Priority)) { "default" } else { $Priority })))

    # --- Honest gating ---
    if ($scrollSeg.Count -lt 2) {
        throw "not enough sustained-scroll perf samples (got $($scrollSeg.Count)); increase -MeasureSeconds"
    }

    if ($Mode -eq "Stock") {
        if ($scrollSeg.StreamAvg -lt $MinSustainedStreamFps -or $scrollSeg.GuestMax -lt $MinUniqueContentFps) {
            Write-Host "result=fail-stock-no-delivery"
            throw "stock gRPC did not deliver live interactive frames: streamAvg=$([math]::Round($scrollSeg.StreamAvg,1)) guestMax=$([math]::Round($scrollSeg.GuestMax,1)) (floors stream>=$MinSustainedStreamFps guest>=$MinUniqueContentFps)"
        }
        Write-Host "note=stock gRPC unary 1080p readback path; visible/usable baseline, NOT 60 FPS."
        Write-Host "result=baseline"
        exit 0
    }

    # Fast mode
    if ($displayPath -eq "gpu-direct" -and $scrollSeg.EffAvg -ge $MinSustainedEffectiveFps -and $scrollSeg.DupMax -le 5.0) {
        Write-Host "result=pass-gpu-direct-60"
        exit 0
    }

    $bottleneckFps = switch ($scrollSeg.Bottleneck) {
        'guest' { $scrollSeg.GuestAvg }; 'stream' { $scrollSeg.StreamAvg };
        'render' { $scrollSeg.RenderAvg }; default { $scrollSeg.EffAvg }
    }
    if ($displayPath -eq 'gpu-direct') {
        Write-Host ("root_cause=Fast delivered general UI via GPU-direct (postFrameDirectGpu, no CPU readback) at effAvg={0:N1} FPS with dup={1:N0}%; guest~=stream~=render means the host pipeline keeps up 1:1 and the limiter is the guest SurfaceFlinger render cadence during scroll (push-based, ~{2:N0} unique fps), not the host path. 60 needs the guest to render continuously; general-UI 60 is out of scope (gfxstream compositor R&D)." -f `
            $scrollSeg.EffAvg, $scrollSeg.DupMax, $scrollSeg.GuestMax)
    } elseif ($displayPath -like 'gpu-shared*' -or $displayPath -eq 'gpu-mixed') {
        Write-Host ("root_cause=Fast general UI used a CPU-composited/readback path (path={0}, bottleneck={1} at {2:N1} FPS); GPU-direct covers continuous-render content only. general-UI 60 requires gfxstream compositor R&D and is out of scope." -f `
            $displayPath, $scrollSeg.Bottleneck, $bottleneckFps)
    } else {
        Write-Host ("root_cause=Fast did not reach the GPU-direct path (path={0}, bottleneck={1} at {2:N1} FPS)." -f `
            $displayPath, $scrollSeg.Bottleneck, $bottleneckFps)
    }
    if ($AllowBaseline) {
        Write-Host "result=baseline"
        exit 0
    }
    Write-Host "result=fast-ui-visible-not-60"
    throw "general-UI 60 not achieved on the Fast path; see root_cause above (pass -AllowBaseline to accept as a documented baseline)"
}
finally {
    foreach ($name in $TouchedEnv) { Set-ProcessEnv -Name $name -Value $SavedEnv[$name] }
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}
