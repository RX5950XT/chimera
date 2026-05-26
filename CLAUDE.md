# Project Chimera — CLAUDE.md

> AI 工作階段快速參考。每次重大變更後更新。

## 當前狀態

**完成度**: BlueStacks Parity Roadmap v3 P0–P4e + Session 2–7 補強 COMPLETE (2026-05-19)
**生產引擎**: `emulator.exe` (Google QEMU+WHPX fork) — `--qemu-backend` / `--hcs-backend` 為 legacy R&D，保留不刪
**BlueStacks parity (emulator.exe 路徑)**: 已達成核心功能同等級（見「BlueStacks Parity 功能清單」）
**Phase 8 (legacy)**: `--cuttlefish` R&D 路徑的 gfxstream/SF stable 問題，不影響生產路徑功能
**Tests**: 19/19 unit tests PASS；3 integration tests（需 emulator 運行中）

## BlueStacks Parity 功能清單（production emulator.exe 路徑）

| 功能 | 狀態 |
|------|------|
| Android boot + WHPX | ✅ |
| 顯示內嵌（gRPC 串流，emulator headless 無彈窗） | ✅ |
| Input (keyboard/mouse/touch/gamepad) | ✅ |
| Multi-touch (MT evdev Type-B) | ✅ |
| IME 文字輸入 | ✅ |
| FPS 鼠標鎖定 | ✅ |
| 十字準心游標 | ✅ |
| APK / OBB 安裝 | ✅ |
| App 管理 (launch/stop/uninstall/clear) | ✅ |
| 釘選常用應用 | ✅ |
| Screen recording + screenshot | ✅ |
| GPS 模擬 (geo fix + route) | ✅ |
| 感應器模擬 (acc/gyro/mag) | ✅ |
| 震動裝置模擬 | ✅ |
| 電池模擬 | ✅ |
| Macro 錄製/播放 | ✅ |
| Key scheme 匯入/匯出 | ✅ |
| Performance HUD (FPS/Lat/Drop) | ✅ |
| Root mode | ✅ |
| Device spoofing (5 flagship profiles) | ✅ |
| Clipboard 同步 | ✅ |
| File sharing (push/pull) | ✅ |
| 網路 Proxy 設定 | ✅ |
| 網速模擬 (GPRS→Full) | ✅ |
| Screen resize / DPI / rotation | ✅ |
| FPS lock (30/60/90/120) | ✅ |
| Eco mode (background 降優先級 + Ctrl+Shift+F) | ✅ |
| Multi-instance (batch start/stop) | ✅ |
| Audio (WASAPI) | ✅ |
| Boss Key 縮至工作列 (Ctrl+Shift+X) | ✅ |
| Trim Memory (Ctrl+Shift+T) | ✅ |
| Mute/unmute (Ctrl+Shift+M) | ✅ |
| Shake 震動模擬 (Ctrl+Shift+3) | ✅ |
| Rotate 循環旋轉 (Ctrl+Shift+4) | ✅ |
| Open Downloads (Ctrl+Shift+6) | ✅ |
| Custom cursor (十字準心 / 標準) | ✅ |
| Pinned apps 釘選常用應用 | ✅ |
| Network proxy 設定 | ✅ |
| Network speed 模擬 (GPRS→Full) | ✅ |

## 架構

```
UI Layer          src/host/ui/           Qt 6 QML 視窗、Dock、設定面板、Input Overlay
Config            src/host/config/       JSON ConfigManager (nlohmann/json)
Input             src/host/input/        InputBridge → gRPC Touch/Key、Console/QMP/ADB；CoordinateMapper；Gamepad；Macro
Graphics          src/host/graphics/     FramebufferCapture (Grpc/Adb/Vnc/SharedMemory/SharedD3D11)；AngleBackend；PerformanceMonitor
Audio             src/host/audio/        WASAPI shared-mode
Instance          src/host/instance/     VM lifecycle；ProcessLauncher；DeviceSpoofer；MemoryTrimmer
Integration       src/host/integration/  ClipboardBridge (CF_UNICODETEXT)；LocationSimulator (geo fix)
Utils             src/common/utils/      Logger；ThreadPool；FileUtils
Tests             tests/unit/            16 Qt Test executables
                  tests/integration/     emulator-boot / input-inject / screencap (QSKIP guards)
```

**Input priority chain**: 滑鼠左鍵/觸控 gRPC `sendTouch` (8554) → HvSocket → Console (5554 telnet) → QMP → ADB；
鍵盤 gRPC `sendKey` (8554) → QMP → ADB（console 無鍵盤通道）
**Display path**: gRPC framebuffer streaming（預設，emulator `-no-window` headless，無彈出視窗）→ opportunistic shared memory / D3D11 shared texture capture（需 producer env vars）→ legacy Win32 window embed（opt-in `--native-embed`）

## 重要決策（不討論不改）

