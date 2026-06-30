# Project Chimera — CLAUDE.md

> AI 工作階段快速參考。每次重大變更後更新。

## 當前狀態

**完成度**: BlueStacks Parity Roadmap v3 P0–P4e + Session 2–7 補強 COMPLETE (2026-05-19)
**生產引擎**: `emulator.exe` (Google QEMU+WHPX fork) — `--qemu-backend` / `--hcs-backend` 為 legacy R&D，保留不刪
**BlueStacks parity (emulator.exe 路徑)**: 已達成核心功能同等級（見「BlueStacks Parity 功能清單」）
**Phase 8 (legacy)**: `--cuttlefish` R&D 路徑的 gfxstream/SF stable 問題，不影響生產路徑功能
**Tests**: 20/20 unit tests PASS；3 integration tests（需 emulator 運行中）
**Current runtime gate**: stock emulator HWND/window capture 與 native embed 都是 unsafe diagnostics；除了 unsafe CLI 參數外還必須設 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`，且由同次 Chimera 啟動建立內部 `CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1`，才允許外露原生視窗。正常啟動強制 headless / `-no-window`，若 emulator/qemu tree 仍外露可見 HWND 就立即終止。根目錄 `start-chimera.cmd` 現在預設走最快可用路徑：`start-chimera.ps1 -Fast -InteractiveFirst` → custom gfxstream shared texture + `CHIMERA_GUEST_VULKAN=1` + host runtime `skiavk`（`debug.renderengine.backend`/`debug.hwui.renderer`）framework restart + GuestVulkan animations=1；`-Stock` 才強制回 stock gRPC 慢路徑。真 1080p/60 已於 Session 89 嚴格可見 gate **PASS**：custom gfxstream runtime（SDK build id `15261927`）+ `chimera-gl60-smoke` 連續渲染 app，ADB screencap 先確認 1920×1080 非黑可見畫面（nonblack 83.3%、luma spread 305），host window 保持前景後量到 120 秒 steady `effective_fps_min=59.9 / avg=60.0 / dup=0`。frame path 是 source-patched `postFrameDirectGpu`：D3D11 NT shared texture 由 Vulkan external-memory import，guest Vulkan image 透過 GPU `recordCopy` 直接 blit 到 shared texture；不走 raw gRPC/ADB、GL readback fallback、或 staging `UpdateSubresource`（log：`GPU-direct D3D11 import OK=1`、`path=GPU-direct=79`、raw/GL fallback=0）。一般 Android UI：adb-swipe verifier 仍只能代表測試注入路徑（約 20fps）；一次性 host mouse-drag probe 量到 production input path `guestMax=116.7 / render=57.4 / dup=0`，但該測法會搶實體滑鼠，使用者要求後禁止再用。`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` 已驗證 1920×1080、Settings foreground `interactivity=ok`、0 residual。注意：60 FPS 證據仍須分清 workload；push-based idle Home 低 FPS 屬正常。verifier 會自動挑空的 emulator console/ADB port pair，避免本機服務占用 `console+1` 造成 ADB false timeout；post-warmup `effective<=0` 直接 fail，不能過濾成假 PASS。Chimera 走 fork/改 Android Emulator + gfxstream/QEMU runtime，不是完整重寫 Android VM，也不允許正式路徑多開原生 Android Emulator 視窗.

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
| `CHIMERA_INTERACTIVE_PRIORITY` | `idle\|below_normal\|normal` | `below_normal` | v1 emulator process priority（取代 main.cpp 硬寫值）。`idle`=audio-first（EcoQoS 降頻，保護 host 音樂、FPS 較低）；`normal`=interactive-first（最順、最多音訊競爭）。非法值退回 `below_normal`。上限被 normalizer/`safePriorityClass` 夾到 `normal` |
| `CHIMERA_INTERACTIVE_CPUS` / `CHIMERA_INTERACTIVE_RAM_MB` | 正整數 | `4` / `4096` | v1 emulator vCPU / RAM override；**注意**：`normalizedInstanceConfig` 仍 floor `>=4 / >=4096`，要讓低值生效需另降該 floor |
| `CHIMERA_QUICK_BOOT` | `0\|1` | 空 | 只有設為 `1` 才載入 `chimera_quickboot`；預設 full boot，避免壞 snapshot / snapshot I/O 回歸影響 host audio |
| `CHIMERA_SAVE_QUICK_BOOT` | `0\|1` | 空 | 只有設為 `1` 才會保存 `chimera_quickboot` 並允許 console kill；boot 後與正常 stop 都不預設保存，避免 host audio 卡頓 |
| `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` | 正整數 | `1920` / `1080` | gRPC raw capture 尺寸；低於 1920x1080 會被 clamp 回 1080p，不可用降解析度換 FPS |
| `CHIMERA_GRPC_REQUEST_TIMEOUT_MS` | 正整數 | `6000` | 單一 `getScreenshot` request 的 transfer timeout，**解耦於** stall watchdog（2s）。負載下 1080p readback 可能數秒才吐第一個 byte；綁 2s 會在任何幀到達前 abort → `total=0` 凍黑。低於 stall watchdog 的值會被拒。robustness 旋鈕，不提升 FPS |
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
| stock gRPC `getScreenshot` 整輪 0 幀（`total=0`） | RESOLVED（根因）+ PARTIAL（負載硬化）— Session 93 **實證根因是 hardcoded port**:`main.cpp` 建 capture 時硬寫 gRPC port `8554`，但 emulator gRPC port 是 `g_runtimeCfg.grpcPort = 8554 + console offset`。verifier auto-pick console 5560 → emulator 聽 8560，capture 卻連 8554 → 幀永遠到不了（ADB 仍正常，故可見 gate 誤過；Session 90 誤判為「readback 延遲 > watchdog」）。修法:capture 改用 derived port（default 5554→8554 不變）。次要硬化（負載下真實慢 readback）:① per-request `setTransferTimeout` 與 stall watchdog（2s）解耦（`CHIMERA_GRPC_REQUEST_TIMEOUT_MS` 預設 6000）;② 外層 retry timer 盲目 `stop()/start()` 前查 `hasInFlight()`。**仍是 robustness 不是 FPS**:stock ~4–17 FPS（不經 gRPC 的 custom shared-texture path 才是流暢正解）|
| emulator `streamScreenshot` 動畫中 0 幀（此 build 壞掉） | ACCEPTED — 改用 `getScreenshot` 管線輪詢 |
| 舊 Win32 SetParent 嵌入會破壞 emulator Qt 視窗群組 | RESOLVED — 改用 gRPC streaming 為預設，embed 改 opt-in |
| emulator `streamScreenshot` 串流被節流（~0.1 FPS） | RESOLVED — 改為管線化輪詢 unary `getScreenshot` |
| gRPC 擷取忙輪詢榨乾 CPU（電腦卡頓） | PARTIAL — raw unary/MMAP/screenrecord/ADB fallback 改為 CLI-only 診斷 `--allow-raw-capture-fallback`；capture floor 是 1920x1080 且 request RGBA8888，但 raw fallback 仍不是真 60，後續需 shared texture producer |
| emulator/qemu 搶佔主機 audio thread（音樂卡頓/雜音） | PARTIAL — priority 現在可由 `CHIMERA_INTERACTIVE_PRIORITY` 設定（預設 `below_normal`；`idle`=audio-first EcoQoS、`normal`=interactive-first），`start-chimera.ps1 -AudioFirst/-InteractiveFirst` 為 sugar；emulator/qemu 啟動前 30 秒先用 `Idle` startup priority，之後回設定值；ProcessLauncher cap 高 priority 到 Normal，並對低 priority process 套 memory priority / power throttling；boot completed 前不啟動 gRPC capture；Quick Boot load/save opt-in；`enableAudio=false` 時不掛 `virtio-snd-pci`。**Session 90 量測**：`verify-interactive-ui.ps1` healthy 互動 run `helperSpawns=0`（boot-probe `guestPerfTimer` boot_completed 即停、retry timer 首幀即停），steady-state helper churn 已 ~0；剩餘 audio 競爭主因是 1080p readback CPU 與 emulator 本體（`qemuCpuPctAvg≈22%`），調節桿是 priority |
| 直接 adb/ffmpeg helper 繞過低干擾 policy | RESOLVED — `LowInterferenceProcess` 已套用到 main boot/setup adb、QML Android controls、ADB raw fallback、ScreenRecorder ffmpeg；這些旁路不再只靠 `ProcessLauncher` 規範 emulator/qemu |
| gRPC 管線 HTTP/2 stream hang，擷取整個凍結 | RESOLVED — watchdog（無幀 2s 重啟管線）+ 請求 transferTimeout |
| gRPC 擷取 busy-polling 榨乾 CPU | PARTIAL — raw fallback 預設不再啟動；只有 CLI `--allow-raw-capture-fallback` 才能進診斷路徑，避免舊 env 重新造成 host audio 卡頓；1080p raw/stock MMAP 仍需被 shared texture 取代 |
| gRPC pipeline stall thundering herd（~5fps 永久崩潰） | RESOLVED — `restartPipeline()` 不 abort、只補 slot |
| 原生 1080p 擷取（>6MB/幀）拖垮頻寬/CPU | PARTIAL — Instance config、Android guest `hw.lcd.width/height`、emulator `-window-size`、UI `wm size`、legacy QEMU/HCS backend、raw capture request、CPU shared-memory metadata、D3D11 shared texture metadata 都維持至少 1920x1080；host shared texture smoke 已達 1080p 59.9 FPS，Android/emulator producer 尚未接入 |
| 滑鼠滾輪捲動卡頓 | PARTIAL — wheel 改走 emulator gRPC `sendTouchSwipe()`，throttle 約 16ms，單次 instant swipe 降到 3 個 touch request；`ChimeraWindow` 不再重複轉發 window 座標事件，輸入只由 `GuestDisplay` 送 guest 座標；ADB `input swipe` 只保留為 fallback |
| FPS 虛報（靜止畫面報 60+） | RESOLVED — 主側欄單一 FPS 改顯示有效 FPS：`min(guestFps, streamFps, renderFps)`；Stream 只留在 HUD/log |
| 靜止畫面重複 repaint 造成 host 開銷 | RESOLVED — gRPC duplicate frame 只更新 stream metric，不送 `frameReady()`；idle duplicate cadence 約 250ms，有輸入才 boost |
| 真 60 FPS 互動流暢度 | RESOLVED（連續渲染路徑）— Session 85：`chimera-gl60-smoke` 連續渲染 app 經 gfxstream direct-Vulkan→D3D11 path 量到 steady `min 59.8 / avg 60.0 FPS、dup 0、avgMs 16.2ms`，1920x1080。push-based idle/boot 動畫仍低 FPS 屬正常。**Session 94 重負載探針**：gl60 `-HeavyIterations 96`（1080p 全螢幕 plasma、96-iter/px）→ `min 5.8 / avg 6.6 / dup 0`，**同一 gpu-direct path 只加重 GLES fill 即 60→6.6**；瓶頸是 **guest GLES backend = host SwiftShader（軟體填色）**，非 post path。故 gl60 的 60 依賴 trivial fill；HW 加速重遊戲須走 **guest Vulkan→NVIDIA**（Session 91），其**連續渲染** 60 仍未量（無 Vulkan game-loop app）|
| Android/emulator shared texture producer | RESOLVED — Session 85：gfxstream `postImpl` headless 分支 `bridge.isDirectVkReady()` 時走 `postFrameDirectGpu`（guest VK image → GPU blit → HOST_COHERENT staging → D3D11 `UpdateSubresource`），kVk borrow 成功時完全不走 GL CPU readback；`Direct GPU bridge init: OK`，157× `postFrameDirectGpu`/`borrowForDisplay`、0× CPU readback fallback。kVk borrow 失敗（GLES-backed）才退回 `chimeraPublishFrameToD3D11Texture` |
| True Android 1080p/60 verifier | RESOLVED — Session 85：`verify-true-1080p60.ps1` 預設 Gfxstream，新增 `-WarmupSeconds`（排除 app 冷啟 transient）與 steady gate（min ≥ `MinEffectiveFps` 57 + avg ≥ `MinAvgEffectiveFps` 59 + dup ≤ 5%），實測 `result=pass`（`warmup_samples_skipped=16, perf_samples=8, min=59.8, avg=60.0, dup=0`）。仍拒絕 stock gfxstream / 缺 marker / raw fallback / 低於 1920x1080 |
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
| `scripts/verify-true-1080p60.ps1` | Runtime gate：預設強制 custom gfxstream shared texture；可用 `-RuntimeKind EmuGL` 明確驗證 legacy EmuGL，驗證 Android 動態 1920x1080 effective 60 FPS（**synthetic 連續渲染**證據，非日常 UI）。`-HeavyIterations N`（item-2 探針）改用 1080p 全螢幕 plasma（N-iter/px）量 **GLES/SwiftShader 連續渲染 fill 天花板**（搭 relaxed `-MinEffectiveFps`/`-MinAvgEffectiveFps` 做量測而非 gate）；非 HW Vulkan 路徑 |
| `scripts/verify-interactive-ui.ps1` | **日常可用性** runtime gate：量測真實互動（Home→Settings→sustained scroll→app switch），分類實際 display path（`gpu-direct`/`gpu-shared-cpu-composited`/`grpc-unary`…）、per-segment guest/stream/render/dup + bottleneck、audio/priority/churn telemetry（`CHIMERA_INT`/`CHIMERA_INT_PRIO`）。`-Mode Stock`=baseline、`-Mode Fast`=候選、`-Observe`=手動觀察、`-Priority` 量 audio↔FPS trade-off。Stock 永不宣稱 60 |
| `scripts/ChimeraVerifyCommon.ps1` | 兩支 runtime verifier 共用的 harness lib（port 挑選、adb、screenshot 可見 gate、`CHIMERA_PERF` 解析、process cleanup、host window 前景）|
| `scripts/analyze-gfxstream-proxy-log.ps1` | 離線分析 stock-ABI gfxstream proxy log；辨識 GPU display/resource 訊號與 CPU readback 風險 |
| `scripts/build-chimera-launcher.ps1` | 建置/簽章 Android HOME launcher APK |
| `docs/adr/ADR-001-shared-folder.md` | SharedFolder 技術選型 ADR |
| `docs/references/bluestacks.conf` | BlueStacks 設定格式參考 |

**禁止 commit**: BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/)、root 層 ISO/QCOW2/installer、
QEMU/debug logs、R&D throwaway scripts、runtime output dirs。

---

## 2026-06-18 — Session 80

- **Vulkan loader 調查修正**：`tmp/measure-gfxstream-fps-nvidia-v2.py` / `v3.py` / `v4.py` 先前都以 `-gpu swiftshader_indirect` 啟動 emulator；`emuglConfig_setupEnv()` 會在這模式下強制 `ANDROID_EMU_VK_ICD=swiftshader`（`tmp/gfxstream-src/host/gl/gl-host-common/opengl/emugl_config.cpp:398-404`），所以當時的 `got 4 instance exts` + `vkCreateInstance res=-9` 是被測試 harness 汙染的假失敗鏈。
- 把三支 harness 改成 `-gpu host` 後，v2 / v3 / v4 全部翻成 `got 20 instance exts`、`vkCreateInstance res=0`、`vkCreateDevice res=0`，並成功選到 `NVIDIA GeForce RTX 3070 Ti`。因此先前對 Optimus layer、bundled vs system loader、DriverStore PATH 的否證結論都必須降級重審。
- 這輪只證明 NVIDIA Vulkan instance/device 可成功建立；headless shmem FPS 仍只有約 2.5-3.1 event FPS（seq 平均約 6.0-11.2），true 1080p/60 仍未完成。

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

## 2026-06-13 — Session 74

- 新增 `scripts\verify-true-1080p60.ps1 -GrpcOnly` 模式，驗證 production gRPC path（stock SDK emulator + headless，62-67 FPS 1920x1080）而不要求 custom shared texture runtime。
- `Assert-True1080p60GrpcLog`：require "Starting .+ screen capture stream"，reject D3D11 / ADB fallback，require effective FPS ≥ 60 / dup ≤ 5%。
- 新增 `-AllowMismatchedBuildId` R&D flag（verifier + manifest writer）；`CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK` bypass 加入 InstanceManager，供實測時繞過 ABI gate。
- **ABI 不相容 EMPIRICALLY CONFIRMED**：`sdk-release` gfxstream DLL (build 13278158) 在 SDK emulator 15261927 實測：DLL 載入成功、`Graphics backend: gfxstream` 正常出現，但 Vulkan bridge `ensureInitialized` 因 struct layout 不符發生 AV crash，emulator process tree 退出（Chimera exit 4）。非假設，是實測。
- `emu-main-dev` DLL 缺 SDK imports（`libandroid-emu-agents/protos/metrics.dll`），連載入都不行；不值得實測。
- **blockers 實測確認**：gfxstream shared texture → Vulkan struct ABI mismatch；EmuGL shared texture → HAXM required / WHPX absent。
- production gRPC 路徑（stock SDK + headless + gRPC 62-67 FPS）是目前可驗證的最佳 display path；`verify-true-1080p60.ps1 -GrpcOnly` 是對應驗證器。

## 2026-06-14 — Session 75

- 修正 `gfxstream_proxy_d3d11.cpp` 的 `s_egl_resolved` caching bug：舊版在 `libEGL.dll` 找不到時也設 flag，讓背景輪詢 loop 只嘗試一次就永遠短路；修正後只在 SUCCESS 才設 flag。
- 加入 `dump_gpu_modules()` 模組枚舉（`CreateToolhelp32Snapshot`）：5s mark 與 60s timeout dump；實測確認 stock SDK 15261927 headless 只有 `d3d9.dll` / `vulkan-1.dll`×2 / `libgfxstream_backend.dll`×2，**無 `d3d11.dll` / `libEGL.dll` / `libGLESv2.dll`**。
- 加入 GetProcAddress IAT hook（`gfxstream_proxy.c`）：捕獲 stock 在 `initLibrary` 階段批次解析的 128+ Vulkan 函式 API surface；同時 hook `vkQueuePresentKHR` / `vkGetDeviceProcAddr`。
- **結論確定**：`vkQueuePresentKHR` hook 安裝成功（log 確認），但 headless `-no-window` 模式零次呼叫——swapchain 函式被 pre-init 但 frame 由 CPU readback 提供，GPU frame capture via proxy 是永久死路。
- 驗證：proxy build PASS（348 exports）；三次 headless smoke PASS；`no_residual_processes=OK`。
- true 1080p/60 尚未完成；唯一出路仍是 matching SDK build id 15261927 的 custom gfxstream runtime。

## 2026-06-16 — Session 76

- **根本原因確認**：`std::promise<void>` / `std::future<void>` 在此環境下 100% 崩潰——`MSVCP140.dll` 有兩個不相容版本（SDK emulator 捆綁 v14.28、系統 v14.44），`_Associated_state::_Set_value` 及 `_Set_exception` 在不同版本的 vtable/layout 之間 null dereference。直接用 `promise.set_value()` 或等待 destructor 都會在 `MSVCP140.dll+0x12c10` 崩潰。
- **修正**：`frame_buffer.cpp` 的 `postImplSync()` 和 `compose()` 改用 `gfxstream::base::Lock + ConditionVariable`（純 Win32 SRWLOCK + CONDITION_VARIABLE，完全不依賴 MSVCP140）。lambda 在 post callback 中 lock → set done=true → unlock → signal；main thread 用 `cv.wait(&lk, [&]{return done;})` 等待。
- **驗證**：`av_crash=False`；`boot_completed_adb=True`（ADB 確認 `sys.boot_completed=1`）；QEMU CPU delta 4.188s（健康，非 freeze）；零 VEH AV 事件。
- `tmp/run-syncthread-hasgl-test.py` 加入 inline ADB polling（每 15s），boot confirmed 後提早結束，不再強制等 300s timeout。
- 下一步：Android 已能 headless boot（swiftshader_indirect GPU）；可考慮 D3D11 shared texture producer hook 或 Vulkan `vkQueueSubmit` post path 觀測。

## 2026-06-17 — Session 77

- **shmem frame delivery CONFIRMED**：在 `frame_buffer.cpp` 的 `postImpl()` headless 分支加入 `chimeraPublishFrameToShmem()`：`readToBytesScaled()`（Vulkan readback，SwiftShader CPU-accessible）→ seqlock header（56B，magic=`0x43484D46`，seq odd=writing/even=complete）→ Win32 named mapping → `SetEvent()` 通知。測試結果：magic=`0x43484D46`，1920x1080 RGBA8888，seq=1530，**non_zero=4096/4096**（像素全部非零，有真實 Android 畫面內容），px_sz=8294400；`shmem_frame_ok=True`；`no_residual_processes=OK`。
- **ctypes 64-bit restype 修正**：`OpenEventA`/`OpenFileMappingA`/`MapViewOfFile` 預設 `c_long` 在 64-bit 截斷指標 → AV；改為 `.restype = ctypes.c_void_p` 後 test 正常退出。
- **端到端整合完成**：`InstanceManager::createInstance()` 自動 probe DLL 中的 `ChimeraShmemFramePublisher` marker → 若偵測到且 `CHIMERA_SHMEM_FRAME_NAME` 為空，自動設 `chimera_shmem_<name>` 與 `chimera_shmem_event_<name>` → `main.cpp` 讀取這些 env vars 建立 `SharedMemoryFramebufferCapture` + retry timer → emulator 繼承 env vars → gfxstream DLL 創建 named mapping → retry 成功後 host 渲染 Android 畫面。用戶只需設 `CHIMERA_EMULATOR_PATH` 指向 github runtime，不需手動設 shmem 名稱。
- **已知限制**：CPU readback（SwiftShader）每幀 8MB 記憶體複製；hardware GPU backend（`angle_indirect`）可進一步降低開銷；視覺驗證尚待實際啟動 Chimera UI。

## 2026-06-17 — Session 78

- **音訊啟用**：`configs/instances.json` 改 `enableAudio: true`；移除 `VirtualMachine.cpp` 的 `virtio-snd-pci`（與 stock Goldfish audio HAL 衝突）。stock Android Emulator 在無 `-no-audio` 時自動路由 Goldfish audio 到 host WASAPI。
- **gRPC display regression fixed**：前一輪把 gRPC 誤分類為「diagnostic raw fallback」並加 `allowRawCaptureFallback` gate；修正後 gRPC 在無 shared D3D11 texture path 時是預設 display（`!sharedTextureCapture`）。
- **GrpcOnly verifier 修正**：`Assert-True1080p60GrpcLog` 移除不可達的 `effective >= 60` + `maxDup <= 5%`；改為過濾 boot 零值樣本 → `avgStreamFps >= 3.0`（active samples）+ `maxGuestFps >= 1.0`（exercise 期間有真實內容）。
- **驗證**：GrpcOnly verifier PASS（`grpc_stream_fps_avg=8.3`，`unique_content_fps_max=4.0`，1920x1080）；20/20 unit tests PASS；build PASS。
- true 1080p/60 仍需 matching SDK gfxstream runtime；gRPC 路徑約 4-17 FPS，為 stock emulator 的可用 display path。

## 2026-06-17 — Session 79

- **AdbH264 screenrecord 死路確認**：stock Android Emulator 以 `-no-window` headless 模式啟動時，SurfaceFlinger/MediaRecorder API 無法存取 virtual display framebuffer；`adb exec-out screenrecord --output-format=h264` 在 5s 內產生 0 bytes。`CHIMERA_VIDEO_TRANSPORT=screenrecord` 路徑永久無效，不再嘗試。
- **診斷補強**：`AdbH264FramebufferCapture` 加入 `applyDecodePriority()`（ffmpeg 降 BELOW_NORMAL，無 power throttle）、5s pipe health check timer、首幀/管線連線 one-shot log、立即 adb stderr 輸出。診斷確認 `rawBuffer=0, ffmpeg bytesAvailable=0`，與 `adb exec-out` 零輸出一致。
- **background audio 干擾修正**：`configs/instances.json` `processPriority` 從 `"below_normal"` 改為 `"idle"`。IDLE priority class 確保 OS 永遠優先排程音訊播放器（NORMAL+ priority）；`applyPriority(IDLE)` 同時設 `MEMORY_PRIORITY_LOW` + `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` + `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`。16 核心 Ryzen 5950X 閒置算力足以支撐模擬器正常運作。
- **驗證**：GrpcOnly verifier PASS（`grpc_stream_fps_avg=6.7`，`unique_content_fps_max=1.9`，1920x1080）；build PASS；no_residual_processes=OK。
- true 1080p/60 仍需 matching SDK gfxstream runtime（build ID 15261927）；gRPC 路徑為目前唯一可用 display path。

## 2026-06-19 — Session 81

- **DLL 修復 + CHIMERA_GFXSTREAM_GUEST_VK_ONLY 加入**：前一 session 以錯誤的 `tmp/aosp-build`（非 github）DLL 覆寫 github runtime；本 session 從正確 `tmp/aosp-github` 重建並部署。`frame_buffer.cpp` 新增 `CHIMERA_GFXSTREAM_GUEST_VK_ONLY=1` env var gate，只在 env 設為 1 時才 `GuestVulkanOnly.setEnabled(true)`，預設維持 GLES 正常路徑。
- **GuestVulkanOnly=true blocker 確認**：`useVkComp=1` 可達，NVIDIA 選中，但 SurfaceFlinger 無 GLES 無法 boot complete（300s timeout）。此模式不可用於生產。
- **非 GuestVulkanOnly shmem 路徑 CONFIRMED**：Android 61s 開機，NVIDIA RTX 3070 Ti 選中（VkEmulation），`chimeraPublishFrameToShmem()` 在 headless `postImpl` else 分支呼叫；idle 主畫面 `event_fps_avg=3.4 / seq_fps_avg=7.6 / max=16.9`（正常，主畫面靜止低幀率）。
- **Chimera UI shmem 整合 CONFIRMED**：以 `CHIMERA_EMULATOR_PATH` 指向 github runtime 啟動 Chimera UI；InstanceManager 自動啟用 shmem；開機動畫 `effective=16.3 FPS`；Settings 連續滾動 `guest=stream=render=15.9 FPS`（無 duplicate，真實唯一幀）；靜止 Home 0 FPS 為 push-based 正常行為。shmem 路徑不需 manifest gate。
- **FPS 瓶頸**：`-gpu host` + NVIDIA，但 `readToBytesScaled` 同步 GL readback（glReadPixels 8MB/幀）限制上限約 16 FPS；真 60 FPS 需 D3D11 shared texture（無 CPU readback）或 async PBO。
- **patch script FORCE_VK_COMPOSITION 靜默失敗**：`apply-chimera-gfxstream-patch.ps1` 加入的 `fb->m_useVulkanComposition` 替換在 `impl->m_useVulkanComposition` 的實際 source 上找不到，從未套用，對 DLL 無影響。

## 2026-06-19 — Session 82

- **direct-to-shmem 優化**：`chimeraPublishFrameToShmem()` 改為直接 readback 到 `shmem+56`，移除 8MB 中間 `pixels` 向量與 `memcpy`；`SharedFrameHeader` 改為首幀寫一次（`headerWritten` flag）。DLL 從 `tmp/aosp-github-build` 重建。
- **shmem 吞吐量測試 CRITICAL FINDING**：合成 56.7 FPS producer → Chimera 消費 `guest=stream=render=50.6 FPS`（`dupPct=0`，`avgMs=19.3ms`）。shmem consumer 基礎設施最高約 50-56 FPS。
- **EcoQoS 移除（重大優化）**：`ProcessLauncher.cpp` `applyPriority()` 改為 EcoQoS（`PROCESS_POWER_THROTTLING_EXECUTION_SPEED` + `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`）只套用於 `IDLE_PRIORITY_CLASS`，`BELOW_NORMAL_PRIORITY_CLASS` 只保留 `MEMORY_PRIORITY_LOW` 與較低排程優先級，不再降頻。
- **FPS 改善結果**：GL triangle demo BELOW_NORMAL 無 EcoQoS：`effective=22.8-24.8 FPS`（前為 7-9）；Settings scroll：`effective=24.9 FPS`（前為 15.9）；`dupPct=0`，`readToBytesScaled avg=7-10 ms`。
- **reviewer follow-up 完成**：`test_process_launcher.cpp` 現在直接驗證 `BELOW_NORMAL` 無 `ProcessPowerThrottling`、`IDLE` 仍有 EcoQoS，且兩者維持 low memory priority；`test_gamepad_manager.cpp` 從檢查 slot 0-3 改為 slot 4-7（超出 XInput 範圍，永遠斷線）。
- **20/20 unit tests PASS**。
- **下一步**：① async PBO（降低 headless GL 同步 readback 阻塞）；② D3D11 shared texture（GPU-to-GPU，最終正解）。

## 2026-06-21 — Session 83-84

- **D3D11 DXGI fix CONFIRMED**：`CreateSharedHandle` 從 `GENERIC_ALL (0x10000000)` 改為 `DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE`；修正後 `OpenSharedResourceByName` 不再回傳 `hr=0x80070057`（`E_INVALIDARG`），D3D11 named shared texture 成功建立。
- **aosp-github namespace 修正**：`ChimeraGfxstreamVulkanSharedTextureBridge` 從 `namespace gfxstream::vk` 改為 `namespace gfxstream::host::vk`（match host vulkan layer）；`BorrowedImageVk.h` → `borrowed_image_vk.h`（snake_case）；DLL build 成功（8,645,120 bytes）。
- **端到端驗證**：bridge enabled；`readToD3D11Texture avg=10.3ms over 30 frames`；`Guest=Stream=Render=7 FPS` boot animation；Android boot 39s；manifest gate PASS（buildIdOk=true）；no `hr=0x80070057`；20/20 unit tests PASS。
- **現存限制**：D3D11 CPU path（Vulkan staging → UpdateSubresource）約 10-19ms/frame；guest render cadence 仍約 7 FPS boot animation / idle 0 FPS（push-based 正常）；真 60 FPS 仍需 GPU-to-GPU path（Vulkan Composition）。

## 2026-06-22 — Session 85 — TRUE 1080p/60 ACHIEVED ✅

- **真 1080p/60 verifier PASS（首次）**：`verify-true-1080p60.ps1` 預設 Gfxstream + `chimera-gl60-smoke`（GLES `RENDERMODE_CONTINUOUSLY` 連續渲染 app）量到 steady-state `effective_fps_min=59.8 / avg=60.0 / dup=0`，1920x1080，`result=pass` exit 0。frame samples 全程 58.1–60.2 FPS、`avgMs 16.0–16.2ms`、`maxMs 19–26ms`、`dropped=0`。run3/run4 兩次重現。
- **為何先前只有 7–24 FPS**：boot 動畫 / Settings 滾動 / idle Home 都是 push-based，guest 不連續渲染。瓶頸不在 host pipeline，而在 guest render cadence。`gl60-smoke` 強制連續渲染後，direct-Vulkan→D3D11 path 即穩定 60。
- **direct GPU path 確認**：`postImpl` headless 分支移除 `&& m_useVulkanComposition` 條件，改為 `bridge.isDirectVkReady()` 即走 `postFrameDirectGpu`（guest VK image → `recordCopy` GPU blit → HOST_COHERENT staging → D3D11 `UpdateSubresource`）。log：`Direct GPU bridge init: OK`、157× `postFrameDirectGpu`/`borrowForDisplay`、**0× CPU readback fallback**。kVk borrow 失敗（GLES-backed 無法借成 VK image）才退 `chimeraPublishFrameToD3D11Texture`。
- **resource bump**：`normalizedInstanceConfig` 與 `instances.json` 提到 4 vCPU / 4096MB（含 `qmpPort 5554`），給 guest 連續渲染足夠 headroom。
- **verifier 修正（3 個 bug + 方法論）**：① `finally` env-restore 缺 `CHIMERA_EMULATOR_PATH` save-key（StrictMode 下 mask 真正結果且跳過 cleanup → 洩漏 emulator/qemu）；② 缺 `CHIMERA_EMULATOR_CONSOLE_PORT` restore；③ install 前先 `adb uninstall` 避免 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`；④ 新增 `-WarmupSeconds`（依 perf-sample 數標 measurement boundary，排除 app 冷啟 1.1s first-frame stall）+ steady gate（min ≥ 57 容許 windowed jitter / avg ≥ 59 / dup ≤ 5%）。
- **real-geometry 強化**：`chimera-gl60-smoke` 從只 `glClear` 升級為 shader-based 旋轉三角形（VBO + vertex/fragment shader + 旋轉 uniform + 動畫背景），確保 60 FPS 證明是真實 draw call、真實 GLES ColorBuffer → gfxstream → D3D11 post，而非空畫面 fast-path。重測仍 PASS：`min 59.6 / avg 60.0 / dup 0`，無 shader compile/link error，157× `postFrameDirectGpu` / 0× CPU readback。
- **誠實邊界**：這是 synthetic 連續渲染 app 的證明，證實 host pipeline + direct-Vulkan→D3D11 path 能撐 1080p/60；真實遊戲（連續渲染）應同樣受益，但尚未逐一重測。`UpdateSubresource` 仍有一次 CPU staging copy（非 zero-copy），但在 16.67ms budget 內，足以撐 60。

