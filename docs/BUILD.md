# Build Guide — Project Chimera

> 環境安裝、建置、測試與貢獻流程。日常 agent 工作流細節（coding 標準、troubleshooting 全表）見根目錄 `AGENTS.md`。

## 環境需求

1. **Visual Studio 2022**（Community 或 BuildTools）
   - 工作負載：**Desktop development with C++**；元件：**Windows 11 SDK (10.0.26100.0)+**
   - 每個新終端都要先載入環境：
     ```powershell
     & "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
     ```
   - **MSVC only**：本機 MSYS2 GCC `cc1plus.exe` crash，不使用 MinGW/Ninja。

2. **Qt 6.8.3**（MSVC 2022 64-bit）→ `C:\Qt\6.8.3\msvc2022_64`
   ```powershell
   pip install aqtinstall
   aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir C:\Qt --modules qtshadertools
   ```

3. **CMake 3.20+**（隨 VS 安裝）、**Python 3.12+**（自動化腳本）

4. **Windows 功能**（管理員執行後重啟）：
   ```powershell
   Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V -All -NoRestart
   Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -All -NoRestart
   ```

5. **Android SDK / AVD**：`python scripts/setup-android-sdk.py` 下載至 `third_party\android-sdk\`、`third_party\android-avd\`。

## 建置與測試

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration   # 23/23
```

- Release build 後 CMake 自動跑 `windeployqt`，`build\Release\chimera-ui.exe` 可直接執行。
- 3 個 integration tests 需要 env（`CHIMERA_ADB_PATH` / `CHIMERA_EMULATOR_PATH` / `CHIMERA_AVD_NAME`）與運行中的 emulator；CI 用 `-LE integration` 略過。
- 選用產物：`scripts\build-chimera-launcher.ps1`（Android HOME launcher APK）、`scripts\build-chimera-gfxstream-runtime.ps1`（custom gfxstream runtime）。

## 執行

```powershell
# 一鍵（推薦，= start-chimera.ps1 -Fast -InteractiveFirst）
.\start-chimera.cmd

# 保守 fallback（stock gRPC 顯示路徑）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Stock

# 只驗 UI/QML，不啟動 Android
.\build\Release\chimera-ui.exe --no-emulator

# Self-test（boot → 1080p → 像素 gate → 互動 → 清理）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Fast -InteractiveFirst -SelfTest
```

Runtime verifier（詳見 [STATUS.md](STATUS.md) 驗證入口與 `AGENTS.md`）：`verify-interactive-ui.ps1`、`verify-true-1080p60.ps1`、`verify-quick-boot.ps1`。

## 疑難排解（常見）

| 症狀 | 解法 |
|------|------|
| CMake 找不到 Qt6 | 確認 `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64` |
| 測試 `0xC0000135`（DLL Not Found） | `$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"` |
| 找不到 `Qt6Quick.dll` | 重跑 `cmake --build build --config Release`（自動部署） |
| WHPX 失敗 | 確認 `C:\Windows\System32\WinHvPlatform.dll` 存在；未啟用則跑上方 Windows 功能指令並重啟 |
| build 後 LNK1104 | `chimera-ui.exe` / `qemu-system*` 還在執行中鎖住輸出檔 |
| 啟動後黑屏 / FPS 0 | 先查 orphan qemu（`Get-Process qemu-system*`）；其餘見 `AGENTS.md` Troubleshooting 全表 |

## 貢獻

本專案主要由 AI Agent 自動化開發，人類監督為輔。

- **回報問題**：GitHub Issues 附重現步驟、環境、錯誤訊息。
- **提交修改**：fork → `git checkout -b feat/your-feature` → 遵循 `AGENTS.md` coding 標準 → `ctest` 通過 → commit 格式 `<type>: <description>`（`feat fix refactor docs test chore perf ci`）。
- **程式碼標準**：C++20 / Qt 6；函式 < 50 行、檔案 < 800 行、巢狀 ≤ 4 層；例外不靜默吞掉；外部輸入都驗證；新功能配單元測試。
- **授權**：提交 PR 即同意以 Apache 2.0 釋出。

---
*最後更新: 2026-07-02（併入原 CONTRIBUTING.md；移除過時的 native-embed/6-tests/AOSP 編譯段）*
