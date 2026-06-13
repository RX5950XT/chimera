# Project Chimera — CLAUDE.md

> AI 工作階段快速參考。每次重大變更後更新。

## 當前狀態

**完成度**: BlueStacks Parity Roadmap v3 P0–P4e + Session 2–7 補強 COMPLETE (2026-05-19)
**生產引擎**: `emulator.exe` (Google QEMU+WHPX fork) — `--qemu-backend` / `--hcs-backend` 為 legacy R&D，保留不刪
**BlueStacks parity (emulator.exe 路徑)**: 已達成核心功能同等級（見「BlueStacks Parity 功能清單」）
**Phase 8 (legacy)**: `--cuttlefish` R&D 路徑的 gfxstream/SF stable 問題，不影響生產路徑功能
**Tests**: 20/20 unit tests PASS；3 integration tests（需 emulator 運行中）
**Current runtime gate**: stock emulator HWND/window capture 與 native embed 都是 unsafe diagnostics；除了 unsafe CLI 參數外還必須設 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`，且由同次 Chimera 啟動建立內部 `CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1`，才允許外露原生視窗。正常啟動強制 headless / `-no-window`，並在啟動後檢查 emulator/qemu process tree，若仍出現可見原生 HWND 就立即終止整棵 tree。預設 full boot 會帶 `-no-snapstorage -no-snapshot -no-snapshot-load -no-snapshot-save`；一般 `stop()` 和 true-1080p60 verifier cleanup 不走 `adb emu kill`，避免 emulator shutdown snapshot/I/O 干擾 host audio。standalone-built modified gfxstream artifact 目前已證實與 SDK Emulator 36.5.11 ABI 不相容，不能當 1080p/60 完成證據；gfxstream shared texture runtime 必須通過 marker + manifest + SDK ABI export + SDK runtime imports + source snapshot build id matching gate。required shared texture 模式在 Android boot 後若沒有 capture / metadata / 第一幀會 bounded exit 3，不可黑屏靜默或 raw fallback。真 1080p/60 仍需 `verify-true-1080p60.ps1` dynamic flow PASS；目前 SDK build id `15261927` 尚未找到 matching public gfxstream source，`sdk-release` `13278158` 與 `emu-36-1-release` `12579432` 都不可當完成證據。source-patched Vulkan bridge 已能編譯並提供低頻 `recordCopy` / `publishFrame` 診斷與 1920x1080 floor，但 mixed ABI manifest gate 仍必須拒絕。新增 `scripts\analyze-gfxstream-proxy-log.ps1` 可離線分類 stock-ABI proxy log；只有 1920x1080 GPU display/resource signal 可作為下一步 hook 候選，CPU readback 訊號不可當 producer。Chimera 走 fork/改 Android Emulator + gfxstream/QEMU runtime，不是完整重寫 Android VM，也不允許正式路徑多開原生 Android Emulator 視窗。

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
Graphics          src/host/graphics/     FramebufferCapture (Grpc/Adb/Vnc/SharedMemory/SharedD3D11)；SharedD3D11TexturePublisher；AngleBackend；PerformanceMonitor
Audio             src/host/audio/        WASAPI shared-mode
Instance          src/host/instance/     VM lifecycle；ProcessLauncher；DeviceSpoofer；MemoryTrimmer
Integration       src/host/integration/  ClipboardBridge (CF_UNICODETEXT)；LocationSimulator (geo fix)
Utils             src/common/utils/      Logger；ThreadPool；FileUtils；LowInterferenceProcess
Tests             tests/unit/            16 Qt Test executables
                  tests/integration/     emulator-boot / input-inject / screencap (QSKIP guards)
```