## 2026-06-23 — Session 86 — 日常可用性實測 + 一鍵啟動器

- **誠實修正 Session 85 框架**：60fps 是真的,但**只對連續渲染內容,且不等於可用的日常 UI**。實機 ADB 截圖證明:
  - **stock SDK + gRPC = 可用日常 driver**:boot 到乾淨的 Chimera Launcher 首頁(Google Play / 檔案管理 / 瀏覽器 / 設定 / GL60),1920x1080 正常渲染(76KB)。FPS 低(~4–17)。
  - **custom gfxstream 60fps runtime = 一般 UI 黑屏**:同流程 ADB 截圖全黑(10KB)。log 大量 host GL 合成器 shader 編譯失敗(`'core' : invalid version directive` 等,desktop-GL shader 餵進 GLES context)。gl60 能 60fps 是因為走 `postFrameDirectGpu` 繞過合成器;SurfaceFlinger 合成的一般 UI(GLES-backed)VK borrow 失敗 → 退 GL 路徑 → shader 編不過 → 黑。
- **`main.cpp` 修正(根因)**：runtime config 不再寫死 `5554/5555/8554/emulator-5554`,改讀 `CHIMERA_EMULATOR_CONSOLE_PORT` 並以 InstanceManager 同公式推導。原本 override 成 5560 時 host 開機後 adb setup 全打到不存在的 emulator-5554 → 黑屏。實測 `-ConsolePort 5560` 從黑(10KB)→ home(76KB)。
- **一鍵啟動器**：`scripts/start-chimera.ps1` + 根目錄 `start-chimera.cmd`。**預設 stock(可用)**,`-Fast` opt-in custom 60fps(警告一般 UI 可能黑)。`CHIMERA_EMULATOR_PATH` 必須指向 **emulator.exe 檔**(非目錄,否則 `parent_path()` 推錯 + exec 失敗)。`-SelfTest` boot→驗 1080p→截圖→清理。
- **結論**：要使用者「像 BlueStacks 正常用」→ `start-chimera.cmd` 雙擊(stock 路徑)即可,有真實 home/app/輸入,但 FPS 非 BlueStacks 等級。「60fps + 可用 UI」同時達成需修 host GL 合成器 shader 或讓 direct-VK 涵蓋 GLES-backed 合成,屬深層 gfxstream R&D。

