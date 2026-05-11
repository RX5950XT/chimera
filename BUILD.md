# Build Guide — Project Chimera

> **狀態**: Phase 0 — 基礎建設完成，編譯環境已驗證

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
git clone https://github.com/chimera-emulator/chimera.git
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

### 4. 執行單元測試

```powershell
ctest --test-dir build -C Release --output-on-failure
```

### 5. 執行 Host UI

```powershell
.\build\Release\chimera-ui.exe
```

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

### QEMU / Android Emulator WHPX 失敗

確保 `WinHvPlatform.dll` 存在於系統 (`C:\Windows\System32\WinHvPlatform.dll`)。
若不存在，表示 Windows Hypervisor Platform 未啟用，請執行上方 PowerShell 指令並重啟。

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

*最後更新: 2026-05-09*
