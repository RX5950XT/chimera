[CmdletBinding()]
param(
    [ValidateRange(10, 600)]
    [int]$BootTimeoutSec = 180,

    [ValidateRange(10, 300)]
    [int]$MeasureSeconds = 30,

    # Time to let the dynamic workload reach steady cadence before the measured window.
    # A GL app cold-starts with a multi-hundred-ms first-frame stall plus a JIT ramp;
    # that one-time transient is not a steady-state stutter and is excluded from the gate.
    [ValidateRange(0, 180)]
    [int]$WarmupSeconds = 30,

    [ValidateRange(0, 10)]
    [int]$WarmupPerfSamples = 2,

    # Steady-state FPS gate. Windowed FPS naturally jitters ~+/-2 around a true 60 FPS
    # producer (frame time ~16.2ms), so the per-sample floor tolerates that jitter while
    # the average floor proves sustained 60. Both are evaluated only on post-warmup samples.
    [ValidateRange(1, 240)]
    [double]$MinEffectiveFps = 57.0,

    [ValidateRange(1, 240)]
    [double]$MinAvgEffectiveFps = 59.0,

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
    # Hide the Chimera host window. Off by default because the verifier's render FPS
    # is the central GuestDisplay/Qt scene graph cadence, and a hidden/minimized
    # window can be OS/Qt-throttled independently of the Android producer.
    [switch]$HideHostWindow,
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

# Number of perf samples emitted before the measured (post-warmup) window begins.
# Set by Exercise-DynamicGuest after the workload reaches steady cadence; the steady
# gate evaluates only samples after this boundary. Stays 0 for ParseOnly/GrpcOnly.
$script:MeasureStartSamples = 0

function Require-File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name not found: $Path"
    }
}

function Test-TcpPortAvailable {
    param([Parameter(Mandatory = $true)][int]$Port)
    $listeners = @(Get-NetTCPConnection -LocalPort $Port -ErrorAction SilentlyContinue |
        Where-Object { $_.State -in @('Listen', 'Bound', 'Established', 'SynSent', 'SynReceived') })
    return $listeners.Count -eq 0
}

function Get-FreeEmulatorConsolePort {
    # Android Emulator reserves an even console port and the following odd ADB port.
    # Some local services (for example VPN clients) bind 5555/5561, so pick a free pair.
    foreach ($port in 5560, 5570, 5580, 5590, 5600, 5610, 5620, 5630, 5640, 5650, 5660, 5670) {
        if ((Test-TcpPortAvailable -Port $port) -and (Test-TcpPortAvailable -Port ($port + 1))) {
            return $port
        }
    }
    throw "no free Android emulator console/ADB port pair found"
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

function Get-AdbScreenshotStats {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [int]$GridX = 32,
        [int]$GridY = 18
    )

    $safeName = $Name -replace '[^A-Za-z0-9_-]', '_'
    $remote = "/sdcard/chimera_${safeName}.png"
    $local = Join-Path $script:RepoRoot "tmp\chimera-${safeName}.png"
    Remove-Item -LiteralPath $local -Force -ErrorAction SilentlyContinue
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "screencap", "-p", $remote) | Out-Null
    Invoke-Adb -Arguments @("-s", $script:Serial, "pull", $remote, $local) | Out-Null
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "rm", "-f", $remote) -IgnoreExit | Out-Null
    if (-not (Test-Path -LiteralPath $local -PathType Leaf)) {
        throw "screenshot pull failed: $local"
    }

    try { Add-Type -AssemblyName System.Drawing -ErrorAction Stop } catch { throw "System.Drawing unavailable for screenshot verification: $($_.Exception.Message)" }
    $bitmap = [System.Drawing.Bitmap]::new($local)
    try {
        $nonBlack = 0
        $sampleCount = 0
        $minLum = 765
        $maxLum = 0
        for ($iy = 0; $iy -lt $GridY; $iy++) {
            $y = [Math]::Min($bitmap.Height - 1, [Math]::Floor(($iy + 0.5) * $bitmap.Height / $GridY))
            for ($ix = 0; $ix -lt $GridX; $ix++) {
                $x = [Math]::Min($bitmap.Width - 1, [Math]::Floor(($ix + 0.5) * $bitmap.Width / $GridX))
                $pixel = $bitmap.GetPixel($x, $y)
                $lum = [int]$pixel.R + [int]$pixel.G + [int]$pixel.B
                if ($lum -gt 30) { $nonBlack++ }
                if ($lum -lt $minLum) { $minLum = $lum }
                if ($lum -gt $maxLum) { $maxLum = $lum }
                $sampleCount++
            }
        }
        $bytes = (Get-Item -LiteralPath $local).Length
        [pscustomobject]@{
            Path = $local
            Bytes = $bytes
            Width = $bitmap.Width
            Height = $bitmap.Height
            Samples = $sampleCount
            NonBlackPercent = if ($sampleCount -gt 0) { 100.0 * $nonBlack / $sampleCount } else { 0.0 }
            LumaSpread = $maxLum - $minLum
        }
    }
    finally { $bitmap.Dispose() }
}

