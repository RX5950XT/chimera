[CmdletBinding()]
param(
    [ValidateRange(10, 600)]
    [int]$BootTimeoutSec = 180,

    [ValidateRange(10, 600)]
    [int]$QuickBootTimeoutSec = 90,

    [ValidateRange(1, 300)]
    [int]$MaxQuickBootSec = 25,

    [string]$Configuration = "Release",
    [string]$SnapshotName = "chimera_quickboot",
    [string]$Serial = "emulator-5554",
    [string]$AvdName = "chimera_dev",
    [switch]$NoCleanStart
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$AppExe = Join-Path $RepoRoot "build\$Configuration\chimera-ui.exe"
$Adb = Join-Path $RepoRoot "third_party\android-sdk\platform-tools\adb.exe"
$AvdDir = Join-Path $RepoRoot "third_party\android-avd\$AvdName.avd"
$QtBin = "C:\Qt\6.8.3\msvc2022_64\bin"

# Reuse the shared, command-line-filtered process helpers + free-port picker so this
# verifier doesn't drift from the others and never force-kills unrelated emulators.
. (Join-Path $PSScriptRoot 'ChimeraVerifyCommon.ps1')

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

    [pscustomobject]@{
        ExitCode = $exitCode
        Output = $text
    }
}

# Get-ChimeraProcesses / Stop-ChimeraProcesses / Wait-NoChimeraProcesses come from
# the dot-sourced ChimeraVerifyCommon.ps1 (cmdline-filtered). No local copies —
# re-copying the shared harness is a drift source (see tasks/lessons.md).

function Remove-StaleAvdLocks {
    if (-not (Test-Path -LiteralPath $script:AvdDir -PathType Container)) {
        return
    }

    $locks = @(Get-ChildItem -LiteralPath $script:AvdDir -Filter "*.lock" -ErrorAction SilentlyContinue)
    if ($locks.Count -eq 0) {
        return
    }

    foreach ($lock in $locks) {
        try {
            $lock.Delete()
        }
        catch {
            Write-Verbose "Could not remove stale AVD lock: $($lock.FullName)"
        }
    }
}

function Start-Chimera {
    param([bool]$QuickBoot)

    $oldQuickBoot = $env:CHIMERA_QUICK_BOOT
    try {
        $env:CHIMERA_QUICK_BOOT = if ($QuickBoot) { "1" } else { "0" }
        $process = Start-Process -FilePath $script:AppExe -WorkingDirectory $script:RepoRoot -PassThru
        Start-Sleep -Seconds 1
        if ($process.HasExited) {
            throw "chimera-ui exited early with code $($process.ExitCode)"
        }

        $process
    }
    finally {
        if ($null -eq $oldQuickBoot) {
            Remove-Item Env:\CHIMERA_QUICK_BOOT -ErrorAction SilentlyContinue
        }
        else {
            $env:CHIMERA_QUICK_BOOT = $oldQuickBoot
        }
    }
}

function Wait-AndroidBoot {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][int]$TimeoutSec,
        $AppProcess = $null
    )

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $deadline = (Get-Date).AddSeconds($TimeoutSec)

    while ((Get-Date) -lt $deadline) {
        if ($null -ne $AppProcess -and $AppProcess.HasExited -and @(Get-ChimeraProcesses).Count -eq 0) {
            throw "$Label exited before emulator reached sys.boot_completed=1"
        }

        $state = Invoke-Adb -Arguments @("-s", $script:Serial, "get-state") -IgnoreExit
        if ($state.ExitCode -eq 0) {
            $boot = Invoke-Adb -Arguments @("-s", $script:Serial, "shell", "getprop", "sys.boot_completed") -IgnoreExit
            if ($boot.Output.Trim() -eq "1") {
                $timer.Stop()
                $seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 1)
                Write-Host "$Label boot_completed_sec=$seconds"
                return $seconds
            }
        }

        Start-Sleep -Seconds 1
    }

    throw "$Label did not reach sys.boot_completed=1 within ${TimeoutSec}s"
}

function Stop-Emulator {
    # Graceful "emu kill" first and WAIT for exit: with Quick Boot active the
    # emulator writes its default_boot snapshot on this signal; force-killing
    # during the save leaves the snapshot unsaved (next boot falls back to cold).
    Invoke-Adb -Arguments @("-s", $script:Serial, "emu", "kill") -IgnoreExit | Out-Null
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline -and @(Get-ChimeraProcesses).Count -gt 0) {
        Start-Sleep -Seconds 1
    }
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}

function Restore-EnvValue {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()][string]$Value
    )
    if ($null -eq $Value) {
        Remove-Item "Env:\$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:\$Name" -Value $Value
    }
}

