# Chimera Lessons

## 2026-06-19 — `BELOW_NORMAL` 與 EcoQoS / memory priority 必須分開建模

- `BELOW_NORMAL_PRIORITY_CLASS` 的目的是把 emulator/qemu 的**排程優先級**降到低於前景 app，這通常已足夠保護 host audio。
- `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`（EcoQoS）是另一個維度：它會進一步壓低 CPU frequency / execution speed。把它跟 `BELOW_NORMAL` 綁在一起，會讓 guest render throughput 大幅下降（本輪 triangle demo 約 7-9 FPS → 22-25 FPS 的差異就是這裡）。
- 但 `ProcessMemoryPriority=MEMORY_PRIORITY_LOW` 仍然對背景 emulator 有價值；不要因為移除 EcoQoS 就順手把 low memory priority 也拿掉。
- **Rule**：資源策略至少拆成三個概念獨立思考／測試：`PriorityClass`、`ProcessMemoryPriority`、`ProcessPowerThrottling`。若需求是「BELOW_NORMAL 保持效能、IDLE 才進 EcoQoS」，實作上必須用不同旗標，不可共用單一 `lowInterference` 布林。
- `test_process_launcher.cpp` 不能只驗 `GetPriorityClass()`；要直接用 `GetProcessInformation()` 驗證 `ProcessMemoryPriority` 與 `ProcessPowerThrottling`，否則 reviewer 指出的回歸不會被測到。

## 2026-06-18 — 不可把長期總目標當成已完成事實

- 使用者的長期目標可以是「完成一切，打造出一個完整的模擬器對標 bluestacks」，但**沒有 transcript / log / build / verifier 證據前，不能把這句話改寫成已完成結論**。
- 若 hook 或使用者指出 `insufficient evidence in transcript`，代表回報把「目標」誤寫成「已證實結果」。
- **Rule**：所有完成宣告都必須綁定可核對證據（例如 PASS log、verifier 結果、build/test 輸出、實際 runtime 行為），沒有證據就只能描述成「目標」「方向」或「尚未完成」。

## 2026-06-18 — Vulkan ICD 測試不能配 `-gpu swiftshader_indirect`

- `emuglConfig_setupEnv()` 會在 `swiftshader_indirect` 模式下強制 `ANDROID_EMU_VK_ICD=swiftshader`（`tmp/gfxstream-src/host/gl/gl-host-common/opengl/emugl_config.cpp:398-404`）。
- 這會在 gfxstream 初始化前覆寫任何手動設的 `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` / 自製 `nvidia_icd.json` 測試條件，讓所謂的「NVIDIA Vulkan 測試」其實跑成 SwiftShader loader path。
- 症狀是 `VkEmulation::create step 5: got 4 instance exts` + `vkCreateInstance res=-9`；把同一支 harness 改成 `-gpu host` 後，立刻翻成 `got 20 instance exts` + `vkCreateInstance res=0` + `vkCreateDevice res=0`。
- **Rule**：凡是驗證 host/NVIDIA Vulkan loader、ICD discovery、`VK_ICD_FILENAMES`、自製 ICD JSON 等情境，不能用 `swiftshader_indirect`；這次三支 harness 改成 `-gpu host` 後即可正確避免該污染。

## 2026-06-17 — AdbH264 screenrecord 在 headless emulator 完全無效

- `adb exec-out screenrecord --output-format=h264 --size 1920x1080 --bit-rate 24000000 -` 在 headless QEMU Android Emulator（`-no-window`）下 **0 bytes** 輸出，無任何 stderr error。
- 根本原因：Android `screenrecord` 走 SurfaceFlinger MediaRecorder API 捕捉 DisplayDevice 畫面；headless mode 下 emulator 的 virtual display 不建立 Android-visible framebuffer surface，SurfaceFlinger 無可抓取的 layer。
- Qt `setStandardOutputProcess` OS-level pipe 本身沒有問題（之前懷疑的方向是錯的）；問題在 data 根本沒有進 pipe。
- `CHIMERA_VIDEO_TRANSPORT=screenrecord` 路徑在 stock headless QEMU emulator 上永久無效，**不應再嘗試**。
- **正確方向**：唯一可行的 60 FPS path 仍是 matching SDK gfxstream shared texture runtime；gRPC unary 是 4-17 FPS 的可用 fallback。

## 2026-06-17 — emulator IDLE priority 比 BELOW_NORMAL 更不干擾 host audio

- 即使 `enableAudio: false`（emulator 不用 WASAPI）+ BELOW_NORMAL + power throttling，WHPX VCPU 的 VM exit 處理在 kernel DPC level 執行，仍可能造成 host audio thread stall。
- IDLE priority class 確保 OS 永遠讓音樂播放器（normal priority）先跑；combined with `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` 進一步限制 CPU frequency/execution。
- 16-core Ryzen 5950X 即使以 IDLE priority 跑 emulator，仍有充足 idle CPU 給 emulator 正常運行（Android boot ≤ 60s 未受影響）。
- **Rule**：背景運行的模擬器 `processPriority` 預設用 `"idle"`；只有在 user 明確要求最佳 emulator 效能時才升為 `"below_normal"`。

## 2026-06-17 — gRPC capture 被錯誤分類為 diagnostic fallback 的回歸

- gRPC `getScreenshot` 是 stock Android Emulator 的合法 display path（4-17 FPS，1920x1080 unary polling）。在某輪修改中被改成「diagnostic raw fallback」並要求 `--allow-raw-capture-fallback` CLI flag，結果所有 stock emulator 用戶完全沒有顯示。
- **正確判斷**：gRPC 在沒有 shared D3D11 texture path 時是預設 display；shared texture 啟動後 signal 切換。條件應是 `!sharedTextureCapture`，不是 `allowRawCaptureFallback`。
- `allowRawCaptureFallback` 只應控制 MMAP / screenrecord / ADB 這些非 gRPC 的診斷 fallback，不應一刀切關掉 gRPC。

## 2026-06-17 — GrpcOnly verifier FPS 不能用 effective = min(guest, stream, render)

- gRPC unary `getScreenshot` 在 1920x1080 每幀約 250ms，pipeline depth=3，理論上限約 12 FPS。
- 靜止畫面所有 gRPC 回傳皆為 duplicate → `guestFps=0` → `effective=min(0,...)=0`，即使 display 正常運作也失敗。
- Boot 階段的 `stream=0.0` 樣本（gRPC 還沒啟動）如果沒有過濾，會拖低平均值到門檻以下。
- **正確 gate**：
  1. 過濾掉 `stream=0.0` 的 boot 樣本（只看 `stream > 0` 的 active 樣本）。
  2. `avgStreamFps >= 3.0`（證明 pipeline 有持續送幀）。
  3. `maxGuestFps >= 1.0`（exercise 期間有真實內容變化被捕獲）。
- 不設 `maxDup <= 5%`（靜止 Home 畫面本來就 100% dup，不代表 display broken）。

## 2026-06-17 — ctypes 在 64-bit Windows 的 HANDLE/LPVOID restype 陷阱

- `ctypes.windll.kernel32` 所有函式的預設回傳型別是 `c_long`（32-bit signed int）。
- 在 64-bit 進程中，`OpenEventA`、`OpenFileMappingA`、`MapViewOfFile` 等回傳 HANDLE 或 LPVOID 的函式，若不設 `restype = ctypes.c_void_p`，高於 2GB 的地址會被截斷為 garbage → `from_address()` 存取無效指標 → Python 自身 AV crash（exit code -1073741819）。
- **修法**：呼叫前設 `.restype = ctypes.c_void_p`；同時設 `.argtypes = [ctypes.c_void_p]` 給 `UnmapViewOfFile`、`CloseHandle` 等接受 HANDLE/LPVOID 的函式，防止符號被截斷傳入。
- `except Exception` 攔不住 ctypes AV：`from_address()` 的 segfault 在 Python 外部觸發，不走 Python 例外機制，無法被 try/except 捕獲。