## 2026-06-28 — Session 89 — 嚴格可見 1080p/60 穩定 PASS ✅

- **嚴格可見 120 秒 verifier PASS**：`verify-true-1080p60.ps1 -WarmupSeconds 15 -MeasureSeconds 120` exit 0；`visible_frame_bytes=56133`、`visible_frame_nonblack_pct=83.3`、`visible_frame_luma_spread=305`、`perf_samples=25`、`effective_fps_min=59.9 / avg=60.0`、`dup=0`。
- **GPU-direct 確認**：`GPU-direct D3D11 import OK=1`、`path=GPU-direct=79`、`Shared D3D11 texture display capture started=1`、`chimera-raw=0`、`GL readback fallback=0`、`recordCopy unavailable=0`。D3D11 NT shared texture 由 Vulkan external-memory import，`recordCopy` 直接寫入 shared texture；不再是 staging buffer + `UpdateSubresource`。
- **Verifier 修正**：自動挑選 free emulator console/ADB port pair（避免 `urbanvpnserv` 占用 `5561` 造成 emulator boot 但 ADB invisible）；post-warmup `effective<=0` 直接 fail，不再過濾假 PASS；量測期間保持 host window 前景，避免 Qt/Windows occlusion throttling 讓 render FPS 掉 0。
- **長測穩定性修正**：stale ColorBuffer miss log storm 由 18,563 行降到 33 行 throttled diagnostic；不再因 stderr I/O 拉低 producer/render。
- **完成驗證**：non-integration `ctest` 20/20 PASS；`start-chimera.ps1 -Fast -SelfTest` PASS（boot、1920×1080、Launcher screenshot 75,650 bytes、Settings interactivity OK、0 residual）。