**Input priority chain**: 滑鼠左鍵/觸控 gRPC `sendTouch` (8554) → HvSocket → Console (5554 telnet) → QMP → ADB；
鍵盤 gRPC `sendKey` (8554) → QMP → ADB（console 無鍵盤通道）
**Display path**: headless GPU/shared texture transports（正式方向，emulator `-no-window` hidden）→ raw gRPC/MMAP/screenrecord/ADB capture（CLI-only 診斷 `--allow-raw-capture-fallback`，不可當 1080p/60 證據）→ legacy Win32 window embed / window capture（unsafe CLI opt-in only）

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
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-gfxstream-runtime.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25
```

## Feature Flags

| 變數 | 值 | 預設 | 說明 |
|------|----|------|------|
| `CHIMERA_INPUT_BACKEND` | `console\|adb\|qmp\|auto` | `auto` | auto = 嘗試 Console，不 Ready 則退回 ADB |
| `CHIMERA_PROCESS_LAUNCHER` | `legacy\|native\|auto` | `auto` | legacy = `_popen`；native = `CreateProcessW` |
| `CHIMERA_EMULATOR_PATH` | path | config | 覆蓋 `configs/android_sdk.json` 的 emulator；custom emulator runtime 會自動 prepend 旁邊的 `lib64/`、`lib/` 到 PATH |
| `CHIMERA_QUICK_BOOT` | `0\|1` | 空 | 只有設為 `1` 才載入 `chimera_quickboot`；預設 full boot，避免壞 snapshot / snapshot I/O 回歸影響 host audio |
| `CHIMERA_SAVE_QUICK_BOOT` | `0\|1` | 空 | 只有設為 `1` 才會保存 `chimera_quickboot` 並允許 console kill；boot 後與正常 stop 都不預設保存，避免 host audio 卡頓 |
| `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` | 正整數 | `1920` / `1080` | gRPC raw capture 尺寸；低於 1920x1080 會被 clamp 回 1080p，不可用降解析度換 FPS |
| `CHIMERA_GRPC_TRANSPORT` | `unary\|mmap` | `unary` | `mmap` 是 `streamScreenshot` MMAP 實驗路徑；現在 RGBA8888 會優先 publish D3D11 shared texture，但最新 dynamic smoke 仍只有約 29 FPS，不能當 1080p/60 證據 |
| `CHIMERA_VIDEO_TRANSPORT` | `screenrecord` | 空 | ADB H.264 screenrecord 實驗路徑；未出第一幀時低頻重啟並輸出 adb/ffmpeg stderr，不可當預設 60 FPS 證據 |
| `CHIMERA_LOG_PATH` | path | 空 | 將 Qt message handler 同步寫到指定 log；runtime verifier 用來解析 `CHIMERA_PERF` |
| `CHIMERA_SHMEM_FRAME_NAME` / `CHIMERA_SHMEM_FRAME_EVENT` | Win32 object name | 空 | CPU-copy shared-memory framebuffer backend；使用 seqlock header，沒有第一幀時仍會讓 gRPC fallback |
| `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE` | `0\|1` | 空 | 啟用 modified EmuGL shared D3D11 texture transport；等同 CLI `--emugl-shared-texture`，stock emulator 不會生效 |
| `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE` | `0\|1` | 空 | fail-closed 模式；runtime 不可用、shared texture 沒第一幀、或 stock gfxstream 都會直接失敗；renderer strict mode 也會禁止 `m_onPost` / `ColorBuffer::readback()` fallback |
| `CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE` | `0\|1` | 空 | 啟用 modified gfxstream shared D3D11 texture transport；等同 CLI `--gfxstream-shared-texture`，stock 或缺 Chimera marker / manifest / SDK ABI export / SDK runtime imports 的 `libgfxstream_backend.dll` 不會生效 |
| `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE` | `0\|1` | 空 | fail-closed 模式；runtime 不可用、binary marker 缺失、manifest 無效、SDK ABI/import 不相容、shared texture 沒第一幀都不得回落 raw gRPC/ADB 或 stock emulator HWND |
| `CHIMERA_D3D11_TEXTURE_METADATA` / `CHIMERA_D3D11_TEXTURE_EVENT` | Win32 object name | 空 | D3D11 named shared texture metadata backend；producer 必須建立 named shared texture，host 用 Qt D3D11 scene graph native render |
| `CHIMERA_EMUGL_D3D11_TEXTURE_METADATA` / `NAME` / `EVENT` | Win32 object name | auto opt-in | modified EmuGL bridge 使用；opt-in 時 host 會自動與 `CHIMERA_D3D11_TEXTURE_*` 同步 |
| `CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA` / `NAME` / `EVENT` | Win32 object name | auto opt-in | modified gfxstream bridge 使用；opt-in 時 host 會自動與 `CHIMERA_D3D11_TEXTURE_*` 同步 |

**CLI 旗標**：`--native-embed` 必須另加 `--allow-unsafe-native-window`，且本次啟動同時有 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`，才會建立 internal diagnostics session 並改用 legacy Win32 視窗嵌入；舊 unsafe display 環境變數只會被警告並忽略，避免多開 stock emulator 視窗。`--no-emulator` 不啟動 emulator；`--emugl-shared-texture` 啟用 modified EmuGL shared texture transport；`--gfxstream-shared-texture` 啟用 modified gfxstream shared texture transport；`--allow-raw-capture-fallback` 才允許 raw gRPC/MMAP/screenrecord/ADB 診斷 fallback，`CHIMERA_ALLOW_RAW_CAPTURE_FALLBACK` 只警告不生效；`--window-capture` 預設拒絕，必須另加 `--allow-unsafe-window-capture` 且同時有 visible diagnostics allowance 才能做本機實驗。

