# Chimera Task Todo

---

## 2026-06-13 Session 73 — initLibrary ABI fix + proxy smoke PASS

### Plan

- [x] 確認 `gfxstream_proxy.c` 中 `initLibrary` C shim 仍存在（root cause）。
- [x] 在 `gfxstream_proxy_renderlib.cpp` 新增 `extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary()`（exact C++ signature pure forward）。
- [x] 從 `gfxstream_proxy.c` 刪除 `initLibrary` C shim。
- [x] 修正 `build-chimera-gfxstream-proxy-runtime.ps1` 中 `initLibrary` skip 的過時註解。
- [x] 修正 `analyze-gfxstream-proxy-log.ps1` 同時計數 `renderlib_wrapper initLibrary` 與 `forward name=initLibrary`。
- [x] Build proxy runtime → `result=pass`，348 exports，gate 通過。
- [x] 跑 headless smoke → boot 完成，`initLibrary=1`，`androidSetOpenglesRenderer=1`，`rendererVtable=1`，analyzer 正確 FAIL `no 1920x1080 GPU display/resource signal`，`no_residual_processes=OK`。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- **Root cause 修正**：`initLibrary` C shim 用 `void*(void*)` 承接 `gfxstream::RenderLibPtr`（`std::unique_ptr<RenderLib>`），在 x64 Windows ABI 下 RCX/隱藏指標行為不相容，是 boot 前 `-1073741819 (AV)` 的根源。
- 改為 `extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary()` 純轉發，`#pragma warning(suppress: 4190)` 壓 MSVC C4190 warning（`unique_ptr` 不是 C-compatible return，但 ABI 正確）。
- Build gate 仍要求 `initLibrary` 為 proxy 本地 export（不可 `forwarded to`）；gate 未放寬。
- Analyzer gate 維持：proxy attach + renderer init + 1920x1080 GPU signal 缺一不可；`forward name=initLibrary` 現在正確被計入 initLibrary count。
- Smoke：emulator 正常 boot，`initLibrary=1 androidSetOpenglesRenderer=1 rendererVtable=1`；analyzer 正確 FAIL，因為 stock headless emulator 沒有 1920x1080 GPU display/resource signal — 這是預期行為。
- 下一步：仍需 matching SDK gfxstream source 的 `DisplayVk::postImpl()` GPU post hook，才能在 stock ABI proxy 上做第一個 GPU display signal，進而接 D3D11 shared texture producer。

---

## 2026-06-13 Session 72 — stock-ABI gfxstream proxy display probe

### Plan

- [x] 盤點 `build-chimera-gfxstream-proxy-runtime.ps1` 與 `src/host/runtime/gfxstream_proxy/` 現有 hook，找出能在 stock SDK 36.5.11 bootable path 觀察 display/resource lifecycle 的最低風險點。
- [x] 補低干擾 proxy 診斷或解析器：只觀察自然呼叫，不主動 map/read/export handle，不回到 CPU readback。
- [x] Build proxy / host 相關 target，跑 parser 與非整合測試。
- [x] 檢查沒有 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- 新增 `scripts\analyze-gfxstream-proxy-log.ps1`，用於分類 stock-ABI proxy log，不啟動 emulator。
- 解析器會要求 proxy DLL loaded 與 renderer init 訊號，且只有 1920x1080 `stream_renderer_flush` / `stream_renderer_resource_create` / `gfxstream_backend_setup_window` 這類 GPU display/resource signal 才算可用候選。
- 只有 `android_onPost`、`renderer_hook getScreenshot` 或 `transfer_read_iov` 會被標成 CPU readback 風險，不可當 shared texture producer 或 60 FPS 證據。
- 正向合成 log PASS；只有 `android_onPost` 的負向合成 log 如預期 fail。
- `build-chimera-gfxstream-proxy-runtime.ps1` PASS，proxy 維持 348 exports，`sharedTextureProducer=false`。
- 完整 non-integration `ctest` 20/20 PASS。
- 既有 proxy logs 目前沒有 1920x1080 GPU display/resource signal；這代表它們不能當 1080p/60 證據。
- 子代理研究 matching source / hook 線索因使用額度限制失敗，沒有可採用結論。
- 本輪沒有啟動 Android runtime；結束後沒有 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- true 1080p/60 尚未完成；下一步仍是取得 matching SDK gfxstream source/ABI，或用 stock-ABI proxy 找到穩定 GPU display-post hook，再接 D3D11 shared texture producer。

---

## 2026-06-13 Session 71 — gfxstream bridge diagnostics 與自研 VM 邊界

### Plan

- [x] 回答架構邊界：不從零重寫完整 Android VM；正式方向是 Chimera shell + headless Android Emulator/QEMU/gfxstream 相容核心 + custom display producer。
- [x] 補 gfxstream Vulkan bridge 低頻診斷：enabled、`recordCopy()` unavailable/ok、`publishFrame()` failure 只按 1/60/240 cadence 記錄，避免 log I/O 影響 host audio。
- [x] 在 runtime bridge 端加入 1920x1080 floor，低於最低尺寸直接拒絕，不讓低解析度 producer 冒充 60 FPS。
- [x] 驗證 patch script 可重套、host build/tests 通過，並實際編譯 source-patched gfxstream bridge。
- [x] 確認 mixed ABI manifest gate 正確拒絕 `sdk-release` source build id `13278158` 對 SDK emulator build id `15261927`。
- [x] 檢查結束後沒有 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- 這輪沒有宣稱 true 1080p/60 達標；`scripts\verify-true-1080p60.ps1` 仍需要 matching SDK gfxstream shared texture producer 才能 PASS。
- `scripts\build-chimera-gfxstream-runtime.ps1` 已編過 `ChimeraGfxstreamVulkanSharedTextureBridge.cpp` 與 `DisplayVk.cpp`，代表新增診斷與 1080p floor 不破壞 C++ build。
- build 最後由 `write-chimera-gfxstream-runtime-manifest.ps1` 正確 fail-closed：`gfxstream source snapshot build id 13278158 does not match base emulator build id 15261927`。
- 這個 fail-closed 是必要防線；不再拿 crash-prone mixed ABI runtime 啟動，避免黑屏、多開原生 Emulator 視窗或 host audio 卡頓。
- 驗證：patch parser / build parser PASS。
- 驗證：`chimera-ui test-instance-manager test-virtual-machine` targeted build PASS。
- 驗證：targeted `ctest` 2/2 PASS；完整 non-integration `ctest` 20/20 PASS。
- 驗證：結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg`。

---

## 2026-06-13 Session 70 — stale emulator port cleanup 與 native embed 休眠

### Plan

- [x] 盤點正式啟動路徑、`NativeEmulatorView` 與 emulator/qemu 資源策略。
- [x] 修正 `VirtualMachine::start()`：啟動前只針對佔住 Chimera ports `5554/5555/8554` 且 process 名稱為 `emulator.exe` / `qemu-system*` 的 stale VM tree 做清理，避免上輪殘留造成雙 VM、多開原生視窗與 host audio 卡頓。
- [x] 修正正式 UI 路徑：`NativeEmulatorView` 只有在 unsafe native embed diagnostics 明確啟用時才可見且才會 pin emulator PID；預設 Chimera 只走 `GuestDisplay`。
- [x] Build + targeted/full non-integration tests + `--no-emulator` UI smoke + 殘留程序檢查。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- 這輪修的是「又多開一個原生 Android Emulator」與「殘留 VM 疊加導致整機/音樂卡頓」的根因防線。
- 正式產品路徑仍是 headless Android Emulator/QEMU/gfxstream 相容核心；原生 emulator HWND 只允許 unsafe diagnostics。
- `NativeEmulatorView pinned` 不會再出現在預設路徑；短 smoke log 只有既有 QML deprecation 警告。
- 驗證：targeted `ctest` 3/3 PASS。
- 驗證：`cmake --build build --config Release` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg`。
- 真 Android 1080p/60 尚未達標；下一步仍是 matching SDK gfxstream shared texture producer / verifier PASS。

---

## 2026-06-13 Session 69 — headless runtime 邊界與 snapshot shutdown I/O 收斂

### Plan

- [x] 回答架構邊界：Chimera 短期不從零自研完整 Android VM；正式路徑是 fork/改 Android Emulator/QEMU/gfxstream 作 headless 相容核心，只留下 Chimera shell。
- [x] 檢查 `VirtualMachine` 啟動/停止參數、snapshot/Quick Boot gate、以及 true-1080p60 verifier cleanup。
- [x] 預設 full boot 除 `-no-snapshot*` 外再加 `-no-snapstorage`，避免 emulator 自動使用 `default_boot` snapshot storage。
- [x] 修正 `VirtualMachine::stop()`：只有明確 `CHIMERA_SAVE_QUICK_BOOT=1` 時才走 `adb emu avd snapshot save` / console kill；一般停止改由 Job Object / process tree 終止，避免 shutdown I/O 干擾 host audio。
- [x] 修正 `verify-true-1080p60.ps1` finally：不再預設送 `adb emu kill`，避免驗證腳本自己觸發 emulator shutdown snapshot/I/O。
- [x] 補 `test-virtual-machine` 斷言預設參數含 `-no-snapstorage`。
- [x] Build + full non-integration tests + 殘留程序檢查。

### Review

- 問題不是要不要「原生 UI」；原生 Android Emulator 視窗外露與多開是 bug。底層仍用 Android Emulator/QEMU/gfxstream 相容層，但正式產品必須 headless、單一 Chimera 視窗。
- `VirtualMachine` 預設 full boot 現在帶 `-no-snapstorage -no-snapshot -no-snapshot-load -no-snapshot-save`。
- `stop()` 不再於一般 quickBoot=false 或 quickBoot=true/save=false 路徑送 `adb emu kill`；只有使用者/驗證明確開 `CHIMERA_SAVE_QUICK_BOOT=1` 才允許 snapshot save + console kill。
- `verify-true-1080p60.ps1` cleanup 改為直接清 Chimera/emulator/qemu process，避免 true verifier 造成背景音樂卡頓回歸。
- 驗證：`cmake --build build --config Release --target test-virtual-machine` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R test-virtual-machine` PASS。
- 驗證：`cmake --build build --config Release` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*`。
- 真 Android 1080p/60 仍未達標；本輪修的是多開/外露/音訊干擾的啟停根因與防回歸。

---

## 2026-06-13 Session 68 — strict shared-texture fail-closed 與 SDK source 對齊盤點

### Plan

- [x] 重新盤點目前 worktree、runtime/verifier 狀態，不把既有 raw fallback 或 invalid runtime 當達標。
- [x] 補 strict shared texture watchdog：required shared texture 模式下，Android boot 後若 capture 未配置、未啟動、或無第一幀，直接 exit 3，不再黑屏/0 FPS 靜默卡住。
- [x] 補 `verify-true-1080p60.ps1`：拒絕 required shared texture capture failure，並修正 early-exit 訊息要顯示 exit code。
- [x] 查 SDK/source 對齊：本機 SDK emulator 36.5.11 build id 是 `15261927`；目前 `sdk-release` gfxstream source 是 `13278158`；`emu-36-1-release` 是 `12579432`，更舊。
- [x] 在 stock gfxstream proxy 加低風險 C export probe，觀察自然 resource lifecycle / readback 訊號，不包 C++ object、不主動 read/map/export。
- [x] Build + targeted/full non-integration tests + strict verifier fail-closed smoke + 殘留程序檢查。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- 這輪沒有宣稱 1080p/60 達標；正式缺口仍是 matching SDK 36.5.11 gfxstream display-post shared texture producer。
- `chimera-ui` 已能在 required gfxstream runtime 不相容時快速 fail closed：log 顯示 `Required shared texture runtime is unavailable; exiting`，沒有進 raw gRPC/ADB fallback。
- `verify-true-1080p60.ps1 -BootTimeoutSec 60 -MeasureSeconds 10` 對目前 invalid runtime 正確失敗，且 early-exit 診斷現在印出 `code 3`；這是正確 gate，不是通過證據。
- 子代理只讀研究確認公開 refs 找不到 SDK build `15261927` matching source；stock proxy 下一步只適合加低風險 C export probe 觀察 resource lifecycle，不能回到 C++ object wrapper 或 CPU readback。
- `build-chimera-gfxstream-proxy-runtime.ps1` 現在會把 `stream_renderer_init/resource_create/create_blob/export_blob/ctx_attach/ctx_detach/resource_map_info/transfer_read_iov/transfer_write_iov/vulkan_info` 與 `gfxstream_backend_setup_window/set_screen_mask/set_screen_background` 建成本地 typed wrappers，並寫入 `chimera-gfxstream-proxy.json` 的 `hookedExports`。
- proxy build PASS；metadata 顯示 `sharedTextureProducer=false`，所以它仍只是定位工具，不是完成證據。
- 驗證：`cmake --build build --config Release --target chimera-ui test-grpc-framebuffer-capture test-instance-manager test-virtual-machine test-qemu-backend` PASS。
- 驗證：script parser PASS。
- 驗證：targeted `ctest` 4/4 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 驗證：結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg`。

---

## 2026-06-12 Session 67 — 可見原生 Emulator 雙 gate 收斂

### Plan

- [x] 釐清架構邊界：短期不從零自研完整 Android VM；正式產品只能是 Chimera shell + headless Android 相容核心。
- [x] 修正 visible emulator gate：`CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 不再單獨足以放行原生 Emulator 視窗。
- [x] 只有同次 CLI 明確啟用 `--native-embed --allow-unsafe-native-window` 或 `--window-capture --allow-unsafe-window-capture` 時，主程式才設定內部 `CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1`。
- [x] `InstanceManager` 現在需要外部 unsafe allowance + 內部 diagnostics session 兩者同時成立，才接受 `allowVisibleEmulatorWindow=true`；否則 normalize 回 headless。
- [x] 移除 `CHIMERA_EMULATOR_START_VISIBLE` 對正式路徑的影響；只有通過 visible diagnostics gate 的 instance 才會可見啟動。
- [x] Build + targeted/full non-integration tests。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- 回答使用者問題：不是堅持使用「原生 UI」；Chimera 應使用 Android Emulator/QEMU/gfxstream 作為 headless 相容核心，像 BlueStacks 一樣替換 host shell、display producer、input、resource policy。從零重寫完整 Android VM 會重做 WHPX/QEMU/ranchu/virtio/gfxstream/Play image/ADB/snapshot/audio/input，短期風險更高。
- 原因修正：以前 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 對 `InstanceManager` 來說就是可見視窗放行條件，容易讓診斷設定殘留後把正式路徑打開。現在需要本次 Chimera 啟動由 CLI 明確設定 internal diagnostics session。
- 驗證：targeted build `test-instance-manager test-virtual-machine test-process-launcher chimera-ui` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-virtual-machine|test-process-launcher"` PASS，3/3。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg`。

---

## 2026-06-12 Session 66 — 單視窗與低干擾 fallback 擷取收斂

### Plan

