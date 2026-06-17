[CmdletBinding()]
param(
    [ValidateRange(10, 600)]
    [int]$BootTimeoutSec = 180,

    [ValidateRange(10, 300)]
    [int]$MeasureSeconds = 30,

    [ValidateRange(0, 10)]
    [int]$WarmupPerfSamples = 2,

    [ValidateRange(1, 240)]
    [double]$MinEffectiveFps = 60.0,

    [ValidateRange(1, 7680)]
    [int]$MinWidth = 1920,

    [ValidateRange(1, 4320)]
    [int]$MinHeight = 1080,

    [string]$Configuration = "Release",
    [string]$Serial = "emulator-5554",
    [string]$AvdName = "chimera_dev",
    [ValidateSet("Gfxstream", "EmuGL")]
    [string]$RuntimeKind = "Gfxstream",
    [string]$RuntimePath = "",
    [string]$ParseOnlyLog = "",
    [switch]$NoCleanStart,
    [switch]$GrpcOnly,
    # R&D only: bypass gfxstream source build ID check for ABI compatibility testing.
    [switch]$AllowMismatchedBuildId
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$AppExe = Join-Path $RepoRoot "build\$Configuration\chimera-ui.exe"
$Adb = Join-Path $RepoRoot "third_party\android-sdk\platform-tools\adb.exe"
$AvdDir = Join-Path $RepoRoot "third_party\android-avd\$AvdName.avd"
$DefaultRuntimeDir = if ($RuntimeKind -eq "Gfxstream") { "chimera-gfxstream-runtime" } else { "chimera-emugl-runtime" }
$DefaultRuntime = Join-Path $RepoRoot "build\$DefaultRuntimeDir\emulator.exe"
$ResolvedRuntime = if ([string]::IsNullOrWhiteSpace($RuntimePath)) { $DefaultRuntime } else { $RuntimePath }
$QtBin = "C:\Qt\6.8.3\msvc2022_64\bin"

function Require-File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name not found: $Path"
    }
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$IgnoreExit
    )
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:Adb @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    $text = ($output | Out-String).Trim()
    if ($exitCode -ne 0 -and -not $IgnoreExit) {
        throw "adb $($Arguments -join ' ') failed ($exitCode): $text"
    }
    [pscustomobject]@{ ExitCode = $exitCode; Output = $text }
}

function Get-ChimeraProcesses {
    $candidates = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -eq "chimera-ui.exe" -or
            $_.Name -eq "emulator.exe" -or
            $_.Name -like "qemu-system*.exe"
        })
    foreach ($candidate in $candidates) {
        $commandLine = [string]$candidate.CommandLine
        $isChimera =
            $candidate.Name -eq "chimera-ui.exe" -or
            $commandLine.Contains($script:RepoRoot) -or
            $commandLine.Contains($script:AvdName) -or
            $commandLine.Contains("chimera")
        if (-not $isChimera) { continue }
        Get-Process -Id $candidate.ProcessId -ErrorAction SilentlyContinue
    }
}

function Stop-ChimeraProcesses {
    @(Get-ChimeraProcesses) | Stop-Process -Force -ErrorAction SilentlyContinue
}

