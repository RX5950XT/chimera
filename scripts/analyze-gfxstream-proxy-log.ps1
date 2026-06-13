param(
    [Parameter(Mandatory = $true)][string]$LogPath,
    [int]$MinimumWidth = 1920,
    [int]$MinimumHeight = 1080,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    throw "log not found: $LogPath"
}

$text = Get-Content -LiteralPath $LogPath -Raw

function Count-Pattern {
    param([Parameter(Mandatory = $true)][string]$Pattern)
    return ([regex]::Matches($text, $Pattern)).Count
}

function Max-Size {
    param([Parameter(Mandatory = $true)][string]$Pattern)
    $maxWidth = 0
    $maxHeight = 0
    foreach ($match in [regex]::Matches($text, $Pattern)) {
        $width = [int]$match.Groups["w"].Value
        $height = [int]$match.Groups["h"].Value
        if (($width * $height) -gt ($maxWidth * $maxHeight)) {
            $maxWidth = $width
            $maxHeight = $height
        }
    }
    return [ordered]@{ width = $maxWidth; height = $maxHeight }
}

$resourceCreatePattern = 'probe stream_renderer_resource_create [^\r\n]*size=(?<w>\d+)x(?<h>\d+)'
$createBlobPattern = 'probe stream_renderer_create_blob [^\r\n]*res=(?<res>\d+)[^\r\n]*size=(?<bytes>\d+)'
$flushPattern = 'stream_renderer_flush [^\r\n]*size=(?<w>\d+)x(?<h>\d+)'
$onPostPattern = 'android_onPost [^\r\n]*size=(?<w>\d+)x(?<h>\d+)'
$setupWindowPattern = 'probe gfxstream_backend_setup_window [^\r\n]*fb=(?<w>\d+)x(?<h>\d+)'

$resourceCreateMax = Max-Size -Pattern $resourceCreatePattern
$flushMax = Max-Size -Pattern $flushPattern
$onPostMax = Max-Size -Pattern $onPostPattern
$setupWindowMax = Max-Size -Pattern $setupWindowPattern

$gpuDisplaySignals = @()
if ($flushMax.width -ge $MinimumWidth -and $flushMax.height -ge $MinimumHeight) {
    $gpuDisplaySignals += "stream_renderer_flush"
}
if ($resourceCreateMax.width -ge $MinimumWidth -and $resourceCreateMax.height -ge $MinimumHeight) {
    $gpuDisplaySignals += "stream_renderer_resource_create"
}
if ($setupWindowMax.width -ge $MinimumWidth -and $setupWindowMax.height -ge $MinimumHeight) {
    $gpuDisplaySignals += "gfxstream_backend_setup_window"
}

$cpuReadbackSignals = @()
if ((Count-Pattern -Pattern 'android_onPost ') -gt 0) {
    $cpuReadbackSignals += "android_onPost"
}
if ((Count-Pattern -Pattern 'renderer_hook getScreenshot ') -gt 0) {
    $cpuReadbackSignals += "renderer_getScreenshot"
}
if ((Count-Pattern -Pattern 'transfer_read_iov ') -gt 0) {
    $cpuReadbackSignals += "stream_renderer_transfer_read_iov"
}

$frameListenerCount = Count-Pattern -Pattern 'renderlib_listener frame '
$exportBlobCount = Count-Pattern -Pattern 'probe stream_renderer_export_blob '
$vtableInstallOk = $text.Contains("renderer_hook install=ok")
$setPostCallbackCount = Count-Pattern -Pattern 'android_setPostCallback '
$repaintCount = Count-Pattern -Pattern 'renderer_hook repaint '
$showSubwindowCount = Count-Pattern -Pattern 'renderer_hook showOpenGLSubwindow '

# Parse max export_blob os_handle (non-zero means a real D3D11 handle was observed)
$exportBlobHandlePattern = 'probe stream_renderer_export_blob [^\r\n]*osHandle=(?<h>-?\d+)'
$maxExportBlobHandle = 0
foreach ($match in [regex]::Matches($text, $exportBlobHandlePattern)) {
    $h = [int64]$match.Groups["h"].Value
    if ([Math]::Abs($h) -gt [Math]::Abs($maxExportBlobHandle)) { $maxExportBlobHandle = $h }
}