## 2026-06-17 — AVD multiinstance.lock 在 crash 後不會自動清除

- `multiinstance.lock` 是 0-byte 的 advisory lock 檔案，不是 OS-managed named object；進程崩潰時不會被 OS 刪除。
- 下輪執行前必須：(1) `Get-Process -Name qemu*,emulator*` 確認全部消失，(2) `cmd /c "del /f /q multiinstance.lock"` 強制刪除（PowerShell Remove-Item 加 -ErrorAction SilentlyContinue 可能靜默失敗）。
- Win32 named mapping/event（`CreateFileMappingA`/`CreateEventA`）在 emulator 進程退出時才消失；host 側 `OpenEventA`/`OpenFileMappingA` 必須在 kill emulator 前執行，否則回傳 null。

## 2026-06-16 — std::promise/future 在 MSVC CRT 不相容環境下完全不可用

- `std::promise<void>::~promise` 和 `_Associated_state::_Set_value` 都在 MSVCP140.dll 內 null dereference（fault_addr=0x0），原因是 SDK emulator 捆綁 v14.28、系統裝了 v14.44，兩個版本的 `_Associated_state` 內部 mutex 偏移不一致。
- **不要用 `std::promise<void>` 或 `std::future<void>` 做 gfxstream frame sync**；改用 `gfxstream::base::Lock + ConditionVariable`（純 Win32 SRWLOCK + CONDITION_VARIABLE），完全不依賴 MSVCP140。
- 修法模式：把 `bool done=false; Lock L; CondVar CV;` 放在 caller stack，lambda 在 callback 裡 `{AutoLock lk(L); done=true;} CV.signal()`，caller 用 `{AutoLock lk(L); CV.wait(&lk, [&]{return done;});}` 等待。headless 路徑（`!CallbackScheduledOrFired()`）直接 proceed，不需 signal。
- DbgHelp.dll Python ctypes 用法：`SymSetOptions(SYMOPT_UNDNAME|SYMOPT_LOAD_LINES)`（0x2|0x10，不含 DEFERRED_LOADS/DEBUG）；`Name` 欄位是 flexible array，`SizeOfStruct=88`（fixed），額外 buffer 另外分配；用 WinDbg 版 dbghelp.dll（`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\dbghelp.dll`），不用系統版。
- 崩潰 offset 在 rebuild 後全部位移；重新 symbolize 前要先確認 log 裡的 offset 是哪個 build 的，避免拿錯 PDB 對應。

## 2026-06-13 — gfxstream ABI 不相容必須實測而非假設

- "Build ID 不一致 = ABI 不相容" 只是 gate 邏輯，不代表實際已測。實測方法：加 bypass env var、複製 patched DLL、跑 verifier，觀察是否 crash。
- `sdk-release` (13278158) DLL 在 SDK emulator 15261927 下：export 解析成功、gfxstream backend 啟動，但 Vulkan bridge 初始化時 crash（struct layout 不符 → AV）。這是 Vulkan-level ABI mismatch，比 export-level mismatch 更難繞過。
- `emu-main-dev` build 缺 SDK imports（libandroid-emu-agents/protos/metrics.dll），根本無法在 SDK emulator 的 lib64/ 環境下載入，必須先查 import list 再嘗試。
- R&D bypass flag 要加在 InstanceManager 和 manifest writer 兩層，且要透過 verifier 的 savedEnv 正確管理，避免殘留汙染後續測試。

## 2026-06-13 — verify script 新增模式要同步更新 parse-only 分支

- `ParseOnlyLog` 分支用同一套 assert 函式：新增 `-GrpcOnly` 這類模式切換時，parse-only 與 live run 都要走新分支，否則離線 log 驗證會使用舊規則。
- verify gate 的「require」與「reject」條件是互斥的：gRPC mode 要求看到 "Starting .+ screen capture stream"，同時拒絕 D3D11；shared texture mode 相反。兩個 assert 函式不可共用同一組 pattern，避免誤判。
- 合成 log 測試要同時跑 pass case 和 fail case：只驗 pass 容易漏掉 false positive；must-reject 條件也要有負向測試覆蓋。

## 2026-06-13 — std::unique_ptr 回傳值不可用 C 假簽名轉發

- `gfxstream::RenderLibPtr` 是 `std::unique_ptr<RenderLib>`；在 x64 Windows，`unique_ptr` 以 RAX 返回（單指標），而 `std::shared_ptr` 以 RCX 隱藏指標返回。兩者都不能用 `void*(void*)` 的 C 假簽名包裝，否則 RCX 被誤用或 stack layout 出錯，導致 `-1073741819 (AV)`。
- 任何 non-trivial C++ return type（`std::unique_ptr`、`std::shared_ptr`、含 destructor 的 struct）跨越 C/C++ ABI 邊界時，必須用 exact C++ signature `extern "C" __declspec(dllexport) TheType func()`；不可用 `void*` 替代。
- MSVC C4190 warning（`extern "C"` 回傳 `unique_ptr` 不相容 C）是正確的提醒，但不是錯誤；`#pragma warning(suppress: 4190)` 可以壓掉，ABI 是正確的。
- Log analyzer pattern 必須跟著 instrumentation 一起演進：instrumentation 改了 log 格式（如 `forward name=initLibrary`），analyzer 要同步新增 pattern，否則 false negative 掩蓋真實狀態。

## 2026-06-13 — proxy log 分析也要 fail-closed

- stock-ABI gfxstream proxy 的 log 不能靠人工印象判讀；要用 `scripts\analyze-gfxstream-proxy-log.ps1` 分類 init、renderer、GPU resource/display、CPU readback 訊號。
- 只有 1920x1080 `stream_renderer_flush` / `stream_renderer_resource_create` / `gfxstream_backend_setup_window` 這類 GPU-side 訊號可作為下一步 shared texture hook 候選；`android_onPost`、`getScreenshot`、`transfer_read_iov` 都是 CPU/readback 風險，不可拿來證明 60 FPS。
- attach-only log 只證明 proxy DLL 被載入；沒有 renderer init 或 GPU display/resource signal 時，必須 fail closed。
- 子代理若因額度或工具問題失敗，不可把 delegation 當研究完成；交接要明確寫「沒有可採用結論」。

## 2026-06-13 — gfxstream bridge 要能證明卡在哪裡

- source-patched gfxstream bridge 不能只在 host probe 顯示 ready；runtime 端也要有低頻證據：bridge enabled、`recordCopy()` 是否被呼叫、`ensureInitialized()` 為何不可用、`publishFrame()` 是否失敗。
- 診斷 log 必須低頻，建議只在第 1、60、每 240 次記錄；高頻 per-frame log 會把 host audio 卡頓問題帶回來。
- 1920x1080 floor 不能只放在 host capture/publisher；runtime producer bridge 也要拒絕低於 1920x1080 的 extent，避免低解析度 runtime 冒充 60 FPS。
- mixed ABI runtime 即使 C++ 編譯成功，也不可啟動驗證；`gfxstreamSourceSnapBuildId` 與 SDK emulator `Pkg.BuildId` 不一致時必須 fail closed，避免黑屏、多開原生 Emulator 或 `0xC0000005`。
- 架構回答要穩定：不要從零重寫完整 Android VM；要 fork/patch Android Emulator/QEMU/gfxstream 作 headless 相容核心，使用者面只允許 Chimera 單一視窗。