Require-File -Path $AppExe -Name "chimera-ui.exe"
Require-File -Path $Adb -Name "adb.exe"

$env:PATH = "$QtBin;$env:PATH"

# Pick a free emulator console/ADB port pair (mirrors the sibling verifiers) so a
# local service binding the default 5554/5555 can't cause a false boot timeout.
if (-not $PSBoundParameters.ContainsKey('Serial')) {
    $consolePort = Get-FreeEmulatorConsolePort
    $Serial = "emulator-$consolePort"
} else {
    $consolePort = [int]($Serial -replace '^emulator-', '')
}
Write-Host "console_port=$consolePort serial=$Serial"

$savedEnv = @{
    CHIMERA_EMULATOR_CONSOLE_PORT = $env:CHIMERA_EMULATOR_CONSOLE_PORT
    CHIMERA_ENABLE_NATIVE_EMBED = $env:CHIMERA_ENABLE_NATIVE_EMBED
    CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW = $env:CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW
    CHIMERA_ENABLE_WINDOW_CAPTURE = $env:CHIMERA_ENABLE_WINDOW_CAPTURE
    CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE = $env:CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE
    CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW = $env:CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW
    CHIMERA_EMULATOR_START_VISIBLE = $env:CHIMERA_EMULATOR_START_VISIBLE
}
Remove-Item Env:\CHIMERA_ENABLE_NATIVE_EMBED -ErrorAction SilentlyContinue
Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW -ErrorAction SilentlyContinue
Remove-Item Env:\CHIMERA_ENABLE_WINDOW_CAPTURE -ErrorAction SilentlyContinue
Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE -ErrorAction SilentlyContinue
Remove-Item Env:\CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW -ErrorAction SilentlyContinue
Remove-Item Env:\CHIMERA_EMULATOR_START_VISIBLE -ErrorAction SilentlyContinue
$env:CHIMERA_EMULATOR_CONSOLE_PORT = "$consolePort"

if (-not $NoCleanStart) {
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}

$fullBootSec = $null
$quickBootSec = $null

try {
    Write-Host "phase=full_boot"
    $fullProcess = Start-Chimera -QuickBoot $false
    $fullBootSec = Wait-AndroidBoot -Label "full_boot" -TimeoutSec $BootTimeoutSec -AppProcess $fullProcess
    Stop-Emulator

    # Seed: a Quick Boot session cold-boots (no default_boot yet) and saves the
    # snapshot on the graceful stop. Only the SECOND Quick Boot launch measures
    # the actual snapshot-load speed.
    Write-Host "phase=quick_boot_seed"
    $seedProcess = Start-Chimera -QuickBoot $true
    Wait-AndroidBoot -Label "quick_boot_seed" -TimeoutSec $BootTimeoutSec -AppProcess $seedProcess | Out-Null
    Stop-Emulator

    Write-Host "phase=quick_boot"
    $quickProcess = Start-Chimera -QuickBoot $true
    $quickBootSec = Wait-AndroidBoot -Label "quick_boot" -TimeoutSec $QuickBootTimeoutSec -AppProcess $quickProcess
    Stop-Emulator

    if ($quickBootSec -gt $MaxQuickBootSec) {
        throw "Quick Boot took ${quickBootSec}s; threshold is ${MaxQuickBootSec}s"
    }

    Write-Host "result=pass"
    Write-Host "full_boot_sec=$fullBootSec"
    Write-Host "quick_boot_sec=$quickBootSec"
    Write-Host "quick_boot_threshold_sec=$MaxQuickBootSec"
}
finally {
    Restore-EnvValue -Name "CHIMERA_EMULATOR_CONSOLE_PORT" -Value $savedEnv.CHIMERA_EMULATOR_CONSOLE_PORT
    Restore-EnvValue -Name "CHIMERA_ENABLE_NATIVE_EMBED" -Value $savedEnv.CHIMERA_ENABLE_NATIVE_EMBED
    Restore-EnvValue -Name "CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW" -Value $savedEnv.CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW
    Restore-EnvValue -Name "CHIMERA_ENABLE_WINDOW_CAPTURE" -Value $savedEnv.CHIMERA_ENABLE_WINDOW_CAPTURE
    Restore-EnvValue -Name "CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE" -Value $savedEnv.CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE
    Restore-EnvValue -Name "CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW" -Value $savedEnv.CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW
    Restore-EnvValue -Name "CHIMERA_EMULATOR_START_VISIBLE" -Value $savedEnv.CHIMERA_EMULATOR_START_VISIBLE
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}