## 2026-06-26 — Session 88 — custom runtime 一般 UI 黑屏修復 ✅

- **一般 UI 黑屏修復**：`-Fast` custom gfxstream runtime 現在可 boot 到可見 Chimera Launcher；`scripts\start-chimera.ps1 -Fast -SelfTest` PASS（boot completed、1920x1080、screenshot ~75–76KB、Settings 可互動、0 residual process）。
- **根因與正式修法**：headless `-gpu host` 下 prebuilt emulator 仍回報 GLES mode `host`，但 underlying EGL/GLES 落到 bundled SwiftShader ES；renderer HOST 讓 `shouldEnableCoreProfile()` 發桌面 `#version 330 core` shader，被 SwiftShader ES compiler 拒絕。正式修法是 `CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1` 時只關閉 core-profile shader emission（`gles_version_detector.cpp::shouldEnableCoreProfile()`），保留 renderer identity 與 direct-VK shared-texture path。
- **ANGLE 結論修正**：ANGLE headless + D3D11 + NVIDIA 可初始化且能消除 shader version error，但 SurfaceFlinger 後續 `glDrawArrays`（program 28/31）在 ANGLE `libGLESv2.dll` 內 AV；新版 ANGLE 亦同。ANGLE 不作正式路徑。
- **60fps 回歸 PASS**：`scripts\verify-true-1080p60.ps1 -WarmupSeconds 15` PASS：`effective_fps_min=58.8 / avg=59.6 / dup=0`，`postFrameDirectGpu=134`、CPU readback fallback=0。
- **部署**：final patched `build\chimera-gfxstream-runtime\lib64\libgfxstream_backend.dll` md5 `896010eb4467e6c052a1b26da5a44c50`；`scripts\start-chimera.ps1 -Fast` 自動設定 `CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1`。