**gfxstream proxy probe**：`scripts\build-chimera-gfxstream-proxy-runtime.ps1` 會建立 `build\chimera-gfxstream-proxy-runtime`，用 stock SDK backend 當 ABI-compatible target，再由 proxy DLL hook C export 層。`initLibrary` wrapper 預設只 forward stock `RenderLibPtr`，`CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` / `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` 只能做本機 opt-in probe。實測包 `Renderer` shared_ptr 會讓 emulator 早退；`stream_renderer_flush` 已加低頻 `resource_get_info` probe，但 headless/no-audio boot completed 仍沒有 flush log；此 runtime 目前 `sharedTextureProducer=false`，只能當 hook/ABI probe，不能當 1080p/60 完成證據。

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
| gRPC 擷取忙輪詢榨乾 CPU（電腦卡頓） | PARTIAL — raw unary/MMAP/screenrecord/ADB fallback 改為 CLI-only 診斷 `--allow-raw-capture-fallback`；capture floor 是 1920x1080 且 request RGBA8888，但 raw fallback 仍不是真 60，後續需 shared texture producer |
| emulator/qemu 搶佔主機 audio thread（音樂卡頓/雜音） | PARTIAL — 預設 2 vCPU；emulator/qemu 啟動前 30 秒先用 `Idle` startup priority，之後回 `below_normal`；Eco mode 解除也只回 `BelowNormal` 並套整棵 process tree；ProcessLauncher 會 cap 高 priority 到 Normal，並對低 priority process 套 memory priority / power throttling / ignore timer resolution；boot completed 前不啟動 gRPC capture；Quick Boot load/save 都是 opt-in；預設 full boot 加 `-no-snapstorage`，一般 stop / true verifier cleanup 不走 `adb emu kill`；`enableAudio=false` 時不掛 `virtio-snd-pci` |
| 直接 adb/ffmpeg helper 繞過低干擾 policy | RESOLVED — `LowInterferenceProcess` 已套用到 main boot/setup adb、QML Android controls、ADB raw fallback、ScreenRecorder ffmpeg；這些旁路不再只靠 `ProcessLauncher` 規範 emulator/qemu |
| gRPC 管線 HTTP/2 stream hang，擷取整個凍結 | RESOLVED — watchdog（無幀 2s 重啟管線）+ 請求 transferTimeout |
| gRPC 擷取 busy-polling 榨乾 CPU | PARTIAL — raw fallback 預設不再啟動；只有 CLI `--allow-raw-capture-fallback` 才能進診斷路徑，避免舊 env 重新造成 host audio 卡頓；1080p raw/stock MMAP 仍需被 shared texture 取代 |
| gRPC pipeline stall thundering herd（~5fps 永久崩潰） | RESOLVED — `restartPipeline()` 不 abort、只補 slot |
| 原生 1080p 擷取（>6MB/幀）拖垮頻寬/CPU | PARTIAL — Instance config、Android guest `hw.lcd.width/height`、emulator `-window-size`、UI `wm size`、legacy QEMU/HCS backend、raw capture request、CPU shared-memory metadata、D3D11 shared texture metadata 都維持至少 1920x1080；host shared texture smoke 已達 1080p 59.9 FPS，Android/emulator producer 尚未接入 |
| 滑鼠滾輪捲動卡頓 | PARTIAL — wheel 改走 emulator gRPC `sendTouchSwipe()`，throttle 約 16ms，單次 instant swipe 降到 3 個 touch request；`ChimeraWindow` 不再重複轉發 window 座標事件，輸入只由 `GuestDisplay` 送 guest 座標；ADB `input swipe` 只保留為 fallback |
| FPS 虛報（靜止畫面報 60+） | RESOLVED — 主側欄單一 FPS 改顯示有效 FPS：`min(guestFps, streamFps, renderFps)`；Stream 只留在 HUD/log |
| 靜止畫面重複 repaint 造成 host 開銷 | RESOLVED — gRPC duplicate frame 只更新 stream metric，不送 `frameReady()`；idle duplicate cadence 約 250ms，有輸入才 boost |
| 真 60 FPS 互動流暢度 | PARTIAL — host shared texture smoke 已達 1920x1080 `Guest/Stream/Render 59.9 FPS`、`Dup 0`；Android/emulator 端 shared texture producer 尚未接入，通知欄/滑動/遊戲 flow 仍需重測 |
| Android/emulator shared texture producer | PARTIAL — `SharedD3D11TexturePublisher` 已抽成正式 producer contract，producer/capture 都會拒絕低於 1920x1080 的 metadata；EmuGL `FrameBuffer::post()` 已有 `ChimeraSharedTextureBridge` hook，成功發布時會跳過 CPU `readback()`；modern gfxstream patch script 已補 headless Vulkan display-post path，讓 bridge enabled 且無 surface 時仍可進 `recordCopy()` / `publishFrame()`；目前 blocker 是 custom runtime source snapshot build id 不等於 SDK emulator build id，實際 SDK 相容 gfxstream producer runtime 仍待接入 |
| True Android 1080p/60 verifier | PARTIAL — `scripts\verify-true-1080p60.ps1` 預設 `-RuntimeKind Gfxstream`，會拒絕 stock gfxstream、缺 binary marker、raw gRPC/ADB/screenrecord fallback、低於 1920x1080、effective FPS < 60 或 duplicate 過高；目前正確 fail-closed 於 `Required shared texture runtime is unavailable` / gfxstream bridge 不可用，尚未有 custom gfxstream runtime PASS 證據 |
| 原生 Android Emulator 視窗外露 | RESOLVED — 正常啟動強制 headless；舊 `headless=false` instance 會 normalize 回 `-no-window`，且正式路徑在 process creation 層級強制 hidden launch。啟動後若 emulator/qemu process tree 仍外露可見 HWND，Chimera 會立即終止該 emulator tree。`--window-capture` / `--native-embed` 除了各自 unsafe flag 與 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 外，還必須由同次 CLI 建立 internal diagnostics session；殘留 env 不能單獨開原生視窗 |
| orphan qemu 累積導致雙 VM、整機卡死 | RESOLVED — `ProcessLauncher::runAsync()` 將子程序放入 kill-on-close Job Object；force-kill `chimera-ui.exe` 後 emulator/qemu 皆消失 |
| 冷開機數十秒 | PARTIAL — Quick Boot 可用 `CHIMERA_QUICK_BOOT=1` 或 verifier 明確啟用；預設 full boot 以保護 host audio，保存/重建 snapshot 走 verifier 或 `CHIMERA_SAVE_QUICK_BOOT=1` |
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
| `scripts/verify-true-1080p60.ps1` | Runtime gate：預設強制 custom gfxstream shared texture；可用 `-RuntimeKind EmuGL` 明確驗證 legacy EmuGL，驗證 Android 動態 1920x1080 effective 60 FPS |
| `scripts/analyze-gfxstream-proxy-log.ps1` | 離線分析 stock-ABI gfxstream proxy log；辨識 GPU display/resource 訊號與 CPU readback 風險 |
| `scripts/build-chimera-launcher.ps1` | 建置/簽章 Android HOME launcher APK |
| `docs/adr/ADR-001-shared-folder.md` | SharedFolder 技術選型 ADR |
| `docs/references/bluestacks.conf` | BlueStacks 設定格式參考 |