function Test-ForegroundPackage {
    param([Parameter(Mandatory = $true)][string]$PackageRegex)

    $window = Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "dumpsys", "window") -IgnoreExit
    if ($window.Output -match "mCurrentFocus=.*$PackageRegex") { return $true }

    $activities = Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "dumpsys", "activity", "activities") -IgnoreExit
    if ($activities.Output -match "topResumedActivity=.*$PackageRegex") { return $true }
    return $false
}

function Test-Gl60Foreground {
    return Test-ForegroundPackage -PackageRegex "com\.chimera\.gl60/(?:\.MainActivity|com\.chimera\.gl60\.MainActivity)"
}

function Wait-VisiblePackageFrame {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$PackageRegex,
        [int]$TimeoutSec = 30,
        [scriptblock]$Relaunch
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastError = "no screenshot attempted"
    $attempt = 0
    while ((Get-Date) -lt $deadline) {
        $attempt++
        if (-not (Test-ForegroundPackage -PackageRegex $PackageRegex)) {
            $lastError = "$Name is not foreground"
            Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "input", "keyevent", "224") -IgnoreExit | Out-Null
            Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "wm", "dismiss-keyguard") -IgnoreExit | Out-Null
            if ($null -ne $Relaunch -and ($attempt % 3) -eq 1) { & $Relaunch }
            Start-Sleep -Seconds 1
            continue
        }
        try {
            $stats = Get-AdbScreenshotStats -Name $Name
            Write-Host ("visible_frame_bytes={0}" -f $stats.Bytes)
            Write-Host ("visible_frame_nonblack_pct={0:N1}" -f $stats.NonBlackPercent)
            Write-Host ("visible_frame_luma_spread={0}" -f $stats.LumaSpread)
            if ($stats.Width -lt $MinWidth -or $stats.Height -lt $MinHeight) {
                throw "visible frame below floor: $($stats.Width)x$($stats.Height)"
            }
            if ($stats.Bytes -lt 20000) {
                throw "visible frame PNG too small; likely black/empty: $($stats.Bytes) bytes"
            }
            if ($stats.NonBlackPercent -lt 10.0 -or $stats.LumaSpread -lt 40) {
                throw "visible frame lacks content: nonBlack=$([math]::Round($stats.NonBlackPercent,1))% spread=$($stats.LumaSpread)"
            }
            return
        }
        catch {
            $lastError = $_.Exception.Message
            Start-Sleep -Seconds 1
        }
    }
    throw "$Name visible-frame gate failed before FPS measurement: $lastError"
}