## 2026-06-13 — snapshot shutdown I/O 不能從 verifier 或 stop() 偷跑

- `-no-snapshot` / `-no-snapshot-save` 不夠防守時，要在預設 full boot 同時加 `-no-snapstorage`，讓 emulator 沒有 `default_boot` snapshot storage 可自動使用。
- 一般停止路徑不要送 `adb emu kill`；它可能讓 emulator 自己走 shutdown snapshot/I/O，造成背景音樂卡頓或雜音。只有明確 `CHIMERA_SAVE_QUICK_BOOT=1` 的 Quick Boot 維護流程才允許 snapshot save + console kill。
- runtime verifier 的 cleanup 也會影響使用者主機；true-1080p60 verifier 不可為了優雅關機而觸發 emulator console kill，應用 bounded process cleanup 並確認沒有殘留 qemu/emulator。
- 架構回答要一致：Chimera 不是完整自研 Android VM；短期保留 Android Emulator/QEMU/gfxstream 相容核心，但正式路徑必須 headless，不能外露或多開原生 Android Emulator 視窗。

## 2026-06-13 — strict shared texture 不能黑屏靜默等待

- `CHIMERA_REQUIRE_*_SHARED_TEXTURE=1` 代表正式 gate；Android boot 後若 shared texture capture 沒配置、metadata mapping 沒出現、或第一幀沒來，必須 bounded fail closed，不可停在黑屏/0 FPS 讓使用者以為卡住。
- verifier 不能只檢查 runtime ready；還要拒絕 `Required shared texture capture ...` 這類 fail-closed 訊息，並要求 `Shared D3D11 texture display capture started` 與動態 `CHIMERA_PERF effective >= 60`。
- 目前 SDK emulator 36.5.11 build id 是 `15261927`，本機 `sdk-release` gfxstream source snapshot 是 `13278158`，`emu-36-1-release` 是 `12579432`；兩者都不能當 matching runtime 來源。
- invalid runtime fail-closed 是必要保護，不是完成證據。真 1080p/60 仍要 matching gfxstream `DisplayVk::postImpl()` / display-post GPU shared texture producer。
- 若暫時找不到 matching source，只能在 stock ABI proxy 內加低風險 C export probe，觀察自然呼叫的 resource lifecycle；不可包 C++ `RenderLib` / `Renderer`，不可主動 map/read/export handle 來找畫面。proxy metadata 必須維持 `sharedTextureProducer=false`，避免把 probe 當正式 60 FPS producer。

## 2026-06-12 — 可見原生 Emulator 不能只靠 env 放行

