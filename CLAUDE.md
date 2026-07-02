# Project Chimera — CLAUDE.md

> AI 工作階段快速參考。每次重大變更後更新。開發歷程與 per-session 詳細記錄一律在 `CONTEXT.md`，本檔不重複保留。

## 當前狀態（2026-07-02 / Session 101）

- **完成度**：BlueStacks Parity Roadmap v3 P0–P4e + 補強 COMPLETE；核心功能同等級（見下方功能清單）。
- **生產引擎**：`emulator.exe`（Google QEMU+WHPX fork）。`--qemu-backend` / `--hcs-backend` / `--cuttlefish` 為 legacy R&D，保留不刪。
- **Tests**：`ctest -LE integration` **23/23 PASS**；3 個 integration tests 需 emulator 運行中。
- **一鍵啟動**：根目錄 `start-chimera.cmd` = `start-chimera.ps1 -Fast -InteractiveFirst`（custom gfxstream shared texture + `CHIMERA_GUEST_VULKAN=1`＝只加 `-feature Vulkan` + normal priority）；`-Stock` 才回 stock gRPC 慢路徑（~4–17 FPS）。啟動時間 `boot≈33s`、`visible_home≈49s`（Session 100 SelfTest；Session 101 量到 87/103s 含連續 boot 噪音，待空機重測）；boot 期間 QML placeholder 由 `AndroidControls.bootReady` 撐到 boot 完成，launcher 以 md5 比對跳過重複 reinstall。
- **顯示路徑（-Fast）**：source-patched gfxstream `postFrameDirectGpu`——GLES 合成內容先 `flushFromGl()+invalidateForVk()` GL→VK 同步 → GPU `recordCopy` blit → D3D11 NT shared texture（Vulkan `D3D11_TEXTURE_BIT`+dedicated import、keyed mutex）→ host `GuestDisplay` `AcquireSync(0)==S_OK` → 私有副本取樣。互動 UI 實測有效 **~43 FPS**（真實可見內容）。
- **Session 101 重大更正**：`-Fast` shared texture 從 Session 85 起發佈的一直是**零幀**（三層 bug：compose 不標 `mGlTexDirty`→kVk image 從未被寫入；`OPAQUE_WIN32` 匯入無 aliasing；consumer 無 AcquireSync）。歷來 gate 只驗 guest ADB screencap + host counters 所以 15 session 全綠——**歷史 GPU-direct「1080p/60 PASS」（S85/89/99）量的是零幀 blit 節奏，不可引用**。三層已修；SelfTest 新增 host 視窗像素 gate（`Get-HostWindowPixelStats`，`host_window_nonblack_pct>=5`）防再犯。另修 emulator idle 自殺：`-idle-grpc-timeout 300` 已移除（shared-texture 顯示不走 gRPC，黑屏無輸入 300s 即自殺）。
- **FPS 誠實邊界**：GLES 內容（SurfaceFlinger SwiftShader-ES 合成）每幀付 GL readback+VK upload 同步成本，連續渲染 60 posts/s 超出預算（gl60 嚴格 gate 目前不通過）；真 60 需 guest **Vulkan-backed** 內容（zero-copy 直通，尚未單獨基準）。push-based idle Home 低 FPS 屬正常。Session 99 的兩個 host 修（`QSG_RENDER_LOOP=threaded`、GuestDisplay present timer 200ms）仍有效保留。任何「可見/60」宣稱必須含 host 視窗像素證據並分清 workload。
- **skiavk UI 切換禁止再試**（Session 100 定案）：playstore user image 無 root，framework restart 必失敗，半套用（HWUI Vulkan + SF SkiaGL）＝app 視窗全黑；三條替代路（root restart / boot-prop / `ctl.restart`）全 probe 實證死路。
- **Headless 邊界**：正常啟動強制 headless / `-no-window`，emulator/qemu tree 外露可見 HWND 即終止。stock HWND/window capture/native embed 是 unsafe diagnostics：需 unsafe CLI 旗標 + `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` + 同次啟動建立的內部 diagnostics session（`CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1`）。Chimera 走 fork/改 Android Emulator + gfxstream/QEMU runtime，不從零重寫 Android VM，正式路徑不多開原生 Emulator 視窗。
- **Harness 紀律**：verifier/self-test 走 `ChimeraVerifyCommon.ps1`（自動挑空 console/ADB port pair、拒 odd console port、cmdline-filtered cleanup 防誤殺非 Chimera emulator）；post-warmup `effective<=0` 直接 fail；adb-swipe 只代表測試注入路徑，真實路徑量測用 `CHIMERA_SYNTHETIC_SCROLL`；禁止會搶實體滑鼠的測法。