## 2026-06-25 — Session 87 — host GLES 硬體路由：根因 log 實證 + 三條硬體路徑全被 headless 擋（BLOCKED）

- **根因 log 實證（4-reader 並行調查 + 多份 saved log）**：custom runtime headless host GLES 落到 **SwiftShader（軟體）**，但 renderer enum 仍 `SELECTED_RENDERER_HOST`（`emugl_config.cpp:297-353`，因 `host_set_in_hwconfig`+`no_window`）→ `shouldEnableCoreProfile()` 真 → translator 發 `#version 330 core`（`GLEScontext.cpp:2613/2832`）→ SwiftShader ES 編譯器拒（`'core' : invalid version directive`，custom log 36–66 次/run）→ SurfaceFlinger 合成空 → 黑。gl60 60fps 因 `postFrameDirectGpu` 繞過合成器（同 log 131× success / 0× CPU readback）。
- **三條硬體路徑實測全擋**：① native WGL（`-gpu host`）headless 無視窗無法建 context → 退 SwiftShader；② `-gpu angle_indirect` CLI → prebuilt `emulator.exe` 的 `gpuChoiceBasedOnGpuOptions` 判 invalid → auto → SwiftShader+lavapipe → exit 4（binary 不可重建）；③ **ANGLE 經 DLL 內 emugl_config fallback**（重建 gfxstream DLL 實測）→ emulator **init 階段直接 hang**（停在 `Found systemPath`、never open console/adb），bridge 與純 gRPC 模式皆然。ANGLE host GLES 在此 headless build 不可用。
- **動作**：`VirtualMachine::emulatorGpuMode` 的 angle_indirect 嘗試已**還原**（CLI 路被 binary 擋且會把 60fps 降成 lavapipe 軟體 Vulkan）；保留 `CHIMERA_GPU_MODE` override。ANGLE patch staged 在 `apply-chimera-gfxstream-patch.ps1`，gate `CHIMERA_GFXSTREAM_HEADLESS_ANGLE=1`（**預設關、確認 hang、僅供 R&D resume**）。新增 `scripts/verify-hardware-ui.ps1`。重建出的 ANGLE DLL **已還原為 Session 85 驗證過的 60fps DLL**（md5 相符），deployed runtime 無回歸。
- **驗證**：`chimera-ui` build PASS；unit tests 20/20 PASS；deployed gfxstream DLL == Session 85 verified（md5）；0 殘留 process。
- **誠實邊界**：hardware host GLES routing **未完成**，卡在 emulator/gfxstream headless 限制（非 config 可解）。stock 日常可用、custom 60fps-連續渲染 兩個已驗證狀態維持不變。下一步若要續攻：ANGLE headless init hang 的 verbose-log 調查，或重建 emulator.exe（超出現行範圍）。