- [x] 回答並固定產品邊界：不從零自研完整 Android VM；正式路徑只能有 Chimera 單一視窗，Android Emulator/QEMU/gfxstream 必須 headless。
- [x] 將 ABI 不相容的 custom gfxstream runtime fail closed，避免再次啟動會 crash/多開的 runtime。
- [x] 降低 MMAP/gRPC fallback 對 UI 與 host audio 的壓力：可用時直接發布 D3D11 shared texture，避免每幀 QImage 配置與 UI thread upload。
- [x] 補齊直接 `QProcess` 啟動的 adb/ffmpeg 低干擾 policy，避免繞過 `ProcessLauncher`。
- [x] Build + targeted/full non-integration tests。
- [x] 跑 true-1080p60 verifier 與殘留程序檢查；不能把 fallback 當 1080p/60 達標證據。
- [x] 同步 `tasks/lessons.md`、`CONTEXT.md`、`AGENTS.md`、`CLAUDE.md`、`docs/project/STATUS.md`。

### Review

- 產品邊界維持不變：完整自研 Android VM 不是短期解法；Chimera 應保留 Android 相容核心，但正式路徑不可外露或多開原生 Android Emulator 視窗。
- `write-chimera-gfxstream-runtime-manifest.ps1` 與 `InstanceManager::probeEmulatorRuntime()` 已要求 gfxstream source snapshot build id 必須對齊 SDK emulator build id。當前 `sdk-release` source snapshot `13278158` 不等於 SDK 36.5.11 emulator build id `15261927`，manifest writer 會拒絕這個 crash-prone mixed ABI runtime。
- strict verifier 對 `build\chimera-gfxstream-runtime-sdk-release\emulator.exe` 正確 fail-closed：`Required shared texture runtime is unavailable`，沒有啟動第二個 emulator/qemu，也沒有 raw fallback 假裝達標。
- `GrpcMmapFramebufferCapture` 現在 RGBA8888 frame 會優先 publish 到 `SharedD3D11TexturePublisher`，只有 RGB888 或 publish 失敗時才退回 QImage。diagnostic smoke 顯示 `gRPC MMAP D3D11 texture publisher started at 1920 x 1080`。
- MMAP fallback smoke 仍只有 `effective=29.4`，所以這不是 1080p/60 完成證據；正式 60fps 仍需 matching gfxstream `DisplayVk::postImpl()` / display-post shared texture producer。
- 新增 `LowInterferenceProcess`，讓 main boot/setup adb、QML Android controls、ADB raw screencap fallback、ScreenRecorder ffmpeg 都套 BelowNormal + low memory priority + power throttling。
- stock SDK gfxstream proxy 已補 `stream_renderer_flush` 低頻 resource-info probe；headless/no-audio boot completed，但 log 仍沒有任何 `stream_renderer_flush`，只看到 `initLibrary`、`android_setOpenglesRenderer`、`android_stopOpenglesRenderer`。flush 不是 SDK 36.5.11 headless active display hook。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg`。

---

## 2026-06-12 Session 65 — gfxstream bad-runtime gate and VM state hardening

### Plan

- [x] 回答架構邊界：不從零重寫完整 Android VM；正式產品只允許 Chimera 單一視窗，Android Emulator/QEMU/gfxstream 只能 headless 當相容核心。
- [x] 修正 `VirtualMachine` 狀態監控 data race：`m_state` 改為 atomic，背景 exit monitor 透過明確 state callback fail closed。
- [x] 實測 standalone custom gfxstream backend：即使關閉 Chimera bridge，仍停在 gfxstream feature 初始化，沒有 5554/5555/8554、沒有 ADB device。
- [x] 收緊 gfxstream runtime gate：除 marker、manifest、`gfxstream_backend_set_screen_background` 外，還要有 SDK runtime imports（`libandroid-emu-agents/protos/metrics`），否則拒絕。
- [x] 收緊 `write-chimera-gfxstream-runtime-manifest.ps1`：缺 SDK runtime imports 的 standalone DLL 不准寫 shared texture manifest。
- [x] 驗證 strict bad-runtime 啟動會快速退出，不留下 `chimera-ui/emulator/qemu/adb`。
- [x] Build + full non-integration tests。

### Review

- 問題不是「為什麼不用自己寫 emulator」；完整自研會重做 WHPX/QEMU/ranchu/virtio/gfxstream/Play image/ADB/snapshot/input/audio，短期更不穩。正確方向是 BlueStacks 類 host shell + headless Android 相容核心，但原生 Emulator 視窗外露是 bug。
- `build\chimera-gfxstream-runtime-sdk-release` 與 `build\chimera-gfxstream-runtime-emu36` 都缺 SDK runtime imports；它們可載入並印 feature list，但不進 `FrameBuffer::initialize()`，不能當 1080p/60 runtime。
- strict invalid runtime smoke：`chimera-ui --gfxstream-shared-texture` exitCode=3，log 為 `SDK runtime imports are missing`，且沒有殘留 emulator/qemu/adb。
- `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` 目前不是穩定 producer 路徑；短 probe 只到 `initLibrary result=wrapped`，不能當正式 shared texture 接線點。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 真 Android 1080p/60 尚未達標；下一步只能找 SDK 36.5.11 相容 source/ABI 重建，或在 bootable stock-ABI proxy 內找到穩定 GPU display-post hook，不能退回 CPU `android_setPostCallback` / raw gRPC readback。

---

## 2026-06-06 Session 64 — gfxstream RenderLib proxy probe

### Plan

- [x] 檢查 stock SDK 36.5.11 gfxstream proxy 的可攔截入口。
- [x] 新增最小 `initLibrary` / `RenderLib` / `Renderer` C++ wrapper 實驗，補齊 `FeatureSet` copy/assign 以避免未 export 符號。
- [x] 將 `RenderLib` / `Renderer` wrapper 設為 opt-in，不允許預設影響 bootable proxy runtime。
- [x] 重建 proxy runtime，並用 hidden/no-audio probe 驗證 default 路徑可 boot。

### Review

- `scripts\build-chimera-gfxstream-proxy-runtime.ps1` 可重建 proxy runtime，348 exports，並保留 `initLibrary` / `android_setOpenglesRenderer` hook log。
- opt-in `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` 會讓 emulator 早退，不能當正式接線點。
- default wrapper env 關閉時，`initLibrary` 只 forward，不包回傳物件；hidden/no-audio probe 達 `sys.boot_completed=1`，log 顯示 `renderlib_wrapper initLibrary result=forwarded` 與 `android_setOpenglesRenderer ...`。
- 驗證：default proxy boot completed in 29283 ms，`leftoverCount=0`。
- 下一步要在不替換整個 `Renderer` shared_ptr 的前提下接 display-post producer；stock DLL 沒有 export `RendererImpl::setPostCallback`，不能靠直接 symbol hook。

---

## 2026-06-06 Session 63 — native emulator architecture clarification

### Plan

- [x] 回答「為什麼不從零自研 Android 模擬器」：短期保留 Android Emulator/QEMU/gfxstream 相容核心，Chimera 只替換 host shell、headless display producer、input 與 resource policy。
- [x] 檢查目前是否殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` 程序。
- [x] 重新盤點 headless visible-window gate：`-no-window`、hidden process launch、visible HWND watchdog、Job Object tree cleanup。
- [x] Targeted build/test 驗證。

### Review

- 目前沒有殘留 Chimera/emulator/qemu/adb 程序。
- 正式路徑已鎖定只允許 Chimera 單一視窗；若 emulator/qemu process tree 外露可見 HWND，`VirtualMachine::start()` 會 fail closed 並終止整棵 emulator tree。
- 驗證：`cmake --build build --config Release --target test-process-launcher test-virtual-machine chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-process-launcher|test-virtual-machine"` PASS。

---

## 2026-06-06 Session 62 — headless visible-window watchdog

### Plan

- [x] 回答架構邊界：不從零重寫整套 Android VM；Chimera 應 fork/patch Android Emulator/QEMU/gfxstream runtime，產品面只顯示 Chimera shell。
- [x] 補 `ProcessLauncher` process tree 可見視窗偵測與整棵 process tree 終止 API。
- [x] 在 `VirtualMachine::start()` headless/hidden 路徑加入啟動期 visible-window watchdog；若 emulator/qemu tree 外露原生視窗，立即終止並回報啟動失敗。
- [x] 補 `test-process-launcher` 覆蓋 hidden async launch 不會產生 visible window。
- [x] Targeted build/test 與殘留程序檢查。

### Review

- 現在不再只相信 `-no-window` 或 `SW_HIDE`；headless 啟動後會檢查 emulator process tree 是否有可見 HWND。
- 若正式路徑仍冒出 `Android Emulator - ...` 這類原生視窗，Chimera 會立刻 kill emulator tree，避免第二個原生視窗留在桌面上干擾主機音訊/資源。
- 驗證：`cmake --build build --config Release --target test-process-launcher test-virtual-machine chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-process-launcher|test-virtual-machine"` PASS。
- 驗證：沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` 程序。

---

## 2026-06-06 Session 61 — gfxstream Vulkan producer gate and native emulator strategy

### Plan

- [x] 回答架構邊界：不從零重寫整套 Android VM；正式方向是 fork/改 Android Emulator/QEMU/gfxstream runtime，產品只顯示 Chimera shell。
- [x] 檢查自訂 `sdk-release` gfxstream runtime export，確認已補 plain C `initLibrary`，但 runtime 仍卡在 gfxstream backend 初始化，無 ADB/console。
- [x] 用 `verify-true-1080p60.ps1` 走產品路徑驗證，確認正式啟動會同步 AVD 為 Play image、1920x1080、GPU on、landscape、headless。
- [x] 收緊 gfxstream manifest writer：必須有 `ChimeraGfxstreamVulkanSharedTextureBridge` marker，不能再用舊 GL bridge marker 寫 1080p/60 manifest。
- [x] 收緊 host runtime probe：gfxstream manifest 必須宣告 `renderPath=VulkanDisplayVkPost`、`abi=sdk-emulator-36` 才算 shared texture runtime ready。
- [x] 補單元測試，舊 GL bridge manifest 即使有 marker/ABI 字串也會被拒絕。
- [x] Build + targeted/full non-integration tests + 殘留程序檢查。

### Review

- 直接從零寫 Android 模擬器不划算：WHPX/QEMU/ranchu/virtio/gfxstream/Play image/ADB/snapshot/audio/input 都要重做；Chimera 的正確路線是保留相容層、改 runtime producer 與 host shell。
- 多開原生 Emulator 視窗是 bug，不是策略；目前正式路徑會強制 headless/hidden，unsafe native/window capture 只允許本機診斷。
- `sdk-release` runtime 目前不能當完成證據：verifier log 顯示 `Chimera gfxstream shared texture runtime ready` 的舊判斷是誤判，實際上 Android 沒 boot、FPS 為 0。
- 現在舊 runtime 無法再寫 manifest：`write-chimera-gfxstream-runtime-manifest.ps1` 會因缺 `ChimeraGfxstreamVulkanSharedTextureBridge` 正確失敗。
- 驗證：`cmake --build build --config Release --target test-instance-manager chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R test-instance-manager` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` 程序。

---

## 2026-06-05 Session 60 — visible emulator window hard gate

### Plan

- [x] 回答策略邊界：不從零重寫整個 Android VM；保留 Android/QEMU/WHPX/gfxstream 相容層，但正式產品只允許 Chimera shell。
- [x] 檢查殘留程序，確認目前沒有 `chimera-ui` / `emulator` / `qemu-system*`。
- [x] 將 visible stock Android Emulator window 改成雙重診斷 gate：單靠 `--native-embed` / `--window-capture` unsafe 參數不再足夠。
- [x] 在 `InstanceManager` normalize 層擋掉舊設定或程式碼誤設的 `allowVisibleEmulatorWindow=true`。
- [x] 移除 Chimera 視窗標題中的 `Android Emulator` 字樣。
- [x] Build + targeted/full non-integration tests + 殘留程序檢查。

### Review

- 新的全域診斷鎖是 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`；沒有它時，即使帶 unsafe 參數或舊設定要求 visible window，也會回到 headless / `-no-window`。
- `chimera-ui` 視窗標題現在只顯示 `Chimera`。
- 驗證：`cmake --build build --config Release --target test-instance-manager chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R test-instance-manager` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` 程序。

---

## 2026-06-05 Session 58 — headless process window gate and host audio noise guard

### Plan

- [x] 釐清自研邊界：短期不從零重寫 WHPX/QEMU/ranchu/gfxstream/Play/ADB；正式方向是 fork/改 Android Emulator runtime，產品只露出 Chimera shell。
- [x] 修正 `ProcessLauncher` resource policy warning：相同 pid/error 只記一次，避免啟動時高頻 warning 造成 log I/O 噪音。
- [x] 修正 headless 啟動：正式路徑不只帶 `-no-window`，也在 Windows process creation 層級強制 hidden launch。
- [x] 跑短 runtime smoke：確認 emulator 子程序有 `-no-window`，且 `MainWindowTitle` 為空。
- [x] 跑完整 non-integration 測試與殘留行程檢查。

### Review