## BlueStacks Parity 功能清單（production emulator.exe 路徑，皆 ✅）

| 類別 | 功能 |
|------|------|
| 核心 | Android boot + WHPX；headless 顯示內嵌（無原生彈窗）；Multi-instance 批次啟停 |
| 輸入 | Keyboard/mouse/touch/gamepad(XInput, focus-gated)；Multi-touch (MT evdev Type-B)；IME；FPS 鼠標鎖定；十字準心/自訂游標；Key scheme 匯入匯出；Macro 錄製/播放 |
| 顯示 | Screen resize / DPI / rotation；FPS lock (30/60/90/120)；Performance HUD (FPS/Lat/Drop) |
| App | APK/OBB 安裝；launch/stop/uninstall/clear；釘選常用應用；Chimera Launcher HOME |
| 系統 | Root mode；Device spoofing（5 flagship profiles）；Clipboard 同步；File sharing (push/pull)；網路 Proxy；網速模擬 (GPRS→Full) |
| 模擬 | GPS (geo fix+route)；感應器 (acc/gyro/mag)；震動；電池；Shake (Ctrl+Shift+3)；Rotate (Ctrl+Shift+4) |
| 媒體 | Screen recording + screenshot；Audio (WASAPI) |
| 體驗 | Eco mode (Ctrl+Shift+F)；Boss Key (Ctrl+Shift+X)；Trim Memory (Ctrl+Shift+T)；Mute (Ctrl+Shift+M)；Open Downloads (Ctrl+Shift+6) |

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
Tests             tests/unit/            23 Qt Test executables
                  tests/integration/     emulator-boot / input-inject / screencap (QSKIP guards)
```

**Input priority chain**: 滑鼠左鍵/觸控 gRPC `sendTouch` (8554) → HvSocket → Console (5554 telnet) → QMP → ADB；
鍵盤 gRPC `sendKey` (8554) → QMP → ADB（console 無鍵盤通道）
**Display path**: headless GPU/shared texture transports（正式方向，emulator `-no-window` hidden）→ raw gRPC/MMAP/screenrecord/ADB capture（CLI-only 診斷 `--allow-raw-capture-fallback`，不可當 1080p/60 證據）→ legacy Win32 window embed / window capture（unsafe CLI opt-in only）

## 重要決策（不討論不改）

1. **MSVC only** — MSYS2 GCC 在此機器上 `cc1plus.exe` crash，不嘗試 MinGW
2. **emulator.exe 為生產引擎** — BlueStacks 同等級 (QEMU+WHPX)；`--qemu-backend/--hcs-backend` 是 R&D
3. **ANGLE 動態載入** — `libEGL.dll` + `libGLESv2.dll` via QLibrary；不需要 .lib
4. **AVD 用 `google_apis_playstore`** — Play 支援必要；注意這是 user image（無 root），任何需要 `adb shell stop/start` / root 的 guest 操作結構性不可行（skiavk 教訓）
5. **Port 5554 = Android Console telnet**，不是 JSON QMP（JSON QMP 在 `--qemu-backend` port 4445）
6. **BlueStacks input**: `HD-Bridge-Native.dll` → virtio-input，不是 kernel driver；`BstkDrv.sys` 是 network/filter driver
7. **對 emulator 的所有連線（capture/input/keyboard/console）必須用同一 derived-port 公式**，不可硬寫常數（gRPC = `8554 + console offset`）

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration   # 23/23
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-gfxstream-runtime.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25
```

## Feature Flags