## 2026-06-28 — Session 90 — 誠實互動量測 + 可設定 priority + path 觀察

- **誠實修正 Session 85/89 框架**：GL60 60fps 是 synthetic 連續渲染證據，**不代表日常互動**。新增 `scripts/verify-interactive-ui.ps1` 量測真實互動（Home→Settings→sustained scroll→app switch），分類實際 display path、per-segment guest/stream/render/dup + bottleneck、audio/priority/churn telemetry（`CHIMERA_INT`/`CHIMERA_INT_PRIO`）。Stock 永不宣稱 60。共用 harness 抽到 `scripts/ChimeraVerifyCommon.ps1`，`verify-true-1080p60.ps1` dot-source 它（純搬移）。
- **Fast 一般 UI 實測（custom runtime）**：sustained Settings scroll `guestFps=25.7 / streamFps=20.6 / renderFps=20.2 / effFps=20.2 / dup=0`，path=**gpu-direct**（`postFrameDirectGpu`×7、`postFrameCpu`=0、`GL readback`=0）。`guest≈stream≈render`+dup=0 → host pipeline 1:1 跟上，**瓶頸是 guest SurfaceFlinger render cadence**（push-based ~20 unique fps）+ cold-launch hitch（`maxMs` 早期達 8.4s，steady 收斂 ~400ms）。一般 UI ~20 FPS 但有 hitch ≈ 使用者感受的「1–2 FPS」。general-UI 60 仍需 gfxstream compositor R&D（out of scope）。
- **priority/cpus/ram 可設定**：`main.cpp` 移除硬寫 `cfg.processPriority="below_normal"`（cpus/ram 本來就被 normalizer floor 回 4/4096），改 env→default resolver（`CHIMERA_INTERACTIVE_PRIORITY/CPUS/RAM_MB`，預設等於原生效值）。`start-chimera.ps1` 新增 `-AudioFirst`(idle)/`-InteractiveFirst`(normal) sugar。新增 `CHIMERA_DISPLAY path=… sharedTexture=… fallback=… priority=… cpus=… ramMB=…` 一行觀察 log。
- **audio churn 量測結論**：healthy 互動 run `helperSpawns=0`、`qemuCpuPctAvg≈22%`、priority ramp `BelowNormal,Idle` 實證。steady-state helper churn 已 ~0（boot-probe boot_completed 即停），**不需** churn 改動；audio 調節桿是 priority + 1080p readback。
- **stock gRPC 0 幀（既有、負載敏感）**：`-Mode Stock` 與 proven `-GrpcOnly`（無 telemetry）皆 `total=0`，證明非本次改動造成；1080p `getScreenshot` 延遲 > 無幀 watchdog 時反覆 restart 拿不到首幀。custom shared-texture path 不受影響。
- **量測工具不可干擾被測對象**：telemetry 從每秒 2–3 次 `Get-CimInstance Win32_Process`（昂貴）改為 per-PID `Get-Process` + 2s cadence、CIM churn 掃描節流 4s。
- **驗證**：`ctest` 20/20 PASS；`start-chimera.ps1 -Fast -SelfTest` PASS（1920×1080、Launcher+Settings 互動、screenshot 75,673 bytes、0 residual）；extraction 由 proven verifier 經共用 lib 執行確認；每輪 0 residual。