- `VirtualMachine::start()` 現在對 headless 或未授權 visible window 的啟動一律用 hidden process launch；unsafe diagnostics 仍需明確 allowance。
- smoke 結果：`noWindowArg=True`、`mainWindowTitle=`、`memoryPolicyWarnings=0`。
- targeted 驗證：`test-virtual-machine` / `test-process-launcher` PASS。
- 完整驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS；沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` 程序。
- 真 Android dynamic 1080p/60 仍需 custom gfxstream shared texture producer；這輪修的是多開原生視窗與音訊干擾回歸風險。

---

## 2026-06-05 Session 57 — gfxstream ABI gate and custom runtime reality check

### Plan

- [x] 回答並釐清「為什麼不從零自研 Android 模擬器」：正式方向是 fork/改 Android Emulator/QEMU/gfxstream 相容層，而不是暴露 stock emulator 視窗。
- [x] 將 gfxstream dependency/build scripts 改成可指定 upstream branch，並嘗試以 `emu-36-1-release` 重建 modified gfxstream runtime。
- [x] 實測 `build\chimera-gfxstream-runtime-emu36`：custom DLL 可 build/package，但 Android 180/240 秒內無 ADB device / boot_completed；stock SDK 同 AVD 約 36 秒 boot completed。
- [x] 對照 exports，確認 custom standalone DLL 與 SDK 36.5.11 ABI 不相容：缺 `gfxstream_backend_set_screen_background`，且多出新版 renderer entrypoints。
- [x] 新增 host/runtime gate：modified gfxstream runtime 需同時有 Chimera marker、合法 manifest、SDK ABI export，否則拒絕啟動 shared texture strict path。
- [x] 更新 manifest writer，缺 SDK ABI export 時拒絕寫 `chimera-gfxstream-shared-texture.json`。
- [x] 補 `test-instance-manager` coverage，鎖住 incompatible gfxstream ABI 會被 reject。
- [x] Build + tests + script parser 驗證。

### Review

- 不再把 standalone-built gfxstream DLL 當成可替換 SDK emulator 36.5.11 的 runtime；這會讓 QEMU 活著但 Android/ADB 不起來，造成黑屏與資源干擾。
- `InstanceManager::probeEmulatorRuntime()` 現在要求 `gfxstream_backend_set_screen_background` ABI marker，避免不相容 DLL 被誤判成 ready。
- `scripts\write-chimera-gfxstream-runtime-manifest.ps1` 現在會拒絕目前不相容的 `build\chimera-gfxstream-runtime-emu36`，舊 manifest 已移除。
- 驗證：`cmake --build build --config Release --target test-instance-manager chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R test-instance-manager` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：PowerShell parser for gfxstream build/apply/manifest/verifier scripts 通過。
- 驗證：stock SDK emulator direct boot 同 AVD 可 boot completed；custom standalone gfxstream runtime 不能 boot，不能當 1080p/60 證據。
- 狀態：真 Android 1080p/60 仍未達標；下一步必須取得/對齊 SDK 36.5.11 對應 source/ABI，或改成在 stock ABI wrapper 層接 shared texture producer，不能用不相容 DLL 硬塞。

---

## 2026-06-04 Session 56 — modified gfxstream runtime build

### Plan

- [x] 修正 AOSP gfxstream / AEMU 在 MSVC 下的 GNU extension 相容問題，集中收斂到 `scripts/apply-chimera-gfxstream-patch.ps1`。
- [x] 建立 `scripts/prepare-chimera-gfxstream-deps.ps1`，讓 `tmp\aosp` 依賴可檢查/補齊。
- [x] 更新 `scripts/build-chimera-gfxstream-runtime.ps1`，從 AOSP gfxstream source 建出 modified `libgfxstream_backend.dll`。
- [x] 將 SDK emulator runtime 複製成獨立 `build\chimera-gfxstream-runtime` scaffold，再覆蓋 Chimera 版 gfxstream backend。
- [x] 寫出 `chimera-gfxstream-shared-texture.json`，並驗證 DLL 內含 `ChimeraGfxstreamSharedTextureBridge` marker。
- [x] 驗證 `chimera-ui` build、20/20 non-integration tests、no-emulator/native-embed gate smoke 與無殘留 emulator/qemu。

### Review

- `scripts\build-chimera-gfxstream-runtime.ps1` 現在可成功產出 `build\chimera-gfxstream-runtime\emulator.exe`、`lib64\libgfxstream_backend.dll` 與 `lib64\chimera-gfxstream-shared-texture.json`。
- `libgfxstream_backend.dll` 已包含 `ChimeraGfxstreamSharedTextureBridge` marker；manifest 宣告 `D3D11SharedTexture`、`1920x1080`、`targetFps=60`。
- AOSP gfxstream build 仍會有 upstream CMake / flatbuffers git tag warning，但 DLL build、runtime scaffold、manifest writer 都成功。
- 驗證：`build\chimera-gfxstream-runtime\emulator.exe -help` 可執行，未留下 emulator/qemu 程序。
- 驗證：`cmake --build build --config Release --target chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：`chimera-ui --no-emulator --native-embed` smoke 後沒有 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` 殘留。
- 這是 modified gfxstream runtime artifact 與封裝完成；真 Android 動態 1080p/60 還需要用 `scripts\verify-true-1080p60.ps1` 跑完整 runtime flow 才能宣稱達標。

---

## 2026-06-03 Session 55 — headless-only product launch gate

### Plan

- [x] 釐清「為什麼不完整自研 Android 模擬器」：短期不重做 WHPX/QEMU/ranchu/virtio/Play image/ADB/snapshot/audio/input，正式方向是 fork/改 Android Emulator runtime 與 gfxstream producer。
- [x] 封住多開原生 Android Emulator 視窗：`InstanceConfig` / `VirtualMachineConfig` 預設改為 headless。
- [x] 新增 `allowVisibleEmulatorWindow` unsafe gate；沒有此 gate 時，即使舊設定或程式寫 `headless=false`，仍會帶 `-no-window`。
- [x] 讓 `--native-embed` 跟 `--window-capture` 一樣需要額外 unsafe allowance，避免正常啟動外露 stock emulator 視窗/工具列。
- [x] 補單元測試鎖住預設 headless 與舊設定 normalization。
- [x] Build + targeted tests 驗證。

### Review

- 正式產品路徑現在會強制 headless：預設 VM args 包含 `-no-window`。
- `--native-embed` 現在只有同時加 `--allow-unsafe-native-window` 或設定 `CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW=1` 才會生效；否則會拒絕並維持 stream path。
- 舊 `configs/instances.json` 若殘留 `headless:false`，`InstanceManager` 會 normalize 回 headless，避免舊設定再次打開原生 Android Emulator 視窗。
- 驗證：`cmake --build build --config Release --target test-virtual-machine test-instance-manager chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-instance-manager"` PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：`chimera-ui --no-emulator --native-embed` 在未加 unsafe allowance 時只輸出 unsafe warning，沒有啟動 emulator/qemu。

---

## 2026-06-03 Session 53 — gfxstream attestation + verifier support

### Plan

- [x] 盤點 repo 內 gfxstream/EmuGL/shared texture/verifier 現況，確認沒有 modern gfxstream source，只有 legacy EmuGL hook。
- [x] 強化 modified gfxstream runtime gate：不再只信任 manifest，還要 `libgfxstream_backend.dll` 內含 Chimera bridge marker。
- [x] 更新 `write-chimera-gfxstream-runtime-manifest.ps1`，stock-like backend 沒有 marker 時拒絕寫 manifest。
- [x] 更新 `verify-true-1080p60.ps1`，新增 `-RuntimeKind Gfxstream|EmuGL`，預設走 gfxstream strict mode。
- [x] Build/test/parser/manifest/verifier parse-only 驗證。
- [x] 同步 `AGENTS.md` / `CLAUDE.md` / `CONTEXT.md` / `docs/project/STATUS.md`，明確禁止把 stock emulator 視窗或 manifest-only runtime 當完成方案。

### Review

- `InstanceManager::probeEmulatorRuntime()` 現在要求 modified gfxstream runtime 同時具備：`libgfxstream_backend.dll`、binary marker `ChimeraGfxstreamSharedTextureBridge`、合法 `chimera-gfxstream-shared-texture.json`。
- 單純把 manifest 放在 stock SDK runtime 旁邊不會通過；status 會指出 `bridge marker is missing`。
- `scripts\write-chimera-gfxstream-runtime-manifest.ps1` 會掃 binary marker，stock-like backend exit 1，帶 marker 的 modified-like artifact 才能寫 manifest。
- `scripts\verify-true-1080p60.ps1` 預設 `-RuntimeKind Gfxstream`，預設 runtime path 為 `build\chimera-gfxstream-runtime\emulator.exe`；`-RuntimeKind EmuGL` 仍可驗證 legacy EmuGL artifact。
- verifier parse-only 現在接受 `Chimera gfxstream shared texture runtime ready` 或 `Chimera EmuGL shared texture runtime ready`，但仍拒絕 raw gRPC/ADB/screenrecord fallback。
- 驗證：`cmake --build build --config Release --target test-instance-manager chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R test-instance-manager` PASS。
- 驗證：PowerShell parser for `verify-true-1080p60.ps1` / `write-chimera-gfxstream-runtime-manifest.ps1` 通過。
- 驗證：manifest script stock-like backend 失敗、modified-like marker artifact 成功。
- 驗證：verifier parse-only gfxstream / EmuGL 正向通過，含 raw gRPC fallback 的 log 失敗。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` PASS，20/20。
- 驗證：`git diff --check` 只有 LF/CRLF 提醒，沒有 whitespace error。
- 驗證：沒有 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` 殘留程序。
- 結論：這輪封住「stock Android Emulator + manifest」與「多開原生 emulator 視窗」的假路徑；真 Android 1080p/60 仍需要實際 modified gfxstream shared texture producer runtime。

---

## 2026-06-03 Session 54 — renderer strict shared texture path

### Plan

- [x] 確認 `GpuFrameBridge` 不是正式 1080p/60 接點：它已經拿到 CPU RGBA pixels，且每幀會 allocation/copy。
- [x] 用子代理並行確認最短接點是 renderer `FrameBuffer::post()` / modern gfxstream `postImpl()`，不是原生視窗或 raw readback。
- [x] 強化 legacy EmuGL `ChimeraSharedTextureBridge`：strict shared texture 模式下禁止 `m_onPost` readback fallback。
- [x] 初始化硬失敗後停止每幀重試與錯誤輸出，避免 shared texture 不可用時持續搶 CPU / I/O。
- [x] 重新 build `build\chimera-emugl-runtime`，確認 runtime DLL 含 bridge 與 strict env marker。
- [x] 盤點 stock `libgfxstream_backend.dll` exports，確認 DLL forwarder 不是可靠方向，下一步走官方 gfxstream source patch/build。

### Review

- `FrameBuffer::post()` 現在只有在 shared texture 未成功且 strict 模式未要求 shared texture 時，才允許 `ColorBuffer::readback()` + `m_onPost`。
- `ChimeraSharedTextureBridge` 會讀 `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE` / `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE`；strict mode 會壓掉 CPU readback fallback。
- `ChimeraSharedTextureBridge::ensureInitialized()` 對缺 extension、mapping/event/D3D texture/shared handle/EGL surface 建立失敗做 hard-unavailable latch，避免每幀重試與 log 洪水。
- 驗證：`scripts\build-chimera-emugl-runtime.ps1` PASS，產出 `build\chimera-emugl-runtime\emulator.exe` 與 `lib64\lib64OpenglRender.dll`。
- 驗證：`build\chimera-emugl-runtime\emulator.exe -help` 可執行。
- 驗證：`lib64OpenglRender.dll` 包含 `ChimeraSharedTextureBridge`、`CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE` 與 bridge error marker。
- 驗證：未啟動 Android VM；沒有 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` 殘留程序。
- 研究：官方 gfxstream source 已抓到 `tmp\gfxstream-src`；modern GL post 路徑位於 `host\FrameBuffer.cpp::postImpl()` 與 `host\PostWorkerGl.cpp::postImpl()`。

---

## 2026-06-03 Session 52 — gfxstream runtime gate + low-interference fallback

### Plan

- [x] 驗證 H.264 fallback 的低干擾改動：移除重複 ffmpeg scale，並將 BGRA decode frame 優先 publish 到 D3D11 shared texture。
- [x] 新增 modern gfxstream shared texture runtime probe，讓 stock `libgfxstream_backend.dll` 不再被當作 Chimera producer runtime。
- [x] 新增 `--gfxstream-shared-texture` / `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE=1` fail-closed 邊界，禁止錯誤 runtime 回落 raw gRPC/ADB。
- [x] 新增 `scripts/write-chimera-gfxstream-runtime-manifest.ps1` 與 runtime gate 單元測試。
- [x] Build、targeted/full tests、no-emulator window-capture gate smoke、manifest script 正反案例、殘留程序檢查。

### Review

- 架構判斷：短期不完整重寫 Android VM；保留 Android/QEMU 相容層，但正式產品路徑必須由 Chimera 自有 host shell、headless runtime、shared D3D11 texture display producer、gRPC/console input 與低干擾 process policy 接管。
- `InstanceManager::probeEmulatorRuntime()` 現在分開辨識 legacy EmuGL manifest 與 modern gfxstream manifest。只有 `producer=ChimeraGfxstreamSharedTextureBridge`、`transport=D3D11SharedTexture`、`minWidth>=1920`、`minHeight>=1080`、`targetFps>=60` 的 gfxstream runtime 才算支援。
- stock SDK `libgfxstream_backend.dll` 會得到 `stock gfxstream runtime; Chimera gfxstream bridge will not load`，不能再被拿來當 1080p/60 producer。
- `main.cpp` 新增 gfxstream shared texture env wiring；要求 gfxstream shared texture 時，runtime 不可用或 shared texture 沒第一幀都會 fail closed，不會退回 raw gRPC/ADB 或 stock emulator HWND。
- `AdbH264FramebufferCapture` 現在低優先級啟動 adb/ffmpeg helper，移除多餘 scale，並把 BGRA frame 優先送進 `SharedD3D11TexturePublisher`；QImage 只作 fallback。
- 驗證：`cmake --build build --config Release --target test-shared-d3d11-texture-capture test-grpc-framebuffer-capture chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-shared-d3d11-texture-capture|test-grpc-framebuffer-capture"` 2/2 PASS。
- 驗證：`cmake --build build --config Release --target test-instance-manager chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-shared-d3d11-texture-capture|test-grpc-framebuffer-capture"` 3/3 PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS；最後 `chimera-ui` rebuild 通過。
- 驗證：`scripts\write-chimera-gfxstream-runtime-manifest.ps1` 缺 `libgfxstream_backend.dll` 時 exit 1，有 backend 時寫出合法 manifest。
- 驗證：`chimera-ui --no-emulator --window-capture` 只記錄 unsafe warning，沒有啟動 emulator/qemu；結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` 殘留。
- `git diff --check` 通過，僅有既有 LF/CRLF warning。

---

## 2026-06-02 Session 51 — EmuGL DLL artifact build

### Plan

- [x] 修正 custom EmuGL build script 的 system MinGW / AOSP prebuilts gate。
- [x] 讓 build 只產出 64-bit legacy EmuGL DLL set，不再展開 stock emulator UI/executable/test targets。
- [x] 修正 `ChimeraSharedTextureBridge.cpp` 的 MinGW C++ 編譯錯誤。
- [x] 驗證 DLL artifact 產出，並確認沒有 `emulator.exe` 時不寫 runtime manifest。
- [ ] 取得或建出真正 custom `emulator.exe` runtime，寫 manifest 並跑 `verify-true-1080p60.ps1`。

### Review

- `scripts/build-chimera-emugl-runtime.ps1` 現在沒有 AOSP prebuilts 時會使用 WSL system MinGW，並只在 `/tmp/chimera-qemu-emugl-build` 臨時 copy 內 patch 舊 build system。
- 臨時 patch 內容：關閉 Windows 32-bit build、跳過 qapi Python2 generation、跳過 emulator executable/tests、移除 EmuGL/gtest unittest modules，並用 `python3 -m lib2to3` 轉換 `gen-entries.py`。
- build target 改為四個 DLL 檔案 target：`lib64OpenglRender.dll`、`lib64EGL_translator.dll`、`lib64GLES_CM_translator.dll`、`lib64GLES_V2_translator.dll`。
- 修正 `ChimeraSharedTextureBridge.cpp`：補 `<string>`，並把 nested `Platform` 定義移出 anonymous namespace。
- 驗證：PowerShell parser 通過；`scripts\build-chimera-emugl-runtime.ps1` 成功產出 `build\chimera-emugl-runtime\lib64\` 下四個 required DLL。
- 重要限制：目前輸出沒有 `emulator.exe`，因此 `chimera-emugl-shared-texture.json` 不會寫入；這是 DLL artifact，不是 runtime ready，不能宣稱 Android 動態 1080p/60 已完成。

---

## 2026-06-01 Session 49 — true 1080p/60 runtime gate

### Plan

- [x] 盤點現有 runtime verifier 與 Perf log，定義「真 1080p/60」可重跑證據。
- [x] 新增嚴格 verifier：強制 shared texture runtime、檢查 Android 1920x1080、驅動畫面變動、解析 Guest/Stream/Render。
- [x] 在 require shared texture 時禁止 raw gRPC/ADB fallback 與空 UI 假跑。
- [x] Build/test/parser 驗證並更新交接文件。

### Review

- 新增 `scripts/verify-true-1080p60.ps1`：啟動時強制 `--emugl-shared-texture`、`CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1`、`CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1`，並檢查 `wm size >= 1920x1080`、動態輸入 flow、Perf log。
- verifier 會拒絕 raw gRPC / ADB / screenrecord fallback；合格條件是穩態 `min(Guest, Stream, Render) >= 60` 且 duplicate rate 不高。
- `main.cpp` 新增 `CHIMERA_LOG_PATH`，並輸出 machine-parseable `CHIMERA_PERF ... effective=...`。
- `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 現在會 fail closed：runtime 不可用時直接退出；shared texture capture 沒出第一幀時拒絕回落 raw gRPC/ADB。
- stock SDK emulator 實跑 verifier 正確失敗：`stock gfxstream runtime; Chimera EmuGL bridge will not load`，且沒有留下 Chimera/emulator/qemu orphan process。
- 修正 `echo_args` helper 改用 `wmain` 輸出 UTF-8，讓 `test-process-launcher::runSyncUnicodeArg` 真正驗證 wide argv。
- 驗證：PowerShell parser / verifier parse-only 正向、低 FPS、fallback case 均符合預期。
- 驗證：`cmake --build build --config Release --target chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-grpc-framebuffer-capture|test-shared-d3d11-texture-capture"` 3/3 PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R test-process-launcher` PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 狀態：這輪封住假達標與 fallback 假跑；真 Android 動態 1080p/60 仍未完成，仍需可用 custom EmuGL/shared texture runtime。

---

## 2026-06-01 Session 48 — custom EmuGL runtime artifact gate

### Plan

- [x] 收緊 custom EmuGL runtime artifact gate，定義完整 DLL/manifest 條件。
- [x] 更新 manifest/build scripts，缺 runtime DLL 時拒絕蓋 manifest。
- [x] 補 `InstanceManager::probeEmulatorRuntime()` runtime gate 測試。
- [x] Build + targeted/full tests 驗證。
- [x] 更新交接文件與 review。

### Review

- `InstanceManager::probeEmulatorRuntime()` 現在要求完整 legacy EmuGL DLL set：`lib64OpenglRender.dll`、`lib64EGL_translator.dll`、`lib64GLES_CM_translator.dll`、`lib64GLES_V2_translator.dll`。
- manifest 不再只看檔案存在；必須包含 `producer=ChimeraSharedTextureBridge`、`transport=D3D11SharedTexture`、`minWidth>=1920`、`minHeight>=1080`、`targetFps>=60` 才算 ready。
- `write-chimera-emugl-runtime-manifest.ps1` 會在缺任一 required DLL 時 fail，不再替不完整 runtime 蓋章。
- `build-chimera-emugl-runtime.ps1` build 後會複製完整 required DLL set，缺一個就 exit 4；可用時也複製 `lib64/gles_mesa` 與 `qemu/`。
- 新增 `test-instance-manager` coverage：stock gfxstream reject、缺 manifest reject、缺 translator DLL reject、invalid manifest reject、完整 runtime accept。
- 驗證：`cmake --build build --config Release --target test-instance-manager chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager"` PASS。
- 驗證：manifest script 對缺 translator DLL 的 fake runtime 如預期失敗，對完整 fake runtime 成功寫 manifest。
- 驗證：PowerShell parser 檢查 build/manifest scripts 通過；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 實際執行 `scripts\build-chimera-emugl-runtime.ps1` 目前停在缺完整 AOSP prebuilts：`missing AOSP prebuilts: .../src/prebuilts/gcc`；這確認目前還沒有可用 custom runtime，不能宣稱 Android dynamic 60 FPS。

