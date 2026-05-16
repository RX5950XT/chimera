# Build Guide — Project Chimera

> **狀態**: Phase 5 framework + stabilization。2026-05-14 驗證：Release build 成功，6/6 Qt unit tests passed。

## 環境需求

### Windows

1. **Visual Studio 2022** (Community 或 BuildTools)
   - 安裝工作負載: **Desktop development with C++**
   - 安裝元件: **Windows 11 SDK (10.0.26100.0)** 或更新版
   - 確認 `cl.exe` 可用:
     ```powershell
     & "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
     cl.exe
     ```

2. **Qt 6.8.3** (MSVC 2022 64-bit)
   - 安裝方式:
     ```powershell
     pip install aqtinstall
     aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir C:\Qt --modules qtshadertools
     ```
   - 安裝後路徑: `C:\Qt\6.8.3\msvc2022_64`

3. **CMake** 3.20+
   - 已隨 Visual Studio 安裝，或從 [cmake.org](https://cmake.org) 下載

4. **Windows 功能啟用**
   ```powershell
   # 以系統管理員身份執行
   Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V -All -NoRestart
   Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -All -NoRestart
   ```
   重啟後生效。

5. **Python 3.12+**
   - 用於執行自動化腳本 (`scripts/setup-android-sdk.py`, `scripts/run.py`)

## 編譯步驟

### 1. 初始化

```powershell
git clone https://github.com/RX5950XT/chimera.git
cd chimera
git submodule update --init --recursive
```

### 2. 設定建置環境

```powershell
# 載入 Visual Studio 開發者環境（每次新終端機都需要）
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

# 設定 Qt 路徑（可選，建議加入系統環境變數）
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
```

### 3. 設定並編譯

```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

Release build 完成後，CMake 會自動呼叫 `windeployqt`，把 Qt runtime 與 QML/平台 plugin 部署到 `build\Release\`，因此 `chimera-ui.exe` 可直接執行，不需要另外手動設 `PATH`。

### 4. 執行單元測試

```powershell
ctest --test-dir build -C Release --output-on-failure
```

目前測試 target:
- `test-config-manager`
- `test-input-mapper`
- `test-instance-manager`
- `test-graphics-framebuffer`
- `test-adb-framebuffer-capture`
- `test-qmp-input`

### 5. 執行 Host UI

```powershell
.\build\Release\chimera-ui.exe
```

只驗證 UI/QML、不啟動 Android：

```powershell
.\build\Release\chimera-ui.exe --no-emulator
```

目前 host UI 預設啟動 Android Emulator 原生視窗並嵌入 Chimera (`NativeEmulatorView`)。這是主要 60Hz 顯示路徑，不走 screenshot stream。

若需要測試舊的畫面擷取管線，可加上 `--stream-capture`；此模式會啟動 Android Emulator gRPC server (`-grpc 8554`) 作為主要擷取路徑，若 gRPC 45 秒內未產生 frame，會並行啟動 ADB raw screencap fallback。

---

## 自動化腳本

### 下載並設定 Android SDK

```powershell
python scripts/setup-android-sdk.py
```

### 啟動 Android 模擬器（獨立驗證）

```powershell
python scripts/run.py --avd chimera_dev --gpu host
```

---

## 疑難排解

### CMake 找不到 Qt6

確保 `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64` 正確指向 Qt 安裝目錄。

### 測試執行時 `0xC0000135` (DLL Not Found)

Qt6 DLL 不在 PATH 中。執行前加入:
```powershell
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
```

### `chimera-ui.exe` 提示找不到 `Qt6Quick.dll`

這代表舊的 build 輸出還沒跑過自動部署。重新執行：

```powershell
cmake --build build --config Release
```

然後確認 `build\Release\Qt6Quick.dll` 存在，再啟動 `.\build\Release\chimera-ui.exe`。

### QEMU / Android Emulator WHPX 失敗

確保 `WinHvPlatform.dll` 存在於系統 (`C:\Windows\System32\WinHvPlatform.dll`)。
若不存在，表示 Windows Hypervisor Platform 未啟用，請執行上方 PowerShell 指令並重啟。

### Android Emulator 顯示卡住或 FPS 很低

- 預設模式先確認 log 有 `Native emulator window attached`。
- 預設 native embedding live smoke 已驗證 guest 為 1280×720、240 dpi、SurfaceFlinger active mode 60.00 Hz。
- visible/native 模式必須保留 `-crash-report-mode never`，否則 Android Emulator 可能卡在 crash-report consent dialog，導致 QEMU/ADB 不啟動。
- gRPC fallback 使用 960px RGB888 stream，1280×720 guest live test 峰值約 32 FPS；ADB fallback 已降頻到 1 秒輪詢，僅供相容。
- 只有執行 `.\build\Release\chimera-ui.exe --stream-capture` 時才需要檢查 `Started GRPC server at [::]:8554`。
- 目前 VNC 不可作為主要路徑：Android Emulator 36.5.11 會回報 `VNC supports only guest GPU`，但 `-gpu guest` 不是此 emulator CLI 支援模式。
- 已落地 native emulator window embedding。若未來要做到更深層的錄影/overlay/texture compositing，仍需共享 GPU texture 或自訂 QEMU display path。

---

## 編譯 AOSP 系統映像

> **注意**: 編譯 AOSP 需要 Linux 環境（WSL2 或實體機）。
> 預估需求: 400 GB 磁碟、64 GB RAM、Ubuntu 22.04 LTS。

```bash
# 在 WSL2 Ubuntu 中
sudo apt update && sudo apt install repo git python3
mkdir ~/aosp && cd ~/aosp
repo init -u https://android.googlesource.com/platform/manifest -b android-14.0.0_r2
repo sync -c -j$(nproc)
source build/envsetup.sh
lunch sdk_phone_x86_64-eng
make -j$(nproc)
```

輸出: `out/target/product/generic_x86_64/system.img`

---

*最後更新: 2026-05-14*