function Wait-NoChimeraProcesses {
    param([int]$TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (@(Get-ChimeraProcesses).Count -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    $remaining = @(Get-ChimeraProcesses | ForEach-Object { "$($_.ProcessName):$($_.Id)" })
    throw "Timed out waiting for Chimera processes to exit: $($remaining -join ', ')"
}

function Remove-StaleAvdLocks {
    if (-not (Test-Path -LiteralPath $script:AvdDir -PathType Container)) { return }
    $locks = @(Get-ChildItem -LiteralPath $script:AvdDir -Filter "*.lock" -ErrorAction SilentlyContinue)
    foreach ($lock in $locks) {
        if ($null -eq $lock) { continue }
        try {
            $lock.Delete()
        }
        catch {
            Write-Verbose "Could not remove stale AVD lock: $($lock.FullName)"
        }
    }
}

function Read-LogText {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return "" }
    return (Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue)
}

function Quote-CmdArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Get-PerfSamples {
    param([Parameter(Mandatory = $true)][string]$LogText)
    $machinePattern = 'CHIMERA_PERF guest=(?<guest>[0-9]+(?:\.[0-9]+)?) stream=(?<stream>[0-9]+(?:\.[0-9]+)?) render=(?<render>[0-9]+(?:\.[0-9]+)?) effective=(?<effective>[0-9]+(?:\.[0-9]+)?) dupPct=(?<dupPct>[0-9]+(?:\.[0-9]+)?)'
    $machineMatches = [regex]::Matches($LogText, $machinePattern)
    if ($machineMatches.Count -gt 0) {
        foreach ($match in $machineMatches) {
            [pscustomobject]@{
                Guest = [double]$match.Groups["guest"].Value
                Stream = [double]$match.Groups["stream"].Value
                Render = [double]$match.Groups["render"].Value
                Effective = [double]$match.Groups["effective"].Value
                DuplicatePercent = [double]$match.Groups["dupPct"].Value
            }
        }
        return
    }

    $pattern = '\[Perf\] Guest: (?<guest>[0-9]+(?:\.[0-9]+)?) FPS \| Stream: (?<stream>[0-9]+(?:\.[0-9]+)?) FPS \| Render: (?<render>[0-9]+(?:\.[0-9]+)?) FPS .* Dup: (?<dup>\d+) \((?<dupPct>[0-9]+(?:\.[0-9]+)?)%\)'
    $matches = [regex]::Matches($LogText, $pattern)
    foreach ($match in $matches) {
        $guest = [double]$match.Groups["guest"].Value
        $stream = [double]$match.Groups["stream"].Value
        $render = [double]$match.Groups["render"].Value
        [pscustomobject]@{
            Guest = $guest
            Stream = $stream
            Render = $render
            Effective = [Math]::Min($guest, [Math]::Min($stream, $render))
            DuplicatePercent = [double]$match.Groups["dupPct"].Value
        }
    }
}

function Assert-True1080p60Log {
    param([string]$LogText)

    if ([string]::IsNullOrWhiteSpace($LogText)) {
        throw "log is empty; cannot prove true 1080p60"
    }

    if ($LogText -notmatch "Chimera (EmuGL|gfxstream) shared texture runtime ready") {
        throw "shared texture runtime was not proven ready"
    }
    if ($LogText -notmatch "Shared D3D11 texture display capture started") {
        throw "shared D3D11 texture capture did not start"
    }
    if ($LogText -match "Chimera shared texture runtime requested but unavailable|Skipping shared texture capture|Skipping EmuGL shared texture capture") {
        throw "shared texture runtime was requested but unavailable"
    }
    if ($LogText -match "Required shared texture capture (was not configured|did not start|did not produce a frame)") {
        throw "required shared texture capture failed"
    }
    if ($LogText -match "ADB raw screen capture fallback started|ADB H\.264 screenrecord display capture selected|Starting gRPC screen capture stream") {
        throw "raw gRPC/ADB fallback was used; this is not valid 1080p60 proof"
    }

    $samples = @(Get-PerfSamples -LogText $LogText)
    if ($samples.Count -le $WarmupPerfSamples) {
        throw "not enough perf samples after warmup: $($samples.Count)"
    }
    $steady = @($samples | Select-Object -Skip $WarmupPerfSamples)
    $minEffective = ($steady | Measure-Object -Property Effective -Minimum).Minimum
    $avgEffective = ($steady | Measure-Object -Property Effective -Average).Average
    $maxDup = ($steady | Measure-Object -Property DuplicatePercent -Maximum).Maximum

    if ($minEffective -lt $MinEffectiveFps) {
        throw "effective FPS below threshold: min=$minEffective threshold=$MinEffectiveFps"
    }
    if ($maxDup -gt 5.0) {
        throw "duplicate rate too high during dynamic proof: maxDup=${maxDup}%"
    }

    Write-Host "perf_samples=$($steady.Count)"
    Write-Host ("effective_fps_min={0:N1}" -f $minEffective)
    Write-Host ("effective_fps_avg={0:N1}" -f $avgEffective)
    Write-Host ("duplicate_pct_max={0:N0}" -f $maxDup)
}

function Assert-True1080p60GrpcLog {
    # GrpcOnly verifier: proves the stock gRPC unary path delivers live 1920x1080
    # frames to the host. It does NOT prove 60 FPS (gRPC getScreenshot at 1920x1080
    # takes ~250ms/frame → ~4-14 FPS ceiling) — that gate belongs to the shared
    # texture path. Here we verify: frames flow (avgStreamFps >= 3) and unique
    # content arrives during dynamic exercise (maxGuestFps >= 1).
    param([string]$LogText)

    if ([string]::IsNullOrWhiteSpace($LogText)) {
        throw "log is empty; cannot prove gRPC display delivery"
    }

    if ($LogText -notmatch "Starting .+ screen capture stream") {
        throw "gRPC screen capture stream did not start"
    }
    if ($LogText -match "Shared D3D11 texture display capture started") {
        throw "shared D3D11 texture path was active; use Gfxstream/EmuGL mode instead"
    }
    if ($LogText -match "ADB raw screen capture fallback started|ADB H\.264 screenrecord display capture selected") {
        throw "ADB raw/screenrecord fallback was used; not a valid gRPC delivery proof"
    }
    if ($LogText -match "Required shared texture capture (was not configured|did not start|did not produce a frame)") {
        throw "shared texture was required but failed — unset CHIMERA_REQUIRE_*_SHARED_TEXTURE before running GrpcOnly"
    }

    $samples = @(Get-PerfSamples -LogText $LogText)
    if ($samples.Count -le $WarmupPerfSamples) {
        throw "not enough perf samples after warmup: $($samples.Count)"
    }
    $steady = @($samples | Select-Object -Skip $WarmupPerfSamples)

    # Exclude pre-gRPC boot samples where stream=0. gRPC only starts after
    # boot_completed; those zeros are expected during boot and should not dilute
    # the capture-rate average. We evaluate only samples where capture was active.
    $active = @($steady | Where-Object { $_.Stream -gt 0.0 })
    if ($active.Count -lt 2) {
        throw "fewer than 2 active gRPC capture samples found (stream > 0); gRPC may not have delivered any frames"
    }

    # Stream FPS: rate at which gRPC responses arrive (includes duplicates).
    # >= 3 FPS proves the pipeline is alive and serving frames, not stalled.
    $avgStream = ($active | Measure-Object -Property Stream -Average).Average
    # Guest FPS: rate of unique content changes. During statusbar/swipe exercise
    # Android updates ~2 frames/sec; >= 1 proves real screen changes are captured.
    $maxGuest = ($active | Measure-Object -Property Guest -Maximum).Maximum

    if ($avgStream -lt 3.0) {
        throw "gRPC stream rate too low: avg=$([math]::Round($avgStream,1)) FPS (expected >= 3.0); capture pipeline is not delivering frames"
    }
    if ($maxGuest -lt 1.0) {
        throw "no unique content frames during dynamic exercise: maxGuest=$([math]::Round($maxGuest,1)) FPS (expected >= 1.0)"
    }

    Write-Host "perf_samples=$($steady.Count)"
    Write-Host "grpc_active_samples=$($active.Count)"
    Write-Host ("grpc_stream_fps_avg={0:N1}" -f $avgStream)
    Write-Host ("unique_content_fps_max={0:N1}" -f $maxGuest)
}

function Wait-AndroidBoot {
    param(
        [Parameter(Mandatory = $true)][int]$TimeoutSec,
        $AppProcess = $null
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($null -ne $AppProcess -and $AppProcess.HasExited) {
            throw "chimera-ui exited before Android boot_completed=1, exitCode=$($AppProcess.ExitCode)"
        }
        $state = Invoke-Adb -Arguments @("-s", $script:Serial, "get-state") -IgnoreExit
        if ($state.ExitCode -eq 0) {
            $boot = Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "getprop", "sys.boot_completed") -IgnoreExit
            if ($boot.Output.Trim() -eq "1") { return }
        }
        Start-Sleep -Seconds 1
    }
    throw "Android did not reach sys.boot_completed=1 within ${TimeoutSec}s"
}