**禁止 commit**: BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/)、root 層 ISO/QCOW2/installer、
QEMU/debug logs、R&D throwaway scripts、runtime output dirs。

---

## 2026-06-13 — Session 70

- `VirtualMachine` 正式啟動前會清理佔住 Chimera 固定 ports `5554/5555/8554` 的 stale emulator/qemu tree，防止前一輪殘留造成雙 VM 與 host audio 卡頓。
- `NativeEmulatorView` 只在 unsafe native embed diagnostics 啟用時 pin PID / 可見；預設 UI 路徑保持 `GuestDisplay` + headless backend。
- 驗證：targeted tests 3/3 PASS；Release build PASS；non-integration `ctest` 20/20 PASS；`--no-emulator` smoke 無 native pin log。
- true 1080p/60 尚未完成；仍需 matching SDK gfxstream shared texture producer。

## 2026-06-13 — Session 71

- 產品邊界維持：不從零重寫完整 Android VM；保留 Android Emulator/QEMU/gfxstream headless 相容核心，但使用者面只允許 Chimera 單一視窗。
- gfxstream Vulkan bridge 補低頻 `enabled` / `recordCopy` / `publishFrame` 診斷與 1920x1080 runtime floor，避免黑屏時沒有證據，也避免低解析度 producer 冒充 60 FPS。
- source-patched gfxstream C++ build 已編過 bridge / `DisplayVk`；manifest gate 正確拒絕 source build id `13278158` 對 SDK build id `15261927` 的 mixed ABI runtime。
- 驗證：patch/build parser PASS；targeted build PASS；targeted `ctest` 2/2 PASS；完整 non-integration `ctest` 20/20 PASS；結束後無 Chimera/emulator/qemu/adb/ffmpeg 殘留。
- true 1080p/60 尚未完成；仍需 matching SDK gfxstream shared texture producer。

