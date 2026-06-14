# Project Chimera — AGENTS.md

> Agent 工作指南：如何在此 repo 中正確建置、測試、提交。

## Build System

- **Generator**: CMake + Visual Studio 17 2022（不用 MSYS2 / Ninja）
- **Compiler**: MSVC 19.44+（每個新 terminal 都要先執行 `vcvarsall.bat amd64`）
- **Qt**: 6.8.3 at `C:\Qt\6.8.3\msvc2022_64`
- **Standard**: C++20

```powershell
# 1. 載入 VS 環境（必要）
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

# 2. Configure（已存在 build/ 可略）
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64

# 3. Build
cmake --build build --config Release

# 4. Test（Qt DLL 需在 PATH）
ctest --test-dir build -C Release --output-on-failure -LE integration

# 5. Build Chimera Android HOME launcher（需要時）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1

# 6. Build modified gfxstream shared texture runtime（需要時）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-gfxstream-runtime.ps1
```

## Coding Standards

- 不可變資料優先；不直接修改既有物件
- 函數 < 50 行，檔案 < 800 行，巢狀 ≤ 4 層
- 例外不靜默吞掉；邊界回傳結構化錯誤
- 所有外部輸入都驗證（user input、API response、檔案內容）
- 用 RAII 管理資源；header 盡量前向宣告

## Qt / MOC 規則

- **不要定義 `QT_NO_KEYWORDS`** — 會破壞 `signals:` / `slots:` macro
- QObject 子類必須有 `Q_OBJECT`
- 需要高頻 guest display/render path 用 `QQuickItem` + scene graph texture node；不要回退到 `QQuickPaintedItem` 每幀 `QPainter`
- 每個 QTest module 必須是獨立的 `.exe`（不共用 main）

## CMake 規則

- 每個 `src/` 下的模組有自己的 `CMakeLists.txt`
- Library 預設 `STATIC`
- `nlohmann_json` 是 `INTERFACE` library，指向 `third_party/`
- `CHIMERA_BUILD_TESTS=ON`（預設）才建置 tests
- `CHIMERA_BUILD_QT_UI=ON`（預設）才建置 UI

## Testing