| 變數 | 值 | 預設 | 說明 |
|------|----|------|------|
| `CHIMERA_INPUT_BACKEND` | `console\|adb\|qmp\|auto` | `auto` | auto = 嘗試 Console，不 Ready 則退回 ADB |
| `CHIMERA_PROCESS_LAUNCHER` | `legacy\|native\|auto` | `auto` | legacy = `_popen`；native = `CreateProcessW` |
| `CHIMERA_EMULATOR_PATH` | path | config | 覆蓋 `configs/android_sdk.json` 的 emulator（**指向 emulator.exe 檔非目錄**）；custom runtime 自動 prepend 旁邊 `lib64/`、`lib/` 到 PATH |
| `CHIMERA_GUEST_VULKAN` | `0\|1` | 空 | 設 1 時 emulator 加 `-feature Vulkan`（Vulkan app 直達實體 GPU）。**僅此而已**——skiavk UI 切換已移除且禁止再加 |
| `CHIMERA_INTERACTIVE_PRIORITY` | `idle\|below_normal\|normal` | `below_normal` | emulator process priority。`idle`=audio-first（EcoQoS 降頻）；`normal`=interactive-first（最順、最多音訊競爭）。非法值退 `below_normal`；上限被 normalizer/`safePriorityClass` 夾到 `normal` |
| `CHIMERA_INTERACTIVE_CPUS` / `CHIMERA_INTERACTIVE_RAM_MB` | 正整數 | `4` / `4096` | vCPU / RAM override；`normalizedInstanceConfig` 仍 floor `>=4 / >=4096` |
| `CHIMERA_QUICK_BOOT` / `CHIMERA_SAVE_QUICK_BOOT` | `0\|1` | 空 | 皆 opt-in：載入 / 保存 `chimera_quickboot` snapshot；預設 full boot（帶 `-no-snapstorage`）保護 host audio |
| `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` | 正整數 | `1920` / `1080` | gRPC raw capture 尺寸；低於 1080p 會被 clamp 回，不可降解析度換 FPS |
| `CHIMERA_GRPC_REQUEST_TIMEOUT_MS` | 正整數 | `6000` | 單一 `getScreenshot` transfer timeout，解耦於 stall watchdog（2s）；低於 watchdog 被拒。robustness 旋鈕非 FPS |
| `CHIMERA_GRPC_TRANSPORT` | `unary\|mmap` | `unary` | MMAP 實驗路徑（~29 FPS），不可當 1080p/60 證據 |
| `CHIMERA_VIDEO_TRANSPORT` | `screenrecord` | 空 | ADB H.264 實驗路徑；headless 下 screenrecord 0 bytes（死路），僅診斷 |
| `CHIMERA_LOG_PATH` | path | 空 | Qt message handler 同步寫 log；verifier 解析 `CHIMERA_PERF` 用 |
| `CHIMERA_SYNTHETIC_SCROLL` | `0\|1` | 空 | diagnostics-only：boot 後走 production 輸入路徑（`InputBridge::onTouchPoint→gRPC sendTouch`）連續 fling，不碰實體滑鼠；`verify-interactive-ui.ps1 -SyntheticScroll` 使用 |
| `CHIMERA_VERIFY_WINDOW_ORIGIN` | `"x,y"` | 空 | chimera-ui 啟動即 setPosition（負值=副螢幕）；驗證時避免蓋住使用者畫面 |
| `CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES` | `0\|1` | 空 | custom runtime headless 下關閉 core-profile shader emission（SwiftShader-ES 拒 `#version 330 core`）；`-Fast` 自動設 |
| `CHIMERA_SHMEM_FRAME_NAME` / `CHIMERA_SHMEM_FRAME_EVENT` | Win32 object name | 空 | CPU-copy shared-memory framebuffer backend（seqlock header）；無第一幀時 gRPC fallback |
| `CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE` / `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE` | `0\|1` | 空 | 啟用 / fail-closed 要求 modified gfxstream shared texture transport（等同 CLI `--gfxstream-shared-texture`）；stock 或缺 marker/manifest/SDK ABI/imports 的 DLL 不生效，REQUIRE 下不得回落 raw/stock HWND |
| `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE` / `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE` | `0\|1` | 空 | legacy EmuGL 版同上；strict mode 亦禁 `m_onPost`/`readback()` fallback |
| `CHIMERA_D3D11_TEXTURE_METADATA` / `EVENT`（及 `CHIMERA_EMUGL_*`、`CHIMERA_GFXSTREAM_*` 變體） | Win32 object name | auto opt-in | D3D11 named shared texture metadata；opt-in 時 host 自動同步名稱 |