---

## 2026-06-01 Session 47 — shared-memory no-downscale guard

### Plan

- [x] 盤點剩餘 capture backend 是否還能接受低於 1920x1080 的 active frame。
- [x] 補掉 CPU shared-memory framebuffer 的低解析度 metadata 入口。
- [x] 更新 shared-memory unit test，正常 frame 使用 1920x1080，並新增 1280x720 拒絕測試。
- [x] Build + targeted/full tests 驗證。
- [x] 更新交接文件與 review。

### Review

- `SharedMemoryFramebufferCapture` 現在使用 `SharedMemoryFrameAbi` 的 `kMinimumFrameWidth/Height`，低於 1920x1080 的 CPU-copy frame metadata 會被拒絕，不會 emit `frameReady()`。
- `test-shared-memory-framebuffer-capture` 的正向案例改為 1920x1080 frame，避免測試本身成為低解析度成功範例。
- 新增 `rejectsLowResolutionFrame()`，證明 1280x720 shared-memory metadata 只會觸發錯誤，不會產出畫面。
- 驗證：`cmake --build build --config Release --target test-shared-memory-framebuffer-capture chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-shared-memory-framebuffer-capture|test-shared-d3d11-texture-capture|test-grpc-framebuffer-capture"` 3/3 PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- `rg` 掃描剩餘低解析度測試值後，命中皆為 clamp/reject guard 測試，不是成功路徑。

---

## 2026-06-01 Session 46 — no-downscale cleanup before true 60 FPS proof

### Plan

- [x] 掃描 active config / script / shared texture 入口，找低於 1920x1080 的繞路點。
- [x] 把 shared D3D11 producer/consumer 改成低於 1920x1080 直接拒絕。
- [x] 把 qemu/cuttlefish config、legacy run script、HCS 診斷腳本改成至少 1920x1080。
- [x] 修正 lessons / README / STATUS 裡會誤導後續 agent 再降解析度的內容。
- [x] Build + unit tests 驗證。
- [x] 記錄 review。

### Review

- `SharedMemoryFrameAbi` 新增共用最低 frame 尺寸常數 `1920x1080`；`SharedD3D11TexturePublisher` 低於此尺寸會拒絕啟動，`SharedD3D11TextureCapture` 收到低解析度 metadata 會報錯且不發 texture frame。
- `test-shared-d3d11-texture-capture` 改用 1920x1080 source，並新增 producer/metadata 低解析度拒絕測試。
- `configs/qemu.json`、`configs/cuttlefish.json`、`scripts/run.py`、三個 HCS diagnostic scripts 都已改成不低於 1920x1080；`scripts/run.py` 對低解析度參數會 clamp。
- `README.md`、`docs/project/STATUS.md`、`tasks/lessons.md` 已把低解析度策略標成 historical/superseded，避免後續 agent 再拿 800x450/1280x720 當完成證據。
- 驗證：`cmake --build build --config Release --target test-shared-d3d11-texture-capture shared_d3d11_texture_producer chimera-ui` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-shared-d3d11-texture-capture|test-grpc-framebuffer-capture|test-instance-manager|test-virtual-machine|test-qemu-backend"` 5/5 PASS。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 驗證：`python -m py_compile` 四個 touched Python scripts 通過；`shared_d3d11_texture_producer --width 1280 --height 720` 如預期拒絕。
- `rg` 掃描 active configs/scripts/docs 後，剩下的低解析度字串都在 historical/guard 說明或已 clamp 的 `-skin {w}x{h}` 參數。
- `git diff --check` 通過，只有既有 CRLF warning。

---

## 2026-06-01 Session 45 — host audio startup isolation

### Plan

- [x] 確認 display capture 是否仍在 Android boot 前啟動。
- [x] 將 emulator/qemu 啟動前段改成更低干擾的 idle/EcoQoS policy，保護 foreground browser/audio。
- [x] 加強 process resource policy：低 priority process 同時套 power throttling / memory priority。
- [x] 補 process launcher / VM priority 測試。
- [x] Build `chimera-ui` 並跑完整非 integration tests。
- [x] 更新 lessons / CONTEXT / 專案文件。

### Review

- 檢查 `main.cpp` 後確認 raw gRPC / MMAP / H.264 capture 由 `androidBootReady` gate 控制，仍需 `sys.boot_completed=1` 後才啟動；本次主因改從 emulator/qemu 啟動期資源搶占處理。
- `VirtualMachine::start()` 現在會用 `Idle`/更低干擾 priority 啟動 emulator，前 30 秒每 50ms 對整棵 process tree 重套 startup priority，覆蓋 qemu child 出生競態。
- 30 秒後才回到設定的 steady priority（預設 `below_normal`），並再追蹤 90 秒，避免子程序晚出生後回到高資源模式。
- `ProcessLauncher::applyPriority()` 對 `below_normal` / `idle` process 追加 memory priority 與 power throttling，包含 ignore timer resolution，降低對 foreground browser/audio 的搶占。
- 新增 `runAsyncAppliesIdlePriority()` 鎖住 idle priority 可套用；既有 high priority cap 仍通過。
- 驗證：`cmake --build build --config Release --target test-process-launcher chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-process-launcher|test-virtual-machine|test-instance-manager"` 3/3 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 受控 runtime smoke：以 hidden window 啟動 `chimera-ui.exe` 12 秒，觀察到 `emulator.exe` 與 `qemu-system-x86_64-headless.exe` 的 Windows priority 都是 `4`（Idle），`chimera-ui.exe` 是 `8`（Normal）。
- 強制停止 `chimera-ui.exe` 後 5 秒確認無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-06-01 Session 44 — gRPC fallback RGBA render path

### Plan

- [x] 檢查 `GuestDisplay` / gRPC 1080p render path 是否仍有每幀 texture 重建或 CPU format convert。
- [x] 將 unary gRPC fallback request 從 RGB888 改為 RGBA8888，對齊 Qt D3D11 texture upload 格式。
- [x] 將 MMAP 實驗 request 也改為 RGBA8888，避免 1080p render thread 再做 RGB -> RGBA 轉換。
- [x] 保留 RGB888 回應相容處理，但在 capture 層轉成 RGBA，避免把轉換丟到 Qt scene graph render path。
- [x] 更新 unit tests，鎖住 1920x1080 request 與 RGBA format。
- [x] Build `chimera-ui` 並跑完整非 integration tests。

### Review

- `GrpcFramebufferCapture::buildImageFormatRequest()` 現在要求 RGBA8888；`imageFromTopDown()` 一律輸出 `QImage::Format_RGBA8888`。
- `GrpcMmapFramebufferCapture` request 改為 RGBA8888；若 runtime 仍回 RGB888，會在 capture 層一次轉成 RGBA。
- 這輪修的是 raw fallback 的 render-thread 壓力，不能把它宣稱為真 1080p/60 完成；真 60 仍需要 shared texture/custom runtime。
- 驗證：`cmake --build build --config Release --target test-grpc-framebuffer-capture chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-grpc-framebuffer-capture"` 1/1 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 為避免再次干擾使用者背景音樂，本輪未啟動 full Android runtime。

---

## 2026-06-01 Session 43 — legacy backend 1080p floor

### Plan

- [x] 掃描 current worktree 的低解析度與 raw readback 漏口。
- [x] 封住 QEMU/HCS legacy backend 低於 1920x1080 的 active fallback。
- [x] 補 `test-qemu-backend` 鎖住 QEMU `virtio-gpu` 與 HCS video monitor 解析度 floor。
- [x] Build `test-qemu-backend` / `chimera-ui` 並跑完整非 integration tests。
- [x] 更新 lessons / CONTEXT / 專案文件。

### Review

- `QemuInstanceConfig` 預設顯示解析度改為 `1920x1080`；`QemuBackend` constructor 會正規化低於 1080p 的 legacy config。
- `main.cpp` 讀取 `configs/qemu.json` / `configs/cuttlefish.json` 的 display 區塊時也會 clamp 到至少 `1920x1080`。
- `HyperVManager::buildHcsJsonString()` 的 synthetic video monitor 改為 `1920x1080`，避免 HCS path 仍用 1280x720。
- `scripts/test-vnc-display.ps1` 的 R&D VNC smoke 也改為 `1920x1080`，避免手動驗證腳本把降階帶回來。
- 新增 `test-qemu-backend`，驗證 `QemuInstanceConfig{800x450}` 仍輸出 `virtio-gpu-pci,xres=1920,yres=1080`，且 HCS JSON 含 `HorizontalResolution=1920` / `VerticalResolution=1080`。
- 驗證：`cmake --build build --config Release --target test-qemu-backend chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-qemu-backend|test-instance-manager|test-virtual-machine|test-grpc-framebuffer-capture"` 4/4 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。

---

## 2026-06-01 Session 42 — startup audio/resource regression guard

### Plan

- [x] 封住新 instance / 舊設定回到 `normal` 或更高 priority 的路徑。
- [x] 讓 emulator/qemu 子程序在啟動前幾秒立即、密集套用 BelowNormal/EcoQoS，避免啟動尖峰干擾主機音樂。
- [x] 移除 UI 裡低於 1920x1080 的螢幕尺寸入口，並在 ADB `wm size` 邊界做 clamp。
- [x] 補單元測試並跑完整非 integration tests。
- [x] 更新 lessons / CONTEXT / 專案文件。

### Review

- `InstanceConfig` / `VirtualMachineConfig` 預設 priority 改為 `below_normal`；`InstanceManager` 讀寫 config 時會把空值補成 `below_normal`，並把 `high/gaming/above_normal/realtime` cap 到 `normal`。
- `VirtualMachine` 邊界不再允許高於 Normal 的 priority class；`ProcessLauncher` 也會把任何高 priority request cap 到 Normal，避免未來新入口繞過 InstanceManager。
- emulator 啟動後前 5 秒改為每 50ms 重套 process tree priority/EcoQoS，再持續每秒追蹤，補上 qemu child 在 resume 後才生出的啟動競態。
- UI 螢幕尺寸 preset 只保留 1920x1080 / 2560x1440；`QmlAndroidControls::setScreenSize()` 也會 clamp 到至少 1920x1080。
- 驗證：`cmake --build build --config Release --target test-process-launcher test-instance-manager chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-virtual-machine|test-process-launcher"` 3/3 PASS；當時完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS。
- 沒有偵測到正在跑的 `qemu-system*` / `emulator` / `chimera-ui`，本輪未啟動 full Android boot，避免再次干擾使用者背景音樂。

---

## 2026-06-01 Session 41 — normalize instance config resolution

### Plan

- [x] 讓 `InstanceManager` load/create/save/get 也正規化到至少 1920x1080。
- [x] 補 unit test，防止低解析度 config 被保存或回傳成有效設定。
- [x] Build + targeted tests。
- [x] 跑完整非 integration tests。
- [x] 更新 lessons / CONTEXT。

### Review

- `InstanceManager.cpp` 新增 `normalizedInstanceConfig()`，load saved configs、create instance、save instances 都會把 width/height clamp 到至少 1920x1080。
- `createInstance()` 現在用 normalized config 建 VM，也把 normalized config 寫回 saved config；UI/設定層不會再保留 `800x450` 這類低解析度作為有效設定。
- `test-instance-manager` 新增 `createInstanceKeeps1080pFloor()`，驗證傳入 `800x450` 後 `getInstanceConfig()` 回傳 `1920x1080`。
- 驗證：`cmake --build build --config Release --target test-instance-manager test-virtual-machine test-grpc-framebuffer-capture` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-virtual-machine|test-grpc-framebuffer-capture"` 3/3 PASS。
- 完整驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS。

---

## 2026-06-01 Session 40 — enforce guest/window 1080p floor