## 2026-06-13 — Session 72

- 新增 `scripts\analyze-gfxstream-proxy-log.ps1`，離線分類 stock-ABI proxy log；只把 1920x1080 GPU display/resource signal 當下一步 hook 候選。
- 合成 log 驗證：1080p `stream_renderer_flush` PASS；只有 `android_onPost` 如預期 fail，避免 CPU readback 被誤判成 producer。
- proxy runtime build PASS，348 exports；non-integration `ctest` 20/20 PASS。
- 既有 proxy logs 沒有 1920x1080 GPU display/resource signal；子代理研究因額度限制失敗，沒有可採用結論。
- 本輪沒有啟動 Android runtime，結束後無 Chimera/emulator/qemu/adb/ffmpeg 殘留。
- true 1080p/60 尚未完成；下一步仍是 matching SDK gfxstream source/ABI 或 stock-ABI GPU display-post hook。

## 2026-06-13 — Session 73

- 修正 `initLibrary` ABI crash 根因：從 `gfxstream_proxy.c` 移除 `void*(void*)` C shim，改為 `gfxstream_proxy_renderlib.cpp` 的 `extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary()`（exact C++ signature）。
- analyzer 同時計數 `renderlib_wrapper initLibrary` 與 `forward name=initLibrary`；build script 過時註解同步修正；gate 未放寬。
- 驗證：proxy build PASS（348 exports）；headless smoke boot 完成，`initLibrary=1 androidSetOpenglesRenderer=1 rendererVtable=1`；analyzer 正確 FAIL `no 1920x1080 GPU display/resource signal`；`no_residual_processes=OK`。
- true 1080p/60 尚未完成；下一步仍是 matching SDK gfxstream `DisplayVk::postImpl()` GPU post hook。

---
*Updated: 2026-06-13 — Session 73（initLibrary ABI fix + proxy smoke PASS）*
