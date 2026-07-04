# ChimeraVerifyCommon.ps1
#
# Side-effect-free shared library of Chimera runtime-verifier helpers.
# Dot-source it from a verifier script: . (Join-Path $PSScriptRoot 'ChimeraVerifyCommon.ps1')
#
# Functions reference caller script-scope variables ($script:Adb, $script:Serial,
# $script:RepoRoot, $script:AvdName, $script:AvdDir, $MinWidth, $MinHeight). The
# dot-sourcing script MUST define those before calling the helpers (same contract
# the monolithic verifiers always used). This file defines functions only — no
# top-level executable statements — so it is safe to dot-source anywhere.

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

# Emulator gRPC control port the host will target, derived from the console port
# EXACTLY as chimera-ui does (main.cpp): 8554 + ((console - 5554) / 2) * 2.
# Input (touch/key) is POSTed here; if it is not free, a stale/other emulator
# answers 127.0.0.1:<grpc> instead and every click is silently dropped while the
# shared-texture display still works ("picture but nothing is clickable").
function Get-EmulatorGrpcPort {
    param([Parameter(Mandatory = $true)][int]$ConsolePort)
    return 8554 + [int](([math]::Floor(($ConsolePort - 5554) / 2)) * 2)
}

function Get-FreeEmulatorConsolePort {
    # Android Emulator reserves an even console port and the following odd ADB port.
    # Some local services (for example VPN clients) bind 5555/5561, so pick a free pair.
    # Also require the derived gRPC control port free: a leftover emulator holding it
    # steals the input channel from the new instance (display works, clicks do not).
    foreach ($port in 5560, 5570, 5580, 5590, 5600, 5610, 5620, 5630, 5640, 5650, 5660, 5670) {
        if ((Test-TcpPortAvailable -Port $port) -and
            (Test-TcpPortAvailable -Port ($port + 1)) -and
            (Test-TcpPortAvailable -Port (Get-EmulatorGrpcPort -ConsolePort $port))) {
            return $port
        }
    }
    throw "no free Android emulator console/ADB/gRPC port set found"
}

function Resolve-EmulatorConsolePort {
    param([Parameter(Mandatory = $true)][int]$ConsolePort)

    if ($ConsolePort -eq 0) { return Get-FreeEmulatorConsolePort }
    if ($ConsolePort -lt 5554 -or $ConsolePort -gt 5680 -or ($ConsolePort % 2) -ne 0) {
        throw "Android emulator console port must be an even port from 5554 to 5680 (ADB uses console+1); got $ConsolePort"
    }
    return $ConsolePort
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
    # MainWindowHandle on an exited process throws; PowerShell's ETS surfaces that
    # as $null, and a later [IntPtr]$null cast kills the verifier mid-run (seen when
    # the user closed the chimera window). Treat every unreadable handle as Zero.
    $hwnd = [IntPtr]::Zero
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            $h = $Process.MainWindowHandle
            if ($null -ne $h) { $hwnd = $h }
        }
    } catch {}
    if ($hwnd -eq [IntPtr]::Zero) {
        # The verifier launches chimera-ui via cmd.exe with redirected output, so
        # the passed process usually has no window. Target the real chimera-ui Qt
        # window instead — otherwise foreground-keeping is a no-op and the Qt
        # render thread gets occlusion-throttled when another window steals focus.
        $ui = @(Get-Process -Name chimera-ui -ErrorAction SilentlyContinue |
            Where-Object { try { -not $_.HasExited -and $_.MainWindowHandle -ne [IntPtr]::Zero } catch { $false } }) |
            Select-Object -First 1
        if ($null -ne $ui) {
            try {
                $h = $ui.MainWindowHandle
                if ($null -ne $h) { $hwnd = $h }
            } catch {}
        }
    }
    if ($hwnd -eq [IntPtr]::Zero) { return }
    if (-not ([System.Management.Automation.PSTypeName]'ChimeraVerifier.NativeMethods').Type) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace ChimeraVerifier {
  public static class NativeMethods {
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr GetWindowLongPtrW(IntPtr hWnd, int nIndex);
  }
}
"@
    }
    # Only act when the window is NOT already pinned-topmost and non-minimized.
    # Re-issuing SetWindowPos/SetForegroundWindow every tick forces window-manager
    # work that periodically hitches the Qt Quick render thread (a single ~1s dip
    # drags the min-FPS sample below the 57 floor). Checking WS_EX_TOPMOST first
    # keeps the steady-state ticks nearly free and removes that self-inflicted jitter.
    $GWL_EXSTYLE = -20
    $WS_EX_TOPMOST = [IntPtr]0x00000008
    $exStyle = [ChimeraVerifier.NativeMethods]::GetWindowLongPtrW([IntPtr]$hwnd, $GWL_EXSTYLE)
    $isTopmost = (([long]$exStyle -band [long]$WS_EX_TOPMOST) -ne 0)
    $isMinimized = [ChimeraVerifier.NativeMethods]::IsIconic([IntPtr]$hwnd)
    if ($isTopmost -and -not $isMinimized) { return }
    # Window origin is configurable (env CHIMERA_VERIFY_WINDOW_ORIGIN="x,y") so the
    # host window can be parked on a secondary monitor during measurement without
    # covering the user's primary screen. Default keeps the primary (40,40) spot.
    # Negative coords (a left/secondary monitor) are valid.
    $winX = 40; $winY = 40
    $origin = [Environment]::GetEnvironmentVariable('CHIMERA_VERIFY_WINDOW_ORIGIN', 'Process')
    if ($origin -and $origin -match '^\s*(-?\d+)\s*,\s*(-?\d+)\s*$') {
        $winX = [int]$Matches[1]; $winY = [int]$Matches[2]
    }
    # Pin HWND_TOPMOST (-1) so the Qt Quick render thread is never occlusion-throttled
    # to 0 FPS when another window holds focus during measurement. SetForegroundWindow
    # from a background process is unreliable (Windows foreground lock), but TOPMOST
    # z-order keeps the window exposed regardless of who owns the foreground.
    [ChimeraVerifier.NativeMethods]::ShowWindow($hwnd, 1) | Out-Null # SW_SHOWNORMAL
    [ChimeraVerifier.NativeMethods]::SetWindowPos([IntPtr]$hwnd, [IntPtr](-1), $winX, $winY, 1280, 760, 0x0040) | Out-Null # HWND_TOPMOST | SWP_SHOWWINDOW
    [ChimeraVerifier.NativeMethods]::SetForegroundWindow($hwnd) | Out-Null
}