### Plan

- [x] 封住 VM guest/window-size 低於 1920x1080 的設定路徑。
- [x] 補 unit test，證明 `VirtualMachine::buildEmulatorArgs()` 不會輸出低解析度 `-window-size`。
- [x] Build + tests，確認 VM args 與 capture floor 都不回歸。
- [x] 更新 lessons / CONTEXT，記錄解析度 floor 不只在 capture 層。

### Review

- `VirtualMachine.cpp` 新增 guest display floor，`applyAvdHardwareConfig()` 會把 `hw.lcd.width/height` clamp 到至少 `1920x1080`。
- emulator 啟動參數 `-window-size` 也改用同一個 guest display floor；即使 config 寫 `800x450`，也會輸出 `1920x1080`。
- `test-virtual-machine` 新增 `emulatorWindowSizeKeeps1080pFloor()`，直接驗證不輸出 `800x450`。
- 驗證：`cmake --build build --config Release --target test-virtual-machine test-grpc-framebuffer-capture` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-grpc-framebuffer-capture"` 2/2 PASS。

---

## 2026-06-01 Session 39 — custom EmuGL build path probe

### Plan

- [x] 檢查 WSL / MinGW 工具鏈是否可用。
- [x] 安裝 WSL 內缺少的 `mingw-w64`，讓 `x86_64-w64-mingw32-gcc` 可用。
- [x] 建立不污染 qemu 子倉庫的 custom runtime build wrapper。
- [x] 用 wrapper 跑 configure/build probe，取得真實 blocker。
- [x] 重跑 host tests，確認 1920x1080 floor 與 runtime gate 不回歸。
- [x] 更新 lessons / CONTEXT，記錄 custom runtime build 的下一個硬缺口。

### Review

- Windows 直接 `bash` 會落到壞的 WSL relay；指定 `wsl -d Ubuntu-24.04` 可用。
- qemu 子倉庫 shell scripts 在 Windows checkout 是 CRLF；直接在 WSL 跑會炸。新增 `scripts/build-chimera-emugl-runtime.ps1 [-AospPrebuiltsDir <path>]`，用 `/tmp` 臨時 copy 並將含 CRLF 的文字檔轉 LF，不改動原始 checkout。
- `mingw-w64` 已安裝到 WSL；`x86_64-w64-mingw32-gcc` 可用。
- build wrapper 會檢查完整 AOSP prebuilts；目前失敗在 `missing AOSP prebuilts: /mnt/d/Workspace_cloud/Personal_Project/chimera/src/prebuilts/gcc`，代表目前 qemu subtree 單獨不足以建 Windows EmuGL runtime。
- 腳本現在用 exit code `3` 明確表示缺 AOSP prebuilts，不會繼續寫 manifest 或誤報 runtime ready。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-grpc-framebuffer-capture"` 2/2 PASS。

---

## 2026-06-01 Session 38 — custom runtime gate for true 1080p/60

### Plan

- [x] 重新盤點目前 1920x1080 floor 與 raw fallback 狀態，不把 gRPC/MMAP/screenrecord 當 60 FPS 完成證據。
- [x] 找出 stock emulator 與 modified EmuGL hook 之間的載入缺口，補可驗證的 custom runtime gate。
- [x] 讓 host 在 shared texture opt-in 時能明確判斷「producer runtime 是否真的可用」，避免誤以為已走 shared texture。
- [x] Build + tests，確認解析度 floor 與 shared texture host path 不回歸。
- [x] 更新 lessons / CONTEXT，記錄這輪距離真 60 FPS 的實際狀態。

### Review

- stock SDK runtime 只有 `libgfxstream_backend.dll`，沒有 legacy `lib64OpenglRender.dll`；因此目前 QEMU subrepo 內的 `ChimeraSharedTextureBridge` 不會被 stock emulator 載入。
- `InstanceManager::probeEmulatorRuntime()` 現在會辨識 stock gfxstream、legacy EmuGL、Chimera manifest，只有 legacy EmuGL + `chimera-emugl-shared-texture.json` 同時存在才視為 shared texture producer runtime ready。
- `--emugl-shared-texture` / `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1` 會設定 `CHIMERA_EMUGL_SHARED_TEXTURE_REQUESTED=1`；啟動 instance 時會 publish `CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_READY/STATUS`。
- 如果 probe 判定 runtime 不支援，host 會跳過 EmuGL shared texture capture，避免 shared texture opt-in 在 stock emulator 上假裝可用；`CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 可把這種情況升級成啟動失敗。
- 新增 `scripts/write-chimera-emugl-runtime-manifest.ps1`：只有 custom runtime 內真的有 `lib64/lib64OpenglRender.dll` 才會寫入 Chimera manifest；對 stock gfxstream runtime 會直接拒絕。
- 驗證：`cmake --build build --config Release --target chimera-ui test-instance-manager test-grpc-framebuffer-capture` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-grpc-framebuffer-capture"` 2/2 PASS；`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS。
- Manifest script 驗證：stock SDK emulator 正確拒絕並回報 `stock gfxstream runtime detected`；fake legacy runtime 可寫出 manifest，且 manifest 包含 `minWidth=1920`、`minHeight=1080`、`targetFps=60`。
- 尚未完成真 60 FPS：這輪把「stock emulator 假 shared texture」堵住，下一步仍需實際 build/接入 custom EmuGL runtime 或 port 到 stock gfxstream backend，再跑 Android dynamic flow。

---

## 2026-05-31 Session 37 — screenrecord regression containment

### Plan

- [x] 確認目前沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` / `adb` 背景程序。
- [x] 確認預設仍是 2 vCPU、guest audio disabled、qemu `below_normal`、Quick Boot/Snapshot opt-in。
- [x] 修正 `CHIMERA_VIDEO_TRANSPORT=screenrecord` 的高頻重啟策略，避免未出第一幀時每 5 秒重啟 adb/ffmpeg。
- [x] 補 adb/ffmpeg stderr tail 到 capture error，讓黑畫面/0 FPS 有可診斷原因。
- [x] Build + unit tests；必要時做短 runtime smoke，確認 1920x1080 floor 與無 orphan process。

### Review

- 回歸風險不是 orphan process；本輪檢查時沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` / `adb`。
- 預設 instance 仍是 `cpus=2`、`enableAudio=false`、`processPriority=below_normal`、`quickBoot=false`。
- 修正 `CHIMERA_VIDEO_TRANSPORT=screenrecord`：ADB-H264 未出第一幀時不再每 5 秒重啟 adb/ffmpeg，改成 30 秒 restart window，且不再並行啟動 raw ADB fallback 增加負載。
- ADB-H264 capture error 現在包含 adb/ffmpeg stderr tail；`ffmpeg -probesize` 從 32 提高到 65536，避免過小 probe 讓 decoder 沒有足夠 SPS/PPS 資料。
- 預設 unary gRPC fallback 不再用 16ms 1080p readback 硬撐，改為 33ms 保守節奏；真 1080p/60 仍必須走 shared texture/custom runtime，不可用 raw readback 佯裝完成。
- 驗證：`cmake --build build --config Release --target chimera-ui test-grpc-framebuffer-capture` 通過；`ctest --test-dir build -C Release --output-on-failure -R test-grpc-framebuffer-capture` 1/1 PASS；`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS。
- `chimera-ui --no-emulator` 短 smoke 強制關閉後沒有 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` 殘留；為避免再次干擾使用者背景音樂，本輪未跑 full Android boot。

---

## 2026-05-31 Session 36 — MMAP regression containment

### Plan

- [x] 釐清為什麼 MMAP 後 FPS/Render 又失真。
- [x] 不使用 1080p full-frame hash，避免把 CPU 成本帶回來造成 host audio 卡頓。
- [x] 將 `CHIMERA_GRPC_TRANSPORT=mmap` 改成 `streamScreenshot` MMAP，使用 emulator stream sequence 判斷真新幀。
- [x] 建置並跑 `test-grpc-framebuffer-capture`。
- [x] 跑短 runtime smoke，確認 1920x1080、guest audio disabled、無 orphan process，並記錄真實 FPS。

### Review

- 回歸點是 unary `getScreenshot` 的 `seq` 固定為 0；拿它判斷新幀會把畫面變化誤判成 duplicate，讓 FPS/Render 指標失真。
- 修正後 MMAP 不再開三條 1080p unary request pipeline；改成單條 `streamScreenshot`，只在 stream sequence 變動時 emit `frameReady()`。
- 沒有引入 full-frame hash；避免每幀掃 1920x1080 像素把 host CPU 壓力帶回來。
- 驗證：`cmake --build build --config Release --target chimera-ui test-grpc-framebuffer-capture` 通過；`ctest --test-dir build -C Release --output-on-failure -R test-grpc-framebuffer-capture` 1/1 PASS。
- Runtime smoke：full boot、`-no-audio`、display `1920x1080 dpi 320`、無殘留 `chimera-ui`/`emulator`/`qemu-system*`；但 stock emulator MMAP stream 只有約 Guest/Stream/Render `12.0 FPS`，不能宣稱 60。
- 結論：MMAP 只能降低 unary polling/resource regression，真 1080p 60+ 仍必須走 modified EmuGL shared texture/custom emulator runtime。

---

## 2026-05-31 Session 35 — audio stutter regression re-fix

### Plan

- [x] 找出為什麼已修過的 host 音樂卡頓又回來。
- [x] 將 Quick Boot 載入與 snapshot 保存都改回明確 opt-in，避免啟動/關閉時隱性 I/O 尖峰。
- [x] 確認 v1 預設仍是 2 vCPU、guest audio disabled、qemu priority 不高於 BelowNormal。
- [x] 補單元測試並跑 Release build / 非 integration tests。
- [x] 更新 `tasks/lessons.md`、`CLAUDE.md`、`AGENTS.md`、`CONTEXT.md`、`docs/project/STATUS.md`。

### Review

- 回歸點是 snapshot 路徑沒有完全關乾淨：boot 後 auto-save 已 opt-in，但 Quick Boot load 仍預設啟用，且 `VirtualMachine::stop()` 在 quickBoot=true 時仍會同步保存 snapshot。
- `CHIMERA_QUICK_BOOT=1` 才載入 `chimera_quickboot`；預設 full boot，避免壞 snapshot 或 snapshot I/O 在啟動時干擾 host 音訊。
- `CHIMERA_SAVE_QUICK_BOOT=1` 才保存 snapshot；當時預設 stop 只送 `adb emu kill`，不再同步做 `avd snapshot save`。Session 69 已再收緊：一般 stop / true verifier cleanup 不再送 `adb emu kill`。
- v1 啟動仍維持 2 vCPU、`below_normal` priority + EcoQoS、guest audio disabled、gRPC capture 等 Android boot completed。
- 驗證：`cmake --build build --config Release --target chimera-ui test-virtual-machine test-process-launcher` 通過；`ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-process-launcher"` 2/2 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS；`chimera-ui --no-emulator` smoke 後無 Chimera/qemu/emulator 殘留。
- 尚未完成：這只處理 host audio 回歸；真 1920x1080 dynamic 60+ 仍要接 custom emulator shared texture runtime。

---

## 2026-05-31 Session 34 — EmuGL shared texture runtime opt-in wiring

### Plan

- [x] 不把 stock emulator 預設切到 shared texture，避免未載入 modified EmuGL 時造成卡頓或輸入回歸。
- [x] 加入 `CHIMERA_EMULATOR_PATH` override，讓 cross-build 出的 custom emulator 不必覆蓋官方 SDK 目錄。
- [x] 新增 host opt-in：`--emugl-shared-texture` / `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1`。
- [x] 自動產生並同步 `CHIMERA_D3D11_TEXTURE_*` 與 `CHIMERA_EMUGL_D3D11_TEXTURE_*` env，讓 host consumer 與 modified EmuGL producer 使用同一組 metadata/event/name。
- [x] 即使 shared texture opt-in，gRPC fallback 仍保留 input activity boost，不讓 input/capture pacing 因 shared texture 尚未出幀而退化。
- [x] 建置 host，跑 opt-in UI smoke。

### Review

- `main.cpp` 現在會在 emulator launch 前處理 `--emugl-shared-texture` / `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1`，產生 `Local\ChimeraEmuglD3D11Meta_<pid>` / texture / event 名稱並寫入 host + EmuGL env。
- `InstanceManager` 會優先使用 `CHIMERA_EMULATOR_PATH`，並把該 emulator 旁的 `lib64/`、`lib/`、本體目錄 prepend 到 `PATH`，支援 SDK-like custom emulator layout。
- 若使用者已提供 `CHIMERA_D3D11_TEXTURE_METADATA` 或 `CHIMERA_EMUGL_D3D11_TEXTURE_METADATA`，host 會沿用既有名稱並補齊另一側 env。
- `grpcCaptureForInput` 現在只要有 unary gRPC fallback 就會設置，不再因 shared texture capture object 存在而失去 input activity boost。
- 驗證：`cmake --build build --config Release --target chimera-ui test-process-launcher test-instance-manager` 通過；`chimera-ui --no-emulator --emugl-shared-texture` smoke 確認 opt-in log 出現且沒有 qemu/emulator 殘留。
- 尚未完成：仍需 custom emulator / modified EmuGL runtime 真的載入 `ChimeraSharedTextureBridge`，才能用 Android 通知欄/滾輪/遊戲 flow 驗證真 1080p 60+。

---

## 2026-05-31 Session 33 — host audio stutter regression containment

### Plan

- [x] 檢查「原本已修好的背景音樂卡頓又回來」是否由啟動 priority、snapshot 保存或 capture retry 回歸造成。
- [x] 讓 emulator process 在 `ResumeThread` 前就套用目標 priority，避免啟動最前段仍以 Normal priority 搶 host audio。
- [x] 將 Quick Boot snapshot 自動保存改為 opt-in，避免每次 boot completed 後 30s 都觸發磁碟/CPU 尖峰。
- [x] 讓 shared D3D11 metadata mapping 尚未存在時安靜重試，不把實驗 shared texture env 變成錯誤洪水。
- [x] 補 `test-process-launcher` 驗證 initial priority，並跑完整非 integration 測試與短 UI smoke。

### Review

- 回歸主因是資源防線沒有套在「啟動第一瞬間」：原本 emulator 先 resume，再等 host 拿到 handle 後才降 priority；現在 `ProcessLauncher::runAsync()` 在 suspended child resume 前就套 `BELOW_NORMAL_PRIORITY_CLASS` / EcoQoS。
- Quick Boot 仍可載入 `chimera_quickboot` 降低 cold boot，但保存 snapshot 不再是預設背景工作；只有 `CHIMERA_SAVE_QUICK_BOOT=1` 才會在 boot 後延遲保存。
- `SharedD3D11TextureCapture` 對 `OpenFileMappingW` 的 `ERROR_FILE_NOT_FOUND` 不再發 `captureError`，避免 opt-in shared texture producer 尚未就緒時洗 UI/log。
- 驗證：`chimera-ui` / `test-shared-d3d11-texture-capture` / `test-grpc-framebuffer-capture` / `test-process-launcher` build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS；`chimera-ui --no-emulator` 短 smoke 後沒有 qemu/emulator/chimera-ui 殘留。
- 尚未完成：這輪是修主機音訊回歸與啟動干擾，不是宣稱 Android dynamic flow 已達真 1080p 60+。