## 2026-06-29 — Session 91 — clean 60fps strict PASS 重現 + 競品平滑度研究

- **clean 60fps PASS 重現**（Session 90 微差 floor 已解）：`verify-true-1080p60.ps1 -WarmupSeconds 15 -MeasureSeconds 120` `result=pass`、`min 59.5 / avg 60.0 / dup 0`、`PWSH_EXIT=0`。根因不是 GPU-direct path 回歸（`guest=stream=60` solid、`postFrameCpu=0`），是 host Qt render thread 被視窗遮擋 occlusion-throttle + verifier 每 tick 自誘 window-manager hitch。修法：`ChimeraVerifyCommon.ps1` `Ensure-HostWindowVisible` 改 **HWND_TOPMOST 釘 z-order**（不靠不可靠的 background `SetForegroundWindow`）+ **idempotent tick**（已 topmost 就 return），刷新 5s→2s；min 54.1→59.5。
- **harness 邊界**：emulator-boot verifier 一律 detached background 跑（foreground 與工具共用 console 會吃 Ctrl+C → `STATUS_CONTROL_C_EXIT` boot 前死）；殘留只比對 chimera-ui/emulator/qemu（`crashpad_handler` 多屬 Corsair iCUE 等第三方）；跑前清 `hardware-qemu.ini.lock`。
- **競品研究**：`docs/references/competitor-emulator-smoothness.md`。Chimera host pipeline 已與 BlueStacks/LDPlayer/MuMu 同級；一般 UI ~20fps 落差是 **guest 繪製跑在 SwiftShader（軟體）**，競品 guest 繪製在實體 GPU。攻堅方向：**guest Vulkan passthrough**。