$summary = [ordered]@{
    logPath = (Resolve-Path -LiteralPath $LogPath).Path
    hasProxyAttach = $text.Contains("dll_process_attach=libgfxstream_backend_proxy")
    initLibrary = (Count-Pattern -Pattern 'renderlib_wrapper initLibrary ') + (Count-Pattern -Pattern 'forward name=initLibrary')
    androidSetOpenglesRenderer = Count-Pattern -Pattern 'android_setOpenglesRenderer '
    rendererVtable = Count-Pattern -Pattern 'renderer_vtable renderer='
    vtableInstallOk = $vtableInstallOk
    frameListener = $frameListenerCount
    streamRendererInit = Count-Pattern -Pattern 'probe stream_renderer_init '
    contextCreate = Count-Pattern -Pattern 'probe stream_renderer_context_create '
    resourceCreate = Count-Pattern -Pattern 'probe stream_renderer_resource_create '
    createBlob = Count-Pattern -Pattern 'probe stream_renderer_create_blob '
    exportBlob = $exportBlobCount
    maxExportBlobHandle = $maxExportBlobHandle
    attachResource = Count-Pattern -Pattern 'probe stream_renderer_ctx_attach_resource '
    flush = Count-Pattern -Pattern 'stream_renderer_flush '
    onPost = Count-Pattern -Pattern 'android_onPost '
    setupWindow = Count-Pattern -Pattern 'probe gfxstream_backend_setup_window '
    setPostCallback = $setPostCallbackCount
    repaint = $repaintCount
    showSubwindow = $showSubwindowCount
    rendererGetScreenshot = Count-Pattern -Pattern 'renderer_hook getScreenshot '
    transferRead = Count-Pattern -Pattern 'transfer_read_iov '
    maxResourceCreate = $resourceCreateMax
    maxFlush = $flushMax
    maxOnPost = $onPostMax
    maxSetupWindow = $setupWindowMax
    gpuDisplaySignals = $gpuDisplaySignals
    cpuReadbackSignals = $cpuReadbackSignals
    hasUsableGpuDisplaySignal = ($gpuDisplaySignals.Count -gt 0)
    hasCpuReadbackRisk = ($cpuReadbackSignals.Count -gt 0)
    hasFrameListener = ($frameListenerCount -gt 0)
    hasExportBlobHandle = ($maxExportBlobHandle -ne 0)
    sharedTextureProducer = $false
}

if ($Json) {
    $summary | ConvertTo-Json -Depth 5
} else {
    Write-Host "proxyAttach=$($summary.hasProxyAttach)"
    Write-Host "initLibrary=$($summary.initLibrary) androidSetOpenglesRenderer=$($summary.androidSetOpenglesRenderer) rendererVtable=$($summary.rendererVtable) vtableInstallOk=$($summary.vtableInstallOk)"
    Write-Host "resourceCreate=$($summary.resourceCreate) createBlob=$($summary.createBlob) exportBlob=$($summary.exportBlob) flush=$($summary.flush)"
    Write-Host "maxResourceCreate=$($resourceCreateMax.width)x$($resourceCreateMax.height) maxFlush=$($flushMax.width)x$($flushMax.height) maxSetupWindow=$($setupWindowMax.width)x$($setupWindowMax.height)"
    Write-Host "frameListener=$($summary.frameListener) hasFrameListener=$($summary.hasFrameListener) setPostCallback=$($summary.setPostCallback)"
    Write-Host "showSubwindow=$($summary.showSubwindow) repaint=$($summary.repaint) maxExportBlobHandle=$($summary.maxExportBlobHandle)"
    Write-Host "onPost=$($summary.onPost) rendererGetScreenshot=$($summary.rendererGetScreenshot) transferRead=$($summary.transferRead)"
    Write-Host "gpuDisplaySignals=$($gpuDisplaySignals -join ',')"
    Write-Host "cpuReadbackSignals=$($cpuReadbackSignals -join ',')"
    Write-Host "hasUsableGpuDisplaySignal=$($summary.hasUsableGpuDisplaySignal)"
}

if (-not $summary.hasProxyAttach) {
    throw "gfxstream proxy was not loaded"
}
if ($summary.initLibrary -eq 0 -and $summary.androidSetOpenglesRenderer -eq 0) {
    throw "gfxstream renderer initialization was not observed"
}
if (-not $summary.hasUsableGpuDisplaySignal) {
    throw "no 1920x1080 GPU display/resource signal observed"
}
if ($summary.hasCpuReadbackRisk) {
    Write-Warning "CPU readback signal observed: $($cpuReadbackSignals -join ',')"
}
