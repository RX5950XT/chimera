param(
    [string]$AospRoot = "tmp\aosp",
    [string]$Branch = "main",
    [ValidateRange(1, 100000)]
    [int]$Depth = 1,
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$aospPath = if ([System.IO.Path]::IsPathRooted($AospRoot)) {
    $AospRoot
} else {
    Join-Path $repoRoot $AospRoot
}

$repos = @(
    @{ Path = "hardware\google\gfxstream"; Url = "https://android.googlesource.com/platform/hardware/google/gfxstream" },
    @{ Path = "external\angle"; Url = "https://android.googlesource.com/platform/external/angle" },
    @{ Path = "hardware\google\aemu"; Url = "https://android.googlesource.com/platform/hardware/google/aemu" },
    @{ Path = "external\gfxstream-protocols"; Url = "https://android.googlesource.com/platform/external/gfxstream-protocols" },
    @{ Path = "external\flatbuffers"; Url = "https://android.googlesource.com/platform/external/flatbuffers" },
    @{ Path = "external\libdrm"; Url = "https://android.googlesource.com/platform/external/libdrm" }
)

$missing = New-Object System.Collections.Generic.List[object]
foreach ($repo in $repos) {
    $path = Join-Path $aospPath $repo.Path
    if (Test-Path -LiteralPath (Join-Path $path ".git") -PathType Container) {
        Write-Host "present $($repo.Path)"
        continue
    }
    $missing.Add($repo) | Out-Null
}

if ($CheckOnly) {
    if ($missing.Count -gt 0) {
        foreach ($repo in $missing) {
            Write-Host "missing $($repo.Path)"
        }
        exit 2
    }
    Write-Host "gfxstream_deps=present"
    exit 0
}

New-Item -ItemType Directory -Force -Path $aospPath | Out-Null
foreach ($repo in $missing) {
    $path = Join-Path $aospPath $repo.Path
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    Write-Host "clone $($repo.Path)"
    $hasBranch = $false
    if (![string]::IsNullOrWhiteSpace($Branch) -and $Branch -ne "main") {
        $remote = git ls-remote --heads $repo.Url $Branch
        if ($LASTEXITCODE -eq 0 -and ![string]::IsNullOrWhiteSpace(($remote | Out-String).Trim())) {
            $hasBranch = $true
        }
    } elseif ($Branch -eq "main") {
        $hasBranch = $true
    }

    if ($hasBranch) {
        git clone --depth=$Depth --branch $Branch $repo.Url $path
    } else {
        Write-Warning "branch '$Branch' not found for $($repo.Path); using repository default"
        git clone --depth=$Depth $repo.Url $path
    }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "gfxstream_deps=ready"