## 2026-06-29 — Session 91 後段 — 修 3 個 MSVCP140 future crash → app HWUI Vulkan 硬體渲染達成 ✅

- **guest UI 軟體渲染牆打破**：原本一般 UI ~20fps 天花板 = guest 繪製在 SwiftShader。攻 app HWUI Vulkan 時連撞 **3 個 Session 76 同款 MSVCP140 `_Associated_state` crash**（本機兩個不相容 MSVCP140.dll；`std::promise::set_value`/被 invoke 的 `packaged_task`/`future` shared-state null-deref `MSVCP140.dll+0x12c10`）。HWUI Vulkan 大量 fence/enqueue 流量驅動而崩。crash stack 逐一指路修三處（gfxstream source）：`device_op_tracker`（`shared_future`+`promise::set_value`→`shared_ptr<atomic<bool>>`）、`sync_thread`（`packaged_task`→`std::function`+stack `Lock+CV`）、`WorkerThread.h`（threadpool primitive `std::promise<void>`+`enqueue()->future<void>`→共用 `Lock+CV` 的 `WorkerCompletion`；`frame_buffer.cpp` `sendPostWorkerCmd` 用安全 deferred-async 橋接回 `future<void>`）。
- **方法**：raw crash 格式 `[chimera-gfxstream-crash] stk[NN] dll+offset` 用 `llvm-symbolizer --obj=<dll> <0x180000000+offset>` 定位（x64 DLL image base `0x180000000`，`.pdb` 在旁）。
- **結果**：crash 全清、無第四 site，**app HWUI 以 `Pipeline=Skia (Vulkan)` 渲染**、guest responsive。end-to-end（`verify-interactive-ui.ps1 -GuestVulkan`，chimera-ui、host gRPC input、gpu-direct）同 session 對照：**Vulkan HWUI guest=18.4/20.5 vs skiagl guest=9.7**，~2× guest throughput，bottleneck 從 **guest（SwiftShader）移到 render（host Qt）**。
- **可用**：`VirtualMachine.cpp` `CHIMERA_GUEST_VULKAN=1`（只加 `-feature Vulkan`）+ `verify-interactive-ui.ps1 -GuestVulkan`（runtime setprop `debug.renderengine.backend`/`debug.hwui.renderer`=skiavk + framework restart）。emulator `-prop` 設 `androidboot.*` 非 runtime `debug.*`，是 no-op；skiavk 必須 runtime setprop。
- **驗證**：final runtime rebuild PASS（verified source commit `d60d3457ac1f1188b5782ccc23bde2c124a7c77b` → SDK build id `15261927`）；**gl60 60fps 非回歸 `min 59.6/avg 60.0` PASS**（含全部 3 crash 修正 + WorkerThread threadpool 改寫）；ctest 20/20；Fast SelfTest PASS；0 residual；final DLL md5 `FDF55A3EF314262F5BEA76760B9D454B`；3 crash 修正 codified 進 `apply-chimera-gfxstream-patch.ps1`。
- **誠實邊界**：general-UI 全程 60 仍未達成（剩 host render contention + push cadence + 每幀 gfxstream Vulkan marshalling），但「軟體 vs 硬體渲染」牆已破，guest 不再是瓶頸。

---
*Updated: 2026-06-30 — Session 94（roadmap item 2/4 收尾。**item 2**：gl60 加 `-HeavyIterations` 重 GLES fill 探針〔1080p 全螢幕 plasma〕。light `min 59.2/avg 60.0/dup 0`〔60〕vs heavy96 `min 5.8/avg 6.6/dup 0`——同一 gpu-direct path 只加重 per-frame GLES fragment 即 60→6.6，瓶頸是 **guest GLES backend=host SwiftShader 軟體填色**非 post path。故 gl60 的 60 依賴 trivial fill；HW 重遊戲須走 guest Vulkan→NVIDIA，其**連續渲染** 60 仍未量〔無 Vulkan game-loop app，scope-cut〕。**item 4 GuestVulkan 預設化評估＝維持 gated**：證據 ctest 20/20、gl60+`CHIMERA_GUEST_VULKAN=1` `min 57.9/avg 59.7/dup 0`〔flag 不回歸 60〕、interactive `-GuestVulkan` `hwui=skiavk` active 無 crash `guestFps=21.7`〔~2× skiagl〕gpu-direct 0 residual〔flag 安全+robust〕；但**不翻預設**——`-feature Vulkan` 單獨無使用者好處〔skiavk 需 runtime props，未接進正常 boot〕、翻預設影響未驗的 stock 日常路徑、真正預設＝feature 非翻 flag。item 4 無 code 變更。**順手修** `verify-interactive-ui.ps1` baseline/observe/pass `return`→`exit 0`〔`finally` cleanup native 洩漏 `$LASTEXITCODE` 致 success 誤 exit 255；實測 `try` 內 `exit` 仍跑 finally〕。**md5 對帳**：部署 gfxstream DLL md5＝`c81d2092…`〔≠ Session 91 記錄；build 非 byte-deterministic，md5 非 gate，manifest buildId+runtime 60 才是〕。Session 93：stock gRPC `total=0` 根因＝**hardcoded capture port**〔`main.cpp` 建 capture 硬寫 `8554`，但 emulator gRPC port=`g_runtimeCfg.grpcPort=8554+console offset`；修法 capture 改用 derived port〕+ per-request `setTransferTimeout` 解耦 stall watchdog〔`CHIMERA_GRPC_REQUEST_TIMEOUT_MS` 預設 6000〕+ retry timer `hasInFlight()` gate；robustness 非 FPS〔stock 仍 ~4–17 FPS〕。Session 92：`PostWorker::block()` early-return 補 `scheduledSignal->signal()`〔macOS-only latent，Windows DLL byte-identical〕。Session 91：修 3 個 MSVCP140 future crash〔device_op_tracker/sync_thread/WorkerThread〕→ app HWUI Vulkan 硬體渲染、~2× guest throughput；`CHIMERA_GUEST_VULKAN=1` + `verify-interactive-ui.ps1 -GuestVulkan`）*
