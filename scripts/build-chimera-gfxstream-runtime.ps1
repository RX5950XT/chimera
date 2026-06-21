param(
    [string]$SourceDir = "tmp\aosp\hardware\google\gfxstream",
    [string]$BuildDir = "tmp\aosp-build\gfxstream",
    [string]$InstallDir = "build\chimera-gfxstream-runtime",
    [string]$BaseEmulatorDir = "third_party\android-sdk\emulator",
    [string]$Branch = "main",
    [string]$SourceBuildId = "",
    [switch]$PrepareDeps,
    [switch]$SkipConfigure,
    [switch]$AllowMismatchedBuildId
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$sourcePath = Join-Path $repoRoot $SourceDir
$buildPath = Join-Path $repoRoot $BuildDir
$installPath = Join-Path $repoRoot $InstallDir
$baseEmulatorPath = Join-Path $repoRoot $BaseEmulatorDir

$sourceFullPath = [System.IO.Path]::GetFullPath($sourcePath)
$defaultAospRoot = Join-Path $repoRoot "tmp\aosp"
if ($sourceFullPath.EndsWith("hardware\google\gfxstream", [System.StringComparison]::OrdinalIgnoreCase)) {
    $defaultAospRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $sourceFullPath "..\..\.."))
}

if ($PrepareDeps) {
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "prepare-chimera-gfxstream-deps.ps1") `
        -AospRoot $defaultAospRoot `
        -Branch $Branch
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (!(Test-Path -LiteralPath $sourcePath)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sourcePath) | Out-Null
    $gfxstreamUrl = "https://android.googlesource.com/platform/hardware/google/gfxstream"
    $hasBranch = $false
    if (![string]::IsNullOrWhiteSpace($Branch) -and $Branch -ne "main") {
        $remote = git ls-remote --heads $gfxstreamUrl $Branch
        if ($LASTEXITCODE -eq 0 -and ![string]::IsNullOrWhiteSpace(($remote | Out-String).Trim())) {
            $hasBranch = $true
        }
    } elseif ($Branch -eq "main") {
        $hasBranch = $true
    }

    if ($hasBranch) {
        git clone --depth=1 --branch $Branch $gfxstreamUrl $sourcePath
    } else {
        Write-Warning "branch '$Branch' not found for gfxstream; using repository default"
        git clone --depth=1 $gfxstreamUrl $sourcePath
    }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

powershell -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot "apply-chimera-gfxstream-patch.ps1") `
    -SourceDir $sourcePath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($SkipConfigure) {
    Write-Host "Patched Chimera gfxstream source: $sourcePath"
    exit 0
}

$depCheck = powershell -NoProfile -ExecutionPolicy Bypass `
    -File (Join-Path $PSScriptRoot "prepare-chimera-gfxstream-deps.ps1") `
    -AospRoot $defaultAospRoot `
    -Branch $Branch `
    -CheckOnly
if ($LASTEXITCODE -ne 0) {
    Write-Warning "AOSP gfxstream dependencies are missing under $defaultAospRoot"
    Write-Warning "Run: powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-gfxstream-runtime.ps1 -PrepareDeps -Branch $Branch"
    $depCheck | ForEach-Object { Write-Warning $_ }
}

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
New-Item -ItemType Directory -Force -Path $installPath | Out-Null

function Resolve-VsTool {
    param([Parameter(Mandatory = $true)][string]$Name)
    $toolsRoot = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
    if (!(Test-Path -LiteralPath $toolsRoot)) {
        throw "Visual Studio tools not found: $toolsRoot"
    }
    $tool = Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter $Name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\bin\Hostx64\x64\$Name" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (!$tool) {
        throw "Visual Studio tool not found: $Name"
    }
    return $tool.FullName
}

function New-SdkImportLib {
    param(
        [Parameter(Mandatory = $true)][string]$LibExe,
        [Parameter(Mandatory = $true)][string]$OutDir,
        [Parameter(Mandatory = $true)][string]$DllName,
        [Parameter(Mandatory = $true)][string]$ExportName
    )
    $defPath = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($DllName) + ".def")
    $libPath = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($DllName) + ".lib")
    @(
        "LIBRARY $DllName",
        "EXPORTS",
        "    $ExportName"
    ) | Set-Content -LiteralPath $defPath -Encoding ASCII
    & $LibExe /nologo "/def:$defPath" "/out:$libPath" /machine:x64 | Out-Host
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $libPath)) {
        throw "failed to create import library for $DllName"
    }
}

$sdkImportLibDir = Join-Path $buildPath "chimera-sdk-imports"
New-Item -ItemType Directory -Force -Path $sdkImportLibDir | Out-Null
$libExe = Resolve-VsTool -Name "lib.exe"
New-SdkImportLib -LibExe $libExe -OutDir $sdkImportLibDir `
    -DllName "libandroid-emu-agents.dll" `
    -ExportName "?Callback@LineConsumer@emulation@android@@SAHPEAXPEBDH@Z"
New-SdkImportLib -LibExe $libExe -OutDir $sdkImportLibDir `
    -DllName "libandroid-emu-protos.dll" `
    -ExportName "??0AllRefArDo@android_emulator@@QEAA@XZ"
New-SdkImportLib -LibExe $libExe -OutDir $sdkImportLibDir `
    -DllName "libandroid-emu-metrics.dll" `
    -ExportName "??0AdbAssistantStats@android_studio@@QEAA@XZ"

cmake -S $sourcePath -B $buildPath `
    -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_INSTALL_PREFIX=$installPath" `
    "-DCHIMERA_SDK_IMPORT_LIB_DIR=$sdkImportLibDir"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $buildPath --config Release --target gfxstream_backend
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$candidateDlls = @(
    (Join-Path $buildPath "host\Release\gfxstream_backend.dll"),
    (Join-Path $buildPath "host\Release\libgfxstream_backend.dll"),
    (Join-Path $buildPath "Release\gfxstream_backend.dll"),
    (Join-Path $buildPath "Release\libgfxstream_backend.dll")
)
$dll = $candidateDlls | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$dll) {
    throw "gfxstream backend DLL was not produced under $buildPath"
}

if (!(Test-Path -LiteralPath (Join-Path $baseEmulatorPath "emulator.exe"))) {
    throw "base Android Emulator runtime not found: $baseEmulatorPath"
}

robocopy $baseEmulatorPath $installPath /E /NFL /NDL /NJH /NJS /NC /NS /NP | Out-Host
if ($LASTEXITCODE -gt 7) {
    exit $LASTEXITCODE
}

New-Item -ItemType Directory -Force -Path (Join-Path $installPath "lib64") | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $installPath "lib64\libgfxstream_backend.dll") -Force

$manifestArgs = @(
    "-File", (Join-Path $PSScriptRoot "write-chimera-gfxstream-runtime-manifest.ps1"),
    "-RuntimeDir", $installPath,
    "-SourceDir", $sourcePath
)
if ($AllowMismatchedBuildId) { $manifestArgs += "-AllowMismatchedBuildId" }
if (![string]::IsNullOrWhiteSpace($SourceBuildId)) { $manifestArgs += "-SourceBuildId"; $manifestArgs += $SourceBuildId }
powershell -NoProfile -ExecutionPolicy Bypass @manifestArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built Chimera gfxstream runtime: $installPath"