---

## 2026-05-31 Session 32 — EmuGL shared texture bridge hook

### Plan

- [x] 在 QEMU/EmuGL `FrameBuffer::post()` 找到實際 guest color buffer 發布點。
- [x] 新增 `ChimeraSharedTextureBridge`，從 EmuGL color buffer texture 嘗試發布到 named D3D11 shared texture。
- [x] 成功發布 shared texture 時跳過 `m_onPost` 的 `ColorBuffer::readback()`，避免 GPU path 仍被 CPU readback 拖垮。
- [x] 將 bridge 加入 `libOpenglRender/Android.mk`，Windows host build 連結 `d3d11/dxgi`。
- [x] 跑 host build / unit tests / diff check。

### Review

- `ColorBuffer` 新增 `getTextureName()`，讓 bridge 可拿到 guest color buffer 的 GL texture name。
- `FrameBuffer::post()` 現在會在 sub-window 與 headless/no-subwindow 路徑都嘗試 `ChimeraSharedTextureBridge::publish()`。
- Bridge 使用 `eglCreatePbufferFromClientBuffer` / `EGL_ANGLE_d3d_share_handle_client_buffer` 將 named D3D11 shared texture 包成 EGL surface，再用既有 `TextureDraw` 將 guest texture 畫入 shared texture。
- Bridge 讀取 `CHIMERA_EMUGL_D3D11_TEXTURE_METADATA/NAME/EVENT`，並 fallback 到 host 既有 `CHIMERA_D3D11_TEXTURE_METADATA/NAME/EVENT`；texture name 未指定時用目前 process id 產生，host 會從 metadata 得到真實 texture name。
- 驗證：host `chimera-ui` / producer / shared texture / gRPC tests build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS；1920x1080/60 producer smoke 退出碼 0。
- 這仍需自建/接入 custom emulator 才會生效；stock Android SDK emulator 不會載入此 repo 的 modified `libOpenglRender`。
- 尚未完成：還沒做 custom emulator runtime 驗證，因此不能宣稱 Android 動態畫面已達真 1080p 60+。

---

## 2026-05-31 Session 31 — reusable D3D11 shared texture producer

### Plan

- [x] 不再把測試 helper 當一次性 producer；抽出正式 `SharedD3D11TexturePublisher`，作為 Android/emulator 端接入 shared texture 的 producer contract。
- [x] 保留 1920x1080 預設與 capture floor，不用縮圖或 raw screenshot 當 60 FPS 證據。
- [x] 將 `shared_d3d11_texture_producer` 改用正式 publisher，避免測試 helper 與未來 emulator bridge 各自維護一份 metadata / shared handle 寫法。
- [x] 讓既有 `SharedD3D11TextureCapture` 單元測試直接驗證 publisher 產生的 named texture / metadata 可被 consumer 打開。
- [x] 跑完整 Release build / unit tests / diff check。

### Review

- 新增 `SharedD3D11TexturePublisher`：建立 named D3D11 texture、metadata mapping、optional frame event，並用 odd/even sequence 發布 frame metadata。
- Publisher 支援 `publishColor()` 作為 60Hz smoke source，也支援 `publishTexture(void*)`，供下一步 emulator/custom display bridge 把 D3D11 texture copy 到 shared texture。
- `shared_d3d11_texture_producer` 預設改為 1920x1080 / 60fps，已不再內建 1280x720 預設。
- 初步驗證：`shared_d3d11_texture_producer --width 1920 --height 1080 --fps 60 --seconds 1` 退出碼 0；`test-shared-d3d11-texture-capture` 3/3 PASS。
- 完整驗證：`cmake --build build --config Release --target chimera-ui shared_d3d11_texture_producer test-shared-d3d11-texture-capture test-grpc-framebuffer-capture` 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS。
- 尚未完成：Android/emulator 端尚未把 `FrameBuffer::post()` / `ColorBuffer::post()` 的實際 guest frame 接到 publisher；真 1080p 60+ 仍未完成。

---

## 2026-05-27 Session 30 — audio stutter regression guard / 1080p fallback containment

### Plan

- [x] 檢查使用者回報的「啟動後背景音樂卡頓/雜音」是否由 1080p raw fallback、native embed 或 MMAP 實驗路徑回歸造成。
- [x] 將 `streamScreenshot` MMAP 路徑修正為 top-down copy，並移除 1080p full-frame hash；但維持 opt-in，不當預設。
- [x] 明確拒絕 native Win32 embed 作預設：attach log 成功不代表可用，實測仍會黑畫面/工具列外漏。
- [x] 保留 1920x1080 capture floor，不用降解析度掩蓋效能問題。
- [x] 降低 host audio 被搶占風險：預設 Quick Boot、qemu `below_normal`、Windows EcoQoS、guest audio disabled、gRPC idle cadence 降到 250ms。
- [x] 建置 `chimera-ui` / gRPC / process launcher 相關 target，跑完整 unit tests。

### Review

- 回歸主因不是 UI 字串，而是把 1080p CPU screenshot fallback 與實驗顯示路徑推得太靠前；已把 MMAP 改為 `CHIMERA_GRPC_TRANSPORT=mmap` 才啟用，預設回到 unary gRPC fallback。
- `ProcessLauncher` 現在對 `below_normal` / `idle` process 套用 Windows power throttling，讓 emulator/qemu 更不容易搶 foreground browser/audio thread。
- `main.cpp` 預設 `CHIMERA_QUICK_BOOT` 啟用，只有 `CHIMERA_QUICK_BOOT=0` 才 full boot；Session 33 已把 boot completed 後自動保存 `chimera_quickboot` 改為 `CHIMERA_SAVE_QUICK_BOOT=1` opt-in。
- `GrpcFramebufferCapture` 靜止 duplicate capture cadence 從 50ms 降到 250ms，且啟動時不再先用 2s interactive cadence 忙打 1080p request。
- 短 smoke 驗證過 qemu priority 為 BelowNormal、guest audio log 為 `disabled by default (-no-audio)`、guest size 仍是 1920x1080；為避免再次干擾使用者背景音樂，未做更長時間 runtime 壓測。
- 驗證：`cmake --build build --config Release --target chimera-ui test-process-launcher test-grpc-framebuffer-capture` 通過；`test-process-launcher` 15/15 PASS；`test-grpc-framebuffer-capture` 4/4 PASS；`ctest --test-dir build -C Release --output-on-failure -LE integration` 19/19 PASS。
- 注意：這輪是回歸止血與主機資源保護；真正 1080p 60+ 仍需 Android/emulator 端 shared D3D11 texture producer，不能宣稱完成。

---

## 2026-05-27 Session 29 — enforce 1080p floor, no hidden downscale

### Plan

- [x] 找出仍偷偷把 gRPC capture 預設降到 800x450 的程式碼與文件。
- [x] 在 `GrpcFramebufferCapture` 層建立 1920x1080 最低解析度防線，env var 設小也會被 clamp 回 1080p。
- [x] 新增 unit test 驗證 800x450 request 會提升到 1920x1080，且 full-resolution request encoding 正確。
- [x] 用 1920x1080 D3D11 shared texture producer 跑 runtime smoke，驗證不是靠降解析度拿 60fps。
- [x] 更新交接文件，明確禁止用低於 1920x1080 的 capture 當預設或驗證捷徑。

### Review

- `GrpcFramebufferCapture::normalizedCaptureSize()` 現在最低只接受 1920x1080；`main.cpp` 預設與 `CHIMERA_CAPTURE_WIDTH/HEIGHT` 都會通過同一個 clamp。
- `test-grpc-framebuffer-capture` 覆蓋解析度 floor 與 gRPC image request 的 1920x1080 encoding。
- Runtime shared texture smoke 使用 `shared_d3d11_texture_producer --width 1920 --height 1080 --fps 60`，結果為 `Guest: 59.9 FPS | Stream: 59.9 FPS | Render: 59.9 FPS | Avg: 16.3ms | Dup: 0`。
- `ctest --test-dir build -C Release --output-on-failure -LE integration` 為 19/19 PASS。
- 注意：這輪修掉「偷降解析度」與證明 host shared texture path 可在 1080p 接近 60；Android/emulator 端 shared texture producer 仍是下一個硬缺口，尚未宣稱真機通知欄/遊戲 flow 完成。

---

## 2026-05-27 Session 28 — shared memory/shared D3D11 texture renderer

### Plan

- [x] 將 `GuestDisplay` 從 `QQuickPaintedItem` paint path 改成 Qt scene graph texture node。
- [x] 新增 CPU-copy shared-memory framebuffer backend，先用 seqlock ABI 驗證 frame delivery。
- [x] 新增 D3D11 shared texture metadata backend，讓 producer 可用 named shared texture 對接。
- [x] 讓 `GuestDisplay` 在 D3D11 RHI 下用 `OpenSharedResourceByName` + `QSGD3D11Texture::fromNative()` 渲染。
- [x] D3D11 RHI 下的 CPU frame fallback 改用 persistent texture + `UpdateSubresource()`，避免每幀重建 GPU texture。
- [x] 將 D3D11 metadata capture 從 UI thread QTimer 改為 worker thread 等待 frame event，避免 UI event loop 卡住 frame delivery。
- [x] 新增 runtime helper producer，使用 named D3D11 shared texture 驗證 host shared texture path 可真實 60Hz。
- [x] 保留 gRPC fallback；shared-memory/shared-texture 沒出第一幀時不可造成永久黑畫面。
- [x] Release build 與 unit tests 驗證。

### Review

- 新增 `SharedMemoryFrameAbi.h`、`SharedMemoryFramebufferCapture`、`SharedD3D11TextureCapture`。
- `GuestDisplay` 現在支援三種 texture path：D3D11 persistent upload、CPU `QImage` fallback、named D3D11 shared texture native render。
- `SharedD3D11TextureCapture` 只在 even sequence 新 frame 時計入 Stream，不再用重複 metadata tick 灌水；capture worker 直接等 Win32 frame event。
- 新增 `shared_d3d11_texture_producer` helper，GPU 端用 `ClearRenderTargetView` 固定節拍更新 named texture。
- `test-shared-d3d11-texture-capture` 會真的建立 named D3D11 shared texture，並用另一個 D3D11 device 透過名稱打開，避免只測 metadata 字串。
- 驗證：`cmake --build build --config Release --target chimera-ui test-shared-memory-framebuffer-capture test-shared-d3d11-texture-capture` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 18/18 PASS。
- Runtime shared texture smoke：`shared_d3d11_texture_producer` + `chimera-ui --no-emulator` 實測 `Guest: 59.6 FPS | Stream: 59.6 FPS | Render: 59.6 FPS | Avg: 16.1ms | Dup: 0`，結束後無殘留程序。
- 注意：host renderer / metadata capture 已就緒；Android/emulator 端真正 producer 尚未接入，因此還不能宣稱遊戲/通知欄實測已穩定 1080p 60 FPS。

---

## 2026-05-26 Session 27 — honest FPS + wheel/notification drag smoothness

### Plan

- [x] 移除 host title bar 左上角灰色副標，保留白色 `CHIMERA` Logo。
- [x] 將側欄 FPS 改成更保守的有效 FPS，不再用 Stream delivery 假裝互動流暢。
- [x] 修正 wheel 與 notification shade drag：避免 gRPC touch 事件洪峰與重疊 swipe。
- [x] 維持 800x450 raw capture 預設，避免用 1024x576/1080p `getScreenshot` 硬推造成更嚴重掉幀。
- [x] Build + unit tests + runtime smoke 驗證，並同步 lessons / handoff 文件。

### Review