- `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 只能表示診斷授權，不能單獨讓正式路徑外露原生 Android Emulator 視窗；主程式必須在同次 CLI 明確要求 unsafe diagnostics 時，才設定內部 session gate。
- `InstanceManager` normalize 層要同時檢查外部 unsafe allowance 與內部 diagnostics session；否則任何 `allowVisibleEmulatorWindow=true` 都要回到 headless / `-no-window`。
- 不要再讓 `CHIMERA_EMULATOR_START_VISIBLE` 這類環境變數影響正式路徑；可見啟動只能屬於本次明確 unsafe diagnostics。

## 2026-06-12 — 低干擾 policy 不能只套正式 VM launcher

- 使用者回報背景音樂卡頓時，不只檢查 emulator/qemu priority；所有直接 `QProcess` 啟動的 adb/ffmpeg 旁路都可能繞過 `ProcessLauncher`，包含 boot poll、support app install、QML 控制面板、raw screencap fallback、ScreenRecorder。
- 這類 helper process 要共用 `LowInterferenceProcess`：BelowNormal priority、low memory priority、power throttling、ignore timer resolution。不要在各檔案手寫不同版本。
- raw fallback 即使 opt-in 診斷也不可高頻重啟；舊 ADB raw stream 250ms restart 會放大 host contention，需拉長 backoff。
- MMAP/gRPC fallback 若已拿到 RGBA8888 frame，應直接 publish D3D11 shared texture；不要每幀建立 QImage 再交給 UI thread upload。fallback smoke 若只有 30 FPS 左右，必須明確標為未達標。

## 2026-06-12 — gfxstream source build id 必須對齊 SDK emulator build id

- standalone gfxstream backend 可 `LoadLibrary`、有 Chimera marker、SDK imports、ABI export，仍可能在 SDK emulator 36.5.11 初始化時 `0xC0000005`；只靠 marker/export/import 不足以證明 ABI 可用。
- `chimera-gfxstream-shared-texture.json` 必須記錄 `gfxstreamSourceSnapBuildId` 與 `baseEmulatorBuildId`，且兩者要相等；manifest writer 和 host probe 都要 fail closed。
- stock SDK 36.5.11 proxy 可以 boot，但 headless path 沒看到 `android_setPostCallback`；包 `RenderLib` / `Renderer` C++ object 會 crash。`stream_renderer_flush` 加 resource-info probe 後仍在 headless boot/dynamic statusbar flow 中 0 次呼叫，不可把它當 active display hook。正式路徑仍是 matching source 的 `DisplayVk::postImpl()` GPU post hook。

## 2026-06-12 — standalone gfxstream DLL 不可只看 export/marker

- `libgfxstream_backend.dll` 有 Chimera marker、`initLibrary`、`gfxstream_backend_set_screen_background` 仍不代表能替換 SDK Emulator 36.5.11；若缺 `libandroid-emu-agents.dll`、`libandroid-emu-protos.dll`、`libandroid-emu-metrics.dll` 這些 SDK runtime imports，實測會停在 gfxstream feature 初始化，沒有 ADB/console/gRPC。
- 這類 runtime 必須在 manifest writer 與 host probe 階段 fail closed；不可讓它進入正式啟動後黑屏、吃資源或外露原生 emulator 視窗。
- `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` / `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` 只能是本機 probe；只看到 `initLibrary result=wrapped` 不代表可用 producer，也不能拿 CPU `android_setPostCallback` readback 當 1080p/60 解法。
- `VirtualMachine` 背景監控 thread 不可直接讀寫非 atomic state；監控 emulator/qemu process tree 時，VM state 要 thread-safe，避免 fail-closed 路徑本身不穩。

## 2026-06-06 — RenderLib/Renderer wrapper 不可預設啟用

- stock SDK 36.5.11 proxy 可 hook plain C `initLibrary`，但把回傳的 `RenderLib` 包成 C++ wrapper 會影響 boot 穩定性；預設必須 forward 原物件。
- `android_setOpenglesRenderer` 收到的 `std::shared_ptr<Renderer>` 可以被 opt-in 包裝做實驗，但實測 `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` 會讓 emulator 早退，不可當正式接線點。
- proxy runtime 的 baseline 驗證要看 hidden/no-audio boot completed 與 leftover cleanup；只看到 DLL attach 或 initLibrary log 不代表可用。
- stock DLL 未 export `RendererImpl::setPostCallback` 這類內部 method；下一步 shared texture producer 要避開整個 shared_ptr 替換與直接 C++ symbol hook，改找 display-post/source-patch 或更穩定 ABI 接點。

## 2026-06-06 — headless 必須驗證沒有可見 HWND

- 不可只相信 `-no-window`、`CREATE_NO_WINDOW` 或 `SW_HIDE`；正式 emulator 啟動後要檢查 emulator/qemu process tree 是否外露可見 Win32 視窗。
- 若 headless 路徑出現 `Android Emulator - ...` 原生視窗，正確行為是 fail closed 並終止整棵 emulator tree，不是讓使用者手動關掉或嘗試用 UI 蓋住。
- 這個 watchdog 是產品邊界：Chimera 可以沿用 Android Emulator/QEMU/gfxstream 相容層，但正式使用者面只能有 Chimera 視窗。

## 2026-06-05 — unsafe 可視化 Emulator 視窗要雙重鎖

- `--native-embed` / `--window-capture` 這類參數即使標成 unsafe，也不能單獨打開 stock Android Emulator 視窗；必須再有 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 這種內部診斷鎖。
- `InstanceManager` normalize 層也要擋 visible window，否則舊設定或非 UI 程式路徑仍可能把 `allowVisibleEmulatorWindow=true` 傳進 `VirtualMachine`。
- 產品名稱與視窗標題不要再寫 `Android Emulator`；底層可沿用 Android 相容 runtime，但使用者面只能看到 Chimera。

## 2026-06-04 — AOSP gfxstream build 要可重建

- modified gfxstream runtime 的 source/deps/build 需固定在 ignored `tmp\aosp` / `tmp\aosp-build`，不要手改 `third_party/android-sdk/emulator`。
- upstream gfxstream/AEMU Windows CMake 偏 clang/GNU extension；MSVC 相容修正要全部寫進 `scripts/apply-chimera-gfxstream-patch.ps1`，包含 `/Wno-*`、`include_next`、GNU attributes/atomics、designated initializer、compound literal、`__PRETTY_FUNCTION__`、同名 struct/function `offsetof`。
- `build\chimera-gfxstream-runtime` 必須是完整 runtime scaffold：先複製 SDK emulator 目錄，再覆蓋 modified `lib64\libgfxstream_backend.dll`，否則 manifest writer 有 DLL 但沒有可執行 runtime。
- `libgfxstream_backend.dll` build 成功只代表 artifact ready；仍要檢查 binary marker、manifest、`emulator.exe -help`，並確認沒有殘留 emulator/qemu。
- 這條路線不是完整自研 VM；是 fork/改 Android Emulator + gfxstream runtime，正式產品仍必須 headless 且只顯示 Chimera 單一視窗。
- SDK 36.5.11 stock gfxstream proxy smoke 顯示 `stream_renderer_flush` 不是 boot path 活躍入口；實際初始化會走 `initLibrary` / `android_setOpenglesRenderer`。shared texture 接線不要只盯 `stream_renderer_flush`，要沿 RenderLib/Renderer object 或 source patch 的 post path 找。
- `emu-main-dev` 的 gfxstream source layout 已移除舊 `host/gl/DisplayGl.cpp` 路徑；現有 Chimera patch 不能假設所有 branch 都有同一套 GL display 檔案。`build-chimera-gfxstream-runtime.ps1 -SkipConfigure` 必須在 patch script 失敗時 fail-closed，不能回報 patched success。
- headless 不能只信 `-no-window`；正式路徑還要在 `CreateProcessW` 啟動時用 hidden window policy，避免 emulator GUI 或 toolbar 短暫外露。
- resource policy 失敗 warning 不可高頻刷 log；相同 pid/error 記一次即可，否則 log I/O 會把音訊卡頓問題帶回來。

## 2026-06-03 — 不要把自研 VM 和自研 runtime producer 混為一談

- 完整重寫 Android 模擬器代表重做 WHPX/QEMU/ranchu kernel/virtio/gfxstream/Play image 相容/ADB/snapshot/input/audio；這不是短期修 1080p/60 與 host audio 干擾的合理路徑。
- 正確方向是保留 Android/QEMU 相容層，但替換會造成瓶頸或產品破壞的部分：host shell、headless runtime、display producer、input path、process/resource policy。
- stock emulator HWND/window capture 不是正式顯示路徑；即使能看到畫面，也會漏出原生工具列、破壞產品邊界，且不能當 1080p/60 證據。
- `GpuFrameBridge` 已經是 CPU pixels callback，會每幀 allocation/copy；它不是 1080p/60 的最佳接點。正確接點要在 renderer `FrameBuffer::post()` / modern gfxstream `postImpl()` 的 `ColorBuffer` 階段。
- modern Android 34 Play image 要優先做 gfxstream shared texture bridge；classic EmuGL artifact 不能跑 modern `kernel-ranchu` image，只能當 build/probe 參考。
- `libgfxstream_backend.dll` 存在只代表 stock gfxstream runtime；必須有 `chimera-gfxstream-shared-texture.json` 且 producer 是 `ChimeraGfxstreamSharedTextureBridge`，才可視為 Chimera producer runtime ready。
- gfxstream manifest 本身也不夠；host probe 和 manifest writer 都必須檢查 `libgfxstream_backend.dll` 內含 `ChimeraGfxstreamSharedTextureBridge` marker，否則 stock binary 加 manifest 會再次變成假 runtime ready。
- stock `libgfxstream_backend.dll` 有大量 C++ mangled exports；不要走 DLL forwarder 假改 runtime，應以官方 gfxstream source patch/build 建 modified runtime。
- `verify-true-1080p60.ps1` 預設應走 `-RuntimeKind Gfxstream`，因 Android 34 Play image 的 production 方向是 modern gfxstream；legacy EmuGL 只能用 `-RuntimeKind EmuGL` 明確驗證。
- 使用者看到多開原生 Android Emulator 視窗時，不要再調 UI 隱藏；要從 runtime gate / `-no-window` / unsafe window-capture fail-closed 下手。
- `--native-embed` 也會外露 stock emulator 視窗/工具列；它和 `--window-capture` 一樣只能是 unsafe diagnostics。正式路徑要讓 `InstanceConfig` / `VirtualMachineConfig` 預設 headless，且舊 `headless=false` 設定必須被 normalize 回 `-no-window`。

## 2026-06-03 — fallback 也不能傷 host audio

- ADB H.264 / screenrecord 是診斷 fallback，不是 production 60 FPS 路徑；helper process 必須低優先級、低 memory priority、power throttling，失敗時也不能短週期重啟。
- 解碼後 BGRA frame 應優先 publish 到 D3D11 shared texture，避免每幀 QImage allocation/copy 與 Qt texture upload 造成滑動或背景音樂卡頓。
- ffmpeg 已經輸出指定尺寸/格式時，不要再加重複 `scale=1920:1080`；這類看似無害的 filter 會直接增加 CPU/GPU 負擔。
- strict shared texture 模式下不可讓 renderer 繼續走 `m_onPost` / `ColorBuffer::readback()`；shared texture 初始化硬失敗也要 latch，避免每幀重試與 stderr/log I/O 影響主機音樂。

## 2026-06-02 — EmuGL DLL artifact 不等於 runtime ready

- custom EmuGL build 可以先產出 legacy `lib64OpenglRender/EGL/GLES` DLL artifact，但沒有 custom `emulator.exe` 與合法 manifest 時，仍不可視為 shared texture runtime ready。
- 不可把 stock emulator.exe 放進 DLL artifact 目錄假裝完成；stock gfxstream 不會載入本 repo 的 `ChimeraSharedTextureBridge`，會回到原生視窗/原生 runtime 問題。
- 舊 qemu build system 在 system MinGW 下應只 patch 臨時 copy：關閉 32-bit、跳過 emulator executable/tests/qapi Python2，直接 build 四個 `objs-chimera-emugl/lib64/*.dll` target。
- `ChimeraSharedTextureBridge` 這類 EmuGL source 要用 MinGW/GCC 驗證，不只靠 MSVC；nested class 的定義不能放在 anonymous namespace，缺標準 header 也會被 GCC 抓出來。

## 2026-05-22 — 顯示與效能驗證

- 不可把 Android Emulator Win32 native embed 當作預設修法；它會黑畫面、破壞 Qt emulator 視窗群組，且可能讓工具列外漏。預設維持 headless gRPC streaming，native embed 只能作 opt-in 實驗。
- FPS log 只代表 frame 有 paint，不代表使用者看到可用桌面。遇到「空畫面」必須同時抓 ADB screenshot 與 Chimera/GuestDisplay 畫面，判斷是 guest state 還是 host display path。
- Quick Boot snapshot 可能保存壞的 guest state；在 snapshot 穩定前，預設 full boot，Quick Boot 只能用 `CHIMERA_QUICK_BOOT=1` 或 verifier 明確啟用。
- 開機後要主動 wake、dismiss keyguard、HOME，否則 stream 可能停在近乎空的鎖定/載入畫面。

## 2026-05-24 — 觸控與解析度效能

- Android Console `event mouse` 會回 OK，但不保證被 Android launcher 當成 touchscreen tap；普通點擊要走 emulator gRPC `sendTouch` 或其他真觸控路徑，不可讓 console mouse 假成功吃掉事件。
- 不能把 guest 解析度和 stream 擷取解析度混為一談；Android 與 capture/request 都必須維持至少 1920x1080。raw `getScreenshot` 1080p 會掉到 15-30 FPS 時，不能再用低解析度換 FPS，必須改 shared texture/custom runtime。
- 使用者抱怨「畫面靜止/點不動」時，驗證要包含「點擊後 foreground package 改變」，只看 screenshot 或 FPS 不夠。
- 不可把 capture loop FPS 當成使用者體感 FPS；靜止畫面重複擷取 60 次只代表 host 在輪詢。UI 必須分開顯示 guest/content FPS、stream FPS、render FPS 與 duplicate rate。
- 使用者明講「FPS 虛報」時，主側欄不可顯示單純 Stream FPS；Stream 只代表畫面傳輸 cadence，Guest 內容 FPS、Render FPS、Dup rate 必須保留在 HUD/log 供查證。
- 降低主機卡頓要避免靜止畫面重複 repaint：重複 frame 應只更新 stream metric，不送入 QML。若產品面要求主 FPS 穩定 60，capture 可維持 16ms，但 duplicate path 仍不可觸發 repaint。
- 想做 BlueStacks 類乾淨首頁，應做真正 Android HOME launcher 並在 boot completed 後 install/set-home，而不是只在 host UI 疊一層假首頁。
- 側邊欄是操作面板，不是除錯儀表板；使用者要乾淨時，主卡只顯示單一 FPS，Guest/Stream/Render/Dup 這類細節留給 log 或可切換 HUD。
- Android HOME launcher 不可用接近純黑的空狀態；至少要有固定標題或固定入口與 empty state，並用 `uiautomator dump` + screencap 驗證畫面真的有內容。
- 設 HOME 成功不等於當前畫面已切到 HOME；host boot flow 要 explicit `am start -n com.chimera.launcher/.MainActivity`，再用 top activity / UI tree 驗證。
- Android HOME 要常駐 status bar 時，theme 不可設 `android:windowFullscreen=true`；只隱藏 navigation bar 即可，否則橫向畫面會出現厚黑邊且狀態列圖示不可見。
- 乾淨首頁不要再列舉所有 `CATEGORY_LAUNCHER` app；固定展示必要入口，缺套件時顯示停用狀態，避免 TMobile/Setup 類系統殘留破壞簡潔感。

## 2026-05-25 — App provisioning 與 Home 啟動

- 需要 Google Play 時必須切到 `google_apis_playstore` system image；單純側載 Play Store APK 不等於有 Play services/授權環境。
- Android `DocumentsUI` 不是可靠的檔案管理首頁；要有可點的「檔案管理」入口，應安裝並驗證實際 file manager package，例如 `me.zhanghai.android.files`。
- Home App 不能只手刻四個 intent；固定入口要置頂，但也要掃描 `ACTION_MAIN` + `CATEGORY_LAUNCHER`，讓 Google Play 新安裝的 app 自動出現在首頁。
- 驗證 Launcher 點擊時要用 `uiautomator` 的 tile bounds / content-desc 點擊，再用 foreground package 判斷；只看圖示存在或用固定座標容易誤判。
- 固定入口不能以灰色 disabled tile 交付；Chrome / Files 缺失時要有內建 fallback Activity，否則使用者看到的是假入口。
- 動態掃描不可無差別追加 system launchers；只追加 user-installed packages，否則 Play image 會把 `Settings`、`TMobile` 這類系統殘留塞回乾淨首頁。
- 使用者回報「開模擬器後音樂卡、雜音」時，要先看 host 資源搶占：qemu 不可 High priority、vCPU 不可預設吃滿 4 核、開機前不可啟動 gRPC screenshot loop；`enableAudio=false` 時也不可掛 `virtio-snd-pci`。
- 不可用 ADB shell 處理滾輪/滑動主路徑；`adb shell input swipe` 每次都 spawn shell，會造成 host 與 Android input queue 抖動。wheel 要走 emulator gRPC touch swipe 並 throttle，高成本 ADB 只能當 fallback。
- BelowNormal priority 不一定比較好；它能保護 host audio，但本機 app switch smoke 曾掉到 41-46 FPS。預設要用 2 vCPU + Normal priority + 不高於 Normal 的上限，再靠降低 raw capture 尖峰取得 host headroom。
- gRPC raw capture 的預設值必須用 runtime smoke 證明；過去 960x540 / 896x504 / 800x450 的低解析度調整已被使用者明確否決。現行 policy 是至少 1920x1080，效能只能靠 shared memory/shared texture/custom runtime 解，不准再降解析度。

## 2026-05-26 — 誠實 FPS 與互動流暢度

- 主側欄 FPS 不可再顯示單純 Stream delivery；使用者體感要看 `min(Guest, Stream, Render)`，靜止畫面或 duplicate frame 顯示 0 是正確且誠實的。
- sampled fingerprint 會低估 Android 內容變化，不能拿來當誠實 Guest FPS；除非有逐 flow 驗證，否則維持 full-frame fingerprint 或更可靠的 frame-dirty signal。
- raw `getScreenshot` + `QImage`/`QQuickPaintedItem` 不是可宣稱真 1080p/60 的路徑；通知欄/滑動 flow 實測 Stream 60+ 時 Guest/Render 仍可能只有 9-14 FPS。真 60 phase 要改 shared memory/shared texture + scene graph texture renderer。
- 滾輪/拖曳的優化目標不是把事件全部送進 guest，而是照 60Hz frame pacing 合併；wheel burst 應節流到約 16ms，且單次 instant swipe request 數要最小化。
- BlueStacks 類效能方向應拆成硬體加速 boot gate、renderer profile、frame pacing、resource profile、Eco mode 與低延遲 input；不要只調高 qemu priority 或盲目提高 capture 解析度。

## 2026-05-27 — Shared texture 不是 CPU-copy

- CPU shared memory 只能降低 IPC/程序啟動成本，仍會有 CPU copy 與 Qt texture upload；不可把它宣稱為真正 shared texture。
- D3D11 shared texture 必須用 producer 建立 named shared handle，consumer 用 Qt scene graph 的 D3D11 device 在 render thread `OpenSharedResourceByName`，再交給 `QSGD3D11Texture::fromNative()`。
- 即使還在 CPU frame fallback，也不要每幀 `createTextureFromImage()` 重建 GPU texture；D3D11 RHI 下應維持 persistent texture，逐幀 `UpdateSubresource()`。
- Shared capture backend 必須是 opportunistic：沒有第一幀時要讓 gRPC fallback 接手，不能因為 env var 設錯就永久黑畫面。
- Shared frame metadata 必須用 odd/even sequence 做 seqlock；只看 header 或只測字串會漏掉 producer 寫入中的 torn frame。
- 測試 shared texture 時至少要建立真 D3D11 shared resource，並用第二個 D3D11 device 打開；只測 metadata signal 不足以證明 named handle 可用。
- 不可讓 shared texture metadata capture 依賴 UI thread QTimer；frame event 應由 worker 等待，且只有新 even sequence 才能計入 Stream/Guest，否則又會變成「看起來 60、實際沒新 frame」。
- Runtime helper producer 也不能用高成本 CPU 全圖填色來測 60fps；要用 GPU render/clear 與固定 frame pacing，否則會把 producer 自己的 30fps 誤判成 renderer 瓶頸。
- 不可用降低 capture 解析度來換取漂亮 FPS；Chimera 的 capture/request floor 必須至少 1920x1080，低於這個值的 env 或預設都要被程式 clamp 回 1080p。

## 2026-05-27 — 回歸不能傷到主機音訊

- Native embed attach log 成功不等於 display path 可用；只要 toolbar 外漏、viewport 黑畫面或視窗覆蓋異常，就只能維持 opt-in 實驗，不可當預設。
- Emulator gRPC MMAP frame 在目前環境是 top-down；照 bottom-up 複製會把 Android 畫面翻轉。這類 orientation 修正一定要用 screenshot 實證，不可憑記憶猜。
- 1080p MMAP / raw CPU path 不能做 full-frame hash 當每幀 dirty detection；那會把成本搬到 host CPU，造成音訊和滑動卡頓。要用可靠 sequence / dirty signal。
- 追 1080p 60 FPS 時不可回歸到搶主機資源：預設 qemu 應低於前景音樂工作、套 EcoQoS、Quick Boot opt-in、guest audio disabled、boot completed 前不啟動 gRPC capture。
- 靜止畫面 raw fallback 不能用 16ms busy cadence 長時間輪詢；idle duplicate cadence 要保守，只有輸入或內容變化時才 boost。

## 2026-05-31 — Producer contract 要先變成正式模組

- 不能把 1080p/60 的 shared texture producer 寫死在測試 helper 裡；helper 能跑不代表 emulator bridge 可接。先抽正式 publisher API，讓 helper、unit test、未來 QEMU bridge 共用同一份 metadata / named texture / sequence 寫法。
- Shared D3D11 metadata 必須由 producer 用 odd/even sequence 寫入；consumer 只接受 even 且一致的 sequence，避免 UI 顯示半寫入 metadata。
- 測試 source 預設也要是 1920x1080 / 60fps；測試 helper 預設 1280x720 會讓驗證訊號偏離使用者要求。
- CPU shared-memory framebuffer 也不能留低解析度成功案例；即使只是 fallback/test source，低於 1920x1080 都要 reject 或 clamp，否則會變成下一個假 60 FPS 入口。

## 2026-05-31 — Shared texture hook 要關掉 readback

- 在 EmuGL 端加入 shared texture hook 時，成功發布 GPU shared texture 後必須跳過 `m_onPost` 的 `ColorBuffer::readback()`；否則 CPU readback 還是會吃滿頻寬，體感不會接近 60 FPS。
- `FrameBuffer::post()` 的 sub-window 與 headless/no-subwindow 分支都要接 shared texture；只改 native window 分支會漏掉 Chimera 目前常用的 headless streaming 路徑。
- Bridge env 應 fallback 到 host 使用的 `CHIMERA_D3D11_TEXTURE_*`，避免 host consumer 與 emulator producer 需要兩套不同設定。

## 2026-06-01 — 真 1080p/60 要 fail closed

- `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 不能只擋 runtime probe；runtime 不可用、shared texture 沒出第一幀、或 capture fallback 被啟動時都必須 fail closed，不可留下空 UI 或回落 raw gRPC 假跑。
- 60 FPS verifier 必須 parse `Guest/Stream/Render` 或 `CHIMERA_PERF effective`，並在動態畫面下檢查 duplicate rate；只看側欄或單一 Stream FPS 一律不算證據。
- `scripts/verify-true-1080p60.ps1` 的成功條件是 Android `wm size >= 1920x1080`、custom EmuGL shared texture runtime ready、Shared D3D11 capture started、有效 FPS 達標、且沒有 raw gRPC/ADB/screenrecord fallback。
- Windows 測試 helper 若要驗證 Unicode argv，必須用 `wmain` 取得 wide argv 再輸出 UTF-8；`main(char**)` 會經 ANSI codepage 破壞中文。

## 2026-05-31 — 音訊回歸要卡在啟動第一瞬間

- 修 host 音樂卡頓時，不能只在 emulator 已經 resume 後才調 priority；child process 必須先 suspended 建立、套 priority/EcoQoS，再 resume，否則啟動前 1-2 秒仍會搶 foreground audio。
- qemu child 是 emulator resume 後才生出來；只在父程序 resume 前套 priority 不夠，啟動前幾秒要高頻重套整棵 process tree，避免 child 短暫以 Normal priority 搶主機音訊。
- 高 priority 必須在多層邊界被封住：Instance config 正規化、VirtualMachine priority mapping、ProcessLauncher applyPriority 都不得允許 High/Realtime 落到 emulator tree。
- UI 解析度 preset 也算降階入口；既然要求至少 1920x1080，就不能留下 720p/低於 1080p 的 `wm size` 快捷按鈕，ADB control 邊界也要 clamp。
- Legacy/R&D backend 也不能留下低解析度 fallback；`--qemu-backend`、`--hcs-backend`、config loader default 都要和 production path 一樣至少 1920x1080，否則之後 debug 或 fallback 會把降階重新帶回來。
- Quick Boot snapshot 保存不是免費的背景工作；不可每次 boot completed 後自動保存。預設只載入 snapshot，保存/重建必須走 verifier 或 `CHIMERA_SAVE_QUICK_BOOT=1`。
- 實驗 shared texture env 不能造成 UI/log 錯誤洪水；producer 尚未建立 metadata mapping 時應安靜重試，讓 gRPC fallback 維持可用。

## 2026-06-05 — modified gfxstream 不能只看 marker/manifest

- standalone build 出來的 `libgfxstream_backend.dll` 即使含 `ChimeraGfxstreamSharedTextureBridge` marker，也不代表可替換 SDK emulator 的 backend；必須比對 SDK 版本 ABI exports。
- Android Emulator 36.5.11 stock backend 需要 `gfxstream_backend_set_screen_background` 等 entrypoint；缺這類 export 的 custom DLL 會讓 QEMU 活著但 Android/ADB 不起來，最後表現成黑屏、卡 boot 與資源干擾。
- manifest writer 與 host runtime probe 都要 fail closed：缺 SDK ABI export 時不得寫 manifest，也不得把 runtime 標成 shared texture ready。
- verifier 失敗時要做 stock 對照 boot；若 stock 同 AVD 能 boot 而 custom 不行，問題在 custom runtime ABI/producer，不要回頭亂改 UI 或 AVD。
- 不可讓 `CHIMERA_ENABLE_NATIVE_EMBED`、`CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW`、`CHIMERA_ENABLE_WINDOW_CAPTURE`、`CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE` 這類環境變數啟用原生 emulator 視窗；unsafe display 只能靠同一次命令列明確傳入，正式 verifier 要先清掉舊 env，避免多開 stock Android Emulator 視窗。
- Modern Android 34/gfxstream 走 Vulkan host path；只有 legacy GL bridge marker 不等於有可用 1080p/60 producer。gfxstream manifest 必須證明 `VulkanDisplayVkPost` 接點與 SDK 36 ABI，否則不得標成 runtime ready。
- 若 verifier 顯示 runtime ready 但 `AndroidConsoleInput` 一直 connection refused、ADB 不起、FPS 全 0，優先檢查 runtime probe 是否過寬，不要把問題推給 UI 或前端 FPS。

## 2026-05-31 — Shared texture runtime 只能 opt-in 啟用

- 在 modified EmuGL 尚未成為預設 runtime 前，不可預設建立 shared texture capture；stock emulator 不會產生 metadata，預設打開只會增加 retry 與排查噪音。
- Host 與 EmuGL env 必須用同一組名稱：`CHIMERA_D3D11_TEXTURE_*` 給 host consumer，`CHIMERA_EMUGL_D3D11_TEXTURE_*` 給 producer，opt-in 時自動互相補齊。
- shared texture opt-in 不能拿掉 gRPC fallback 的 input activity boost；在第一個 shared texture frame 出現前，滾輪/觸控仍需要讓 fallback capture 進入互動 cadence。
- Custom emulator 不應覆蓋官方 SDK 目錄；用 `CHIMERA_EMULATOR_PATH` 指向實驗 runtime，並在啟動前把旁邊的 `lib64/` / `lib/` 放進 PATH，方便切回 stock emulator。

## 2026-05-31 — Quick Boot 不可再次變成預設

- 使用者再次回報 host 背景音樂卡頓時，要檢查 Quick Boot load/save 是否被重新打開；載入 snapshot 與保存 snapshot 都可能造成啟動/關閉時 I/O 尖峰。
- Quick Boot 必須是明確 opt-in：`CHIMERA_QUICK_BOOT=1` 才載入，`CHIMERA_SAVE_QUICK_BOOT=1` 才保存。不可只關掉 boot 後 auto-save，卻漏掉 `VirtualMachine::stop()` 的同步 snapshot save。
- 修音訊回歸不能只看 qemu priority；還要盤點 full-res raw capture、snapshot I/O、orphan qemu 與 shared texture retry 是否把背景資源壓力帶回來。

## 2026-05-31 — Unary MMAP sequence 不能當 dirty signal

- Android Emulator unary `getScreenshot` 的 `Image.seq` 固定是 0；拿它判斷新幀會讓畫面變化被誤判成 duplicate，造成 Guest/Render FPS 失真。
- 1080p MMAP fallback 仍不可每幀做 full-frame hash；這會把成本搬回 host CPU，增加主機音樂卡頓風險。
- MMAP 若要用 emulator 提供的 dirty signal，必須走 `streamScreenshot` sequence；但 stock emulator MMAP stream 實測仍只有約 12 FPS，不能當真 1080p 60+ 完成證據。

## 2026-05-31 — Screenrecord 與 raw gRPC 不可傷 host audio

- 新增 ADB/ffmpeg 類實驗 transport 時，不可在未出第一幀時短週期重啟；每 5 秒重啟 adb/ffmpeg 會把「黑畫面」變成 host 資源干擾。
- screenrecord/H.264 失敗必須帶 adb/ffmpeg stderr tail；只顯示 0 FPS 或黑畫面會讓下一輪又用猜的。
- raw unary `getScreenshot` 是 CPU/GPU readback fallback，不是 1080p/60 的生產路徑；預設不可用 16ms 1080p readback 硬撐，否則背景音樂/前景瀏覽器會受影響。
- 若為保護 host audio 降低 raw fallback cadence，必須誠實記錄「這不是 60 FPS 完成」，下一步仍是 shared texture/custom emulator runtime。

## 2026-06-01 — Shared texture runtime 需要硬證據

- `--emugl-shared-texture` 不能只設定 env 就算接上；host 必須檢查 emulator runtime 是否真的能載入 Chimera producer。
- Stock Android SDK emulator 目前是 `libgfxstream_backend.dll`，不會載入 QEMU subrepo 裡的 legacy `ChimeraSharedTextureBridge`；看到 stock gfxstream 時要明確標示不支援，不可讓 shared texture capture 安靜 retry 成假成功。
- Custom EmuGL runtime 必須同時有 legacy `lib64OpenglRender.dll` 與 Chimera manifest，才可視為 producer runtime ready；只有 DLL 或只有 env 都不夠。
- Custom EmuGL runtime ready 不能只看 renderer DLL；還必須有 EGL/GLES translator DLL、合法 manifest schema、1920x1080/60 manifest floor。缺 dependency 的 runtime 會導致啟動後回落 raw readback 或黑畫面，必須 fail fast。
- 若需要把 shared texture 作為必需條件驗證，使用 `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 讓錯誤直接 fail fast，而不是落回 raw readback 後誤判。

## 2026-06-01 — Custom emulator build 不能靠 qemu subtree 假設

- Windows 上直接 `bash` 可能走壞的 WSL relay；需要明確指定 `wsl -d Ubuntu-24.04`。
- qemu 子倉庫在 Windows checkout 會有 CRLF，WSL build 應用臨時 copy 轉 LF，不要大面積改原始檔換行。
- `mingw-w64` 只是必要條件；這份 qemu subtree 還需要完整 AOSP `prebuilts/gcc`。缺 prebuilts 時要 fail fast，不可繼續 package manifest。
- custom runtime build script 的失敗輸出也是產品證據：它證明目前缺的是完整 AOSP build tree，不是可以靠調 host fallback 補上的效能問題。

## 2026-06-01 — 解析度 floor 必須覆蓋 guest/window/capture

- 只 clamp capture request 不夠；如果 VM config 或 AVD hardware 被寫成低解析度，guest 本身仍會降階，違反「不要偷偷降解析度」。
- `hw.lcd.width/height`、`-window-size`、capture request 都必須使用同一個至少 1920x1080 的 floor。
- 測試不能只看 gRPC request；要直接驗證 emulator args 不會出現 `800x450` 這類低解析度。
- InstanceManager 也要正規化 saved config；否則 UI/設定層仍會保存或回傳低解析度，讓後續流程再次把低解析度當合法輸入。

## 2026-06-01 — Raw fallback 不可把格式轉換丟到 render thread

- 1080p raw gRPC/MMAP fallback 若要求 RGB888，Qt D3D11 texture upload 前仍要轉成 RGBA；這會把每幀 CPU 轉換放進顯示路徑，造成滑動/通知欄體感卡頓。
- fallback capture request 應直接要求 RGBA8888；若 runtime 回 RGB888，也要在 capture 層轉成 `QImage::Format_RGBA8888`，不要讓 `GuestDisplay::updatePaintNode()` 每幀做 format convert。
- 這只是降低 fallback 開銷，不是真 1080p/60 完成證據；穩定 60 仍要靠 shared texture/custom producer，避免 screenshot readback 成為主要 display path。

## 2026-06-01 — Host audio 優先於 emulator 啟動速度

- 使用者回報「一開模擬器背景音樂就卡」時，不要只看 capture cadence；capture 可能已延後到 boot completed，真正干擾仍可能來自 emulator/qemu 啟動尖峰。
- emulator/qemu 啟動前段應用比 steady state 更低干擾的 policy：先 `Idle`，高頻重套整棵 process tree，覆蓋 qemu child 出生競態；boot 完或暖機後再回到 `below_normal`。
- 低 priority process 不只要降 CPU priority，還要套 memory priority / power throttling / ignore timer resolution，避免影響前景瀏覽器播放音樂。
- 驗證這類修正時，不要為了證明而直接 full boot 干擾使用者；先用 build/unit tests 鎖住資源 policy，再安排明確的短 runtime smoke。

## 2026-06-01 — 不可把 stock emulator 視窗當正式顯示路徑

- Windows Graphics Capture 依賴 stock Android Emulator HWND；它會暴露原生 emulator 視窗與工具列，違反 Chimera 必須由自有 UI 接管的產品方向。
- `--window-capture` 必須預設拒絕，只有 `--allow-unsafe-window-capture` / `CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE=1` 才能做本機實驗；正式驗證不得用它當 1080p/60 完成證據。
- Window capture 或 shared texture 嚴格模式失敗時不可退回 1080p raw gRPC/ADB，否則會重新造成 host audio 卡頓與 FPS 假象。
- Eco mode 解除時也不能把 emulator/qemu 拉回 Normal；預設最多回到 BelowNormal，並且必須套整棵 process tree。

## 2026-06-02 — Classic EmuGL artifact 不是 Android 34 production runtime

- `emulator64-x86.exe` + legacy `lib64OpenglRender.dll` 可以作為 shared texture hook 的 build/probe artifact，但它是 classic goldfish path，不等於可跑現代 Android 34 Play Store AVD。
- 現有 `google_apis_playstore/x86_64` image 只有 `kernel-ranchu`；classic emulator 預設找 `kernel-qemu`，直接指定 `kernel-ranchu` 也會因 kernel 格式解析失敗而退出。
- custom runtime verifier 必須鎖定 `CHIMERA_EMULATOR_PATH`，否則很容易「要求 EmuGL」卻實際測到 stock SDK emulator。
- 對 classic runtime 不能傳新版 stock emulator flags：`-grpc`、`-idle-grpc-timeout`、`-window-size`、`-fixed-scale`、`-vsync-rate`、`-no-metrics`、`-crash-report-mode` 都要被排除。
- 真正可對標 BlueStacks 的方向是 modern ranchu/gfxstream runtime bridge：保留現代 Android/Play image 相容性，但把 display producer 改成 shared D3D11 texture；不能回退到 stock emulator HWND 或 raw screenshot readback。

## 2026-06-06 — 不要用 C++ ABI wrapper 硬包 stock gfxstream

- Stock `libgfxstream_backend.dll` 只穩定暴露 export entrypoint，不保證 C++ 內部型別 ABI 可被外部 wrapper 繼承/轉發；`RenderLib` wrapper 會碰到未匯出的 `FeatureSet` 符號。
- gfxstream proxy 的安全邊界應優先維持在 C export hook；要包 C++ virtual interface 前，必須先有同版本 SDK source/import library 與 boot smoke 證據。
- 使用者再次看到多開原生 Android Emulator 視窗時，先驗證 command line 是否有 `-no-window`、是否有 unsafe native/window capture flag、是否有 orphan emulator/qemu，而不是改 UI。
- 「自己寫一個 Android 模擬器」不是短期修法；產品路線是保留 Android 相容核心，改 host shell、headless display producer、input 與 resource policy。

## 2026-06-12 — Raw fallback 與 stale manifest 不能再造成假成功

- raw gRPC/MMAP/screenrecord/ADB capture 是診斷路徑，不可再由環境變數啟用；必須同一次 CLI 明確傳 `--allow-raw-capture-fallback`，避免舊 env 讓啟動時重新干擾 host audio。
- gfxstream manifest writer 在驗證前必須先移除 stale manifest；缺 marker、缺 SDK ABI export、缺 SDK runtime import 時不能留下舊 manifest 讓 host 誤判 runtime ready。
- Headless gfxstream producer 不能依賴 native surface；bridge enabled 時，`FrameBuffer::postImpl()` 必須能把 frame 送進 `PostWorkerVk`，`DisplayVk::post()` 也不能因沒有 surface 直接 return。
- 輸入只能由 `GuestDisplay` 做座標轉換後轉發；window 層直接送 mouse/wheel/key 會造成雙送、座標錯誤與滾輪卡頓。
- legacy/R&D backend 也要遵守 host audio policy：預設 2 vCPU / 2048MB、hidden launch、startup `Idle`、暖機後 `BelowNormal`，不能因為是 fallback 就回到高干擾配置。

## 2026-06-14 — stock SDK headless 是純 Vulkan，GPU frame capture via proxy 是死路

- `resolve_angle_egl()` 類函式不能在確認找到模組「之前」就設 resolved flag；flag 只能在 SUCCESS（模組找到且 function pointer 全部解析成功）時才設，否則輪詢 loop 第一次失敗後就永遠不再重試。
- Stock SDK 15261927 headless mode：`vulkan-1.dll` 從第一毫秒就已載入（兩個實例：emulator 自帶 `lib64/vulkan-1.dll` 與系統 `vulkan-1.dll`）；`libEGL.dll` 在整個 emulator 存活期間 NEVER 出現；`d3d11.dll` 也不載入。任何 ANGLE/EGL/D3D11 proxy 策略對 stock SDK 15261927 都是永久死路，無需繼續實驗。
- Stock pre-initializes `vkQueuePresentKHR` 等 swapchain 函式（透過 GetProcAddress 批次解析 128+ Vulkan 函式），但 headless `-no-window` 模式 NEVER 實際呼叫它，因為沒有 VkSurface/Swapchain。GPU frame capture via proxy DLL（hook `vkQueuePresentKHR`）在 headless 模式下毫無用武之地。
- GetProcAddress IAT hook 是有效的 Vulkan API surface logger：patch 前必須先 save real `GetProcAddress`（否則 hook 自己呼叫 `GetProcAddress` 會遞迴爆炸）；攔截回傳值可以強迫替換 `vkQueuePresentKHR` 等函式指標，技術上可行，只是 headless 下永遠不會被呼叫。
- `vtable[35]` crash（`libgfxstream_backend_stock.dll+0x6D83D`）是 stock 嘗試用 EGL-based screenshot path 但 EGL 未初始化；QEMU catch 後走 Vulkan CPU readback，對 gRPC getScreenshot 無害。這是 expected behavior，不是 bug。
- GPU frame capture via proxy 在 headless 下沒有乾淨 frame boundary signal；`vkQueueSubmit` 有 hook 點但 per-submit 沒有 frame 邊界，且中間可能有幾十次 compute/transfer submit；不是可行的 60 FPS producer 路徑。唯一出路仍是 matching SDK build id 的 custom gfxstream runtime。

## 2026-06-13 — 原生 emulator 多開要同時防 UI path 與 stale process

- `NativeEmulatorView` 就算沒有 attach，也不應在正式路徑 pin emulator PID 或保持可見；預設產品路徑只允許 `GuestDisplay`，native embed 只屬於 unsafe diagnostics。
- 啟動前不能只靠 Job Object 清「本次」子程序；若前一輪已經留下佔用 `5554/5555/8554` 的 emulator/qemu，下一輪會疊出雙 VM 並搶 host audio。正式啟動前要清同 port 且 process 名稱為 emulator/qemu 的 stale VM tree。
- 清 stale process 要縮小打擊面：不可殺所有 Android Studio emulator；只能依 Chimera 固定 ports 與 VM process 名稱交集處理。