function Assert-AndroidDisplayFloor {
    $wm = Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "wm", "size")
    if ($wm.Output -notmatch "(\d+)x(\d+)") {
        throw "could not parse wm size: $($wm.Output)"
    }
    $width = [int]$Matches[1]
    $height = [int]$Matches[2]
    if ($width -lt $MinWidth -or $height -lt $MinHeight) {
        throw "Android wm size below floor: ${width}x${height}, floor=${MinWidth}x${MinHeight}"
    }

    $density = Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "wm", "density")
    Write-Host "wm_size=${width}x${height}"
    Write-Host "wm_density=$($density.Output)"
}

function Exercise-DynamicGuest {
    param([Parameter(Mandatory = $true)][int]$Seconds)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "cmd", "statusbar", "expand-notifications") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 700
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "cmd", "statusbar", "collapse") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 300
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "input", "swipe", "1700", "850", "250", "850", "250") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 500
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "input", "swipe", "250", "850", "1700", "850", "250") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 500
    }
}

if (-not [string]::IsNullOrWhiteSpace($ParseOnlyLog)) {
    $logForParse = Read-LogText -Path $ParseOnlyLog
    if ($GrpcOnly) {
        Assert-True1080p60GrpcLog -LogText $logForParse
    } else {
        Assert-True1080p60Log -LogText $logForParse
    }
    Write-Host "result=pass"
    return
}

