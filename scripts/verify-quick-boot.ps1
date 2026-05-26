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

function Get-ChimeraProcesses {
    $processes = @()
    $processes += Get-Process -Name "chimera-ui" -ErrorAction SilentlyContinue
    $processes += Get-Process -Name "emulator" -ErrorAction SilentlyContinue
    $processes += Get-Process -Name "qemu-system*" -ErrorAction SilentlyContinue
    $processes | Where-Object { $null -ne $_ }
}

function Stop-ChimeraProcesses {
    $processes = @(Get-ChimeraProcesses)
    if ($processes.Count -eq 0) {
        return
    }

    $processes | Stop-Process -Force -ErrorAction SilentlyContinue
}

function Wait-NoChimeraProcesses {
    param([int]$TimeoutSec = 30)

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (@(Get-ChimeraProcesses).Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 500
    }

    $remaining = @(Get-ChimeraProcesses | ForEach-Object { "$($_.ProcessName):$($_.Id)" })
    throw "Timed out waiting for Chimera processes to exit: $($remaining -join ', ')"
}

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

function Save-Snapshot {
    Write-Host "saving_snapshot=$script:SnapshotName"
    Invoke-Adb -Arguments @("-s", $script:Serial, "emu", "avd", "snapshot", "save", $script:SnapshotName) | Out-Null
}

function Stop-Emulator {
    Invoke-Adb -Arguments @("-s", $script:Serial, "emu", "kill") -IgnoreExit | Out-Null
    Start-Sleep -Seconds 2
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}

Require-File -Path $AppExe -Name "chimera-ui.exe"
Require-File -Path $Adb -Name "adb.exe"

$env:PATH = "$QtBin;$env:PATH"

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
    Save-Snapshot
    Stop-Emulator

    Write-Host "phase=quick_boot"
    $quickProcess = Start-Chimera -QuickBoot $true
    $quickBootSec = Wait-AndroidBoot -Label "quick_boot" -TimeoutSec $QuickBootTimeoutSec -AppProcess $quickProcess
    Save-Snapshot
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
    Stop-ChimeraProcesses
    Wait-NoChimeraProcesses -TimeoutSec 30
    Remove-StaleAvdLocks
}
