[CmdletBinding()]
param(
    [string]$SourceRuntimeDir = "",
    [string]$OutDir = "",
    [string]$StockBackendName = "libgfxstream_backend_stock.dll"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProxySource = Join-Path $RepoRoot "src\host\runtime\gfxstream_proxy\gfxstream_proxy.c"
$RenderLibProxySource = Join-Path $RepoRoot "src\host\runtime\gfxstream_proxy\gfxstream_proxy_renderlib.cpp"
$GfxstreamSourceRoot = Join-Path $RepoRoot "tmp\aosp-sdk-release\hardware\google\gfxstream"
$AemuSourceRoot = Join-Path $RepoRoot "tmp\aosp-sdk-release\hardware\google\aemu"
if ([string]::IsNullOrWhiteSpace($SourceRuntimeDir)) {
    $SourceRuntimeDir = Join-Path $RepoRoot "third_party\android-sdk\emulator"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $RepoRoot "build\chimera-gfxstream-proxy-runtime"
}

function Require-File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Name not found: $Path"
    }
}

function Resolve-VsTool {
    param([Parameter(Mandatory = $true)][string]$Name)
    $toolsRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
    $tool = Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter $Name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\bin\Hostx64\x64\$Name" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($null -eq $tool) {
        throw "Visual Studio tool not found: $Name"
    }
    $tool.FullName
}

function Get-DllExports {
    param(
        [Parameter(Mandatory = $true)][string]$Dumpbin,
        [Parameter(Mandatory = $true)][string]$Dll
    )
    $names = New-Object System.Collections.Generic.List[string]
    $output = & $Dumpbin /exports $Dll
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin /exports failed: $Dll"
    }
    foreach ($line in $output) {
        if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') {
            $names.Add($Matches[1])
        }
    }
    $names
}

$sourceRoot = (Resolve-Path -LiteralPath $SourceRuntimeDir).Path
$outRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
$sourceBackend = Join-Path $sourceRoot "lib64\libgfxstream_backend.dll"
Require-File -Path (Join-Path $sourceRoot "emulator.exe") -Name "source emulator.exe"
Require-File -Path $sourceBackend -Name "source libgfxstream_backend.dll"
Require-File -Path $ProxySource -Name "gfxstream proxy source"
Require-File -Path $RenderLibProxySource -Name "gfxstream renderlib proxy source"
Require-File -Path (Join-Path $GfxstreamSourceRoot "include\render-utils\RenderLib.h") -Name "gfxstream RenderLib.h"
Require-File -Path (Join-Path $AemuSourceRoot "base\include\aemu\base\files\Stream.h") -Name "aemu base headers"

$dumpbin = Resolve-VsTool -Name "dumpbin.exe"
$link = Resolve-VsTool -Name "link.exe"
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
Require-File -Path $vcvars -Name "vcvarsall.bat"
$exports = @(Get-DllExports -Dumpbin $dumpbin -Dll $sourceBackend)
if ($exports.Count -lt 100) {
    throw "unexpectedly low gfxstream export count: $($exports.Count)"
}
foreach ($required in @(
    "android_setOpenglesRenderer",
    "gfxstream_backend_set_screen_background",
    "gfxstream_backend_setup_window",
    "stream_renderer_flush"
)) {
    if ($exports -notcontains $required) {
        throw "required stock gfxstream export missing: $required"
    }
}

New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$robocopyOutput = & robocopy $sourceRoot $outRoot /E /NFL /NDL /NJH /NJS /NP
$robocopyExit = $LASTEXITCODE
if ($robocopyExit -gt 7) {
    throw "robocopy failed ($robocopyExit): $($robocopyOutput -join [Environment]::NewLine)"
}

$outLib64 = Join-Path $outRoot "lib64"
$outBackend = Join-Path $outLib64 "libgfxstream_backend.dll"
$outStockBackend = Join-Path $outLib64 $StockBackendName
Require-File -Path $outBackend -Name "copied libgfxstream_backend.dll"
Copy-Item -LiteralPath $outBackend -Destination $outStockBackend -Force