Require-File -Path $AppExe -Name "chimera-ui.exe"
Require-File -Path $Adb -Name "adb.exe"
if (-not $GrpcOnly) {
    Require-File -Path $ResolvedRuntime -Name "Chimera $RuntimeKind emulator runtime"
}

$env:PATH = "$QtBin;$env:PATH"
$logDir = Join-Path $RepoRoot "tmp"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stderrLog = Join-Path $logDir "chimera-true-1080p60.err"
$stdoutLog = Join-Path $logDir "chimera-true-1080p60.out"
$messageLog = Join-Path $logDir "chimera-true-1080p60.log"
Remove-Item -LiteralPath $stderrLog, $stdoutLog, $messageLog -Force -ErrorAction SilentlyContinue

$savedEnv = @{
    CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE = $env:CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE
    CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE = $env:CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE
    CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE
    CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE
    CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK = $env:CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK
    CHIMERA_QUICK_BOOT = $env:CHIMERA_QUICK_BOOT
    CHIMERA_GRPC_TRANSPORT = $env:CHIMERA_GRPC_TRANSPORT
    CHIMERA_VIDEO_TRANSPORT = $env:CHIMERA_VIDEO_TRANSPORT
    CHIMERA_LOG_PATH = $env:CHIMERA_LOG_PATH
    CHIMERA_EMULATOR_PATH = $env:CHIMERA_EMULATOR_PATH
    CHIMERA_ENABLE_NATIVE_EMBED = $env:CHIMERA_ENABLE_NATIVE_EMBED
    CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW = $env:CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW
    CHIMERA_ENABLE_WINDOW_CAPTURE = $env:CHIMERA_ENABLE_WINDOW_CAPTURE
    CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE = $env:CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE
    CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW = $env:CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW
    CHIMERA_EMULATOR_START_VISIBLE = $env:CHIMERA_EMULATOR_START_VISIBLE
}