1. **MSVC only** — MSYS2 GCC 在此機器上 `cc1plus.exe` crash，不嘗試 MinGW
2. **emulator.exe 為生產引擎** — BlueStacks 同等級 (QEMU+WHPX)；`--qemu-backend/--hcs-backend` 是 R&D
3. **ANGLE 動態載入** — `libEGL.dll` + `libGLESv2.dll` via QLibrary；不需要 .lib
4. **AVD 用 `google_apis`** — 避免 `google_apis_playstore` 的 ADB RSA 驗證鎖
5. **Port 5554 = Android Console telnet**，不是 JSON QMP（JSON QMP 在 `--qemu-backend` port 4445）
6. **BlueStacks input**: `HD-Bridge-Native.dll` → virtio-input，不是 kernel driver；`BstkDrv.sys` 是 network/filter driver

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25
```

## Feature Flags

| 變數 | 值 | 預設 | 說明 |
|------|----|------|------|
| `CHIMERA_INPUT_BACKEND` | `console\|adb\|qmp\|auto` | `auto` | auto = 嘗試 Console，不 Ready 則退回 ADB |
| `CHIMERA_PROCESS_LAUNCHER` | `legacy\|native\|auto` | `auto` | legacy = `_popen`；native = `CreateProcessW` |
| `CHIMERA_QUICK_BOOT` | `0\|1` | `0` | `1` = 啟用 `chimera_quickboot` snapshot；預設 full boot，直到 snapshot 空畫面/ADB offline 風險完全穩定 |
| `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` | 正整數 | `1920` / `1080` | gRPC raw capture 尺寸；低於 1920x1080 會被 clamp 回 1080p，不可用降解析度換 FPS |
| `CHIMERA_SHMEM_FRAME_NAME` / `CHIMERA_SHMEM_FRAME_EVENT` | Win32 object name | 空 | CPU-copy shared-memory framebuffer backend；使用 seqlock header，沒有第一幀時仍會讓 gRPC fallback |
| `CHIMERA_D3D11_TEXTURE_METADATA` / `CHIMERA_D3D11_TEXTURE_EVENT` | Win32 object name | 空 | D3D11 named shared texture metadata backend；producer 必須建立 named shared texture，host 用 Qt D3D11 scene graph native render |

**CLI 旗標**：`--native-embed` 改用 legacy Win32 視窗嵌入（預設為 gRPC streaming，emulator headless）；`--no-emulator` 不啟動 emulator。

## 已知問題

| 問題 | 狀態 |
|------|------|
| SurfaceFlinger crash-loop (`--cuttlefish`) | OPEN — Phase 8 (gfxstream) |
| ADB TCP blocked (boot_completed=1 未到達) | OPEN — Phase 8 解鎖 |
| Console 無鍵盤通道（`event keydown` 不存在、`event send` EV_KEY 只到觸控裝置） | RESOLVED — 鍵盤改走 emulator gRPC `sendKey`，<5ms（getevent 驗證）|
| gRPC 截圖偶發 stall / app switch 尖峰造成短暫掉幀 | PARTIAL — 不再允許降到 800x450 當預設；1080p raw `getScreenshot` 仍慢，解法是 shared GPU texture/custom QEMU 顯示路徑 |
| emulator `streamScreenshot` 動畫中 0 幀（此 build 壞掉） | ACCEPTED — 改用 `getScreenshot` 管線輪詢 |
| 舊 Win32 SetParent 嵌入會破壞 emulator Qt 視窗群組 | RESOLVED — 改用 gRPC streaming 為預設，embed 改 opt-in |
| emulator `streamScreenshot` 串流被節流（~0.1 FPS） | RESOLVED — 改為管線化輪詢 unary `getScreenshot` |
| gRPC 擷取忙輪詢榨乾 CPU（電腦卡頓） | PARTIAL — idle duplicate cadence 約 50ms、有輸入才 boost 到 16ms；capture floor 是 1920x1080，後續需用 shared texture producer 降低 1080p 成本 |
| emulator/qemu 搶佔主機 audio thread（音樂卡頓/雜音） | RESOLVED — 預設 2 vCPU + `normal` priority（不高於 Normal）；boot completed 前不啟動 gRPC capture；`enableAudio=false` 時不掛 `virtio-snd-pci` |
| gRPC 管線 HTTP/2 stream hang，擷取整個凍結 | RESOLVED — watchdog（無幀 2s 重啟管線）+ 請求 transferTimeout |
| gRPC 擷取 busy-polling 榨乾 CPU | RESOLVED — idle duplicate cadence 約 50ms，互動時 pace 到 16ms + depth 3 管線 |
| gRPC pipeline stall thundering herd（~5fps 永久崩潰） | RESOLVED — `restartPipeline()` 不 abort、只補 slot |
| 原生 1080p 擷取（>6MB/幀）拖垮頻寬/CPU | PARTIAL — Android guest 與 capture request 都維持至少 1920x1080；host shared texture smoke 已達 1080p 59.9 FPS，Android/emulator producer 尚未接入 |
| 滑鼠滾輪捲動卡頓 | PARTIAL — wheel 改走 emulator gRPC `sendTouchSwipe()`，throttle 約 16ms，單次 instant swipe 降到 3 個 touch request；ADB `input swipe` 只保留為 fallback |
| FPS 虛報（靜止畫面報 60+） | RESOLVED — 主側欄單一 FPS 改顯示有效 FPS：`min(guestFps, streamFps, renderFps)`；Stream 只留在 HUD/log |
| 靜止畫面重複 repaint 造成 host 開銷 | RESOLVED — gRPC duplicate frame 只更新 stream metric，不送 `frameReady()`；idle duplicate cadence 約 50ms，有輸入才 boost |
| 真 60 FPS 互動流暢度 | PARTIAL — host shared texture smoke 已達 1920x1080 `Guest/Stream/Render 59.9 FPS`、`Dup 0`；Android/emulator 端 shared texture producer 尚未接入，通知欄/滑動/遊戲 flow 仍需重測 |
| orphan qemu 累積導致雙 VM、整機卡死 | RESOLVED — `ProcessLauncher::runAsync()` 將子程序放入 kill-on-close Job Object；force-kill `chimera-ui.exe` 後 emulator/qemu 皆消失 |
| 冷開機數十秒 | PARTIAL — Quick Boot verifier 曾量到 full boot 66.7s → snapshot boot 9.7s；但 snapshot 可能保存壞的 guest state，預設改回 full boot，需 `CHIMERA_QUICK_BOOT=1` 才啟用 |
| 開機後 Stream 顯示近乎空畫面 | RESOLVED — full boot 後自動 wake / dismiss keyguard / HOME；runtime 驗證 ADB screenshot 為 1920x1080 橫向 Home，gRPC 62-67 FPS |
| 首頁點擊完全沒反應 | RESOLVED — 普通點擊改走 emulator gRPC `sendTouch`；runtime 驗證點 Settings 後 foreground package 進入 `com.android.settings` |
| Pixel Launcher / 原生首頁雜亂 | PARTIAL — 新增 `com.chimera.launcher` HOME app 並於 boot completed 後自動 install/set-home；完整 ROM 級精簡仍需後續移除/停用多餘系統元件 |
| Chimera Launcher 啟動後只剩黑底/狀態列 | RESOLVED — 移除 Activity 啟動時強制 immersive hide system bars；host 改 explicit start launcher component；runtime UI tree / screenshot 驗證 `CHIMERA` 可見 |
| 側欄 FPS 卡資訊太擠 | RESOLVED — 主側欄只顯示單一 FPS 數字；Guest/Stream/Render/Dup 細節保留在 log/HUD |
| Launcher 厚黑邊 / status bar 不常駐 | RESOLVED — Launcher theme 不再 fullscreen；runtime screenshot 驗證 status bar 圖示可見且首頁只顯示 Google Play、檔案管理、瀏覽器、設定 |
| Host shell 佔用太多模擬器空間 | RESOLVED — 頂欄降到 46px、外框 margin 10px、側欄降到 190/172px |
| Google Play / 檔案管理 / 瀏覽器入口不可用 | RESOLVED — AVD 切到 `google_apis_playstore`；Material Files 從 `third_party/android-apps/material-files.apk` 自動安裝；Chrome/Files 使用明確 component 啟動 |
| Google Play 新安裝 app 不出現在 Home | RESOLVED — Chimera Launcher 固定四入口置頂，並追加所有 `ACTION_MAIN` / `CATEGORY_LAUNCHER` app |
| Home 固定入口灰掉、Settings/TMobile 亂入 | RESOLVED — 檔案管理/瀏覽器加入 Chimera 內建 fallback Activity；動態掃描只追加 user-installed apps 並排除系統殘留 |

## 路徑

| 資源 | 路徑 |
|------|------|
| 專案根目錄 | `D:\Workspace_cloud\Personal_Project\chimera\` |
| Build 輸出 | `build\Release\` |
| Qt | `C:\Qt\6.8.3\msvc2022_64\` |
| Android SDK | `third_party\android-sdk\` |
| AVD | `third_party\android-avd\chimera_dev.avd\` |
| Third-party APK cache | `third_party\android-apps\` |
| Instance 設定 | `configs\instances.json` |
| Chimera Launcher source | `tools\chimera-launcher\` |
| Chimera Launcher APK | `build\launcher\chimera-launcher.apk` |
| ANGLE headers | `third_party\angle\` |

## 參考文件

| 文件 | 用途 |
|------|------|
| `AGENTS.md` | Build、測試、Git、Coding 標準、疑難排解 |
| `CONTEXT.md` | 開發歷程、相位記錄、bug 修正紀錄 |
| `tasks/todo.md` | 當前任務規劃、清理範圍與驗證回顧 |
| `scripts/verify-quick-boot.ps1` | Runtime smoke：重建 `chimera_quickboot`、驗證 Quick Boot 秒數與 orphan cleanup |
| `scripts/build-chimera-launcher.ps1` | 建置/簽章 Android HOME launcher APK |
| `docs/adr/ADR-001-shared-folder.md` | SharedFolder 技術選型 ADR |
| `docs/references/bluestacks.conf` | BlueStacks 設定格式參考 |

**禁止 commit**: BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/)、root 層 ISO/QCOW2/installer、
QEMU/debug logs、R&D throwaway scripts、runtime output dirs。

---
*Updated: 2026-05-27 — Session 28（shared D3D11 texture renderer + truthful 60fps smoke）*
