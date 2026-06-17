param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDir,

    [string]$SourceDir = "",

    # R&D only: manually specify gfxstream source build ID when git log has no "Snap for" commit.
    # Requires -AllowMismatchedBuildId to take effect unless it happens to match the SDK build ID.
    [string]$SourceBuildId = "",

    # R&D only: allow mismatched gfxstream source build ID vs SDK emulator build ID.
    # This bypasses the ABI safety gate. Use only for exploratory testing.
    [switch]$AllowMismatchedBuildId
)

$ErrorActionPreference = "Stop"

$resolved = Resolve-Path -LiteralPath $RuntimeDir
$root = $resolved.Path
$emulator = Join-Path $root "emulator.exe"
$sourceProperties = Join-Path $root "source.properties"
$lib64 = Join-Path $root "lib64"
$gfxstream = Join-Path $lib64 "libgfxstream_backend.dll"
$manifest = Join-Path $lib64 "chimera-gfxstream-shared-texture.json"

# Fail closed: never leave an older "ready" manifest beside a runtime that has
# not passed the current ABI/import gates.
Remove-Item -LiteralPath $manifest -Force -ErrorAction SilentlyContinue

if (!(Test-Path -LiteralPath $emulator)) {
    throw "emulator.exe not found in runtime dir: $root"
}
if (!(Test-Path -LiteralPath $gfxstream)) {
    throw "gfxstream backend not found: $gfxstream"
}

$marker = "ChimeraGfxstreamVulkanSharedTextureBridge"
$bytes = [System.IO.File]::ReadAllBytes($gfxstream)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
if (!$text.Contains($marker)) {
    throw "gfxstream backend does not contain required Vulkan display-post bridge marker '$marker': $gfxstream"
}
$gpuCopyMarker = "ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopy"
if (!$text.Contains($gpuCopyMarker)) {
    throw "gfxstream backend does not contain required Vulkan GPU copy marker '$gpuCopyMarker': $gfxstream"
}
$requiredAbiExport = "gfxstream_backend_set_screen_background"
if (!$text.Contains($requiredAbiExport)) {
    throw "gfxstream backend ABI is incompatible with this SDK emulator; missing export '$requiredAbiExport': $gfxstream"
}
foreach ($requiredImport in @(
    "libandroid-emu-agents.dll",
    "libandroid-emu-protos.dll",
    "libandroid-emu-metrics.dll"
)) {
    if (!$text.Contains($requiredImport)) {
        throw "gfxstream backend ABI is incompatible with this SDK emulator; missing SDK runtime import '$requiredImport': $gfxstream"
    }
}

function Find-Dumpbin {
    $roots = @()
    $vsRoot = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022"
    if (Test-Path -LiteralPath $vsRoot) { $roots += $vsRoot }
    foreach ($rootPath in $roots) {
        $candidate = Get-ChildItem -LiteralPath $rootPath -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like "*\bin\Hostx64\x64\dumpbin.exe" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    return ""
}

function Test-PeExport {
    param(
        [Parameter(Mandatory = $true)][string]$Dll,
        [Parameter(Mandatory = $true)][string]$ExportName
    )
    $dumpbin = Find-Dumpbin
    if ([string]::IsNullOrWhiteSpace($dumpbin)) {
        throw "dumpbin.exe not found; cannot verify PE exports for $Dll"
    }
    $output = & $dumpbin /exports $Dll 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin /exports failed for ${Dll}: $($output | Out-String)"
    }
    $pattern = "^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+$([regex]::Escape($ExportName))\b"
    return [regex]::IsMatch(($output | Out-String), $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
}

if (!(Test-PeExport -Dll $gfxstream -ExportName "initLibrary")) {
    throw "gfxstream backend ABI is incompatible with this SDK emulator; missing plain C initLibrary export: $gfxstream"
}

function Get-SourceProperty {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ""
    }
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match "^$([regex]::Escape($Name))=(.+)$") {
            return $Matches[1].Trim()
        }
    }
    return ""
}