$proxyWorkDir = Join-Path $outRoot "chimera-proxy"
New-Item -ItemType Directory -Force -Path $proxyWorkDir | Out-Null
$defPath = Join-Path $proxyWorkDir "libgfxstream_backend.def"
$defLines = New-Object System.Collections.Generic.List[string]
$defLines.Add("LIBRARY libgfxstream_backend")
$defLines.Add("EXPORTS")
foreach ($name in $exports) {
    if (-not $name.StartsWith("?")) {
        continue
    }
    $defLines.Add("    $name=$([IO.Path]::GetFileNameWithoutExtension($StockBackendName)).$name")
}
Set-Content -LiteralPath $defPath -Value $defLines -Encoding ASCII

$thunkSource = Join-Path $proxyWorkDir "gfxstream_proxy_thunks.c"
$thunkLines = New-Object System.Collections.Generic.List[string]
$thunkLines.Add("#include <stdint.h>")
$thunkLines.Add("uintptr_t chimera_gfxstream_forward_call(const char *name, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12);")
$thunkLines.Add("#define CHIMERA_FORWARD_EXPORT(name) __declspec(dllexport) uintptr_t name(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12) { return chimera_gfxstream_forward_call(#name, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12); }")
foreach ($name in $exports) {
    if ($name.StartsWith("?")) { continue }
    if ($name -eq "stream_renderer_flush") { continue }
    if ($name -eq "stream_renderer_init") { continue }
    if ($name -eq "stream_renderer_context_create") { continue }
    if ($name -eq "stream_renderer_resource_create") { continue }
    if ($name -eq "stream_renderer_create_blob") { continue }
    if ($name -eq "stream_renderer_export_blob") { continue }
    if ($name -eq "stream_renderer_ctx_attach_resource") { continue }
    if ($name -eq "stream_renderer_ctx_detach_resource") { continue }
    if ($name -eq "stream_renderer_resource_map_info") { continue }
    if ($name -eq "stream_renderer_transfer_read_iov") { continue }
    if ($name -eq "stream_renderer_transfer_write_iov") { continue }
    if ($name -eq "stream_renderer_vulkan_info") { continue }
    if ($name -eq "gfxstream_backend_setup_window") { continue }
    if ($name -eq "gfxstream_backend_set_screen_mask") { continue }
    if ($name -eq "gfxstream_backend_set_screen_background") { continue }
    if ($name -eq "initLibrary") { continue }  # provided by exact C++ export in gfxstream_proxy_renderlib.cpp
    if ($name -eq "android_setOpenglesRenderer") { continue }
    if ($name -eq "android_setPostCallback") { continue }
    if ($name -eq "NvOptimusEnablement") { continue }
    if ($name -eq "AmdPowerXpressRequestHighPerformance") { continue }
    $thunkLines.Add("CHIMERA_FORWARD_EXPORT($name)")
}
Set-Content -LiteralPath $thunkSource -Value $thunkLines -Encoding ASCII