- 依 BlueStacks 類方向請子代理研究後，結論一致：短期要先做到 hardware acceleration、frame pacing、低延遲 input 與誠實 telemetry；真 1080p/60+ 不能靠 raw `getScreenshot`，需 shared memory/shared texture/GPU capture。
- 主側欄 FPS 改為有效 FPS：`min(Guest, Stream, Render)`，靜止畫面或 duplicate frame 不再用 Stream 60+ 假裝流暢。
- 左上角灰色副標已移除；Host shell / HUD / sidebar 主要可見字串改為繁體中文。
- 滾輪路徑繼續走 emulator gRPC touch，不回退 ADB shell；wheel throttle 從 8ms 拉到 16ms，instant swipe 從 4 個 touch request 降到 3 個，降低高頻滾輪洪峰。
- gRPC duplicate idle cadence 改為約 50ms；有輸入時才喚回較高 cadence。duplicate frame 不送 QML repaint。
- 1024x576 與 sampled fingerprint 已驗證不可靠：sampled fingerprint 會低估內容變化，1024x576 raw path 仍不夠穩。預設保留 800x450，full fingerprint 用於誠實 Guest FPS。
- 驗證：`scripts\build-chimera-launcher.ps1` 通過；`cmake --build build --config Release --target chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：Android `wm size=1920x1080`、`wm density=320`；qemu priority 未高於 Normal；結束後無 `chimera-ui` / `qemu-system*` 殘留。
- Runtime Perf 證據：短版通知欄/滑動 flow 顯示 Stream 可到 `61.3 FPS`，但 Guest/Render 只有 `8.9 FPS`；較長 flow 最高也只有 Guest `13.9 FPS` / Render `12.9 FPS`。所以本輪沒有宣稱真 60，已把 UI 改成會顯示這個低值。
- 下一個真正 60 FPS phase：新增 shared memory/shared texture capture + scene graph texture renderer，並用 notification shade / wheel scroll / app switch 三個動態 flow 驗證 Guest/Render/visible latency。

---

## 2026-05-25 Session 26 — wheel/input jank + stream headroom

### Plan

- [x] 重跑 runtime smoke，確認 host audio mitigation 後是否仍有 app 切換掉幀。
- [x] 釐清滑鼠滾輪卡頓路徑，移除高成本 ADB shell wheel fallback。
- [x] 調整 gRPC raw capture 預設，保留 1920x1080 guest/input，同時降低 host 傳輸/CPU 尖峰。
- [x] Build + unit tests + runtime smoke 驗證功能、FPS、priority 與無 orphan process。
- [x] 同步更新 lessons / CONTEXT / CLAUDE / AGENTS / STATUS。

### Review

- 滾輪根因已確認：舊路徑每次 wheel 都跑 `adb shell input swipe`，會造成 shell spawn 與 Android input queue 抖動。現在優先走 emulator gRPC `sendTouchSwipe()`，並以 12ms throttle 合併高頻 wheel 事件；只有沒有 gRPC 時才 fallback ADB。
- 預設 gRPC raw capture 從 1024x576/960x540/896x504 收斂到 800x450；Android guest、座標與 UI viewport 仍維持 1920x1080/320dpi。
- qemu/emulator priority 預設維持 `Normal`，不再用 High；BelowNormal 會降低 host 搶占，但連續 app switch 實測會掉到 41-46 FPS，因此不作預設。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke 通過：Google Play / 檔案管理 / 瀏覽器 / 設定皆能開啟，Home 無 TMobile / duplicate Settings / disabled tiles，Stream samples `62.6, 62.4, 62.6, 63.0, 62.7, 62.9, 62.8, 62.2`，min 62.2、avg 62.6。
- 收尾確認無 `chimera-ui` / `qemu-system*` 殘留。

---

## 2026-05-25 Session 25 — host audio stutter mitigation

### Plan

- [x] 盤點 emulator 啟動期造成 host 音樂卡頓/雜音的資源搶占來源。
- [x] 降低 qemu/emulator 對 host audio thread 的排程干擾。
- [x] 延後 gRPC capture 到 Android `sys.boot_completed=1` 後，避免開機期間 CPU/IO 雙重搶占。
- [x] Build + unit tests + runtime smoke 驗證 app、FPS、process priority 與無 orphan process。
- [x] 更新 `tasks/lessons.md` 與交接文件。

### Review

- 預設 vCPU 由 4 降到 2；process priority 維持 `normal`，不再拉到 High。`below_normal` 可降低 host 搶占，但後續 app switch smoke 掉到 41-46 FPS，已改回 Normal 作預設。
- `enableAudio=false` 時不再額外掛 `virtio-snd-pci`，避免 `-no-audio` 旁邊又建立 guest sound device。
- gRPC screen capture 現在等 Android boot completed 後才啟動；開機前不再用 screenshot stream 跟 emulator boot 搶 CPU/IO。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke 後續以 `normal` priority + 800x450 capture 重驗通過，細節見 Session 26。
- 收尾確認無 `chimera-ui` / `qemu-system*` 殘留。

---

## 2026-05-25 Session 24 — fix Home app false positives

### Plan

- [x] 修正固定入口灰色不可用：檔案管理 / 瀏覽器不可再只顯示假圖示。
- [x] 移除 Home 動態掃描塞回來的系統垃圾與重複 Settings。
- [x] 保留 Google Play 新安裝 App 會出現在 Home 的行為，但只列使用者安裝 app。
- [x] 重建 Launcher APK，跑 runtime smoke 驗證首頁 XML 與四個入口點擊。
- [x] 更新 `tasks/lessons.md` 與交接文件。

### Review

- 新增 `BrowserActivity` / `FileManagerActivity` 作為內建 fallback；Chrome / Material Files 不存在時固定入口仍可點，不再灰掉。
- `queryLaunchableApps()` 只追加非 system / 非 updated-system app，並過濾固定入口與 `com.tmobile*`、Settings、DocumentsUI。
- Runtime smoke：`home_has_tmobile=false`、`home_has_duplicate_settings=false`、`disabled_tiles=[]`。
- Runtime tap：Google Play → `com.android.vending`；檔案管理 → `com.chimera.launcher/.FileManagerActivity`；瀏覽器 → `com.chimera.launcher/.BrowserActivity`；設定 → `com.android.settings`。
- Stream samples：`62.8, 61.2, 60.8, 64.6, 62.2, 62.4, 62.8, 62.4`，min 60.8、avg 62.4。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS；結束後無 `chimera-ui` / `qemu-system*` 殘留。

---

## 2026-05-25 Session 23 — app provisioning + compact FPS/fullscreen + custom title bar

### Plan

- [x] 盤點目前 AVD/system image、已安裝 package、Launcher resolver 狀態，確認 Google Play / 檔案管理 / 瀏覽器缺失根因。
- [x] 讓必要 app 可用：優先用 Play Store system image 或可驗證的系統/開源 APK；避免安裝不可信來源 APK。
- [x] 修正滑動卡頓：量測 Stream/Render/Dup 與 UI repaint，針對實際瓶頸調整。
- [x] 調整側欄：移除 top bar 連線 pill，把全螢幕移到 FPS 卡右側，縮小 FPS 卡。
- [x] 調整視窗標題列：改用自繪深色 title bar，Logo 移入 title bar，釋放原本 header 空間。
- [x] Build + unit tests + runtime smoke 驗證 app 可點、FPS/滑動、UI 排版與無 orphan process。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Play Store system image 已啟用，Material Files 由 `third_party/android-apps/material-files.apk` 自動安裝，Chrome 使用 Play image 內既有安裝。
- Chimera Launcher 改為固定四入口置頂，並追加所有 `ACTION_MAIN` / `CATEGORY_LAUNCHER` app；Google Play 新安裝的可啟動 app 會出現在 Home。
- 固定入口 runtime smoke 通過：Google Play → `com.android.vending`、檔案管理 → `me.zhanghai.android.files`、瀏覽器 → `com.android.chrome`、設定 → `com.android.settings`。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Stream 穩態仍可維持約 60 FPS；app 切換期間可見短暫 50fps 左右尖刺，完整遊戲級鎖 60 仍需 shared texture / GPU capture。

---

## 2026-05-24 Session 22 — more emulator space + required apps + 60 FPS

### Plan

- [x] 盤點 host header、launcher padding、package list 與目前 stream FPS 現況。
- [x] 縮小 host 頂欄與側欄占用，讓模擬器 viewport 取得更多畫面空間。
- [x] 調整 Android Home：縮窄上方黑邊、保留 status bar、只展示必要 app 入口。
- [x] 開機後精簡/停用多餘 launcher app；保留 Play、檔案管理、瀏覽器、設定。
- [x] 調整 capture 目標與指標，驗證 stream 穩定 60+。
- [x] Build + unit tests + runtime smoke 驗證。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Host shell compact pass：外框 margin 16→10、頂欄 62→46px、側欄 204→190px，讓同視窗尺寸下 viewport 得到更多可用空間。
- Android HOME 改為固定四個入口：Google Play、檔案管理、瀏覽器、設定；不再列舉所有 launchable app，因此 TMobile 等多餘圖示不會出現在首頁。不存在的 app 會保留入口但停用，不假裝可開。
- Launcher theme 移除 fullscreen，Android status bar 常駐；ADB screenshot 驗證上方厚黑邊消失，狀態列時間/圖示可見。
- gRPC duplicate frame 仍不送 QML repaint，但 idle capture cadence 維持 16ms，讓主側欄單一 Stream FPS 在靜止首頁也反映穩定 60Hz delivery；Guest/Render/Dup 細節仍在 log/HUD。
- Launcher APK build + sign verify 通過；Release build 通過；unit tests 16/16 PASS。
- Runtime smoke 通過：`wm size=1920x1080`、`wm density=320`、`policy_control=immersive.navigation=*`、HOME top activity 是 `com.chimera.launcher`、UI tree 包含四個必要入口且不含 TMobile、tap `設定` 後 foreground 進入 `com.android.settings`。
- 穩態 FPS smoke 通過：boot 後等待 35 秒，Stream FPS 樣本 `61.9, 62.7, 63.1, 63.2, 62.4`，最低 61.9、平均 62.7。

---

## 2026-05-24 Session 21 — black screen + simplified sidebar

### Plan

- [x] 診斷目前黑屏：確認 top activity、launcher crash/logcat、ADB screenshot 與 host stream 是否一致。
- [x] 修正 Chimera Launcher 黑屏 / HOME 啟動穩定性。
- [x] 將側邊欄效能卡簡化成乾淨單一 FPS 顯示，不塞 stream/latency/duplicate 細節。
- [x] 整理側邊欄按鈕排版，保留可用主操作並避免佔用太多空間。
- [x] Build + unit tests + runtime smoke 驗證黑屏、HOME、按鈕與 FPS UI。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- 修正 launcher 黑屏風險：移除 Activity 啟動時強制 immersive hide system bars，改成正常可見的深色 HOME；無 app 時顯示 `No launchable apps`，不會只剩空黑畫面。
- Host install flow 在設 HOME 後新增 explicit `am start -n com.chimera.launcher/.MainActivity`，不只依賴 HOME resolver。
- 側邊欄效能卡改為 64px 高的單一 FPS 顯示；移除 Guest/Stream/Latency/Dup 小字堆疊。
- 主側欄操作精簡為導航、鎖鼠標、鍵位配置、截圖、錄影、安裝 APK、應用程式、剪貼簿同步、設定；移除主頁上較少用且佔位的 OBB/檔案/GPS/感應器/多開/巨集等入口。
- Launcher APK build + sign verify 通過；Release build 通過；unit tests 16/16 PASS。
- Runtime smoke 通過：`wm size=1920x1080`、`wm density=320`、top activity 是 `com.chimera.launcher`，UI tree 包含 `CHIMERA` / `Apps`，ADB screenshot 非黑屏且顯示 Settings / TMobile app icon。
- 點 launcher 中 Settings icon 後 foreground 進入 `com.android.settings`，確認 HOME app 可點。

---

## 2026-05-24 Session 20 — truthful FPS + lower overhead + clean launcher

### Plan

- [x] 盤點 PerformanceMonitor / gRPC frame metadata，確認 FPS 虛報來源。
- [x] 將前端顯示 FPS 改為真實新 guest frame / rendered frame 指標，避免重複畫面被算成 60 FPS。
- [x] 降低 capture 開銷：減少靜止畫面重複 repaint / log 誤導，保留互動時 60 FPS。
- [x] 建立乾淨 Chimera Android Launcher，取代 Pixel Launcher 首頁與多餘 Google 元件。
- [x] 開機後自動安裝/設為 HOME，並驗證首頁乾淨、可點、效能數據同步。
- [x] Build + unit tests + runtime smoke 驗證。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- FPS 虛報根因：舊 `PerfMonitor.fps` 計算的是 capture/presentation loop，不是 Android guest 新內容。現在 `fps/guestFps` 只計 content-changing frame，另有 `streamFps`、`renderFps`、`duplicateRate`。
- gRPC capture 現在會對 raw frame 做 fingerprint；重複畫面只更新 stream metric，不再送進 `GuestDisplay::setFrame()` 觸發 QML repaint。連續重複後 idle capture 退到 100ms，輸入或內容變更後回到 16ms 互動頻率。
- 新增 `tools/chimera-launcher/` Android HOME launcher 與 `scripts/build-chimera-launcher.ps1`；host 在 boot completed 後自動安裝 `build/launcher/chimera-launcher.apk`、設為 HOME、啟動 HOME。
- Launcher APK build 通過並用 `apksigner verify` 驗證；過程中補裝 Android `build-tools;34.0.0`。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Runtime smoke 通過：`wm size=1920x1080`、`wm density=320`、`pm path com.chimera.launcher` 存在、HOME top activity 為 `com.chimera.launcher`、啟動 Settings 後 foreground 進入 `com.android.settings`、log 有 `[Perf] Guest/Stream/Render` 分離指標。
- 靜止首頁 smoke 顯示 `Guest: 0.0 FPS`、`Stream: ~10 FPS`、`Dup: 100%`，代表靜止畫面不再謊報 60 FPS，也不再每幀重繪。

---

## 2026-05-24 Session 19 — 1080p stream + clickable guest input

### Plan

- [x] 盤點目前 gRPC stream、GuestDisplay 座標映射、InputBridge 點擊注入路徑。
- [x] 將預設 Android guest / capture / input logical size 升到 1920x1080 landscape。
- [x] 修正普通滑鼠點擊卡在首頁無反應：不要讓 emulator console mouse 假成功吃掉 tap。
- [x] Build + unit tests 驗證。
- [x] Runtime smoke 驗證：1080p、FPS、ADB tap 後畫面/guest state 有變化、無 orphan process。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Android guest / input logical size 已升到 1920x1080 landscape / 320 dpi；settings preset 也新增 1080p。
- 1080p raw gRPC capture 實測只有 15-32 FPS；1280x720 raw 約 35-59 FPS。預設改為 1024x576 raw capture + smooth scaling，runtime 62-67 FPS、0 dropped。
- `EmulatorGrpcInput` 新增 `sendTouch`；普通滑鼠左鍵與 QML touch 優先走 emulator gRPC touchscreen，不再依賴 Console `event mouse`。
- Runtime gRPC touch smoke：點 Settings 後 `dumpsys activity/window` 看到 `com.android.settings`，確認 guest 真有收到點擊。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- 清理後無 Chimera 啟動的 `chimera-ui` / `emulator` / `qemu-system*`；本機仍有一個非本 repo 的 `LINE_playstore_x86_64` qemu 行程，未處理。

---

## 2026-05-22 Session 18 — 60 FPS display path + landscape system adaptation

### Plan

- [x] 確認 1-2 FPS 根因是否來自 ADB fallback / 錯誤 native embed 路徑。
- [x] 回退 native emulator embed 為 opt-in；預設維持 headless gRPC streaming。
- [x] 關閉預設 ADB display fallback，避免 1 FPS screencap 蓋掉 gRPC。
- [x] 將 AVD hardware config 與 guest boot settings 固定為 1280x720 landscape / 240 dpi / 60Hz。
- [x] 修正 full boot 後停在空鎖定/載入畫面：自動 wake / dismiss keyguard / HOME。
- [x] Build + unit tests 驗證。
- [x] Runtime smoke 驗證 FPS、方向、ADB `wm size` / rotation 狀態與 orphan cleanup。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- native embed 實測仍會黑畫面並讓 Android Emulator 工具列外漏；已回退為 `--native-embed` opt-in。
- 預設顯示路徑為 headless gRPC；ADB raw screencap fallback 只在 `--adb-display-fallback` 時啟用，避免 1 FPS fallback。
- Quick Boot snapshot 在本輪造成 ADB offline / 空畫面風險；預設改為 full boot，`CHIMERA_QUICK_BOOT=1` 才啟用 snapshot。
- Runtime clean full boot 驗證：`sys.boot_completed=1`，`wm size=1280x720`，`wm density=240`，ADB screenshot 為正常橫向 Home。
- gRPC runtime log 穩定 61-65 FPS；未看到 native attach、ADB fallback 或 Qt cache warning spam。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。

---

## 2026-05-22 Session 17 — Quick Boot runtime verifier

### Plan

- [x] 新增可重跑的 Quick Boot runtime smoke script，取代手寫 PowerShell 片段。
- [x] 腳本驗證：full/snapshot 啟動達 `sys.boot_completed=1`、保存 `chimera_quickboot`、結束後無 orphan process。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。
- [x] 最終跑 `git diff --check` 與狀態盤點。

### Review

- 新增 `scripts/verify-quick-boot.ps1`，自動 full boot 建立 `chimera_quickboot` snapshot，再以 Quick Boot 重啟並檢查門檻。
- 最終腳本驗證通過：full boot 66.7s；Quick Boot 9.7s；threshold 25s；結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-05-22 Session 16 — Quick Boot fallback hardening

### Plan

- [x] 檢查 Quick Boot snapshot 失效時目前只能手動 `CHIMERA_QUICK_BOOT=0` 回退。
- [x] 加入 snapshot 啟動早退自動 full-boot retry，降低壞 snapshot 卡住啟動的風險。
- [x] Build + unit tests 驗證。
- [x] Runtime smoke 驗證：既有 `chimera_quickboot` snapshot 啟動仍可達 boot complete。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Runtime smoke 通過：既有 `chimera_quickboot` snapshot 啟動 12s 達 `sys.boot_completed=1`。
- smoke 結束後 snapshot save 成功，且無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-05-22 Session 15 — Quick Boot snapshot path

### Plan

- [x] 確認 `VirtualMachine` 目前固定使用 `-no-snapshot`，導致每次 full boot。
- [x] 依 Android Emulator 官方 Quick Boot 行為改用可控 `quickBoot` 設定。
- [x] 抽出 `VirtualMachine::buildEmulatorArgs()` 並新增 snapshot 參數單元測試。
- [x] Build + unit tests 驗證。
- [x] 可行時做 runtime snapshot save / relaunch 啟動時間驗證。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Runtime quick-boot 驗證通過：第一次啟動達 `sys.boot_completed=1` 為 44s；保存 `chimera_quickboot` snapshot 後第二次啟動為 10s。
- 驗證結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-05-22 Session 14 — Job Object hardening + gRPC cleanup

### Plan

- [x] 盤點 Session 13 未驗證項，優先處理會造成整機卡死的 orphan qemu 防護。
- [x] 補上 `ProcessLauncher::runAsync()` Job Object 建立、設定、assign、resume 的 warning log。
- [x] 清理 `GrpcFramebufferCapture` temporary diagnostics 與過時註解。
- [x] Build + unit tests 驗證；可行時再做 force-kill orphan runtime 驗證。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md` 的交接紀錄。