function Get-GitValue {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    try {
        $value = (& git -C $Repo @Arguments 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -eq 0) { return $value }
    } catch {
        return ""
    }
    return ""
}

$baseEmulatorBuildId = Get-SourceProperty -Path $sourceProperties -Name "Pkg.BuildId"
$baseEmulatorRevision = Get-SourceProperty -Path $sourceProperties -Name "Pkg.Revision"
$gfxstreamSourceCommit = ""
$gfxstreamSourceSubject = ""
$gfxstreamSourceSnapBuildId = ""
if (![string]::IsNullOrWhiteSpace($SourceDir)) {
    $resolvedSource = Resolve-Path -LiteralPath $SourceDir -ErrorAction Stop
    $gfxstreamSourceCommit = Get-GitValue -Repo $resolvedSource.Path -Arguments @("rev-parse", "HEAD")
    $gfxstreamSourceSubject = Get-GitValue -Repo $resolvedSource.Path -Arguments @("log", "-1", "--pretty=%s")
    if ($gfxstreamSourceSubject -match "Snap for (\d+)") {
        $gfxstreamSourceSnapBuildId = $Matches[1]
    }
}
if ([string]::IsNullOrWhiteSpace($gfxstreamSourceSnapBuildId) -and ![string]::IsNullOrWhiteSpace($SourceBuildId)) {
    if (-not $AllowMismatchedBuildId) {
        throw "-SourceBuildId requires -AllowMismatchedBuildId (R&D only)"
    }
    $gfxstreamSourceSnapBuildId = $SourceBuildId
    Write-Warning "R&D: using manually-specified source build ID: $gfxstreamSourceSnapBuildId"
}
if ([string]::IsNullOrWhiteSpace($baseEmulatorBuildId)) {
    throw "base emulator build id is missing from source.properties: $sourceProperties"
}
if ([string]::IsNullOrWhiteSpace($gfxstreamSourceSnapBuildId)) {
    throw "gfxstream source snapshot build id is missing; pass -SourceDir pointing at the matching gfxstream checkout, or -SourceBuildId -AllowMismatchedBuildId for R&D"
}
if ($gfxstreamSourceSnapBuildId -ne $baseEmulatorBuildId) {
    if (-not $AllowMismatchedBuildId) {
        throw "gfxstream source snapshot build id $gfxstreamSourceSnapBuildId does not match base emulator build id $baseEmulatorBuildId; refusing crash-prone mixed ABI runtime (use -AllowMismatchedBuildId for R&D testing only)"
    }
    Write-Warning "R&D: allowing mismatched build IDs: gfxstream=$gfxstreamSourceSnapBuildId emulator=$baseEmulatorBuildId"
}

$gitCommit = $null
try {
    $gitCommit = (git -C (Split-Path -Parent $PSScriptRoot) rev-parse --short HEAD 2>$null).Trim()
} catch {
    $gitCommit = "unknown"
}
if ([string]::IsNullOrWhiteSpace($gitCommit)) {
    $gitCommit = "unknown"
}

$payload = [ordered]@{
    producer = "ChimeraGfxstreamSharedTextureBridge"
    transport = "D3D11SharedTexture"
    renderPath = "VulkanDisplayVkPost"
    abi = "sdk-emulator-36"
    minWidth = 1920
    minHeight = 1080
    targetFps = 60
    sourceCommit = $gitCommit
    gfxstreamSourceCommit = $gfxstreamSourceCommit
    gfxstreamSourceSubject = $gfxstreamSourceSubject
    gfxstreamSourceSnapBuildId = $gfxstreamSourceSnapBuildId
    baseEmulatorRevision = $baseEmulatorRevision
    baseEmulatorBuildId = $baseEmulatorBuildId
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
}

New-Item -ItemType Directory -Force -Path $lib64 | Out-Null
$payload | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifest -Encoding UTF8
Write-Host "Wrote Chimera gfxstream shared texture manifest: $manifest"