$importLib = Join-Path $proxyWorkDir "libgfxstream_backend.lib"
$expFile = Join-Path $proxyWorkDir "libgfxstream_backend.exp"
$proxyObj = Join-Path $proxyWorkDir "gfxstream_proxy.obj"
$renderLibProxyObj = Join-Path $proxyWorkDir "gfxstream_proxy_renderlib.obj"
$thunkObj = Join-Path $proxyWorkDir "gfxstream_proxy_thunks.obj"
$includeFlags = @(
    "/I`"$GfxstreamSourceRoot\include`"",
    "/I`"$GfxstreamSourceRoot\host`"",
    "/I`"$GfxstreamSourceRoot\host\gl\gl-host-common\include`"",
    "/I`"$GfxstreamSourceRoot\host\features\include`"",
    "/I`"$AemuSourceRoot\windows\includes`"",
    "/I`"$AemuSourceRoot\base\include`"",
    "/I`"$AemuSourceRoot\host-common\include`"",
    "/I`"$AemuSourceRoot\snapshot\include`""
) -join " "
$compileCmd = "`"$vcvars`" amd64 >nul && cl /nologo /LD /O2 /MD /GS- /DNOMINMAX " +
    "/c `"$ProxySource`" /Fo`"$proxyObj`" && cl /nologo /O2 /MD /GS- /EHsc /std:c++20 /DNOMINMAX $includeFlags " +
    "/c `"$RenderLibProxySource`" /Fo`"$renderLibProxyObj`" && cl /nologo /O2 /MD /GS- " +
    "/c `"$thunkSource`" /Fo`"$thunkObj`" && link /NOLOGO /DLL " +
    "`"$proxyObj`" `"$renderLibProxyObj`" `"$thunkObj`" /DEF:`"$defPath`" /OUT:`"$outBackend`" /IMPLIB:`"$importLib`""
& cmd.exe /d /s /c $compileCmd
if ($LASTEXITCODE -ne 0) {
    throw "cl/link failed while building gfxstream proxy backend"
}
Remove-Item -LiteralPath $expFile -Force -ErrorAction SilentlyContinue

$proxyExports = (& $dumpbin /exports $outBackend) -join "`n"
if ($proxyExports -notmatch [regex]::Escape([IO.Path]::GetFileNameWithoutExtension($StockBackendName))) {
    throw "proxy backend exports are not forwarded to $StockBackendName"
}
if ($proxyExports -notmatch 'stream_renderer_flush' -or
    $proxyExports -match 'stream_renderer_flush \(forwarded to') {
    throw "stream_renderer_flush hook export was not built correctly"
}
if ($proxyExports -notmatch 'android_setPostCallback' -or
    $proxyExports -match 'android_setPostCallback \(forwarded to') {
    throw "android_setPostCallback hook export was not built correctly"
}
foreach ($hookedProbe in @(
    "stream_renderer_init",
    "stream_renderer_resource_create",
    "stream_renderer_create_blob",
    "stream_renderer_export_blob",
    "stream_renderer_transfer_read_iov",
    "stream_renderer_vulkan_info",
    "gfxstream_backend_set_screen_background")) {
    if ($proxyExports -notmatch $hookedProbe -or
        $proxyExports -match "$hookedProbe \(forwarded to") {
        throw "$hookedProbe probe export was not built correctly"
    }
}
if ($proxyExports -notmatch 'initLibrary' -or
    $proxyExports -match 'initLibrary \(forwarded to') {
    throw "initLibrary wrapper export was not built correctly"
}
$metadata = [ordered]@{
    kind = "chimera-gfxstream-forwarder-proxy"
    sourceRuntime = $sourceRoot
    stockBackend = "lib64\$StockBackendName"
    proxyBackend = "lib64\libgfxstream_backend.dll"
    exportCount = $exports.Count
    hookedExports = @(
        "initLibrary",
        "android_setOpenglesRenderer",
        "android_setPostCallback",
        "stream_renderer_flush",
        "stream_renderer_init",
        "stream_renderer_context_create",
        "stream_renderer_resource_create",
        "stream_renderer_create_blob",
        "stream_renderer_export_blob",
        "stream_renderer_ctx_attach_resource",
        "stream_renderer_ctx_detach_resource",
        "stream_renderer_resource_map_info",
        "stream_renderer_transfer_read_iov",
        "stream_renderer_transfer_write_iov",
        "stream_renderer_vulkan_info",
        "gfxstream_backend_setup_window",
        "gfxstream_backend_set_screen_mask",
        "gfxstream_backend_set_screen_background")
    sharedTextureProducer = $false
}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outRoot "chimera-gfxstream-proxy.json") -Encoding ASCII

Write-Host "result=pass"
Write-Host "runtime=$outRoot"
Write-Host "exports=$($exports.Count)"
Write-Host "stockBackend=lib64\$StockBackendName"