function Get-HostWindowPixelStats {
    # PrintWindow-captures the chimera-ui window and samples the CENTRAL guest
    # display region (skips the always-lit side panel / top bar). This is the only
    # gate that checks what the USER actually sees: guest-side ADB screencaps and
    # host-side FPS counters both stayed green while the shared-texture window was
    # black for 15 sessions (Vulkan OPAQUE_WIN32 import never aliased the D3D11
    # texture, so sequences advanced over all-zero pixels).
    param(
        [string]$Name = "hostwindow",
        [int]$GridX = 24,
        [int]$GridY = 14
    )

    $ui = @(Get-Process -Name chimera-ui -ErrorAction SilentlyContinue |
        Where-Object { try { -not $_.HasExited -and $_.MainWindowHandle -ne [IntPtr]::Zero } catch { $false } }) |
        Select-Object -First 1
    if ($null -eq $ui) { throw "host window capture failed: no chimera-ui window" }
    $hwnd = $ui.MainWindowHandle

    if (-not ([System.Management.Automation.PSTypeName]'ChimeraVerifier.CaptureMethods').Type) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace ChimeraVerifier {
  public static class CaptureMethods {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  }
}
"@
    }
    try { Add-Type -AssemblyName System.Drawing -ErrorAction Stop } catch { throw "System.Drawing unavailable for host window capture: $($_.Exception.Message)" }

    $rect = New-Object ChimeraVerifier.CaptureMethods+RECT
    if (-not [ChimeraVerifier.CaptureMethods]::GetWindowRect([IntPtr]$hwnd, [ref]$rect)) {
        throw "host window capture failed: GetWindowRect"
    }
    $w = $rect.R - $rect.L
    $h = $rect.B - $rect.T
    if ($w -le 0 -or $h -le 0) { throw "host window capture failed: empty rect ${w}x${h}" }

    $safeName = $Name -replace '[^A-Za-z0-9_-]', '_'
    $local = Join-Path $script:RepoRoot "tmp\chimera-${safeName}.png"
    $bitmap = [System.Drawing.Bitmap]::new($w, $h)
    try {
        $gfx = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $hdc = $gfx.GetHdc()
            # PW_RENDERFULLCONTENT (2): captures D3D swapchain content, not just GDI.
            $ok = [ChimeraVerifier.CaptureMethods]::PrintWindow([IntPtr]$hwnd, $hdc, 2)
            $gfx.ReleaseHdc($hdc)
            if (-not $ok) { throw "host window capture failed: PrintWindow" }
        }
        finally { $gfx.Dispose() }
        $bitmap.Save($local, [System.Drawing.Imaging.ImageFormat]::Png)

        # Sample only x 10-70% / y 20-80%: inside the guest display area of the
        # default layout, clear of the right side panel and window chrome.
        $nonBlack = 0
        $sampleCount = 0
        $minLum = 765
        $maxLum = 0
        for ($iy = 0; $iy -lt $GridY; $iy++) {
            $y = [int]($h * (0.20 + 0.60 * ($iy + 0.5) / $GridY))
            for ($ix = 0; $ix -lt $GridX; $ix++) {
                $x = [int]($w * (0.10 + 0.60 * ($ix + 0.5) / $GridX))
                $pixel = $bitmap.GetPixel($x, $y)
                $lum = [int]$pixel.R + [int]$pixel.G + [int]$pixel.B
                if ($lum -gt 30) { $nonBlack++ }
                if ($lum -lt $minLum) { $minLum = $lum }
                if ($lum -gt $maxLum) { $maxLum = $lum }
                $sampleCount++
            }
        }
        [pscustomobject]@{
            Path = $local
            Width = $w
            Height = $h
            Samples = $sampleCount
            NonBlackPercent = if ($sampleCount -gt 0) { 100.0 * $nonBlack / $sampleCount } else { 0.0 }
            LumaSpread = $maxLum - $minLum
        }
    }
    finally { $bitmap.Dispose() }
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
    # Native tools legitimately write notes/warnings to stderr (e.g. javac's
    # "deprecated API" note). Under $ErrorActionPreference = "Stop" with stderr
    # redirected (detached verifier runs), Windows PowerShell turns those lines
    # into terminating NativeCommandErrors. Success/failure is the exit code.
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Command
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}
