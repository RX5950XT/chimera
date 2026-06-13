param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDir
)

$ErrorActionPreference = "Stop"

$resolved = Resolve-Path -LiteralPath $RuntimeDir
$root = $resolved.Path
$emulator = Join-Path $root "emulator.exe"
$lib64 = Join-Path $root "lib64"
$requiredDlls = @(
    "lib64OpenglRender.dll",
    "lib64EGL_translator.dll",
    "lib64GLES_CM_translator.dll",
    "lib64GLES_V2_translator.dll"
)
$legacyRenderer = Join-Path $lib64 $requiredDlls[0]
$gfxstream = Join-Path $lib64 "libgfxstream_backend.dll"
$manifest = Join-Path $lib64 "chimera-emugl-shared-texture.json"

if (!(Test-Path -LiteralPath $emulator)) {
    throw "emulator.exe not found in runtime dir: $root"
}
if (!(Test-Path -LiteralPath $legacyRenderer)) {
    if (Test-Path -LiteralPath $gfxstream) {
        throw "stock gfxstream runtime detected; it cannot load ChimeraSharedTextureBridge"
    }
    throw "modified legacy EmuGL renderer not found: $legacyRenderer"
}
foreach ($dll in $requiredDlls) {
    $path = Join-Path $lib64 $dll
    if (!(Test-Path -LiteralPath $path)) {
        throw "incomplete Chimera EmuGL runtime; required DLL missing: $path"
    }
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
    producer = "ChimeraSharedTextureBridge"
    transport = "D3D11SharedTexture"
    minWidth = 1920
    minHeight = 1080
    targetFps = 60
    sourceCommit = $gitCommit
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
}

New-Item -ItemType Directory -Force -Path $lib64 | Out-Null
$payload | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifest -Encoding UTF8
Write-Host "Wrote Chimera EmuGL shared texture manifest: $manifest"