try {
    if (-not $NoCleanStart) {
        Stop-ChimeraProcesses
        Wait-NoChimeraProcesses -TimeoutSec 30
        Remove-StaleAvdLocks
    }

    if ($GrpcOnly) {
        Remove-Item Env:\CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_EMULATOR_PATH -ErrorAction SilentlyContinue
        $runtimeArg = ""
    } elseif ($RuntimeKind -eq "Gfxstream") {
        $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = "1"
        $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = "1"
        Remove-Item Env:\CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE -ErrorAction SilentlyContinue
        if ($AllowMismatchedBuildId) {
            $env:CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK = "1"
            Write-Host "WARNING: R&D mode — build ID check bypassed for ABI compatibility testing"
        } else {
            Remove-Item Env:\CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK -ErrorAction SilentlyContinue
        }
        $env:CHIMERA_EMULATOR_PATH = (Resolve-Path -LiteralPath $ResolvedRuntime).Path
        $runtimeArg = "--gfxstream-shared-texture"
    } else {
        $env:CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE = "1"
        $env:CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE = "1"
        Remove-Item Env:\CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        $env:CHIMERA_EMULATOR_PATH = (Resolve-Path -LiteralPath $ResolvedRuntime).Path
        $runtimeArg = "--emugl-shared-texture"
    }
    $env:CHIMERA_QUICK_BOOT = "0"
    $env:CHIMERA_LOG_PATH = $messageLog
    Remove-Item Env:\CHIMERA_GRPC_TRANSPORT -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_VIDEO_TRANSPORT -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_ENABLE_NATIVE_EMBED -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_ENABLE_WINDOW_CAPTURE -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW -ErrorAction SilentlyContinue
    Remove-Item Env:\CHIMERA_EMULATOR_START_VISIBLE -ErrorAction SilentlyContinue

    $appArgs = if ([string]::IsNullOrWhiteSpace($runtimeArg)) { "" } else { " $runtimeArg" }
    $cmdLine = "$(Quote-CmdArgument -Value $AppExe)$appArgs 1>$(Quote-CmdArgument -Value $stdoutLog) 2>$(Quote-CmdArgument -Value $stderrLog)"
    $cmdArgs = "/d /s /c `"$cmdLine`""
    $process = Start-Process -FilePath "cmd.exe" `
        -ArgumentList $cmdArgs `
        -WorkingDirectory $RepoRoot `
        -WindowStyle Hidden `
        -PassThru

    Start-Sleep -Seconds 2
    if ($process.HasExited) {
        $process.Refresh()
        $process.WaitForExit() | Out-Null
        $exitCode = if ($null -ne $process.ExitCode) { $process.ExitCode } else { "unknown" }
        throw "chimera-ui exited early with code ${exitCode}: $(Read-LogText -Path $stderrLog)"
    }

    Wait-AndroidBoot -TimeoutSec $BootTimeoutSec -AppProcess $process
    Assert-AndroidDisplayFloor
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.launcher/.MainActivity") -IgnoreExit | Out-Null
    Start-Sleep -Seconds 5
    Exercise-DynamicGuest -Seconds $MeasureSeconds
    Start-Sleep -Seconds 6
    $logText = (Read-LogText -Path $messageLog)
    if ([string]::IsNullOrWhiteSpace($logText)) {
        $logText = Read-LogText -Path $stderrLog
    }
    if ($GrpcOnly) {
        Assert-True1080p60GrpcLog -LogText $logText
    } else {
        Assert-True1080p60Log -LogText $logText
    }
    Write-Host "result=pass"
}
finally {
    if ($savedEnv.CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE -eq $null) { Remove-Item Env:\CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE = $savedEnv.CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE }
    if ($savedEnv.CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE -eq $null) { Remove-Item Env:\CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE = $savedEnv.CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE }
    if ($savedEnv.CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -eq $null) { Remove-Item Env:\CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = $savedEnv.CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE }
    if ($savedEnv.CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -eq $null) { Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = $savedEnv.CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE }
    if ($savedEnv.CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK -eq $null) { Remove-Item Env:\CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK -ErrorAction SilentlyContinue } else { $env:CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK = $savedEnv.CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK }
    if ($savedEnv.CHIMERA_QUICK_BOOT -eq $null) { Remove-Item Env:\CHIMERA_QUICK_BOOT -ErrorAction SilentlyContinue } else { $env:CHIMERA_QUICK_BOOT = $savedEnv.CHIMERA_QUICK_BOOT }
    if ($savedEnv.CHIMERA_GRPC_TRANSPORT -eq $null) { Remove-Item Env:\CHIMERA_GRPC_TRANSPORT -ErrorAction SilentlyContinue } else { $env:CHIMERA_GRPC_TRANSPORT = $savedEnv.CHIMERA_GRPC_TRANSPORT }
    if ($savedEnv.CHIMERA_VIDEO_TRANSPORT -eq $null) { Remove-Item Env:\CHIMERA_VIDEO_TRANSPORT -ErrorAction SilentlyContinue } else { $env:CHIMERA_VIDEO_TRANSPORT = $savedEnv.CHIMERA_VIDEO_TRANSPORT }
    if ($savedEnv.CHIMERA_LOG_PATH -eq $null) { Remove-Item Env:\CHIMERA_LOG_PATH -ErrorAction SilentlyContinue } else { $env:CHIMERA_LOG_PATH = $savedEnv.CHIMERA_LOG_PATH }
    if ($savedEnv.CHIMERA_EMULATOR_PATH -eq $null) { Remove-Item Env:\CHIMERA_EMULATOR_PATH -ErrorAction SilentlyContinue } else { $env:CHIMERA_EMULATOR_PATH = $savedEnv.CHIMERA_EMULATOR_PATH }
    if ($savedEnv.CHIMERA_ENABLE_NATIVE_EMBED -eq $null) { Remove-Item Env:\CHIMERA_ENABLE_NATIVE_EMBED -ErrorAction SilentlyContinue } else { $env:CHIMERA_ENABLE_NATIVE_EMBED = $savedEnv.CHIMERA_ENABLE_NATIVE_EMBED }
    if ($savedEnv.CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW -eq $null) { Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW -ErrorAction SilentlyContinue } else { $env:CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW = $savedEnv.CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW }
    if ($savedEnv.CHIMERA_ENABLE_WINDOW_CAPTURE -eq $null) { Remove-Item Env:\CHIMERA_ENABLE_WINDOW_CAPTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_ENABLE_WINDOW_CAPTURE = $savedEnv.CHIMERA_ENABLE_WINDOW_CAPTURE }
    if ($savedEnv.CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE -eq $null) { Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE = $savedEnv.CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE }
    if ($savedEnv.CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW -eq $null) { Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW -ErrorAction SilentlyContinue } else { $env:CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW = $savedEnv.CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW }
    if ($savedEnv.CHIMERA_EMULATOR_START_VISIBLE -eq $null) { Remove-Item Env:\CHIMERA_EMULATOR_START_VISIBLE -ErrorAction SilentlyContinue } else { $env:CHIMERA_EMULATOR_START_VISIBLE = $savedEnv.CHIMERA_EMULATOR_START_VISIBLE }

    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}