**CLI 旗標**：`--native-embed` 必須另加 `--allow-unsafe-native-window` + `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`（同次啟動建立 internal diagnostics session）；`--window-capture` 同理需 `--allow-unsafe-window-capture`；殘留 env 只警告不生效。`--no-emulator` 不啟動 emulator；`--gfxstream-shared-texture` / `--emugl-shared-texture` 啟用對應 transport；`--allow-raw-capture-fallback` 才允許 raw gRPC/MMAP/screenrecord/ADB 診斷 fallback（env 版不生效）。

**gfxstream proxy probe**（legacy R&D，已定案死路）：`build-chimera-gfxstream-proxy-runtime.ps1` 建 stock-ABI hook probe；stock headless 純 Vulkan、swapchain 永不呼叫，`sharedTextureProducer=false`，只能當 ABI probe，不可當 1080p/60 證據。

## 已知問題

| 問題 | 狀態 |
|------|------|
| `-Fast` host 視窗零幀黑屏（S85 起潛伏） | RESOLVED — Session 101 三層修復（`flushFromGl+invalidateForVk` 前置同步、`D3D11_TEXTURE_BIT`+dedicated import、GuestDisplay keyed-mutex acquire+私有副本〔`WAIT_TIMEOUT` 過得了 `SUCCEEDED()`，須 `==S_OK`〕）；SelfTest 新增 host 視窗像素 gate |
| emulator idle 自殺（黑屏「等多久都黑」第二半） | RESOLVED — Session 101 移除 `-idle-grpc-timeout 300` + regression test；orphan 由 Job Object 管 |
| `-Fast` skiavk 半套用黑屏 | RESOLVED — Session 100 移除 skiavk UI 切換（user image 結構性不可行，禁再試）；SelfTest 補 screenshot 內容 gate |
| 載入慢 + boot 期間裸黑無回饋 | RESOLVED — Session 100：`boot≈33s`、`visible_home≈49s`（原 ~80–110s）；placeholder 綁 `bootReady`；Quick Boot 維持 opt-in |
| 背景手把輸入漏進 guest | RESOLVED — Session 100：`GamepadManager` poll 前檢查 `applicationState()==ApplicationActive`（focus-gate） |
| 真 60 FPS | PARTIAL — **Session 101 更正：歷史 GPU-direct 60（S85/89/99）全是零幀 blit 節奏，不可引用**。修復後互動 UI ~43 eff FPS（真實可見）；GLES 內容受每幀 GL readback 同步限制不到 60；真 60 需 guest Vulkan-backed 內容（zero-copy，未單獨基準）。Session 99 的 host 修（threaded render loop、present timer 200ms）與 Session 94 GLES fill 探針結論（重 GLES=SwiftShader 軟體填色牆）仍有效 |
| gRPC 截圖慢 / stall / busy-poll | PARTIAL — stock gRPC ~4–17 FPS 為 fallback 本質；watchdog+transferTimeout 解 hang；raw fallback CLI-only；1080p floor 強制。流暢正解是 shared-texture path |
| stock gRPC 整輪 0 幀（`total=0`） | RESOLVED — Session 93 根因＝capture 硬寫 port 8554（須用 derived `g_runtimeCfg.grpcPort`）；次要硬化 transferTimeout 解耦 + `hasInFlight()` gate |
| emulator/qemu 搶 host audio | PARTIAL — priority 可設（`CHIMERA_INTERACTIVE_PRIORITY`；startup 前 30s Idle）；helper 走 `LowInterferenceProcess`；steady-state churn ~0；調節桿是 priority + readback CPU |
| 滑鼠滾輪卡頓 | PARTIAL — wheel 走 gRPC `sendTouchSwipe()` throttle ~16ms；輸入只由 `GuestDisplay` 轉發 |
| 原生 Emulator 視窗外露 / orphan qemu / 雙 VM | RESOLVED — 強制 headless + 可見 HWND watchdog 終止 tree；Job Object kill-on-close；啟動前清 stale port tree |
| FPS 虛報 / 靜止 repaint 開銷 | RESOLVED — 主側欄顯示 effective=min(guest,stream,render)；duplicate frame 不觸發 repaint |
| Chimera Launcher HOME（乾淨首頁/入口/status bar） | RESOLVED — `com.chimera.launcher` 自動 install/set-home；固定四入口+fallback Activity；動態只追加 user-installed；完整 ROM 級精簡仍待後續 |
| 冷開機數十秒 | PARTIAL — Quick Boot opt-in（`CHIMERA_QUICK_BOOT=1` / verifier）；預設 full boot 保護 host audio |
| SurfaceFlinger crash-loop / ADB TCP blocked（`--cuttlefish`） | OPEN — legacy R&D 路徑，不影響生產路徑 |
| emulator `streamScreenshot` 節流/0 幀 | ACCEPTED — 改用 unary `getScreenshot` 管線輪詢 |

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
| Chimera Launcher source / APK | `tools\chimera-launcher\` / `build\launcher\chimera-launcher.apk` |
| ANGLE headers | `third_party\angle\` |
| gfxstream source tree（手改+patch script 同步） | `tmp\aosp-github\hardware\google\gfxstream\`（ignored） |
| Custom gfxstream runtime 輸出 | `build\chimera-gfxstream-runtime\` |

## 參考文件

| 文件 | 用途 |
|------|------|
| `AGENTS.md` | Build、測試、Git、Coding 標準、疑難排解 |
| `CONTEXT.md` | 開發歷程、session 記錄、bug 修正紀錄 |
| `tasks/todo.md` / `tasks/lessons.md` | 當前任務規劃回顧 / 修正教訓規則 |
| `docs/project/STATUS.md` | 目前狀態快照與已知限制 |
| `scripts/verify-quick-boot.ps1` | Quick Boot smoke（重建 snapshot、驗秒數與 cleanup） |
| `scripts/verify-true-1080p60.ps1` | 連續渲染 runtime gate（gl60 synthetic，非日常 UI）；`-HeavyIterations N` 量 GLES/SwiftShader fill 天花板。**S101 後 GLES 內容嚴格 60 gate 不再通過（同步成本），任何數字要配 host 視窗像素證據** |
| `scripts/verify-interactive-ui.ps1` | 日常可用性 gate（Home→Settings→scroll→app switch；path 分類 + per-segment metrics + telemetry）；`-SyntheticScroll` 走真實輸入路徑；Stock 永不宣稱 60 |
| `scripts/ChimeraVerifyCommon.ps1` | 共用 harness（port 挑選、adb、screenshot/host-window 像素 gate、`CHIMERA_PERF` 解析、cmdline-filtered cleanup） |
| `scripts/build-chimera-gfxstream-runtime.ps1` / `apply-chimera-gfxstream-patch.ps1` | custom gfxstream runtime build / patch codify（改 script 後必 grep tree 確認落地） |
| `scripts/build-chimera-launcher.ps1` | 建置/簽章 Android HOME launcher APK |
| `docs/adr/ADR-001-shared-folder.md` | SharedFolder 技術選型 ADR |
| `docs/references/competitor-emulator-smoothness.md` | 競品（BlueStacks/LDPlayer/MuMu）平滑度研究 |

**禁止 commit**: BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/)、root 層 ISO/QCOW2/installer、QEMU/debug logs、R&D throwaway scripts、runtime output dirs。

---
*Updated: 2026-07-02 — Session 101：`-Fast` host 視窗黑屏三層根因全修（GL→VK 同步 / D3D11_TEXTURE import / keyed-mutex acquire）+ emulator idle 自殺修復 + host 視窗像素 gate；歷史 GPU-direct 60fps 宣稱全面更正為零幀 blit 節奏。詳細歷程見 `CONTEXT.md`。*
