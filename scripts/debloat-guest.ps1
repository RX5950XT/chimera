<#
.SYNOPSIS
  Trim unnecessary Google/system apps from the running Chimera guest to save
  memory and stop background crash-loops (Session 105).

.DESCRIPTION
  Uses `pm disable-user --user 0` (reversible, no root needed) on a curated list
  of user-facing apps that a gaming/benchmark emulator does not need. Disabling
  cancels their persisted JobScheduler jobs, which fixes the "Apps may not
  schedule more than 150 distinct jobs" crash-loop (Google Photos et al.) that
  accumulates in persistent /data.

  Kept on purpose (do NOT disable — system-critical or user-facing essentials):
    Play Store (com.android.vending), GMS (com.google.android.gms/.gsf),
    SystemUI, Settings, Gboard, WebView/Chrome, permissioncontroller,
    Chimera launcher, and all providers. com.google.android.as and
    com.google.android.settings.intelligence are intentionally left ENABLED —
    disabling them mid-run triggers a "Process system isn't responding" ANR
    (system_server binds to them). Their job accumulation is instead reset by a
    one-time disable+enable if they ever crash-loop again.

  System LOCALE is not handled here: the non-root user image has no CLI to set
  the system locale ("-prop persist.sys.locale" and "setprop persist.*" are both
  SELinux-denied). Set it once via Settings > System > Languages (add 繁體中文
  (台灣), remove English); it writes persist.sys.locale and persists in /data.

.PARAMETER Serial
  adb serial (default emulator-5554).

.PARAMETER Restore
  Re-enable every package in the list instead of disabling.
#>
[CmdletBinding()]
param(
    [string]$Serial = "emulator-5554",
    [switch]$Restore
)

$ErrorActionPreference = "Stop"

function Resolve-Adb {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\third_party\android-sdk\platform-tools\adb.exe"),
        (Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"),
        "adb"
    )
    foreach ($c in $candidates) { if (Get-Command $c -ErrorAction SilentlyContinue) { return $c } }
    throw "adb not found (checked third_party, LOCALAPPDATA SDK, PATH)"
}

# Curated bloat set — pure user-facing apps unneeded for a gaming/benchmark guest.
$Bloat = @(
    'com.google.android.apps.photos',            # crash-looper (150-job cap)
    'com.google.android.youtube',
    'com.google.android.apps.youtube.music',
    'com.google.android.gm',                      # Gmail
    'com.google.android.apps.messaging',
    'com.google.android.apps.maps',
    'com.google.android.calendar',
    'com.google.android.contacts',
    'com.google.android.deskclock',
    'com.google.android.apps.docs',               # Drive/Docs
    'com.google.android.apps.wellbeing',
    'com.google.android.apps.restore',
    'com.google.android.projection.gearhead',     # Android Auto
    'com.google.android.googlequicksearchbox',    # Google app / Assistant (heavy bg)
    'com.google.android.apps.customization.pixel',
    'com.google.android.apps.wallpaper',
    'com.google.android.apps.wallpaper.nexus',
    'com.android.wallpaper.livepicker',
    'com.google.android.tag',
    'com.google.android.apps.nexuslauncher'       # Pixel Launcher (Chimera launcher is HOME)
)

$adb = Resolve-Adb
$action = if ($Restore) { "enable" } else { "disable-user --user 0" }
$verb   = if ($Restore) { "Re-enabling" } else { "Disabling" }

Write-Host "$verb $($Bloat.Count) packages on $Serial ..."
foreach ($pkg in $Bloat) {
    $out = (& $adb -s $Serial shell "pm $action $pkg" 2>&1) -join ' '
    "{0,-46} {1}" -f $pkg, ($out.Trim() -replace '.*new state: ', '' -replace 'Package .* ', '')
}

$disabledCount = (& $adb -s $Serial shell "pm list packages -d 2>/dev/null | wc -l").Trim()
Write-Host "Done. Guest now reports $disabledCount disabled packages."