- Framework: Qt Test (`QTest`)
- 格式: `QTEST_MAIN(TestClassName)` + `#include "file.moc"` 在檔尾
- 執行: `ctest --test-dir build -C Release`
- Runtime Quick Boot smoke:
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25`
- Runtime display/input smoke：啟動後確認 `wm size=1920x1080`、`wm density=320`、`pm path com.chimera.launcher` 存在、HOME foreground 是 `com.chimera.launcher`，並驗證 tap/launch 後 foreground package 真的改變。Perf log 要看 Guest/Stream/Render 分離；靜止畫面 Guest FPS 可為 0。
- Runtime shared texture smoke：可用 `shared_d3d11_texture_producer` + `chimera-ui --no-emulator` 驗證 host path；helper 預設 1920x1080/60。合格數據需 Guest/Stream/Render 同步接近 60、`Dup: 0`，不能只看單一 Stream FPS。
- True Android 1080p/60 verifier：`powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-true-1080p60.ps1`。預設 `-RuntimeKind Gfxstream`，必須使用 custom shared texture runtime；stock gfxstream runtime 正確結果是 fail，不可當達標。classic EmuGL 只能用 `-RuntimeKind EmuGL` 明確驗證，不能跑 modern Android 34 Play image 時不可當完成證據。
- 測試失敗 `0xC0000135` → Qt DLL 未在 PATH
- **Unit tests** (20/20): config-manager, input-mapper, instance-manager, virtual-machine, qemu-backend,
  graphics-framebuffer, adb-framebuffer-capture, grpc-framebuffer-capture, shared-memory-framebuffer-capture, shared-d3d11-texture-capture,
  qmp-input, process-launcher, android-console-input, coordinate-mapper,
  clipboard-bridge, location-simulator, device-spoofer, macro-engine, gamepad-manager, audio-bridge
- **Integration tests** (`tests/integration/`, 3 個): 需要 env vars
  `CHIMERA_ADB_PATH`, `CHIMERA_EMULATOR_PATH`, `CHIMERA_AVD_NAME`；
  CI 用 `-LE integration` 略過

## Git Workflow

- Format: `<type>: <description>`（`feat fix refactor docs test chore perf ci`）
- **沒有使用者明確要求不 commit**
- **沒有確認不 push**
- 多個相關零碎提交才 squash；要保留 bisect 能力就不 squash

## Commit 排除（禁止進版控）

`.gitignore` 已涵蓋下列；commit 前確認 `git status` 不含這些：

- **BlueStacks 逆向 binaries**：`Binaries/`、`Client/`、`Engine/`、`Dumps/`
- **debug/擷取產物**：`*.err`、`*.out`、`*.ppm`、`verify*.png`、`qemu_*.png`、
  `shot_*.png`、`chimera-perf.*`、`*.log`
- **R&D 拋棄式腳本**：`run-qemu-*.ps1`、`test-qemu-*.bat`、`test_grpc_*.py`
- **大型 binary 與下載物**：`*.img/*.vhdx/*.qcow2/*.iso/*.dll/*.exe/*.lib`、
  `third_party/android-sdk|android-avd|android-apps|ffmpeg`
- **執行期資料**：`build/`、`instances/`、`recordings/`、`screenshots/`、`tmp/`

清理時先跑 `git ls-files --others --exclude-standard` 確認未追蹤來源檔，再跑
`git ls-files -oi --exclude-standard` 盤點 ignored 產物。`build/`、`third_party/android-sdk/`、
`third_party/android-avd/`、`third_party/android-apps/`、`third_party/ffmpeg/` 是可重建的本機快取；除非要重建環境，預設保留。

## 需維護的文件

| 文件 | 何時更新 |
|------|---------|
| `CLAUDE.md` | 架構、模組邊界、決策變更時 |
| `AGENTS.md` | 工作流程、環境、標準變更時 |
| `CONTEXT.md` | 每個重大 Phase 完成後 |
| `docs/project/STATUS.md` | 重大里程碑或 blocker 解決後 |

## Safety Checklist（commit 前）

- [ ] 無硬編碼 secrets
- [ ] 所有使用者輸入已驗證
- [ ] 錯誤訊息不洩漏敏感資料
- [ ] Tests 本地通過

## Troubleshooting

| 症狀 | 解法 |
|------|------|
| `cc1plus.exe` crash | 用 MSVC；不修 MSYS2 GCC |
| Qt6 not found by CMake | 加 `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64` |
| `chimera-ui.exe` 缺 `Qt6*.dll` | Rebuild Release；`windeployqt` 已在 post-build 自動執行 |
| Tests `0xC0000135` | `$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"` |
| MOC 編譯錯誤 `signals:` | 不要定義 `QT_NO_KEYWORDS` |
| 多個 `main()` 衝突 | 每個 test 獨立 `add_executable()` |
| Emulator "multiple emulators with same AVD" | `taskkill /F /IM emulator.exe`；移除 `*.avd/*.lock` |
| QMP connection refused | 確認 `-ports 5554,5555`（console=5554, ADB=5555） |
| ANGLE DLL 找不到 | CMake post-build 自動 copy 到 `build/Release/` |
| VNC 卡在 resize loop | 只在維度真正改變時才設 `m_resizedThisUpdate=true` |
| ADB screencap 空資料 | 確認 `sys.boot_completed=1`；用 raw format（不加 `-p`） |
| 啟動後 FPS 掉到 0–1、整機極卡 | 先檢查有無 **orphan qemu**：`Get-Process qemu-system*`；有則 kill 全部再重啟。正常情況下 `ProcessLauncher` Job Object 會在 host 被 force-kill 時清掉 emulator tree |
| 打開模擬器後主機音樂卡頓或有雜音 | 檢查 emulator/qemu 啟動前 30 秒應套 `Idle` startup priority，之後才回 `BelowNormal`；高 priority 會被 cap 到 `Normal`；低 priority process 應套 memory priority + power throttling；預設 vCPU=2；啟動前要清佔住 Chimera ports `5554/5555/8554` 的 stale emulator/qemu tree，避免雙 VM；Quick Boot load/save 都必須 opt-in；預設 full boot 必須帶 `-no-snapstorage`，一般 stop / true verifier cleanup 不可送 `adb emu kill`；raw gRPC/MMAP/screenrecord/ADB fallback 只能由同一次 CLI `--allow-raw-capture-fallback` 啟用，`CHIMERA_ALLOW_RAW_CAPTURE_FALLBACK` 不生效；screenrecord/ffmpeg 未出第一幀時不可高頻重啟；`enableAudio=false` 時不得加 `virtio-snd-pci` |
| 新增 adb/ffmpeg/QProcess helper | 必須使用 `LowInterferenceProcess` 套 BelowNormal、low memory priority、power throttling，避免繞過 `ProcessLauncher` 後再次干擾背景音樂；raw fallback 只能診斷 opt-in，不可當 1080p/60 證據 |
| 出現 `AssignProcessToJobObject failed` | 代表目前程序可能在 nested job 或權限受限；此次啟動的 emulator tree 不保證跟 host 同生共死，結束前要確認無 orphan qemu |
| Quick Boot snapshot 壞掉、ADB offline 或啟動後空畫面 | Quick Boot 預設關閉；只有 `$env:CHIMERA_QUICK_BOOT="1"` 才載入 snapshot。snapshot 保存/重建預設不在一般啟動或關閉時執行；一般 full boot 要加 `-no-snapstorage`，一般 stop 不走 console kill；用 `scripts\verify-quick-boot.ps1` 重建並驗證，或明確設 `CHIMERA_SAVE_QUICK_BOOT=1` |
| 預設啟動後仍是黑/空畫面 | 先確認不是 native embed：預設應顯示 `Stream · 已連線`；`Native · 已連線` 只應出現在明確加 `--native-embed --allow-unsafe-native-window` 時。再抓 ADB screenshot 判斷 guest state |
| 出現原生 Android Emulator 視窗或工具列 | 正式路徑必須 headless，`InstanceConfig` / `VirtualMachineConfig` 預設與舊設定都要 normalize 到 `-no-window`，且 process launch 要 hidden。啟動前要清同 port stale emulator/qemu；啟動後還要檢查 emulator/qemu process tree；headless 路徑若外露可見 HWND 必須 fail closed 並終止整棵 emulator tree。`NativeEmulatorView` 只能在 unsafe diagnostics 下 pin PID / 可見。`--window-capture` 需 `--allow-unsafe-window-capture`；`--native-embed` 需 `--allow-unsafe-native-window`；兩者還必須同時設 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 並由同次 CLI 建立 internal diagnostics session，才可外露原生視窗。殘留 env 不得單獨放行；只能做本機診斷，不能當 1080p/60 完成證據 |
| 畫面有但點擊沒反應 | 普通點擊應走 emulator gRPC `sendTouch`；不要只看 Android Console `event mouse` 回 OK。用 `dumpsys activity/window` 驗證 tap 後 foreground package 是否改變 |
| 1080p capture FPS 低於 60 | 不可降低預設解析度；Instance config、guest `hw.lcd.width/height`、emulator `-window-size`、UI `wm size` preset、legacy QEMU/HCS backend、`GrpcFramebufferCapture` request 都必須 clamp 到至少 1920x1080。raw gRPC/MMAP fallback 應要求 RGBA8888，但只能用 CLI `--allow-raw-capture-fallback` 做診斷；MMAP/ADB H.264/screenrecord 都不能當預設或完成證據；效能要靠 shared texture/custom producer 解，不准用 800x450 或 raw readback 當完成證據 |
| True 1080p/60 verifier 失敗 | 先看 `tmp\chimera-true-1080p60.log`。預設合格必須有 `Chimera gfxstream shared texture runtime ready`（或明確 `-RuntimeKind EmuGL` 時有 `Chimera EmuGL shared texture runtime ready`）、`Shared D3D11 texture display capture started`、Android `wm size >= 1920x1080`、動態 flow 中 `CHIMERA_PERF effective >= 60` 且 duplicate 低；任何 raw gRPC/ADB/screenrecord fallback 都是不合格。若 log 有 `Required shared texture capture ...`，代表 strict mode 正確 fail closed，不可改成 fallback 通過 |
| Shared texture 啟用後黑畫面 | 確認 `CHIMERA_D3D11_TEXTURE_METADATA` 指向 metadata mapping，producer 已建立 named D3D11 shared texture，且 metadata 尺寸至少 `1920x1080`；低於此尺寸會被 producer/capture 拒絕。Qt 必須走 D3D11 RHI（目前 main 會設 `QSG_RHI_BACKEND=d3d11`）。若沒有第一幀，gRPC fallback 應接手 |
| Modified EmuGL shared texture runtime | stock emulator 不會載入本 repo 的 `ChimeraSharedTextureBridge`。只有 custom emulator/EmuGL runtime 驗證時才用 `--emugl-shared-texture` 或 `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1`；host 會自動同步 `CHIMERA_D3D11_TEXTURE_*` 與 `CHIMERA_EMUGL_D3D11_TEXTURE_*` |
| Require EmuGL shared texture 後直接退出 | `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 是 fail-closed 模式；runtime 不可用、shared texture 沒第一幀、或 stock gfxstream 都會直接失敗。renderer 端也不得回落 `m_onPost` / `ColorBuffer::readback()`，且 shared texture 初始化硬失敗要 latch，避免每幀重試影響 host audio |
| Modified gfxstream shared texture runtime | stock `libgfxstream_backend.dll` 不等於 Chimera producer。modified runtime 必須同時具備 Vulkan display-post producer marker `ChimeraGfxstreamVulkanSharedTextureBridge`、合法 `chimera-gfxstream-shared-texture.json`、`renderPath=VulkanDisplayVkPost`、`abi=sdk-emulator-36`、SDK 相容 ABI export（目前至少檢查 `gfxstream_backend_set_screen_background`）、SDK runtime imports（`libandroid-emu-agents.dll`、`libandroid-emu-protos.dll`、`libandroid-emu-metrics.dll`），以及 source snapshot build id 對齊 SDK emulator build id；manifest producer 必須是 `ChimeraGfxstreamSharedTextureBridge`。runtime bridge 應提供低頻 `enabled` / `recordCopy` / `publishFrame` 診斷並拒絕低於 1920x1080 的 producer。`scripts\write-chimera-gfxstream-runtime-manifest.ps1 -RuntimeDir <custom-emulator-dir>` 會先刪掉 stale manifest，再拒絕 stock-like、只有舊 GL bridge marker、缺 SDK imports、source mismatch、或 ABI 不相容的 backend |
| SDK gfxstream proxy probe | `scripts\build-chimera-gfxstream-proxy-runtime.ps1` 可建立 `build\chimera-gfxstream-proxy-runtime`，保存 stock backend 為 `libgfxstream_backend_stock.dll` 並以 proxy DLL 維持 stock exports。typed C probes 只可觀察自然呼叫的 `stream_renderer_*` / `gfxstream_backend_*` 訊號，不可主動 map/read/export handle。`initLibrary` 由 `gfxstream_proxy_renderlib.cpp` exact C++ signature 提供，不可改回 `void*(void*)` C 假簽名。**Session 75 最終結論**：stock SDK 15261927 headless 是純 Vulkan（`vulkan-1.dll` 載入，`libEGL.dll` / `d3d11.dll` 從不出現）；`vkQueuePresentKHR` 被 pre-init 但 headless 模式零次呼叫；GPU frame capture via proxy DLL 是永久死路，不繼續探索 D3D11/EGL/vkQueuePresentKHR/vkQueueSubmit 路線。`sharedTextureProducer=false`，proxy runtime 只作 ABI probe，不可當 1080p/60 完成證據 |
| Analyze gfxstream proxy log | 用 `scripts\analyze-gfxstream-proxy-log.ps1 -LogPath <proxy.log>` 分類 stock-ABI proxy 訊號。合格候選至少要看到 1920x1080 `stream_renderer_flush` / `stream_renderer_resource_create` / `gfxstream_backend_setup_window` 這類 GPU display/resource signal；只有 `android_onPost`、`getScreenshot` 或 `transfer_read_iov` 屬 CPU readback 風險，不可當 shared texture producer 或 60 FPS 證據 |
| Build modified gfxstream runtime | 用 `scripts\build-chimera-gfxstream-runtime.ps1 [-PrepareDeps] [-Branch <branch>]`；source/deps 在 ignored `tmp\aosp*`，build 在 `tmp\aosp-build\gfxstream*`，輸出在 `build\chimera-gfxstream-runtime*`。MSVC/GNU extension 修正必須補進 `scripts\apply-chimera-gfxstream-patch.ps1`，不要手改 `tmp\aosp` 當正式修法。C++ 編過 `ChimeraGfxstreamVulkanSharedTextureBridge.cpp` / `DisplayVk.cpp` 只代表 patch 可編譯；若 source snapshot build id 不匹配 SDK emulator build id，manifest gate 必須 fail closed。注意：standalone-built DLL 可能與 SDK emulator ABI 不相容；若缺 SDK runtime imports，實測會停在 gfxstream feature 初始化、不進 `FrameBuffer::initialize()`，未通過 ABI gate 與 full boot/verifier 不可當完成證據 |
| gfxstream source snapshot mismatch | modified gfxstream manifest 必須記錄 `gfxstreamSourceSnapBuildId` 與 `baseEmulatorBuildId`，且要與 SDK emulator `source.properties` 的 `Pkg.BuildId` 匹配；只要 build id 不同，即使 DLL 有 Chimera marker、SDK imports、ABI export，也要 fail closed，避免 `0xC0000005` 或黑屏/多開。目前 SDK Emulator 36.5.11 是 build `15261927`，本機 `sdk-release` source 是 `13278158`，`emu-36-1-release` 是 `12579432`，都不能當 matching runtime |
| Require gfxstream shared texture 後直接退出 | `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE=1` 是 fail-closed 模式；runtime 不可用、manifest 無效、shared texture 沒第一幀都不得退回 raw gRPC/ADB 或 stock emulator HWND |
| Custom emulator runtime 切換 | 不要覆蓋 `third_party/android-sdk/emulator`；用 `CHIMERA_EMULATOR_PATH=<custom>\emulator.exe` 指向實驗 runtime。`InstanceManager` 會自動把該目錄旁的 `lib64/`、`lib/`、本體目錄 prepend 到 PATH |
| Custom EmuGL runtime manifest | shared texture producer runtime 必須有 `lib64OpenglRender.dll`、`lib64EGL_translator.dll`、`lib64GLES_CM_translator.dll`、`lib64GLES_V2_translator.dll` 與合法 `chimera-emugl-shared-texture.json`。用 `scripts\write-chimera-emugl-runtime-manifest.ps1 -RuntimeDir <custom-emulator-dir>` 寫入 manifest；stock `libgfxstream_backend.dll` 或缺 translator DLL 的 runtime 會被拒絕 |
| Custom EmuGL runtime build | 用 `scripts\build-chimera-emugl-runtime.ps1 [-AospPrebuiltsDir <path>]` 走 WSL + MinGW build；目前可產出 classic `emulator.exe` / `emulator64-x86.exe`、完整 legacy EmuGL DLL set、MinGW runtime DLL 與 manifest，且 `emulator64-x86.exe -help` 可執行。但此 classic runtime 仍不能跑 Android 34 `google_apis_playstore/x86_64`：modern image 只有 `kernel-ranchu`，classic path 期待 `kernel-qemu`；不可把它當 1080p/60 完成證據 |
| 為什麼不完整自研 Android 模擬器 | 短期不要重做整套 WHPX/QEMU/ranchu/virtio/gfxstream/Play image/ADB/snapshot/audio/input。Chimera 方向是 fork/改 Android Emulator + gfxstream/QEMU runtime，保留 Android 相容層，但改寫 host shell、headless display producer、input 與 resource policy；正式路徑只能有一個 Chimera 視窗，不可外露或多開原生 Android Emulator 視窗 |
| Emulator shared texture producer 接線 | `libOpenglRender` 已有 `ChimeraSharedTextureBridge` hook；成功發布 shared texture 時必須跳過 `m_onPost` / `ColorBuffer::readback()`。接入/驗證優先看 renderer `FrameBuffer::post()` / modern gfxstream `postImpl()` 的 `ColorBuffer` 階段；`GpuFrameBridge` 已是 CPU pixels callback，太晚 |
| Shared texture 只有 30fps 左右 | 先確認 producer 真的以 60Hz 產生新 even sequence；consumer 端 `SharedD3D11TextureCapture` 應由 worker 等 frame event，不可退回 UI thread QTimer |
| Shared memory/frame metadata 偶發破圖 | 檢查 producer 是否遵守 odd/even sequence seqlock；odd 表示寫入中，consumer 只接受相同且為 even 的 sequence。CPU shared-memory frame 也必須至少 1920x1080；低於此尺寸應被拒絕，不可拿來當 60 FPS 證據 |
| UI 顯示 60 FPS 但體感卡 | 主側欄 FPS 必須是有效 FPS（`min(Guest, Stream, Render)`），不可顯示單純 Stream delivery；靜止畫面可為 0，HUD/log 才看 `Guest/Stream/Render/Dup` |
| 滑鼠滾輪捲動卡頓 | 確認 `InputBridge::Event::Wheel` 走 `EmulatorGrpcInput::sendTouchSwipe()` 並有約 16ms throttle；輸入只能由 `GuestDisplay` 轉成 guest 座標後送出，不可讓 `ChimeraWindow` 再送一次 window 座標；不可讓 wheel 主路徑退回 `adb shell input swipe` |
| 靜止首頁 CPU 偏高 | 檢查 gRPC duplicate path：重複 frame 不應 emit `frameReady()` 或觸發 QML repaint；idle duplicate cadence 約 250ms，有輸入才 boost 到 16ms |
| Guest FPS 被低估或高估 | 不可未驗證就改 sampled fingerprint；誠實 FPS 要用可靠 dirty signal，或在已知成本可接受時才用 full-frame fingerprint。unary `getScreenshot` 的 `seq` 固定為 0，不能當 dirty signal；MMAP 需用 `streamScreenshot` sequence 並用通知欄/滑動 flow 驗證 Guest/Render |
| 沒有乾淨 HOME / 回到 Pixel Launcher | 先建 `scripts\build-chimera-launcher.ps1`，再確認 `build\launcher\chimera-launcher.apk` 存在；boot completed 後 host 會 install 並 `cmd package set-home-activity com.chimera.launcher/.MainActivity` |
| HOME 只剩黑底/狀態列 | 不能只看 top activity；同時跑 `uiautomator dump` 檢查 `CHIMERA` 與固定入口，並抓 `exec-out screencap -p`。Launcher 需 explicit `am start -n com.chimera.launcher/.MainActivity` |
| Google Play / 檔案管理 / 瀏覽器缺失 | Play 要使用 `google_apis_playstore` system image；檔案管理由 `third_party/android-apps/material-files.apk` 自動安裝；Chrome 由 Play image / Play 安裝提供 |
| Google Play 新安裝 app 沒出現在 Home | Chimera Launcher 應掃描 `ACTION_MAIN` + `CATEGORY_LAUNCHER`，但只追加 user-installed packages；用 `uiautomator dump` 檢查 app label/content-desc |
| Home 出現 `Settings` 重複或 `TMobile` | 動態 app 掃描過濾失效；不可追加 system / updated-system package，也要排除固定入口 package |
| Home 圖示存在但灰掉或點了沒反應 | 固定入口必須有內建 fallback Activity；用 tile content-desc bounds 點擊，再看 `dumpsys activity` foreground package 與 `logcat -s ChimeraLauncher:I` |
| HOME 上方厚黑邊 / 沒有通知列 | 檢查 Launcher theme 不可設 `android:windowFullscreen=true`；只允許 `policy_control=immersive.navigation=*`，status bar 要常駐 |
| 側欄 FPS 卡太亂 | 主側欄只放一個 FPS 數字；Guest/Stream/Render/Dup 細節放 log 或 HUD，不要塞回主卡 |
| build 後 LNK1104 | 確認 `chimera-ui.exe` / `qemu-system*` 未在執行中，鎖住輸出檔 |

---
*Keep this file updated. Agents depend on it.*