function Wait-VisibleGl60Frame {
    param([int]$TimeoutSec = 30)

    Wait-VisiblePackageFrame `
        -Name "gl60-visible" `
        -PackageRegex "com\.chimera\.gl60/(?:\.MainActivity|com\.chimera\.gl60\.MainActivity)" `
        -TimeoutSec $TimeoutSec `
        -Relaunch { Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.chimera.gl60/.MainActivity") -IgnoreExit | Out-Null }
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

function Ensure-HostWindowVisible {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    $Process.Refresh()
    if ($Process.MainWindowHandle -eq [IntPtr]::Zero) { return }
    if (-not ([System.Management.Automation.PSTypeName]'ChimeraVerifier.NativeMethods').Type) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace ChimeraVerifier {
  public static class NativeMethods {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
  }
}
"@
    }
    [ChimeraVerifier.NativeMethods]::ShowWindow($Process.MainWindowHandle, 1) | Out-Null # SW_SHOWNORMAL
    [ChimeraVerifier.NativeMethods]::SetWindowPos($Process.MainWindowHandle, [IntPtr]::Zero, 40, 40, 1280, 760, 0x0040) | Out-Null # SWP_SHOWWINDOW
    [ChimeraVerifier.NativeMethods]::SetForegroundWindow($Process.MainWindowHandle) | Out-Null
}

function Get-PerfSamples {
    param([Parameter(Mandatory = $true)][string]$LogText)
    $machinePattern = 'CHIMERA_PERF guest=(?<guest>[0-9]+(?:\.[0-9]+)?) stream=(?<stream>[0-9]+(?:\.[0-9]+)?) render=(?<render>[0-9]+(?:\.[0-9]+)?) effective=(?<effective>[0-9]+(?:\.[0-9]+)?) dupPct=(?<dupPct>[0-9]+(?:\.[0-9]+)?).* total=(?<total>\d+)'
    $machineMatches = [regex]::Matches($LogText, $machinePattern)
    if ($machineMatches.Count -gt 0) {
        foreach ($match in $machineMatches) {
            [pscustomobject]@{
                Guest = [double]$match.Groups["guest"].Value
                Stream = [double]$match.Groups["stream"].Value
                Render = [double]$match.Groups["render"].Value
                Effective = [double]$match.Groups["effective"].Value
                DuplicatePercent = [double]$match.Groups["dupPct"].Value
                Total = [int]$match.Groups["total"].Value
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
    # Skip past the boot zeros and the workload warmup ramp. MeasureStartSamples is the
    # sample count captured after warmup by Exercise-DynamicGuest (0 for ParseOnly).
    $skip = [Math]::Max($WarmupPerfSamples, $script:MeasureStartSamples)
    if (($samples.Count - $skip) -lt 2) {
        throw "not enough steady-state perf samples: total=$($samples.Count) skip=$skip"
    }
    $steady = @($samples | Select-Object -Skip $skip)
    $zeroEffective = @($steady | Where-Object { $_.Effective -le 0 })
    if ($zeroEffective.Count -gt 0) {
        throw "post-warmup perf contains zero effective samples: count=$($zeroEffective.Count)"
    }
    $active = $steady
    if ($active.Count -eq 0) {
        throw "no perf samples in measured window"
    }
    $minEffective = ($active | Measure-Object -Property Effective -Minimum).Minimum
    $avgEffective = ($active | Measure-Object -Property Effective -Average).Average
    $maxDup = ($active | Measure-Object -Property DuplicatePercent -Maximum).Maximum

    if ($minEffective -lt $MinEffectiveFps) {
        throw "effective FPS floor below threshold: min=$minEffective threshold=$MinEffectiveFps"
    }
    if ($avgEffective -lt $MinAvgEffectiveFps) {
        throw "average effective FPS below threshold: avg=$([math]::Round($avgEffective,1)) threshold=$MinAvgEffectiveFps"
    }
    if ($maxDup -gt 5.0) {
        throw "duplicate rate too high during dynamic proof: maxDup=${maxDup}%"
    }

    Write-Host "perf_samples=$($active.Count)"
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

function Invoke-CheckedTool {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

function Build-Gl60SmokeApk {
    $sdk = Join-Path $RepoRoot "third_party\android-sdk"
    $buildTools = Join-Path $sdk "build-tools\34.0.0"
    $androidJar = Join-Path $sdk "platforms\android-34\android.jar"
    $sourceRoot = Join-Path $RepoRoot "tools\chimera-gl60-smoke"
    $outDir = Join-Path $RepoRoot "build\gl60-smoke"
    $generatedDir = Join-Path $outDir "gen"
    $classesDir = Join-Path $outDir "classes"
    $dexDir = Join-Path $outDir "dex"
    $classesJar = Join-Path $outDir "classes.jar"
    $compiledResources = Join-Path $outDir "compiled.zip"
    $unsignedApk = Join-Path $outDir "gl60-unsigned.apk"
    $alignedApk = Join-Path $outDir "gl60-aligned.apk"
    $signedApk = Join-Path $outDir "gl60.apk"
    $keystore = Join-Path $outDir "debug.keystore"

    Require-File -Path (Join-Path $buildTools "aapt2.exe") -Name "aapt2"
    Require-File -Path (Join-Path $buildTools "d8.bat") -Name "d8"
    Require-File -Path (Join-Path $buildTools "zipalign.exe") -Name "zipalign"
    Require-File -Path (Join-Path $buildTools "apksigner.bat") -Name "apksigner"
    Require-File -Path $androidJar -Name "android.jar"
    Require-File -Path (Join-Path $sourceRoot "AndroidManifest.xml") -Name "GL60 smoke manifest"

    New-Item -ItemType Directory -Force $outDir, $generatedDir, $classesDir, $dexDir | Out-Null
    Get-ChildItem -LiteralPath $generatedDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    Get-ChildItem -LiteralPath $classesDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    Get-ChildItem -LiteralPath $dexDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
    New-Item -ItemType Directory -Force $generatedDir, $classesDir, $dexDir | Out-Null

    Invoke-CheckedTool { & (Join-Path $buildTools "aapt2.exe") compile --dir (Join-Path $sourceRoot "res") -o $compiledResources }
    Invoke-CheckedTool { & (Join-Path $buildTools "aapt2.exe") link `
            -I $androidJar `
            --manifest (Join-Path $sourceRoot "AndroidManifest.xml") `
            --java $generatedDir `
            -o $unsignedApk `
            $compiledResources }

    $javaFiles = @(Get-ChildItem -Path (Join-Path $sourceRoot "src") -Recurse -Filter *.java |
        ForEach-Object { $_.FullName })
    $javaFiles += @(Get-ChildItem -Path $generatedDir -Recurse -Filter *.java |
        ForEach-Object { $_.FullName })
    Invoke-CheckedTool { & javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $androidJar -d $classesDir $javaFiles }
    Invoke-CheckedTool { & jar cf $classesJar -C $classesDir . }
    Invoke-CheckedTool { & (Join-Path $buildTools "d8.bat") --lib $androidJar --output $dexDir $classesJar }
    Invoke-CheckedTool { & jar uf $unsignedApk -C $dexDir classes.dex }
    Invoke-CheckedTool { & (Join-Path $buildTools "zipalign.exe") -f 4 $unsignedApk $alignedApk }

    if (-not (Test-Path -LiteralPath $keystore -PathType Leaf)) {
        Invoke-CheckedTool { & keytool -genkeypair -keystore $keystore -storepass android -keypass android `
                -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
                -dname "CN=Chimera GL60 Debug,O=Chimera,C=TW" }
    }

    Invoke-CheckedTool { & (Join-Path $buildTools "apksigner.bat") sign `
            --ks $keystore `
            --ks-pass pass:android `
            --key-pass pass:android `
            --out $signedApk `
            $alignedApk }
    Invoke-CheckedTool { & (Join-Path $buildTools "apksigner.bat") verify $signedApk }
    return $signedApk
}

function Start-Gl60SmokeWorkload {
    param([Parameter(Mandatory = $true)][string]$ApkPath)

    # Remove any pre-existing install first: the debug keystore can change between
    # runs, and a signature mismatch (INSTALL_FAILED_UPDATE_INCOMPATIBLE) would
    # otherwise abort the run before any FPS is measured.
    Invoke-Adb -Arguments @("-s", $script:Serial, "uninstall", "com.chimera.gl60") -IgnoreExit | Out-Null
    Invoke-Adb -Arguments @("-s", $script:Serial, "install", "-r", $ApkPath) | Out-Null
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-W", "-n", "com.chimera.gl60/.MainActivity") | Out-Null
    Start-Sleep -Seconds 3
    Wait-VisibleGl60Frame -TimeoutSec 30
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "dumpsys", "gfxinfo", "com.chimera.gl60", "reset") -IgnoreExit | Out-Null
}

function Exercise-DynamicGuest {
    param(
        [Parameter(Mandatory = $true)][int]$Seconds,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$AppProcess
    )

    if (-not $GrpcOnly) {
        $apk = Build-Gl60SmokeApk
        Start-Gl60SmokeWorkload -ApkPath $apk
        # Warm up before the measured window: the GL app cold-starts with a
        # multi-hundred-ms first-frame stall and a JIT/cadence ramp. Wait for cadence,
        # then mark the measurement boundary at the current perf-sample count so the
        # steady-state gate evaluates only frames produced after warmup.
        Start-Sleep -Seconds $WarmupSeconds
        Ensure-HostWindowVisible -Process $AppProcess
        $warmupLogText = [string](Read-LogText -Path $script:messageLog)
        $script:MeasureStartSamples = @(Get-PerfSamples -LogText $warmupLogText).Count
        Write-Host "warmup_samples_skipped=$script:MeasureStartSamples"
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            Ensure-HostWindowVisible -Process $AppProcess
            Start-Sleep -Seconds 5
        }
        return
    }

    # gRPC-only mode proves stock screenshot delivery, not 60 FPS. Settings scroll is
    # enough to produce unique content without requiring a custom APK, but it still
    # must prove the expected Settings surface is foreground and non-black before
    # using frame cadence as evidence.
    Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.android.settings/.Settings") -IgnoreExit | Out-Null
    Wait-VisiblePackageFrame `
        -Name "grpc-settings-visible" `
        -PackageRegex "com\.android\.settings/(?:\.Settings|com\.android\.settings\.Settings)" `
        -TimeoutSec 30 `
        -Relaunch { Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "am", "start", "-n", "com.android.settings/.Settings") -IgnoreExit | Out-Null }
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "input", "swipe", "960", "900", "960", "100", "150") -IgnoreExit | Out-Null
        Start-Sleep -Milliseconds 60
    }
}

if (-not [string]::IsNullOrWhiteSpace($ParseOnlyLog)) {
    $logForParse = Read-LogText -Path $ParseOnlyLog
    # ANGLE is an R&D-only compositor path; true-1080p60 verifier must not inherit it from the shell.
    Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_ANGLE -ErrorAction SilentlyContinue

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

# Pick a free emulator console/ADB port pair. The ADB port is console+1;
# local services can bind only the odd ADB side (for example 5555/5561), which
# makes the emulator boot internally but prevents adb from seeing emulator-PORT.
if ([string]::IsNullOrWhiteSpace($env:CHIMERA_EMULATOR_CONSOLE_PORT)) {
    $env:CHIMERA_EMULATOR_CONSOLE_PORT = "$(Get-FreeEmulatorConsolePort)"
    Write-Host "selected_console_port=$env:CHIMERA_EMULATOR_CONSOLE_PORT"
}

$originalSerial = $script:Serial
# Auto-derive ADB serial from CHIMERA_EMULATOR_CONSOLE_PORT when using the default serial.
if ($Serial -eq "emulator-5554" -and $env:CHIMERA_EMULATOR_CONSOLE_PORT -match '^\d+$') {
    $Script:Serial = "emulator-$env:CHIMERA_EMULATOR_CONSOLE_PORT"
    Write-Host "ADB serial overridden from CHIMERA_EMULATOR_CONSOLE_PORT: $Script:Serial"
}

$env:PATH = "$QtBin;$env:PATH"
$logDir = Join-Path $RepoRoot "tmp"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stderrLog = Join-Path $logDir "chimera-true-1080p60.err"
$stdoutLog = Join-Path $logDir "chimera-true-1080p60.out"
$script:messageLog = Join-Path $logDir "chimera-true-1080p60.log"
Remove-Item -LiteralPath $stderrLog, $stdoutLog, $script:messageLog -Force -ErrorAction SilentlyContinue

$savedEnv = @{
    CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE = $env:CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE
    CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE = $env:CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE
    CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE = $env:CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE
    CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE = $env:CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE
    CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK = $env:CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK
    CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES = $env:CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES
    CHIMERA_GFXSTREAM_HEADLESS_ANGLE = $env:CHIMERA_GFXSTREAM_HEADLESS_ANGLE
    CHIMERA_QUICK_BOOT = $env:CHIMERA_QUICK_BOOT
    CHIMERA_GRPC_TRANSPORT = $env:CHIMERA_GRPC_TRANSPORT
    CHIMERA_VIDEO_TRANSPORT = $env:CHIMERA_VIDEO_TRANSPORT
    CHIMERA_LOG_PATH = $env:CHIMERA_LOG_PATH
    CHIMERA_EMULATOR_PATH = $env:CHIMERA_EMULATOR_PATH
    CHIMERA_EMULATOR_CONSOLE_PORT = $env:CHIMERA_EMULATOR_CONSOLE_PORT
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
        Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES -ErrorAction SilentlyContinue
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
        $env:CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES = "1"
        $env:CHIMERA_EMULATOR_PATH = (Resolve-Path -LiteralPath $ResolvedRuntime).Path
        $runtimeArg = "--gfxstream-shared-texture"
    } else {
        $env:CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE = "1"
        $env:CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE = "1"
        Remove-Item Env:\CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES -ErrorAction SilentlyContinue
        $env:CHIMERA_EMULATOR_PATH = (Resolve-Path -LiteralPath $ResolvedRuntime).Path
        $runtimeArg = "--emugl-shared-texture"
    }
    $env:CHIMERA_QUICK_BOOT = "0"
    $env:CHIMERA_LOG_PATH = $script:messageLog
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
    $startProcessArgs = @{
        FilePath = "cmd.exe"
        ArgumentList = $cmdArgs
        WorkingDirectory = $RepoRoot
        PassThru = $true
    }
    if ($HideHostWindow) {
        $startProcessArgs.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    }
    $process = Start-Process @startProcessArgs

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
    Exercise-DynamicGuest -Seconds $MeasureSeconds -AppProcess $process
    Start-Sleep -Seconds 6
    $logText = (Read-LogText -Path $script:messageLog)
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
    if ($savedEnv.CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES -eq $null) { Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES -ErrorAction SilentlyContinue } else { $env:CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES = $savedEnv.CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES }
    if ($savedEnv.CHIMERA_GFXSTREAM_HEADLESS_ANGLE -eq $null) { Remove-Item Env:\CHIMERA_GFXSTREAM_HEADLESS_ANGLE -ErrorAction SilentlyContinue } else { $env:CHIMERA_GFXSTREAM_HEADLESS_ANGLE = $savedEnv.CHIMERA_GFXSTREAM_HEADLESS_ANGLE }
    if ($savedEnv.CHIMERA_QUICK_BOOT -eq $null) { Remove-Item Env:\CHIMERA_QUICK_BOOT -ErrorAction SilentlyContinue } else { $env:CHIMERA_QUICK_BOOT = $savedEnv.CHIMERA_QUICK_BOOT }
    if ($savedEnv.CHIMERA_GRPC_TRANSPORT -eq $null) { Remove-Item Env:\CHIMERA_GRPC_TRANSPORT -ErrorAction SilentlyContinue } else { $env:CHIMERA_GRPC_TRANSPORT = $savedEnv.CHIMERA_GRPC_TRANSPORT }
    if ($savedEnv.CHIMERA_VIDEO_TRANSPORT -eq $null) { Remove-Item Env:\CHIMERA_VIDEO_TRANSPORT -ErrorAction SilentlyContinue } else { $env:CHIMERA_VIDEO_TRANSPORT = $savedEnv.CHIMERA_VIDEO_TRANSPORT }
    if ($savedEnv.CHIMERA_LOG_PATH -eq $null) { Remove-Item Env:\CHIMERA_LOG_PATH -ErrorAction SilentlyContinue } else { $env:CHIMERA_LOG_PATH = $savedEnv.CHIMERA_LOG_PATH }
    if ($savedEnv.CHIMERA_EMULATOR_CONSOLE_PORT -eq $null) { Remove-Item Env:\CHIMERA_EMULATOR_CONSOLE_PORT -ErrorAction SilentlyContinue } else { $env:CHIMERA_EMULATOR_CONSOLE_PORT = $savedEnv.CHIMERA_EMULATOR_CONSOLE_PORT }
    if ($savedEnv.CHIMERA_EMULATOR_PATH -eq $null) { Remove-Item Env:\CHIMERA_EMULATOR_PATH -ErrorAction SilentlyContinue } else { $env:CHIMERA_EMULATOR_PATH = $savedEnv.CHIMERA_EMULATOR_PATH }
    if ($savedEnv.CHIMERA_ENABLE_NATIVE_EMBED -eq $null) { Remove-Item Env:\CHIMERA_ENABLE_NATIVE_EMBED -ErrorAction SilentlyContinue } else { $env:CHIMERA_ENABLE_NATIVE_EMBED = $savedEnv.CHIMERA_ENABLE_NATIVE_EMBED }
    if ($savedEnv.CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW -eq $null) { Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW -ErrorAction SilentlyContinue } else { $env:CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW = $savedEnv.CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW }
    if ($savedEnv.CHIMERA_ENABLE_WINDOW_CAPTURE -eq $null) { Remove-Item Env:\CHIMERA_ENABLE_WINDOW_CAPTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_ENABLE_WINDOW_CAPTURE = $savedEnv.CHIMERA_ENABLE_WINDOW_CAPTURE }
    if ($savedEnv.CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE -eq $null) { Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE -ErrorAction SilentlyContinue } else { $env:CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE = $savedEnv.CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE }
    if ($savedEnv.CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW -eq $null) { Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW -ErrorAction SilentlyContinue } else { $env:CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW = $savedEnv.CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW }
    if ($savedEnv.CHIMERA_EMULATOR_START_VISIBLE -eq $null) { Remove-Item Env:\CHIMERA_EMULATOR_START_VISIBLE -ErrorAction SilentlyContinue } else { $env:CHIMERA_EMULATOR_START_VISIBLE = $savedEnv.CHIMERA_EMULATOR_START_VISIBLE }
    $script:Serial = $originalSerial

    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}
