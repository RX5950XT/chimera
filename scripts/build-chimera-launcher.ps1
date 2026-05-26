param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$ConfigPath = Join-Path $Root "configs/android_sdk.json"
$Config = Get-Content -Raw $ConfigPath | ConvertFrom-Json
$Sdk = [string]$Config.sdk_root
$BuildToolsVersion = "34.0.0"
$BuildTools = Join-Path $Sdk "build-tools/$BuildToolsVersion"
$AndroidJar = Join-Path $Sdk "platforms/android-34/android.jar"
$LauncherRoot = Join-Path $Root "tools/chimera-launcher"
$OutDir = Join-Path $Root "build/launcher"
$GeneratedDir = Join-Path $OutDir "gen"
$ClassesDir = Join-Path $OutDir "classes"
$DexDir = Join-Path $OutDir "dex"
$ClassesJar = Join-Path $OutDir "classes.jar"
$CompiledResources = Join-Path $OutDir "compiled.zip"
$UnsignedApk = Join-Path $OutDir "chimera-launcher-unsigned.apk"
$AlignedApk = Join-Path $OutDir "chimera-launcher-aligned.apk"
$SignedApk = Join-Path $OutDir "chimera-launcher.apk"
$Keystore = Join-Path $OutDir "debug.keystore"

function Require-File([string]$Path, [string]$Name) {
    if (-not (Test-Path $Path)) {
        throw "$Name not found: $Path"
    }
}

function Ensure-BuildTools {
    if (Test-Path (Join-Path $BuildTools "aapt2.exe")) {
        return
    }
    $SdkManager = Join-Path $Sdk "cmdline-tools/latest/bin/sdkmanager.bat"
    Require-File $SdkManager "sdkmanager"
    Write-Host "Installing Android build-tools;$BuildToolsVersion ..."
    $licenses = ("y`n" * 32)
    $licenses | & $SdkManager "--sdk_root=$Sdk" "build-tools;$BuildToolsVersion"
}

function Invoke-Tool([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

Ensure-BuildTools
Require-File $AndroidJar "android.jar"
Require-File (Join-Path $BuildTools "aapt2.exe") "aapt2"
Require-File (Join-Path $BuildTools "d8.bat") "d8"
Require-File (Join-Path $BuildTools "zipalign.exe") "zipalign"
Require-File (Join-Path $BuildTools "apksigner.bat") "apksigner"
Require-File (Join-Path $LauncherRoot "AndroidManifest.xml") "launcher manifest"

New-Item -ItemType Directory -Force $OutDir, $GeneratedDir, $ClassesDir, $DexDir | Out-Null
Get-ChildItem -LiteralPath $GeneratedDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath $ClassesDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath $DexDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
New-Item -ItemType Directory -Force $GeneratedDir, $ClassesDir, $DexDir | Out-Null

Invoke-Tool { & (Join-Path $BuildTools "aapt2.exe") compile --dir (Join-Path $LauncherRoot "res") -o $CompiledResources }
Invoke-Tool { & (Join-Path $BuildTools "aapt2.exe") link `
        -I $AndroidJar `
        --manifest (Join-Path $LauncherRoot "AndroidManifest.xml") `
        --java $GeneratedDir `
        -o $UnsignedApk `
        $CompiledResources }

$JavaFiles = @(Join-Path $GeneratedDir "com/chimera/launcher/R.java")
$JavaFiles += Get-ChildItem -Path (Join-Path $LauncherRoot "src") -Recurse -Filter *.java |
    ForEach-Object { $_.FullName }
Invoke-Tool { & javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $AndroidJar -d $ClassesDir $JavaFiles }
Invoke-Tool { & jar cf $ClassesJar -C $ClassesDir . }
Invoke-Tool { & (Join-Path $BuildTools "d8.bat") --lib $AndroidJar --output $DexDir $ClassesJar }
Invoke-Tool { & jar uf $UnsignedApk -C $DexDir classes.dex }
Invoke-Tool { & (Join-Path $BuildTools "zipalign.exe") -f 4 $UnsignedApk $AlignedApk }

if (-not (Test-Path $Keystore)) {
    Invoke-Tool { & keytool -genkeypair -keystore $Keystore -storepass android -keypass android `
            -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
            -dname "CN=Chimera Debug,O=Chimera,C=TW" }
}

Invoke-Tool { & (Join-Path $BuildTools "apksigner.bat") sign `
        --ks $Keystore `
        --ks-pass pass:android `
        --key-pass pass:android `
        --out $SignedApk `
        $AlignedApk }

Invoke-Tool { & (Join-Path $BuildTools "apksigner.bat") verify $SignedApk }
Write-Host "Built $SignedApk"