### Review

- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Force-kill 驗證通過：`chimera-ui.exe` 啟動後出現 `emulator` + `qemu-system*`，強制結束 host 後兩者皆消失，未留下 orphan。
- `chimera-debug.log` 未出現 `ProcessLauncher` Job Object warning，表示本機 assign/resume path 正常。

---

## 2026-05-21 Session 13 — gRPC 60fps 穩定 + orphan qemu 根因修復

### 已驗證修復

- [x] **gRPC 擷取解析度下調至 640×360**：`main.cpp` `grpcCaptureWidth/Height` 改為 640/360。
  原 1280×720 每幀 2.76MB；640×360 僅 0.69MB（4× 頻寬降低）。直接 gRPC/grpcurl 驗證 emulator 確實會 server-side downscale。
- [x] **gRPC pipeline stall 不 abort**：`GrpcFramebufferCapture::restartPipeline()` 改為只補 slot、不 abort in-flight。
  先前 abort 全部 + re-prime depth 會造成 thundering herd → capture 永久崩到 ~5fps。
- [x] **幀率穩定 60fps / 0 dropped**：clean launch（先殺所有 stale qemu/emulator）後持續 60–68 fps，
  dropped=0，平均幀時間 ~15ms。長跑 8000+ 幀仍穩定。
- [x] **優先級 Normal**：emulator/qemu 由 High → Normal，避免搶佔主機音訊執行緒。

### 驗證數據

```
[Perf] FPS: 62.1 | Avg: 15.5ms | Max: 42.0ms | Dropped: 0 / 8074
```

### 未驗證（待測）

- [x] **ProcessLauncher Job Object**：`runAsync()` 已改 `CREATE_SUSPENDED` → `AssignProcessToJobObject(job, pi.hProcess)` → `ResumeThread()`。
  目的：chimera-ui crash / force-kill 時 Windows 自動殺掉整個 emulator+qemu tree，避免 orphan。
- [x] **Force-kill orphan 測試**：build 後 launch → force-kill `chimera-ui.exe` → 確認 qemu-system-x86_64-headless.exe 同時消失。
- [x] **Job Object 失敗警告**：`AssignProcessToJobObject()` 若失敗（nested job 等）會發 warning log。

### 待清理

- [x] `[GrpcDiag]` temporary diagnostics（`GrpcFramebufferCapture.cpp` 的 `static int s_diag`）移除或 downgrade。
- [x] `GrpcFramebufferCapture.h` stale comment（restartPipeline 仍說會 abort in-flight）。
- [x] `ProcessLauncher.cpp` Job Object comments 在驗證後精簡。

### Review

- gRPC 效能問題已根治：thundering herd + busy-polling + full-res readback 三項同時修正。
- orphan qemu 是整機卡死的**主因之一**（stale ~2.7GB + fresh launch = 雙 VM 搶 RAM/CPU）。
- Job Object force-kill runtime 驗證已於 Session 14 補齊，orphan qemu 路徑可標 resolved。

---

## 2026-05-21 Cleanup / Commit Hygiene

### Plan

- [x] 盤點 `git status`、ignored/untracked 檔案與文件現況。
- [x] 確認 `src/host/input/EmulatorGrpcInput.*` 與既有 modified 檔案屬未提交開發成果，不納入垃圾清理。
- [x] 確認 `.gitignore` 已涵蓋大型 binary、debug logs、R&D scripts、runtime/build outputs、BlueStacks reverse-engineering binaries。
- [x] 刪除可重建且不需提交的 R&D/output 垃圾：`out/`、root ISO/QCOW2/installer、QEMU/debug logs、runtime 空資料夾、錯誤路徑殘留。
- [x] 保留本機開發仍可能需要的 `build/`、`third_party/android-sdk/`、`third_party/android-avd/`、`third_party/ffmpeg/`。
- [x] 精簡同步 `AGENTS.md`、`CLAUDE.md`、`CONTEXT.md` 的版控衛生紀錄。
- [x] 驗證：`git status --short`、`git ls-files --others --exclude-standard`、`git ls-files -oi --exclude-standard` 摘要、CMake build/test。

### Review

- 清理後 ignored 摘要只剩 `build/` 與 `third_party/` 快取；未追蹤檔只剩 `src/host/input/EmulatorGrpcInput.*` 與本任務的 `tasks/todo.md`。
- 未刪除 Android SDK/AVD/FFmpeg/build cache，避免破壞目前開發與驗證環境。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 15/15 PASS。

---

## 2026-06-01 Session 50 — Stock emulator window-capture rollback + audio guard

### Plan

- [x] 實測 Window D3D11 capture，確認是否能替代 raw gRPC 1080p readback。
- [x] 修正「多開原生 Android Emulator 視窗」：`--window-capture` 預設拒絕，需 `--allow-unsafe-window-capture` 才能進入實驗路徑。
- [x] 關閉不合格 fallback：strict GPU capture 不再自動退回 raw gRPC/ADB。
- [x] 修正 host audio 回歸風險：Eco mode 恢復時只回 `BelowNormal`，並套整棵 emulator/qemu process tree；ADB H.264 診斷 helper 套低優先級與 backoff。
- [x] Build、unit tests、no-emulator gate smoke 驗證。

### Review

- Window D3D11 實測不合格：依賴 stock emulator HWND，失敗後曾退回 gRPC raw readback，且會露出原生 emulator 視窗；此路徑已降為 unsafe 實驗，不可作為正式方案。
- 正式方向維持：Chimera 自有 UI + headless Android backend；真 1080p/60 仍需 custom EmuGL/shared texture runtime，不能靠 stock emulator 可見視窗或 raw screenshot 達標。
- 驗證通過：`cmake --build build --config Release --target chimera-ui`、`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS、`--no-emulator --window-capture` 只警告且未啟動 emulator/qemu。

---

## 2026-06-02 Session 51 — Classic EmuGL runtime executable probe

### Plan

- [x] 確認 `build\chimera-emugl-runtime` 是否產出 custom `emulator.exe`、`emulator64-x86.exe`、legacy EmuGL DLL set 與 manifest。
- [x] 補齊 MinGW runtime DLL 打包，避免 custom executable 在 Windows 以 `0xC0000135` 啟動前失敗。
- [x] 讓 `verify-true-1080p60.ps1` 預設鎖定 custom runtime path，避免要求 EmuGL 卻誤測 stock SDK emulator。
- [x] 依 runtime probe 切到 classic-compatible emulator args，避免對 classic executable 傳 `-grpc`、`-window-size`、`-vsync-rate` 等 stock-only 參數。
- [x] 短 runtime smoke 驗證 custom classic runtime 與 Android 34 Play Store AVD 是否可啟動。
- [x] Build/test 驗證並確認無 emulator/qemu 殘留。

### Review

- `scripts\build-chimera-emugl-runtime.ps1` 現在會產出 classic runtime executable、完整 legacy EmuGL DLL set、MinGW runtime DLL 與 manifest；`emulator64-x86.exe -help` 已可正常輸出 help。
- `InstanceManager` 偵測到 Chimera EmuGL runtime 後會設 `useClassicEmuglRuntime=true` 並關閉 emulator `-grpc` CLI；`VirtualMachine` 對 classic runtime 只傳相容參數。
- 短 runtime smoke 未達標：custom classic runtime 對現有 Android 34 `google_apis_playstore/x86_64` AVD 失敗，原因是 classic emulator 期待 `kernel-qemu`，而現代 image 只有 `kernel-ranchu`；直接指定 `kernel-ranchu` 也失敗，錯誤為 `Can't find 'Linux version ' string in kernel image file`。
- 因此 classic EmuGL runtime 只能算 build/probe artifact，不是可跑 Play Store guest 的 production runtime；真 1080p/60 下一步必須改做 modern ranchu/gfxstream runtime 的 shared texture bridge，不能回頭用 stock emulator HWND。
- 驗證通過：PowerShell parser、manifest writer、`verify-true-1080p60.ps1 -ParseOnlyLog`、`emulator64-x86.exe -help`、`cmake --build build --config Release --target test-virtual-machine test-instance-manager chimera-ui`、`ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-instance-manager"`。

---

## 2026-06-05 Session 58 — Unsafe native window gate hardening

### Plan

- [x] 盤點可能外露 stock Android Emulator 視窗的啟動路徑。
- [x] 讓 `CHIMERA_ENABLE_NATIVE_EMBED` / `CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW` / `CHIMERA_ENABLE_WINDOW_CAPTURE` / `CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE` 不再能啟用 unsafe display。
- [x] 讓 `verify-true-1080p60.ps1` 啟動前清掉 legacy unsafe display env，避免 verifier 帶出第二個原生 emulator 視窗。
- [x] Build、unit tests、parser、no-emulator unsafe env gate smoke 驗證。

### Review

- 正式路徑仍是 Chimera 單一視窗 + headless Android backend；unsafe native/window capture 只能由同一次 CLI 明確傳入診斷旗標。
- 已驗證即使 unsafe env 全設為 `1`，`chimera-ui --no-emulator` 只記 warning，不會啟動 `emulator.exe` / `qemu-system*`。

---

## 2026-06-05 Session 59 — SDK gfxstream proxy runtime probe

### Plan

- [x] 建立 stock SDK ABI 相容的 `libgfxstream_backend.dll` proxy runtime，不覆蓋官方 SDK 目錄。
- [x] 驗證 proxy runtime 可由 Chimera headless 路徑 boot 到 Android，且維持 1920x1080 / 320 dpi。
- [x] 加入低頻 hook log，確認 proxy DLL 被載入並定位 SDK 36.5.11 的初始化入口。
- [x] 加強 snapshot-save guard，避免一般啟動/關閉時產生 snapshot I/O 尖峰。

### Review

- `scripts\build-chimera-gfxstream-proxy-runtime.ps1` 會輸出 `build\chimera-gfxstream-proxy-runtime`，保存 stock backend 為 `libgfxstream_backend_stock.dll`，proxy DLL 保留 348 個 export。
- proxy boot smoke：`sys.boot_completed=1`，display `1920x1080 / 320 dpi`，log 沒有 `Saving snapshot`，結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。
- hook log 已證明 SDK 36.5.11 會載入 proxy 並呼叫 `initLibrary`、`android_setOpenglesRenderer`；`stream_renderer_flush` 在 boot smoke 不是活躍入口。
- 嘗試 vtable patch Renderer `setPostCallback` 會讓 boot 流程提早結束，已撤回，不留作正式路徑。
- `emu-main-dev` source layout 與現有 patch 不相容；`build-chimera-gfxstream-runtime.ps1 -SkipConfigure` 現在會在 patch 失敗時正確 fail-closed。

---

## 2026-06-06 Session 62 — Headless proxy rollback after native window regression

### Plan

- [x] 回答架構策略：不從零重寫完整 Android VM；保留 Android Emulator/QEMU/ranchu/gfxstream 相容核心，但正式產品只允許 Chimera 單一視窗。
- [x] 重建 `build\chimera-gfxstream-proxy-runtime`，保留 stock SDK ABI 相容性，不覆蓋官方 SDK 目錄。
- [x] 撤回高風險 `RenderLib` C++ wrapper，避免依賴 stock `libgfxstream_backend.dll` 未匯出的內部 C++ 符號。
- [x] 短 smoke 驗證 headless：emulator/qemu command line 必須含 `-no-window`，且 process 結束後不得殘留。

### Review

- proxy runtime build PASS：348 exports，hook 維持在 C export 層（`stream_renderer_flush`、`android_setPostCallback`）。
- headless smoke PASS：`emulator.exe` 與 `qemu-system-x86_64-headless.exe` 均帶 `-no-window -no-audio`，`visible_risk_count=0`，`leftover_count=0`。
- `RenderLib` C++ wrapper 已撤回：它會觸發 `FeatureSet` copy constructor unresolved symbol，代表跨 stock DLL 的 C++ ABI 包裝不穩定，不可硬推。
- 這輪只修正「多開原生 emulator 視窗」與 proxy build 風險；true 1080p/60 還是要靠 custom gfxstream shared texture producer 驗證。

---

## 2026-06-12 Session 66 — Headless-only low-interference runtime gate

### Plan

- [x] 回答架構邊界：不從零重寫完整 Android VM；保留 Android 相容核心，但產品面只允許 Chimera 單一視窗。
- [x] 派子代理分別檢查 host audio/resource policy 與 gfxstream display producer 接線點。
- [x] 關閉預設 raw gRPC/MMAP/screenrecord/ADB capture fallback；raw fallback 只能由 CLI `--allow-raw-capture-fallback` 明確啟用。
- [x] 刪除不合格 gfxstream runtime 的 stale manifest，避免舊 manifest 讓 runtime 被假標成 ready。
- [x] 讓 legacy `QemuBackend` 預設低干擾：2 vCPU、2048MB、hidden launch、startup `Idle`，暖機後 `BelowNormal`。
- [x] 修正 `ChimeraWindow` 與 `GuestDisplay` 雙重輸入轉發；輸入只由 `GuestDisplay` 做 guest 座標映射後送出。
- [x] 更新 gfxstream patch script，讓 headless Vulkan display-post 在 bridge enabled 且無 surface 時仍能進 `recordCopy()` / `publishFrame()`。
- [x] Build、unit tests、fail-closed smoke、true-1080p60 verifier 驗證。

### Review

- 不合格 runtime 現在正確 fail-closed：`verify-true-1080p60.ps1` 失敗於 `incompatible gfxstream runtime ABI; required screen background export is missing`，未退回 raw capture，也未殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。
- 已修掉一個會讓滾輪/點擊更卡的 host bug：`ChimeraWindow` 不再直接轉發 mouse/wheel/key，避免同一事件被 window 座標與 guest 座標送兩次。
- 已降低背景音樂干擾風險：raw 1080p screenshot fallback 不再由 env 偷開；legacy backend 也套 startup Idle / BelowNormal policy。
- 驗證通過：`chimera-ui` build、targeted tests 5/5 PASS、完整 non-integration `ctest` 20/20 PASS、fail-closed smoke exit 3 且無殘留 process。
- true Android 1080p/60 仍未達標；下一步是讓 source-patched gfxstream runtime 補齊 SDK 36 ABI/imports，並跑到 verifier PASS。
