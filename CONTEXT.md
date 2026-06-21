# Project Chimera — CONTEXT.md

> 開發歷程記錄。供下一個 Agent 快速接手用，不需要從 git log 重建脈絡。

## 專案目標

Windows Android 模擬器，競品目標是 BlueStacks。純 open-source 元件，無雲端依賴、無廣告、無遙測。

## 最新狀態（2026-06-21 Session 83-84）

- **D3D11 shared texture DXGI fix CONFIRMED**：`CreateSharedHandle` 第二參數從 `GENERIC_ALL (0x10000000)` 改為 `DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE`；修正後 `OpenSharedResourceByName` 不再回傳 `hr=0x80070057`，D3D11 shared texture 成功建立。
- **aosp-github namespace 修正**：bridge 類別從 `namespace gfxstream::vk` 改為 `namespace gfxstream::host::vk`（與 host vulkan layer 一致），`borrowed_image_vk.h` 改為 snake_case include；build 成功（`gfxstream_backend.dll` 8,645,120 bytes）。
- **端到端驗證 PASS**：bridge enabled、`readToD3D11Texture avg=10.3ms over 30 frames`、`Guest=Stream=Render=7 FPS` during boot animation、Android boot 39s、no `hr=0x80070057`；manifest gate 通過（`buildIdOk=true`）；20/20 unit tests PASS。
- **DLL 部署**：同一 DLL 覆蓋至 `build/chimera-gfxstream-runtime/lib64/` 與 `build/chimera-gfxstream-runtime-github/lib64/`。
- **現有 FPS 限制**：D3D11 CPU path（Vulkan staging buffer → UpdateSubresource → D3D11 texture），約 10-19ms/frame，可達 ~52-100fps 理論上限，但 guest render cadence 仍限制在 ~7 FPS boot animation / idle 0 FPS（push-based 正常）。
- **下一步**：① async PBO 降低 GL readback 同步阻塞；② 真正 GPU-to-GPU 路徑（Vulkan Composition 啟用）；③ 更新 manifest 簽名以 `-AllowMismatchedBuildId` 以外的方式通過 ABI gate。

## 最新狀態（2026-06-19 Session 82）

- **direct-to-shmem 優化**：`frame_buffer.cpp` `chimeraPublishFrameToShmem()` 移除 8MB 中間 `pixels` 向量，直接 readback 到 `shmem+56`；`headerWritten` flag 讓 header 只寫一次。DLL 重建至 `build/chimera-gfxstream-runtime-github`。
- **shmem 吞吐量 CRITICAL FINDING**：合成 56.7 FPS producer → Chimera 消費 `guest=stream=render=50.6 FPS`（`dupPct=0`，`avgMs=19.3ms`）。shmem 基礎設施上限 50-56 FPS，不是主要瓶頸。
- **EcoQoS 移除 BELOW_NORMAL 後的最終結果**：最小 OpenGL ES triangle demo `effective=22.8-24.8 FPS`（原 7-9），Settings scroll `effective=24.9 FPS`（原 15.9），`dupPct=0`。`readToBytesScaled` 平均僅 `7-10 ms`，證明目前 24-25 FPS 瓶頸不在 CPU readback API 本身，而在 guest/render cadence + headless GL pipeline 同步模式。
- **資源策略最終修正**：`ProcessLauncher.cpp` 將 `lowMemoryPriority` 與 `ecoQos` 拆開；`BELOW_NORMAL` 保留 `MEMORY_PRIORITY_LOW`，但不再套 `ProcessPowerThrottling`；`IDLE` 才套 EcoQoS。`test_process_launcher` 直接驗證兩者，`test_gamepad_manager` 也改成不依賴主機沒有實體手把；non-integration `20/20 PASS`。
- **4 vCPU 不是解法**：4 vCPU 下 triangle demo app 正常啟動、EGL app_time_stats 正常，但 Chimera 只收到前段少量 frame 後長時間 0 FPS；增加 vCPU 數不是當前瓶頸的直接修法。
- **下一步**：① async PBO（減少 headless GL readback 同步阻塞）；② D3D11 shared texture（GPU-to-GPU，最終正解）。

## 最新狀態（2026-06-19 Session 81）

- **shmem delivery 路徑 CONFIRMED（非 GuestVulkanOnly）**：custom github runtime（`build/chimera-gfxstream-runtime-github`）在非 GuestVulkanOnly 模式下，Android 61s 開機，shmem `event_fps_avg=3.4 / seq_fps_avg=7.6 / max=16.9`（idle 主畫面正常）。`chimeraPublishFrameToShmem()` 在 headless `postImpl` else 分支不受 GuestVulkanOnly 影響，每幀都呼叫。
- **GuestVulkanOnly=true blocker**：新增 `CHIMERA_GFXSTREAM_GUEST_VK_ONLY=1` env gate；設定後 `useVkComp=1` 可達，NVIDIA 選中，但 SurfaceFlinger 無 GLES 無法 boot complete（300s timeout）。不可生產使用。
- **Chimera UI 整合路徑就緒**：InstanceManager 已有 auto-probe `ChimeraShmemFramePublisher` 的邏輯（`InstanceManager.cpp:546-558`），偵測到 marker 後自動設 `CHIMERA_SHMEM_FRAME_NAME/EVENT`。只需 `CHIMERA_EMULATOR_PATH` 指向 github runtime 即可啟用 shmem 顯示，無需手動配置。
- **Chimera UI shmem 整合 CONFIRMED**：以 `CHIMERA_EMULATOR_PATH` 指向 github runtime 啟動；InstanceManager 自動啟用 shmem；開機動畫 `effective=16.3 FPS`，Settings 連續滾動 `guest=stream=render=15.9 FPS`（無 duplicate，真實唯一幀），靜止 Home 0 FPS（push-based 正常）。
- **FPS 瓶頸**：`-gpu host` + NVIDIA，但 `readToBytesScaled` 同步 GL readback 8MB/幀限制約 16 FPS 上限。真 60 FPS 需 D3D11 shared texture（無 CPU readback）。
- **下一步**：在 `postImpl` 加 async PBO 或 D3D11 shared texture producer；或測試遊戲 APK 測量真實遊戲 FPS。

## 最新狀態（2026-06-18 Session 80）

- **Vulkan loader 調查收斂**：先前 custom github runtime 的 NVIDIA 測試腳本 `tmp/measure-gfxstream-fps-nvidia-v2.py`、`v3.py`、`v4.py` 都以 `-gpu swiftshader_indirect` 啟動 emulator，導致 `emuglConfig_setupEnv()` 在 gfxstream 初始化前強制 `ANDROID_EMU_VK_ICD=swiftshader`（`tmp/gfxstream-src/host/gl/gl-host-common/opengl/emugl_config.cpp:398-404`）。因此當時看到的 `VkEmulation::create step 5: got 4 instance exts` + `vkCreateInstance res=-9` 是**被測試 harness 汙染後的假失敗鏈**，不是已證實的 NVIDIA loader/ICD 根因。
- **修正後驗證**：三支腳本改成 `-gpu host` 後，v2 / v3 / v4 全部翻成 `got 20 instance exts`、`vkCreateInstance res=0`、`vkCreateDevice res=0`，並成功選到 `NVIDIA GeForce RTX 3070 Ti`。這表示先前關於 Optimus layer、bundled vs system loader、DriverStore PATH 的否證結論都必須降級重審，不能再當既定事實。
- **目前新結論**：真正已證實的是「不能用 `swiftshader_indirect` 驗證 host/NVIDIA Vulkan ICD 路徑」；`runtime-final-err.log` 這類 stock SDK emulator 樣本也不能直接代入 custom github runtime 的失敗鏈，因為它們只顯示 stock `Selecting Vulkan device` 訊號。
- **額外觀察**：即使 NVIDIA Vulkan instance/device 已成功建立，當前 headless shmem FPS 仍只有約 2.5-3.1 event FPS（seq 平均約 6.0-11.2），距離 true 1080p/60 還很遠。

- **R&D force-on probe**：在 patched github runtime 端加 `CHIMERA_GFXSTREAM_FORCE_VK_COMPOSITION=1` 後，配合 `CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK=1` 重跑 `tmp/measure-gfxstream-fps-nvidia-v4.py`，emulator 在極早期直接 `0xC0000005`（exit code 3221225477）早退，連 `VkEmulation::create` instrumentation 都來不及落 log。這說明 composition gate 不是可以無腦硬打開的開關；它是敏感邊界，但真正修法仍要回頭追 renderer flags / upstream feature negotiation。
- **proxy smoke 新結論**：即使用 stock-ABI proxy runtime，headless host path 仍可看到 `glInteropSupported: true`，但同時 `useVulkanComposition: false`、`useVulkanNativeSwapchain: false`。因此 bottleneck 已從「host Vulkan/GL interop 不成立」收斂成「renderer flags / feature gate 沒把 runtime 帶進 Vulkan composition」。
- **proxy instrumentation 補強**：`src/host/runtime/gfxstream_proxy/gfxstream_proxy.c` 新增 `renderer_flags` bit 解碼 log；`src/host/runtime/gfxstream_proxy/gfxstream_proxy_renderlib.cpp` 新增 `FeatureSet` 摘要 log；`scripts/run-proxy-smoke-test.ps1` 改為尊重外部 `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB` / `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER`，不再強制覆寫成 `0`。目前 renderlib wrapper smoke 會讓 boot 卡住，尚未拿到 `FeatureSet` summary，但不影響上面的 composition gate 結論。
- **目前最直接下一步**：追 `renderer_flags` 上游來源，或在 patched github runtime 端加 opt-in override，驗證只要強制打開 Vulkan composition，`recordCopy()` / `publishFrame()` 是否會被喚起。

## 最新狀態（2026-06-17 Session 79）

- **AdbH264 screenrecord 永久死路**：stock Android Emulator `-no-window` headless 模式下，`adb exec-out screenrecord --output-format=h264` 在 5s 內產生 0 bytes。SurfaceFlinger/MediaRecorder API 無法存取 headless virtual display framebuffer。`CHIMERA_VIDEO_TRANSPORT=screenrecord` 不再嘗試。
- **audio 干擾修正**：`configs/instances.json` `processPriority` 改為 `"idle"`。IDLE priority class 使 OS 永遠優先排程音訊播放器；同時配合 `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` + `MEMORY_PRIORITY_LOW` + `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`，不影響 16 核心 Ryzen 5950X 正常模擬器運作。
- **驗證**：GrpcOnly verifier PASS（`grpc_stream_fps_avg=6.7`，`unique_content_fps_max=1.9`，1920x1080）；build PASS；no_residual_processes=OK。

## 歷史狀態（2026-06-17 Session 78）

- **音訊啟用**：`configs/instances.json` 改 `enableAudio: true`；移除 `VirtualMachine.cpp` 的 `virtio-snd-pci`（與 stock Goldfish audio HAL 衝突，導致 guest-side init fail）。stock Android Emulator 在無 `-no-audio` 時自動路由 Goldfish audio 到 host WASAPI。
- **gRPC display regression fixed**：前一輪把 gRPC 誤分類為「diagnostic raw fallback」並要求 `--allow-raw-capture-fallback` CLI flag；修正後 gRPC 在無 shared D3D11 texture path 時是預設 display（`!sharedTextureCapture`）。`allowRawCaptureFallback` 只控制 MMAP/screenrecord/ADB 診斷 fallback。
- **GrpcOnly verifier 修正**：`Assert-True1080p60GrpcLog` 移除不可達的 `effective >= 60` + `maxDup <= 5%`；改為過濾 boot 零值樣本（stream=0.0 的 pre-gRPC boot 樣本）→ `avgStreamFps >= 3.0`（active samples）+ `maxGuestFps >= 1.0`（exercise 期間有真實內容）。
- **驗證**：GrpcOnly verifier PASS（`grpc_stream_fps_avg=8.3`，`unique_content_fps_max=4.0`，`wm_size=1920x1080`）；20/20 unit tests PASS；build PASS。
- **注意**：CONTEXT.md Session 74 的「62-67 FPS」是在計數含 duplicate 的 `streamFps`，不是 `effective` FPS；gRPC 路徑實際約 4-17 FPS。

## 歷史狀態（2026-06-17 Session 77）

- **shmem frame delivery CONFIRMED**：`chimeraPublishFrameToShmem()` 加入 `frame_buffer.cpp` 的 `postImpl()` headless 分支，使用 `readToBytesScaled()` Vulkan readback（SwiftShader CPU-accessible）→ Win32 named mapping（seqlock header，magic=`0x43484D46` / seq odd=writing / even=complete）→ `SetEvent()` 通知。
- **測試結果**：magic=`0x43484D46`，1920x1080 RGBA8888，seq=1530，**non_zero=4096/4096**（像素全部非零，有真實 Android 畫面內容），px_sz=8294400（= 1920×1080×4）；`shmem_frame_ok=True`；`boot_completed_adb=True`；`no_residual_processes=OK`。
- **ctypes 64-bit restype 修正**：`OpenEventA`/`OpenFileMappingA`/`MapViewOfFile` 預設 `c_long` 截斷 64-bit 指標 → AV；改為 `.restype = ctypes.c_void_p`。
- **端到端整合完成**：`InstanceManager::createInstance()` 自動 probe DLL 中的 `ChimeraShmemFramePublisher` marker → 若偵測到且 `CHIMERA_SHMEM_FRAME_NAME` 為空，自動設 `chimera_shmem_<name>` 與 `chimera_shmem_event_<name>` → `main.cpp` 讀取這些 env vars 建立 `SharedMemoryFramebufferCapture` + retry timer → emulator 繼承 env vars → gfxstream DLL 創建 named mapping → retry 成功後 host 渲染 Android 畫面。用戶只需設 `CHIMERA_EMULATOR_PATH` 指向 github runtime。
- **Unit tests**：新增 `probeEmulatorRuntimeDetectsShmemPublisher`；20/20 PASS（含所有既有 instance manager tests）。
- **已知限制**：CPU readback（SwiftShader）每幀 8MB 記憶體複製；producer seq 在 boot 前就開始累積；hardware GPU backend（`angle_indirect`）可進一步降低開銷；視覺驗證尚待實際啟動 Chimera UI。

## 歷史狀態（2026-06-16 Session 76）

- **std::promise<void> / std::future<void> 在此環境 100% 崩潰**：MSVCP140.dll 有兩個不相容版本（SDK emulator 捆綁 v14.28、系統 v14.44），`_Associated_state::_Set_value` / `_Set_exception` 在兩個版本的 vtable/layout 之間 null dereference（MSVCP140.dll+0x12c10）。
- **修正**：`frame_buffer.cpp` 的 `postImplSync()` 和 `compose()` 改用 `gfxstream::base::Lock + ConditionVariable`（純 Win32 SRWLOCK + CONDITION_VARIABLE，完全不依賴 MSVCP140）。
- **Android headless boot 已確認**：`tmp/run-syncthread-hasgl-test.py` → `av_crash=False`；`boot_completed_adb=True`（ADB 確認 `sys.boot_completed=1`）；QEMU CPU delta 4.188s（健康）；零 VEH AV 事件。
- 用 Python ctypes + DbgHelp.dll（WinDbg 版）symbolize crash stack：`gfxstream::base::Thread::thread_main` → `RenderThread::main` → `decode` → `FrameBuffer::post` → `postImplSync` → `_Associated_state::_Set_value` → crash。

## 歷史狀態（2026-06-14 Session 75）

- Session 75 確認 stock SDK 15261927 headless mode GPU backend 為**純 Vulkan**：`vulkan-1.dll` 從第一毫秒載入（兩個實例），`libEGL.dll` 整個 emulator 存活期 NEVER 出現，`d3d11.dll` 也不載入。
- 修正 `s_egl_resolved` caching bug：`resolve_angle_egl()` 舊版在 `libEGL.dll` 找不到時也設 `s_egl_resolved=true`，導致背景輪詢 loop 只嘗試一次就永遠短路。修正後只在 SUCCESS 才設 flag，允許 retry。
- 加入模組枚舉（`CreateToolhelp32Snapshot`）：5s mark 與 60s timeout 都 dump GPU 相關模組，實測只有 `d3d9.dll` / `vulkan-1.dll`×2 / `libgfxstream_backend.dll`×2，無 `d3d11.dll` / `libEGL.dll` / `libGLESv2.dll`。
- GetProcAddress IAT hook 捕獲 128+ Vulkan 函式 API surface，包含 `vkCreateSwapchainKHR`[32] / `vkQueuePresentKHR`[36] / `vkGetDeviceProcAddr`[17]。hook 安裝確認（`hooking vkQueuePresentKHR` log 出現），但 headless 模式下 `vkQueuePresentKHR` **零次呼叫**。
- **結論**：GPU frame capture via proxy DLL 在 headless `-no-window` 模式下是永久死路；swapchain 函式被 pre-init 但從未 present。唯一出路仍是 matching SDK build id 15261927 的 custom gfxstream runtime。
- 驗證：proxy build PASS（348 exports）；三次 headless smoke PASS，`no_residual_processes=OK`；Session 75 後無 Chimera/emulator/qemu/adb/ffmpeg 殘留。
- production gRPC（stock SDK + headless + gRPC 62-67 FPS）是目前唯一可驗 display path；`verify-true-1080p60.ps1 -GrpcOnly` 是對應驗證器。

## 歷史狀態（2026-06-13 Session 74）

- Session 74 新增 `scripts\verify-true-1080p60.ps1 -GrpcOnly`：驗證 production gRPC path（stock SDK emulator headless，實際 4-17 FPS，不是 60 FPS）而不要求 custom shared texture runtime。（注意：原始設計的 `effective >= 60` gate 已在 Session 78 修正為實際可達的 stream/guest 門檻。）
- `ParseOnlyLog` 分支依 `-GrpcOnly` 選擇對應 assert；`Require-File` 在 GrpcOnly 跳過 custom runtime 檢查；try 主體不設 CHIMERA_EMULATOR_PATH 與 `CHIMERA_REQUIRE_*_SHARED_TEXTURE`，不帶 `--gfxstream/emugl-shared-texture`。
- 驗證：syntax PASS；pass/fail 合成 log parse-only 邏輯 PASS。
- **blockers 明確化**：gfxstream shared texture — 無 public source 符合 SDK build ID 15261927；EmuGL shared texture — legacy QEMU emulator 僅支援 HAXM/KVM，本機 Hyper-V 環境不可用。
- production gRPC（stock SDK + headless + gRPC 62-67 FPS）是目前最佳可驗 display path；true 1080p/60 shared texture 待 matching gfxstream source。

## 歷史狀態（2026-06-13 Session 73）

- Session 73 修正 `initLibrary` ABI crash 根因：`gfxstream_proxy.c` 用 `void*(void*)` C 假簽名承接 `gfxstream::RenderLibPtr`（`std::unique_ptr<RenderLib>`），在 x64 Windows 下 ABI 不相容，是 boot 前 `-1073741819 (AV)` 的根源。
- 改為 `extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary()` 放在 `gfxstream_proxy_renderlib.cpp`，exact C++ signature pure forward，`#pragma warning(suppress: 4190)` 壓 MSVC C4190。
- 從 `gfxstream_proxy.c` 刪除 C shim；build script 過時註解同步修正；analyzer 同時計數 `renderlib_wrapper initLibrary` 與 `forward name=initLibrary` 兩種格式。
- 驗證：build PASS（348 exports，gate 通過）；headless smoke boot 完成，`initLibrary=1 androidSetOpenglesRenderer=1 rendererVtable=1`；analyzer 正確 FAIL `no 1920x1080 GPU display/resource signal`；`no_residual_processes=OK`。
- 目前 stock headless emulator 沒有 GPU display-post signal；analyzer FAIL 是預期行為（gate 未放寬）。

## 歷史狀態（2026-06-13 Session 72）

- Session 72 新增 `scripts\analyze-gfxstream-proxy-log.ps1`，用於分類 stock-ABI gfxstream proxy log，不啟動 emulator。
- 分析器 fail-closed：必須看到 proxy attach + renderer init，且只有 1920x1080 `stream_renderer_flush` / `stream_renderer_resource_create` / `gfxstream_backend_setup_window` 這類 GPU display/resource signal 才算下一步 hook 候選。
- `android_onPost`、`renderer_hook getScreenshot`、`transfer_read_iov` 會被標成 CPU/readback 風險，不可當 shared texture producer 或 60 FPS 證據。
- 驗證：PowerShell parser PASS；正向合成 proxy log PASS；只有 `android_onPost` 的負向合成 log 如預期 fail；`build-chimera-gfxstream-proxy-runtime.ps1` PASS，348 exports；完整 non-integration `ctest` 20/20 PASS。
- 既有 proxy logs 目前沒有 1920x1080 GPU display/resource signal；它們不能當 true 1080p/60 證據。
- 子代理研究 matching source / hook 線索因使用額度限制失敗，沒有可採用結論。
- 本輪沒有啟動 Android runtime；結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- 真 Android 1080p/60 仍未達標；下一步仍是 matching SDK gfxstream source/ABI，或用 stock-ABI proxy 找到穩定 GPU display-post hook，再接 D3D11 shared texture producer。

## 歷史狀態（2026-06-13 Session 71）

- Session 71 再次固定架構邊界：Chimera 不從零重寫完整 Android VM；短期正確方向是 Chimera shell + headless Android Emulator/QEMU/gfxstream 相容核心 + custom display producer。原生 Android Emulator 視窗外露或多開都是 bug，不是策略。
- gfxstream Vulkan bridge 已補低頻 runtime 診斷：bridge enabled、`recordCopy()` unavailable/ok、`publishFrame()` failure 會按低頻 cadence 記錄，方便定位黑屏卡在 display-post 哪一段，同時避免 per-frame log 干擾 host audio。
- runtime bridge 端也加入 1920x1080 floor；低於最低尺寸會被拒絕，不能把低解析度 producer 當成 60 FPS 證據。
- 驗證：patch parser / build parser PASS；`chimera-ui test-instance-manager test-virtual-machine` targeted build PASS；targeted `ctest` 2/2 PASS；完整 non-integration `ctest` 20/20 PASS。
- source-patched gfxstream runtime 已編過 `ChimeraGfxstreamVulkanSharedTextureBridge.cpp` / `DisplayVk.cpp`，最後由 manifest gate 正確 fail-closed：source snapshot build id `13278158` 不等於 SDK emulator build id `15261927`。
- 結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- 真 Android 1080p/60 仍未達標；下一步仍是 matching SDK gfxstream shared texture producer / verifier PASS，不能用 mixed ABI runtime、raw fallback 或原生 emulator 視窗交差。

## 歷史狀態（2026-06-13 Session 69）

- Session 69 收斂 headless runtime / snapshot shutdown I/O：Chimera 短期不從零自研完整 Android VM，而是 fork/改 Android Emulator/QEMU/gfxstream 作 headless 相容核心；正式使用者面只能有 Chimera 單一視窗，原生 Android Emulator 視窗外露或多開都是 bug。
- `VirtualMachine` 預設 full boot 現在同時帶 `-no-snapstorage -no-snapshot -no-snapshot-load -no-snapshot-save`，避免 emulator 自動使用 `default_boot` snapshot storage。
- `VirtualMachine::stop()` 只有在明確 `CHIMERA_SAVE_QUICK_BOOT=1` 時才走 `adb emu avd snapshot save` / console kill；一般停止改由 Job Object / process tree cleanup，降低 shutdown I/O 對 host audio 的干擾。
- `scripts\verify-true-1080p60.ps1` finally 不再預設送 `adb emu kill`，避免 verifier 自己觸發 emulator shutdown snapshot/I/O。
- 驗證：`test-virtual-machine` target build PASS；`ctest -R test-virtual-machine` PASS；完整 `cmake --build build --config Release` PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS；結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。
- 真 Android 1080p/60 仍未達標；這輪修的是多開/外露/音訊干擾的啟停根因與防回歸。

## 歷史狀態（2026-06-13 Session 68）

- Session 68 補上 strict shared texture watchdog：required EmuGL/gfxstream shared texture 模式下，Android boot 後若 host capture 未配置、metadata mapping 未出現、或 capture 已跑但沒有第一幀，Chimera 會 exit 3，不再黑屏/0 FPS 靜默等待，也不會退回 raw gRPC/ADB fallback。
- `scripts\verify-true-1080p60.ps1` 現在會拒絕 `Required shared texture capture ...` 失敗訊息，並修正 early-exit 診斷要印出 exit code。
- SDK/source 對齊結論：本機 Android Emulator SDK 36.5.11 是 build id `15261927`；目前 `tmp\aosp-sdk-release\hardware\google\gfxstream` 的 `sdk-release` source snapshot 是 `13278158`；官方 `emu-36-1-release` 是 `12579432`，更舊，不能當 matching runtime 來源。
- strict verifier 對目前 invalid custom gfxstream runtime 正確 fail-closed：`Required shared texture runtime is unavailable; exiting`，沒有 raw fallback，沒有多開原生 Android Emulator 視窗。
- 子代理只讀研究確認公開 refs 找不到 `15261927` matching source；`prebuilts/android-emulator-build/archive` 也不是 SDK emulator binary archive。若要繼續走 stock SDK 36.5.11，應以 binary ABI/proxy 探針為準。
- stock gfxstream proxy 已加低風險 C export 觀察：`stream_renderer_init/resource_create/create_blob/export_blob/ctx_attach/ctx_detach/resource_map_info/transfer_read_iov/transfer_write_iov/vulkan_info` 與 `gfxstream_backend_setup_window/set_screen_mask/set_screen_background` 會以 typed wrappers 低頻 log 自然呼叫。不可包 `RenderLib` / `Renderer` C++ object，不可主動 `transfer_read` 或走 `android_setPostCallback` CPU readback。
- `scripts\build-chimera-gfxstream-proxy-runtime.ps1` build PASS，`chimera-gfxstream-proxy.json` 會列出 hooked exports；`sharedTextureProducer=false`，所以它仍只是定位工具，不是 1080p/60 完成證據。
- 驗證：`chimera-ui test-grpc-framebuffer-capture test-instance-manager test-virtual-machine test-qemu-backend` build PASS；script parser PASS；targeted ctest 4/4 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS；結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- 真 Android 1080p/60 仍未達標；下一步仍是取得 SDK 36.5.11 matching gfxstream source/ABI，或在 stock ABI proxy 內找到可穩定定位 display-post resource 的 C export probe，再接 D3D11 shared texture producer。

## 歷史狀態（2026-06-12 Session 67）

- Session 67 修正 visible emulator gate：`CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 現在只代表診斷授權，不能單獨讓正式路徑外露原生 Android Emulator 視窗。
- 主程式只有在同次 CLI 明確啟用 `--native-embed --allow-unsafe-native-window` 或 `--window-capture --allow-unsafe-window-capture`，且同時有 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 時，才設定內部 `CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1`。
- `InstanceManager` normalize 層現在要求外部 unsafe allowance + 內部 diagnostics session 兩者同時成立，才接受 `allowVisibleEmulatorWindow=true`；否則舊設定、測試設定或殘留 env 都會回到 headless / `-no-window`。
- `CHIMERA_EMULATOR_START_VISIBLE` 不再能影響正式路徑；只有通過 visible diagnostics gate 的 instance 才會可見啟動。
- 架構邊界再次固定：Chimera 不走短期完整自研 Android VM；保留 Android Emulator/QEMU/gfxstream 作為 headless 相容核心，但使用者面只能有 Chimera 單一視窗。多開原生 Emulator 視窗是 bug。
- 驗證：targeted build `test-instance-manager test-virtual-machine test-process-launcher chimera-ui` PASS；targeted tests 3/3 PASS；完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS；結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- 真 Android 1080p/60 仍未達標；這輪只收斂多開/外露原生 Emulator 的產品邊界。

## 歷史狀態（2026-06-12 Session 66）

- Session 66 再次固定產品邊界：不從零重寫完整 Android VM；Chimera 保留 Android Emulator/QEMU/gfxstream 相容核心，但底層必須 headless，使用者面只能有 Chimera 單一視窗。外露或多開原生 Android Emulator 視窗是 bug，不是策略。
- custom gfxstream runtime gate 再收緊：`chimera-gfxstream-shared-texture.json` 必須包含 `gfxstreamSourceSnapBuildId` 與 `baseEmulatorBuildId`，且必須匹配 SDK emulator build id。當前 source snapshot `13278158` 不等於 SDK 36.5.11 build id `15261927`，manifest writer 會拒絕這個會 crash 的 mixed ABI runtime。
- 直接 `LoadLibrary` 測試證明 `build\chimera-gfxstream-runtime-sdk-release\lib64\libgfxstream_backend.dll` 可載入，但 direct emulator probe 在 `Initializing gfxstream backend` 後 `0xC0000005`，所以不能再把 marker/export/import 當 runtime ready。
- stock SDK gfxstream proxy default path 可 headless boot，但沒有看到 `android_setPostCallback`；`CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` 會 crash。`stream_renderer_flush` + `stream_renderer_resource_get_info` 低頻 probe 已加入並重建 proxy runtime，headless/no-audio boot completed 後仍沒有任何 flush log；正式 shared texture producer 仍應接 matching source 的 `DisplayVk::postImpl()` GPU post path。
- `GrpcMmapFramebufferCapture` 已補低干擾 fallback：RGBA8888 frame 會優先 publish 到 `SharedD3D11TexturePublisher`，避免每幀 QImage allocation 與 UI thread texture upload；RGB888 或 publisher 失敗才退回 QImage。
- MMAP diagnostic smoke：Android boot completed，log 顯示 `gRPC MMAP D3D11 texture publisher started at 1920 x 1080`，但 dynamic sample 只有 `effective=29.4`。這不是 1080p/60 完成證據。
- 新增 `LowInterferenceProcess` 共用工具；main boot/setup adb、QML Android controls、ADB raw screencap fallback、ScreenRecorder ffmpeg 都套 BelowNormal + low memory priority + power throttling，避免直接 `QProcess` 繞過 `ProcessLauncher` 後再次干擾背景音樂。
- strict true-1080p60 verifier 對不相容 runtime 正確 fail-closed：`Required shared texture runtime is unavailable`，沒有啟動第二個 emulator/qemu，也沒有 raw fallback 假裝達標。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS；結束後沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg`。
- 真 Android 1080p/60 仍未達標。下一步是取得/對齊 SDK 36.5.11 matching gfxstream source/ABI，或在 stock-ABI proxy 中做只觀測的 `stream_renderer_flush`/resource probe，再把 display-post GPU texture producer 接到 D3D11 shared texture；不得退回 raw gRPC/ADB/screenrecord 或可見 stock emulator window。

## 歷史狀態（2026-06-12 Session 65）

- Session 65 釐清產品邊界：不從零重寫完整 Android VM；Chimera 保留 Android Emulator/QEMU/gfxstream 相容核心，但底層必須 headless，使用者面只能有 Chimera 單一視窗。外露或多開原生 Android Emulator 視窗是 bug，不是策略。
- `VirtualMachine` 的 `m_state` 已改為 atomic，背景 exit monitor 不再和 start/stop/state 查詢形成 data race；emulator/qemu process tree 消失時會用明確 `VMState::Error` callback fail closed。
- 重新實測 `build\chimera-gfxstream-runtime-sdk-release`：即使設定 `CHIMERA_DISABLE_GFXSTREAM_VK_D3D11_TEXTURE=1` 關掉 Chimera bridge，60 秒內仍沒有 5554/5555/8554、沒有 ADB device，log 停在 gfxstream feature list，未進 `FrameBuffer::initialize()`。
- 對照 `dumpbin /DEPENDENTS`：stock SDK `libgfxstream_backend.dll` 依賴 `libandroid-emu-agents.dll`、`libandroid-emu-protos.dll`、`libandroid-emu-metrics.dll`；standalone `sdk-release` / `emu36` custom backend 都缺這些 SDK runtime imports，不能再當 SDK 36.5.11 相容 runtime。
- `InstanceManager::probeEmulatorRuntime()` 與 `scripts\write-chimera-gfxstream-runtime-manifest.ps1` 現在除 marker、manifest、`gfxstream_backend_set_screen_background` 外，也要求 SDK runtime imports；缺任一項會回報 `SDK runtime imports are missing` 並拒絕 strict shared texture runtime。
- strict invalid runtime smoke 已驗證：`chimera-ui --gfxstream-shared-texture` 在壞 runtime 下 exitCode=3，沒有留下 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。
- `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` 目前不是穩定路徑；短 probe 只記錄 `initLibrary result=wrapped`，沒有可靠 GRPC/producer 證據。不要把 RenderLib/Renderer wrapper 或 CPU `android_setPostCallback` readback 當 1080p/60 解法。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS。
- 真 Android 1080p/60 仍未達標。下一步只能找 SDK 36.5.11 相容 source/ABI 重建 modified gfxstream，或在 bootable stock-ABI proxy 中找到穩定 GPU display-post hook；不得退回 raw gRPC/ADB/screenrecord 或可見 stock emulator window。

## 歷史狀態（2026-06-06 Session 64）

- Session 64 新增 gfxstream proxy RenderLib/Renderer C++ wrapper probe，但 wrapper 只允許 opt-in；default proxy 仍 forward `initLibrary` 原物件，避免影響 boot。
- `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` 實測會讓 emulator 早退，不可當正式 producer 接線點。
- default proxy hidden/no-audio probe 已恢復：`sys.boot_completed=1`、boot completed in 29283 ms、`leftoverCount=0`，proxy log 顯示 `renderlib_wrapper initLibrary result=forwarded` 與 `android_setOpenglesRenderer ...`。
- Session 63 重新確認架構邊界：不從零重寫完整 Android VM；Chimera 保留 Android Emulator/QEMU/gfxstream 相容核心，但正式產品面只允許 Chimera 單一視窗。
- Session 63 重新驗證 headless visible-window gate：`test-process-launcher` / `test-virtual-machine` 2/2 PASS，`chimera-ui` build PASS，且沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。
- Session 62 補上 headless visible-window watchdog：正式 emulator 啟動不只帶 `-no-window` / hidden process policy，還會檢查 emulator/qemu process tree 是否外露可見 HWND。
- 若 headless 路徑仍冒出 `Android Emulator - ...` 原生視窗，`VirtualMachine::start()` 會立即終止整棵 emulator tree 並回報啟動失敗，避免第二個原生視窗留在桌面上干擾主機音訊/資源。
- 新增 `ProcessLauncher::visibleWindowTitlesInProcessTreeById()` 與 `terminateProcessTreeById()`；`test-process-launcher` 已覆蓋 hidden async launch 不外露視窗。
- Session 62 驗證：`test-process-launcher` / `test-virtual-machine` PASS、`chimera-ui` build PASS，沒有殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。
- Session 60 補上 visible stock Android Emulator window 的雙重保險：`--native-embed` / `--window-capture` 即使帶 unsafe 參數，也必須同時設 `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` 才允許外露原生視窗。
- `InstanceManager` normalize 層現在會擋掉舊設定或程式路徑誤設的 `allowVisibleEmulatorWindow=true`，正式路徑回到 headless / `-no-window`。
- `ChimeraWindow` title 改為 `Chimera`，不再顯示 `Android Emulator`。
- Session 60 驗證：`test-instance-manager` PASS、完整 `ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS、沒有殘留 `chimera-ui` / `emulator` / `qemu-system*`。
- 新增 `scripts\build-chimera-gfxstream-proxy-runtime.ps1` 與 `src\host\runtime\gfxstream_proxy\gfxstream_proxy.c`：會複製 stock SDK emulator runtime 到 `build\chimera-gfxstream-proxy-runtime`，把 stock `libgfxstream_backend.dll` 保存為 `libgfxstream_backend_stock.dll`，再產生同 ABI proxy DLL。
- proxy runtime smoke 已驗證：Chimera 以 `CHIMERA_EMULATOR_PATH=build\chimera-gfxstream-proxy-runtime\emulator.exe` 啟動時，Android 可在約 32 秒達 `sys.boot_completed=1`，guest 顯示為 `1920x1080 / 320 dpi`，且沒有殘留 `chimera-ui` / `emulator` / `qemu-system*`。
- proxy hook 已確認被載入：`CHIMERA_GFXSTREAM_PROXY_LOG` 會記錄 `dll_process_attach=libgfxstream_backend_proxy`，且 SDK 36.5.11 初始化路徑會呼叫 `initLibrary` 與 `android_setOpenglesRenderer`；`stream_renderer_flush` 在目前 boot smoke 中不是活躍入口。
- Renderer vtable patch probe 會讓 boot 提早結束，已撤回；不要把 stock Renderer object 的 vtable hard-patch 當正式路徑。下一步應走 source patch，或以完整相容的 RenderLib/Renderer wrapper 實作。
- `emu-main-dev` source layout 已不同於 `emu-36-1-release`，缺舊 `host/gl/DisplayGl.cpp`；現有 shared texture patch 不適用。`build-chimera-gfxstream-runtime.ps1 -SkipConfigure` 已修成 patch 失敗時 fail-closed，避免假成功。
- `VirtualMachine` 一般啟動現在同時帶 `-no-snapshot`、`-no-snapshot-load`、`-no-snapshot-save`；Quick Boot 未明確開 `CHIMERA_SAVE_QUICK_BOOT=1` 時也帶 `-no-snapshot-save`。proxy boot smoke 關閉時 log 未再出現 `Saving snapshot`。
- 目前 proxy 仍是測量/定位基座，`sharedTextureProducer=false`，不可當 1080p/60 完成證據。下一步要在已確認的 `initLibrary` / renderer object path 包裝或 patch gfxstream source，把 display producer 接到 D3D11 shared texture。
- 結論更新：`build\chimera-gfxstream-runtime` / `build\chimera-gfxstream-runtime-emu36` 這類 standalone-built `libgfxstream_backend.dll` 不能再被當成 SDK emulator 36.5.11 可替換 runtime；marker/manifest 不足以證明可用。
- 實測：`build\chimera-gfxstream-runtime-emu36` 可 build/package，但 direct boot 180 秒沒有 ADB device；`scripts\verify-true-1080p60.ps1` 240 秒內未到 `sys.boot_completed=1`。同 AVD 用 stock SDK emulator 約 36 秒 boot completed。
- ABI 證據：custom DLL 缺 SDK stock backend 需要的 `gfxstream_backend_set_screen_background` export，且多出新版 `stream_renderer_platform_import_resource` / `stream_renderer_wait_sync_resource`，所以 QEMU 會活著但 Android/ADB 不起來。
- `InstanceManager::probeEmulatorRuntime()` 現在要求三件事才算 gfxstream shared texture ready：`ChimeraGfxstreamSharedTextureBridge` marker、合法 `chimera-gfxstream-shared-texture.json`、相容 SDK ABI export。
- `scripts\write-chimera-gfxstream-runtime-manifest.ps1` 現在缺 `gfxstream_backend_set_screen_background` 時會拒絕寫 manifest；`build\chimera-gfxstream-runtime-emu36` 的舊 manifest 已移除。
- 驗證：`test-instance-manager` PASS、`ctest --test-dir build -C Release --output-on-failure -LE integration` 20/20 PASS、gfxstream 相關 PowerShell parser checks PASS。
- 這輪修的是「防止不相容 runtime 造成黑屏/多開/資源干擾」；真 Android 動態 1080p/60 仍未達標。下一步要取得 SDK 36.5.11 對應 source/ABI，或做 stock ABI wrapper 層接 shared texture producer。
- `scripts\build-chimera-gfxstream-runtime.ps1` / `prepare-chimera-gfxstream-deps.ps1` 已支援 `-Branch`，可用於後續抓分支對齊 source；但目前公開 `emu-36-1-release` 對 SDK 36.5.11 仍不夠新。
- `--window-capture` / stock emulator HWND capture 已封成 unsafe opt-in；`--native-embed` 也需要 `--allow-unsafe-native-window` 才會生效。`CHIMERA_ENABLE_NATIVE_EMBED` / `CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW` / `CHIMERA_ENABLE_WINDOW_CAPTURE` / `CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE` 只會被警告並忽略，避免舊環境變數偷開原生 Android Emulator 視窗或工具列。
- `InstanceConfig` / `VirtualMachineConfig` 預設 headless；沒有 `allowVisibleEmulatorWindow` unsafe gate 時，即使舊設定寫 `headless=false`，最後仍會帶 `-no-window`。
- 新增 modern gfxstream shared texture runtime gate：stock `libgfxstream_backend.dll` 只會被標成 `stock gfxstream runtime; Chimera gfxstream bridge will not load`，只有具備合法 `chimera-gfxstream-shared-texture.json` 的 modified runtime 才能被當成 shared texture producer。
- gfxstream gate 現在不只看 manifest：`libgfxstream_backend.dll` 必須內含 `ChimeraGfxstreamSharedTextureBridge` marker，否則 stock binary 加 manifest 仍會被拒絕。
- 新增 `--gfxstream-shared-texture` / `CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE=1` 與 `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE=1` fail-closed 模式；runtime 不可用或 shared texture 沒第一幀時，不會退回 raw gRPC/ADB 或 stock emulator 視窗。
- 新增 `scripts/write-chimera-gfxstream-runtime-manifest.ps1`；manifest 必須宣告 `producer=ChimeraGfxstreamSharedTextureBridge`、`transport=D3D11SharedTexture`、`minWidth>=1920`、`minHeight>=1080`、`targetFps>=60`。
- `scripts/verify-true-1080p60.ps1` 預設 `-RuntimeKind Gfxstream`，預設 runtime path 為 `build\chimera-gfxstream-runtime\emulator.exe`；legacy EmuGL runtime 要明確用 `-RuntimeKind EmuGL`。
- legacy EmuGL bridge 已補 strict no-readback：`CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE` / `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE` 開啟時，`FrameBuffer::post()` 不再退回 `m_onPost` / `ColorBuffer::readback()`；shared texture 初始化硬失敗會 latch，避免每幀重試與錯誤輸出干擾 host audio。
- `scripts\build-chimera-emugl-runtime.ps1` 最新可重新 build 出含 strict bridge marker 的 `build\chimera-emugl-runtime\lib64\lib64OpenglRender.dll`；`emulator.exe -help` 可執行。
- ADB H.264 fallback 已降干擾：移除重複 ffmpeg scale、helper process 套低優先級/低 memory priority/power throttling，解碼 BGRA frame 優先送 `SharedD3D11TexturePublisher`，QImage 僅作 fallback。
- `scripts/build-chimera-emugl-runtime.ps1` 現可產出 classic `emulator.exe` / `emulator64-x86.exe`、完整 legacy EmuGL DLL set、MinGW runtime DLL 與 `chimera-emugl-shared-texture.json`。
- `emulator64-x86.exe -help` 可執行；host 也已新增 classic-compatible args，避免把 `-grpc`、`-window-size`、`-vsync-rate` 傳給 classic runtime。
- 這仍不是 production runtime：短 smoke 證明 classic runtime 無法跑 Android 34 `google_apis_playstore/x86_64`，因 modern image 只有 `kernel-ranchu`，classic path 期待/解析的是 `kernel-qemu`。
- 架構決策：短期不完整自研 Android VM；保留 Android/QEMU 相容層，改寫 host shell、headless display producer、input 與 resource policy，下一步是 modern ranchu/gfxstream shared texture bridge。正式產品路徑只能有一個 Chimera 視窗，不可再多開或外露原生 Android Emulator 視窗。
- modern gfxstream source 已抓到 `tmp\gfxstream-src` 做只讀分析；實際接點在官方 `host\FrameBuffer.cpp::postImpl()` / `host\PostWorkerGl.cpp::postImpl()`。`GpuFrameBridge` 太晚，已經是 CPU pixels；stock DLL forwarder 風險高，因 `libgfxstream_backend.dll` 匯出大量 C++ mangled symbols。
- 真 Android 動態 1080p/60 尚未達標；下一步要 patch/replace modern gfxstream producer 並讓 `scripts/verify-true-1080p60.ps1` 在 dynamic flow PASS，不能回退到 stock emulator HWND 或 raw screenshot fallback。

## 引擎決策（最重要）

**生產引擎**: `emulator.exe`（Google QEMU+WHPX fork，與 BlueStacks 自製引擎同等級）
- `--qemu-backend`（stock QEMU 11 + Cuttlefish）= legacy R&D
- `--hcs-backend`（Hyper-V HCS）= legacy R&D
- 兩者保留不刪，但不是開發重心

**BlueStacks 輸入路徑更正**: `BstkDrv.sys` 是 network/filter driver，**不是** input driver。
BlueStacks 透過 `HD-Bridge-Native.dll` → virtio-input 注入輸入。
Chimera 的等效路徑：Android Console `event` protocol on port 5554（繞過 ADB ~100ms shell-spawn）。

---

## Phase 1–4：核心 MVP（2026-05 之前）

| 功能 | 說明 |
|------|------|
| `emulator.exe` 啟動 | WHPX + AVD `chimera_dev`，Android 34 x86_64 |
| Display streaming | 預設 headless gRPC framebuffer streaming；`NativeEmulatorView` Win32 embed 僅 `--native-embed` opt-in |
| Qt 6 QML shell | D3D11 RHI；`GuestDisplay` 維持 aspect ratio；傳統中文 UI |
| InputBridge v1 | Qt events → QMP（Nagle disabled）→ ADB fallback；60+ 鍵 mapping |
| Gamepad | XInput 60Hz，14 鍵 |
| Macro engine | 背景執行緒，支援 loop |
| Device spoofing | 5 flagship build.prop profiles |
| WASAPI audio | shared-mode render thread |
| MemoryTrimmer | 輪詢 `/proc/meminfo`，壓力觸發 trim |
| ScreenRecorder | FFmpeg H.264 MP4 + PNG fallback |
| ANGLE | `libEGL.dll` + `libGLESv2.dll` via QLibrary（Chrome 147） |

---

## Phase 5–7：Hyper-V Native Stack + Cuttlefish（2026-05-17）

### Phase 5：HCS + HvSocket
- HCS VM lifecycle（`computecore.dll` 動態載入）
- Serial console pipe：HCS 是 SERVER；host 用 `CreateFile` CLIENT 連接
- AF_HYPERV port 16（input）+ port 17（display）→ 26–27 FPS, 0 dropped
- `hyperv_drm.ko` → `/dev/fb0` 1280×720 → BGRA→RGB24 relay ~30 FPS
- WSL2 6.6 kernel 自訂：`dxgkrnl + hv_sock=m + hyperv_drm=m + CONFIG_DMABUF_HEAPS=y`

### Phase 6：AOSP Cuttlefish + SurfaceFlinger
- AOSP VHDXs：system/vendor/userdata/metadata
- Android init → APEX（20+ packages）→ servicemanager → SurfaceFlinger starts（~3.7s）
- **Phase 6c 關鍵 bug**: `CONFIG_DMABUF_HEAPS` 預設未開 → `gralloc.ranchu.so` 無法開 `/dev/dma_heap/system` → SF crash-loop → QEMU 在 t≈7s 退出
  - 修法：`scripts/patch-kernel-dmabuf.sh` 加入 `CONFIG_DMABUF_HEAPS=y`
  - 驗證：`scripts/test-qemu-cuttlefish.py` 5/5 PASS（SF 仍有 crash-loop，但 QEMU 存活）

### Phase 7：chimera-ui.exe --cuttlefish
- `--cuttlefish` 讀 `configs/cuttlefish.json`，啟動 QEMU，VNC port 5901，QMP port 4445
- `VncFramebufferCapture` 連接 RFB 003.008，1280×720
- **VNC 無限 resize loop bug**：QEMU 每次 FBU response 都夾帶 `ExtendedDesktopSize`
  - 修法：只在維度真正改變時才設 `m_resizedThisUpdate=true`
- SMPTE color bars 在 initrd t≈1.5s 畫到 /dev/fb0 → VNC → GuestDisplay ~5 FPS, 0 dropped
- ❌ ADB TCP blocked：`adbd` 需要 `boot_completed=1`，被 SF crash-loop 阻擋

---

## BlueStacks Parity Roadmap v3 P0–P4e（2026-05-18）

### P0 — AndroidConsoleInput
- 新檔案：`src/host/input/AndroidConsoleInput.{h,cpp}`
- 狀態機：`Disconnected → ConnectedUnauthed → AuthPending → Probing → Ready`
- 協議：token file `%USERPROFILE%\.emulator_console_auth_token` → `auth <token>` → `OK/KO`
- 事件：`event mouse <x> <y> 0 <buttons>`；`event text keydown/keyup <keycode>`
- 啟動時 probe 支援的 event 格式；鍵盤 probe 失敗 → keyboard fallback ADB，mouse 繼續 Console
- 指數退避重連；`isConnected()` 只在 `Ready` state 回 true

### P1b — InstanceRuntimeConfig
- 新 struct：`{consolePort, adbPort, grpcPort, adbSerial}`
- index-based port allocation：instance N → consolePort = 5554+2N, adbPort = 5555+2N
- 移除 production code 中所有 `emulator-5554` hardcode

### P1a — CoordinateMapper + InputBridge Pipeline
- 分層：`HostInputEvent → NormalizedInputEvent → GuestInputEvent → BackendCommand`
- `CoordinateMapper`：`hostViewPoint → guestFramebufferPoint → consoleRawPoint`
- 處理：rotation swap/invert；letterbox/pillarbox offset；DPI；window scale
- **重要**: `gx = nx * (m_guestW - 1)`，center of 1280 → 639（不是 640）
- Macro recorder 記錄 pre-transform `Normalized` events，replay 才不受 window resize 影響

### P2 — ProcessLauncher Rewrite
- `runSync` 從 `_popen` → `CreateProcessW` + concurrent stdout/stderr drain threads
- `quoteArg()` 實作 `CommandLineToArgvW` round-trip 規則
- `CHIMERA_PROCESS_LAUNCHER=legacy|native|auto` rollback flag
- 15 unit tests（含 spaces、embedded `"`、Unicode path、metachar、大 stdout/stderr deadlock）

### P3a — LocationSimulator geo fix
- `src/host/integration/LocationSimulator.{h,cpp}`
- `setGeoSink()` → `geo fix <lon> <lat> <alt>` 透過 AndroidConsoleInput 發送
- 節流：1 Hz / 1e-6° movement threshold；順序是 **lon lat alt**（注意不是 lat lon）

### P3b — ClipboardBridge Unicode
- `CF_TEXT` → `CF_UNICODETEXT`（修正 CJK/emoji 遺失）
- `syncHostToGuest` 透過 `clipboard set <text>`；CJK + emoji round-trip 驗證

### P3c — SharedFolder ADR
- `docs/adr/ADR-001-shared-folder.md`
- 選擇：ADB push/pull to `/sdcard/Download/` 作 v1
- 評估的選項：virtiofs（QEMU 未支援）、MTP bridge、content provider、Samba/WebDAV

### P4a — Stub Cleanup
- 刪除：`GraphicsBridge`, `Renderer`, `WindowsNotifier`
- 修正：`Framebuffer::readFrontBuffer()` race → 改成 return `Buffer` by value under lock

### P4b — Integration Tests
- `tests/integration/`：emulator-boot / input-inject / screencap
- `QSKIP` guards（需要 env vars 才執行）；CI 用 `-LE integration` 略過

### P4c — Multi-Instance Grid UI
- `QmlInstanceManager`：batch start/stop, grid layout, sort-by-name
- 每個 instance 獨立的 console/adb/grpc port

### P4d — PerformanceMonitor Visible Latency
- `onInputEvent()` 啟動 timer；`onFrameRendered()` 計算 `m_visibleLatencyMs`
- per-stage timers：capture / decode / render
- `targetHitRate`：統計在 1.5× target interval 內完成的 frames 比例
- **symbol fix**: header 定義 `kMaxSamples`，舊 .cpp 誤用 `MAX_SAMPLES`（已修正）

### P4e + Session 2 補強（2026-05-18，commit 4d5005f）
- **APK 安裝**：`FileDialog` + `adb install -r`，async `QProcess` + `installStatus` property
- **Settings 面板**：FPS selector (30/60/90/120)、螢幕旋轉 (0/90/180/270°)、Eco mode、Root toggle
- **Volume 控制**：`VolumeUp/Down` via `AndroidKeyCode`
- **Mouse drag 修正**：`m_heldMouseButtons` bitmask 在 InputBridge 追蹤，透過 `ev.code` 傳給 Console path
  - 問題：`MouseMove` 沒帶 button state → Console 永遠看到 buttons=0（hover）即使拖曳中
  - 修法：`onMouseButton()` 維護 `m_heldMouseButtons`；`onMouseMove()` 設 `ev.code = m_heldMouseButtons`
- **Root mode**：`enableRoot` → `-writable-system` emulator arg；post-boot `adb root`
- **Screen rotation**：`setGuestRotation(deg)` → `InputBridge::setRotation(deg)` + `adb shell settings put system user_rotation <0-3>`
- **QProcess signal collision bug**：`installApk()` 與 `adbRoot()` 共用同一個 `QProcess*`，舊的 `finished` signal 會累積
  - 修法：`runAdbAsync()` 在每次重用 `QProcess` 前呼叫 `disconnect()`
- **New tests**：`test_android_console_input` (10 cases) + `test_coordinate_mapper` (14 cases) → 9/9 PASS

---

## 目前 Blockers

| Blocker | 原因 | 解法 |
|---------|------|------|
| ADB TCP (`--cuttlefish`) | `boot_completed=1` 未到達，`adbd` 未啟動 | Phase 8：gfxstream → SF stable |
| SurfaceFlinger crash-loop | `goldfish-opengl` vendor 需要 gfxstream cap set 3；stock QEMU 11 virgl 只有 cap sets 1,2 | 選項 A：custom QEMU + gfxstream；選項 B：找有 SwiftShader APEX 的 Cuttlefish image |

---

## Phase 8 計畫（下一步）

**目標**: SurfaceFlinger stable → `boot_completed=1` → ADB TCP → 完整 Android UI

**選項評估**:
- **選項 A（推薦）**: Build custom QEMU with gfxstream（crosvm style）
  - 需要 `gfxstream-vk` renderer 支援；Android Emulator 自帶的 QEMU DLL 已含此能力
  - 風險：build 複雜，需要 Chromium/crosvm toolchain
- **選項 B**: 找 AOSP Cuttlefish images with SwiftShader APEX fallback
  - SwiftShader 不需要 gfxstream；純 CPU rendering
  - 風險：FPS 很低（~5–10 FPS）
- **選項 C**: 直接使用 Android Emulator 的 QEMU DLL（`emulator.exe` 內部使用的版本）
  - 需要 Qt6CoreAndroidEmu.dll（目前缺）
  - 風險：授權問題

---

## 重要 Bug 修正記錄

| Bug | 症狀 | Root Cause | 修法 | Commit |
|-----|------|-----------|------|--------|
| VNC 無限 resize loop | FPS 趨近 0，CPU 飆高 | QEMU 每次 FBU response 都夾帶 `ExtendedDesktopSize` | 只在維度真正改變才設 flag | Phase 7 |
| CONFIG_DMABUF_HEAPS | QEMU t≈7s 退出，SF crash-loop | Kernel 未開 `DMABUF_HEAPS`，`gralloc.ranchu.so` 無法開 `/dev/dma_heap/system` | `patch-kernel-dmabuf.sh` 加 config | d3aa004 |
| Mouse drag 無效 | 拖曳時 Android 只看到 hover，不看到 drag | `MouseMove` 未帶 button state，Console 永遠送 `buttons=0` | `m_heldMouseButtons` + `ev.code` | e4adde0 |
| QProcess signal 累積 | adbRoot 後再 installApk，舊 finished lambda 又觸發 | `if (!m_adbProcess) { new QProcess; connect }` 只建一次，後續不重連 | `disconnect()` before each `connect()` | 4d5005f |
| MAX_SAMPLES symbol | build error | Header 定義 `kMaxSamples`，.cpp 用 `MAX_SAMPLES` | 統一用 `kMaxSamples` | Session 2 |
| CoordinateMapper center test | 期望 (640,360) 但得到 (639,359) | `gx = nx * (m_guestW - 1)`，center 0.5 * 1279 = 639.5 → 639 | 修正測試期望值 | Session 2 |
| MemoryTrimmer | trim 無效 | `am memory-factor set CRITICAL` 語法；正確是 `send-trim-memory` | 修正 ADB 指令 | 0143503 |
| DockButton `detail` property | `QQmlApplicationEngine failed to load component`，整個視窗黑屏無法開啟 | `DockButton` 沒有 `detail` property，Session 2 誤用（只有 `SideButton`/`NavButton` 有）| 移除 `detail`，把 "30 FPS" 文字合併到 `text` | 66d15d2 |

---

## Session 3 補強（2026-05-18）

- ✅ **QML crash fix**：`DockButton` 誤用 `detail` property → app 完全無法開啟 → 移除該行，build 通過
- ✅ **First-boot setup**：`applyGuestFirstBootSetup()` 在每次 `boot_completed=1` 後自動執行：
  - `device_provisioned=1` + `user_setup_complete=1` → 跳過 Android 設定精靈
  - `setup_wizard_has_run=1` → 抑制「完成設定」通知
  - `stay_on_while_plugged_in=3` → 螢幕永不關閉（充電中 = 模擬器永遠在充電）
  - 預設 IME 設為 Gboard
- ✅ **Audio toggle**：`enableAudio` 欄位加入 `VirtualMachineConfig`/`InstanceConfig`；UI 開關在 Settings 進階頁
- ✅ **Device profile selector**：Settings 頁加入 5 款旗艦裝置選單；`QmlInstanceManager::availableDeviceProfiles()` 回傳 DeviceSpoofer 內建清單
- ✅ **Clipboard sync**：Side panel 按鈕 → `ClipboardBridge::instance().syncHostToGuest()`
- ✅ **GPS location UI**：Side panel 新增 GPS 頁面，台北/東京/首爾/上海預設城市 + 自訂座標
- ✅ **感應器注入**：加速計/陀螺儀預設 preset + 自訂，透過 `AndroidConsoleInput::sendSensor()`
- ✅ **電池模擬**：充電狀態 + 電量 slider，透過 `AndroidConsoleInput::sendPowerCapacity/Status()`
- ✅ **File sharing**：`pushFileToGuest()` → ADB push 到 `/sdcard/Download/`
- ✅ **Unit tests 擴充**：ClipboardBridge、LocationSimulator、DeviceSpoofer、MacroEngine、GamepadManager、AudioBridge → **15/15 PASS**
- ✅ **DeviceSpoofer bug fix**：`applyProfile()` 原本對不存在的 AVD 也會建立 junk 目錄 → 改為檢查 `config.ini` 才視為有效 AVD
- ✅ **AudioBridge CoUninitialize bug fix**：`shutdown()` 無條件呼叫 `CoUninitialize()` 即使 COM 是 Qt 初始化的（`RPC_E_CHANGED_MODE`）→ 加 `m_coOwned` flag
- ✅ **ChimeraWindow input forwarding**：`keyPressEvent`/`keyReleaseEvent`/`mouseEvent`/`wheelEvent`/`resizeEvent` → `InputBridge`；`takeScreenshot()`/`showInputMapper()` → emit 對應 signals

---

## 重要 Bug 修正記錄（續）

| Bug | 症狀 | Root Cause | 修法 | Commit |
|-----|------|-----------|------|--------|
| DeviceSpoofer junk dir | 對任意 AVD name 都建立 `overlay/system/` | `applyProfile` 只检查 `avdDir.empty()` | 改為檢查 `avdDir / "config.ini"` exists | 9bcc532 |
| AudioBridge segfault | WASAPI test 結束時 crash | `CoUninitialize()` 在 COM 非本函式所初始化時被呼叫 | `m_coOwned` 旗標追蹤 | c08151f |

- ✅ **GPS 路線模擬**：`QmlAndroidControls::startGpsRoute(waypoints, speedKmh)` → `LocationSimulator::loadRoute` + `startSimulation`；GPS 頁面加入台北→東京預設路線 + 停止按鈕；`main.cpp` 加入 1 Hz timer 驅動 `LocationSimulator::update()`
- ✅ **多開管理補強**：`InstanceManager.batchStart/batchStop/sortByName()` 現在有 QML 按鈕（全部啟動/全部停止/名稱排序）
- ✅ **感應器/電池 Console 狀態回報**：原本 null 時靜默無效 → 現在回傳錯誤訊息 `installStatus`

---

## Session 4 補強（2026-05-19）

- ✅ **APK 安裝通知**：`installApk()` 成功後 emit `notificationRequested` signal；`ChimeraWindow.qml` 加 `Connections { target: AndroidControls }` → `trayIcon.showMessage()`
- ✅ **螢幕尺寸預設**：Settings 頁目前只保留至少 1920x1080 的尺寸入口；`setScreenSize()` / `resetScreenSize()` via `adb shell wm size`
- ✅ **App Manager 強化**：ListView delegate 新增「清除」（`pm clear`）和「卸載」（`adb uninstall`）按鈕；卸載時從本機清單同步移除
- ✅ **截圖存 Downloads + 通知**：`takeScreenshot()` 從 `screenshots/` 相對路徑改為 `screenshotDir()`（Downloads）+ `trayIcon.showMessage()`
- ✅ **OBB 擴充資料安裝**：`installObb(fileUrl, packageName)` → `mkdir -p /sdcard/Android/obb/<pkg>/` → `adb push`；獨立 one-shot QProcess 避免 m_adbProcess 衝突；QML 加「安裝 OBB」SideButton + 對話框
- ✅ **Gamepad Console 路徑**：`onGamepadButton()` 優先呼叫 `hasConsoleKeyboard()` + `sendKeyEvent()`，不再全走慢速 ADB
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 5 補強（2026-05-19）

- ✅ **Multi-touch（Linux MT evdev Type-B）**：`AndroidConsoleInput::sendMultiTouch()` 透過 `event send 3:47:<slot> 3:57:<id> 3:53:<x> 3:54:<y> ... 0:0:0` 注入；`InputBridge` 用 `m_touchPointSlots` + `m_touchSlotIds` 追蹤最多 10 個 MT slot；`GuestDisplay::touchEvent()` 呼叫 `onTouchPoint()`
- ✅ **IME 文字輸入**：`AndroidConsoleInput::sendText()` → `clipboard set <utf8>` + `event keydown/keyup 279`（KEYCODE_PASTE）；`InputBridge::onTextInput()` 呼叫 sendText，ADB fallback 用 `adb shell input text '<escaped>'`；`GuestDisplay::inputMethodEvent()` 轉交 `onTextInput()`
- ✅ **FPS 鼠標鎖定**：`GuestDisplay::setMouseLocked()`：進入時 `Qt::BlankCursor` + 將物理游標 warp 至 widget center；`mouseMoveEvent()` 在 locked 模式計算 delta 累加到 `m_virtualMouse`，每幀 warp 回 center；Escape 解鎖；側邊欄 `SideButton` + `Alt+M` shortcut
- ✅ **Visible latency wiring**：`GuestDisplay::paint()` 在 frame 畫完後 emit `framePainted()`；`main.cpp` 以 `Qt::QueuedConnection` 連接 → `PerfMonitor::onFrameRendered()`
- ✅ **Performance HUD overlay**：`ChimeraWindow.qml` displayShell 左上角 semi-transparent overlay（z=10），顯示 FPS（顏色 warn/danger 分級）、Lat（>50ms warn）、Drop；SideButton "效能 HUD" + `Ctrl+Shift+P` shortcut
- ✅ **Bug：setScreenBrightness 雙 runAdbAsync**：第二條 `adb` 指令被第一條仍在執行的 `m_adbProcess` 阻擋 → 改為兩個 chained one-shot `QProcess`
- ✅ **Bug：GPS 模擬標籤不 reactive**：QML 用 `AndroidControls.isGpsSimulating()`（函式呼叫，不更新 binding）→ 加 `Q_PROPERTY(bool gpsSimulating ...)` + 改 QML 用 property binding
- ✅ **Bug：sendAndroidKeyCode 永遠走 ADB**：Back/Home/Recents/Menu 即使 Console Ready 仍走慢速 ADB → 改為優先 `sendKeyEvent()` via Console
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 6 補強（2026-05-19）

- ✅ **Pinned Apps 釘選常用應用**：`pinApp(pkg)` / `unpinApp(pkg)` 持久化至 `QSettings("chimera/pinnedApps")`；主側邊欄頂部「常用應用程式」section（有釘選時顯示）；App Manager 每個 item 加入「釘選/已釘」toggle；卸載時自動 unpinApp
- ✅ **Network Proxy 設定**：`setNetworkProxy(host, port)` → 三段 chained QProcess 設定 `global_http_proxy_host` / `global_http_proxy_port` / `http_proxy`；`clearNetworkProxy()` 刪除設定；`proxyEnabled` / `proxyHost` / `proxyPort` Q_PROPERTY reactive；Settings 頁面加入 proxy host/port 欄位 + Apply/Clear 按鈕
- ✅ **Network Speed 模擬**：`AndroidConsoleInput::sendNetworkSpeed(profile)` → `network speed <profile>` telnet 指令；`setNetworkSpeed(profile)` → Settings 頁 6 個按鈕（FULL/LTE/HSDPA/UMTS/EDGE/GPRS）
- ✅ **Shake 震動模擬**：3 段快速加速度脈衝（±15 m/s²，80ms 間隔）→ Console sensor injection；Sensor/Battery 頁「震動裝置」按鈕
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 7 補強（2026-05-19）

- ✅ **Custom Cursor / 十字準心游標**：`GuestDisplay::setCursorMode(int mode)`（0=標準箭頭，1=十字準心）→ `QQuickItem::setCursor(Qt::CrossCursor)` / `unsetCursor()`；與 FPS mouse lock 正確互動（lock 時 BlankCursor 優先，解鎖後根據 `m_cursorMode` 恢復）；`cursorMode` Q_PROPERTY reactive；側邊欄「游標：十字準心 / 標準」SideButton
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 8 補強（2026-05-19）

- ✅ **缺失鍵盤快捷鍵**（對齊 bluestacks.conf）：`Ctrl+Shift+3`=震動、`Ctrl+Shift+4`=旋轉（循環 0/90/180/270）、`Ctrl+Shift+X`=Boss Key 縮至工作列、`Ctrl+Shift+T`=Trim Memory、`Ctrl+Shift+M`=靜音切換、`Ctrl+Shift+6`=開啟 Downloads 資料夾
- ✅ **toggleMute()**：`KEYCODE_VOLUME_MUTE(164)` via InputBridge/Console
- ✅ **trimMemory()**：`adb shell am send-trim-memory com.android.systemui RUNNING_CRITICAL`
- ✅ **downloadDir()**：`QStandardPaths::DownloadLocation` 回傳路徑供 QML 開啟
- ✅ **旋轉狀態同步**：`root.currentRotation` property；Settings 頁旋轉按鈕同步更新；`Ctrl+Shift+4` 循環旋轉並顯示狀態；Settings 頁旋轉按鈕 highlighted 反映當前旋轉
- ✅ **Boss Key**：`root.hide()` + tray notification（雙擊圖示可還原）
- ✅ **Tests**：15/15 PASS（無迴歸）

*Updated: 2026-05-19 — Session 8*

---

## Session 9：顯示路徑改為 gRPC streaming + Console 輸入修正（2026-05-19）

### 問題背景
使用者回報：開啟 Chimera 時 emulator 仍會彈出獨立的原生視窗，沒有乖乖內嵌。

### 根因
舊的 `NativeEmulatorView` Win32 `SetParent` 視窗嵌入法本質脆弱：modern Android emulator
（Qt 6.5.3）擁有複雜的多視窗群組（device 視窗 + 垂直工具列 + Extended Controls + 大量
helper 視窗）。把其中一個視窗 reparent 進外部 process 的視窗會破壞 emulator 的 Qt 視窗
管理——emulator 會銷毀/重建視窗，最後被嵌入的常常是**工具列**（`Qt653QWindowToolSaveBits`）
而非 device 視窗，畫面變黑。每個 session 都在打地鼠。

### 解法（架構決策）
**改用 gRPC framebuffer streaming 為預設顯示路徑**（BlueStacks 做法——自己渲染 guest
framebuffer，不 wrap 別的 process 的視窗）：
- `main.cpp`：`nativeDisplayEnabled` 改為 `--native-embed` opt-in；預設 `streamCapture=true`
- emulator 以 `cfg.headless=true` → `-no-window` 啟動，**完全不會有彈出視窗**
- 幀流經 `GrpcFramebufferCapture`（port 8554）→ `GuestDisplay` QML item 渲染
- 新增 context property `nativeEmbedEnabled`；`ChimeraWindow.qml` 的
  `NativeEmulatorView.nativeEmbeddingEnabled` 綁定它（預設 false，NativeEmulatorView 休眠）
- legacy Win32 embed 經 `--native-embed` 仍可用，保留不刪

### Console 輸入修正（`AndroidConsoleInput.cpp`）
手動 telnet 驗證 emulator console 協定後修正兩個 bug：
1. **auth 解析**：`auth <token>` 後 console 回 `Android Console: type 'help'...` 接著
   `OK`，舊程式把第一行資訊 banner 誤判為拒絕。改為只有 `KO` 開頭才是拒絕，其他資訊行
   忽略、繼續等 `OK`/`KO`。
2. **probe 卡死**：console 錯誤終止行格式是 `KO: <reason>`（冒號），舊程式只認 `KO`/`KO `，
   導致 probe 永遠等不到終止行。改用 `line.startsWith("KO")`。
3. **`event keydown` 不存在**：emulator console `event` 只有 `send/types/codes/text/mouse`
   子指令，沒有 `keydown`/`keyup`。probe 正確偵測到 → keyboard 退回 ADB，mouse 走 console。
   狀態正常達 `Ready`。

### 驗證
- emulator headless 啟動，0 個彈出視窗
- Android 開機完成，主畫面渲染於 Chimera viewport 內
- 觸發 `am start` 開啟 Chrome → gRPC 串流即時更新（畫面變動推幀，靜態 0 FPS 屬正常省電）
- `AndroidConsoleInput` 狀態 `Disconnected→ConnectedUnauthed→AuthPending→Probing→Ready`

### gRPC 顯示效能修正（Session 9 後段）
症狀：顯示內嵌後，持續動畫下 FPS 僅 2–15、幀間隔達 16 秒。

隔離測試（python gRPC client 直接打 emulator）：
- `streamScreenshot`（server-streaming RPC）：**3 幀 / 21 秒 = 0.1 FPS**——此 RPC 被節流/壞掉
- `getScreenshot`（unary RPC）輪詢：~24/s；管線化（depth 2–4 並行）：**50–55/s**
- 結論：瓶頸是 emulator 的 `streamScreenshot`，**不是** Qt HTTP/2

修法：`GrpcFramebufferCapture` 從 `streamScreenshot` 改為**管線化輪詢 `getScreenshot`**
（`m_pipelineDepth=3` 個 unary 請求並行 in-flight，完成一個就補一個）；錯誤時 200ms
backoff 避免開機前 tight-loop。

驗證：持續動畫下 **30–44 FPS**，幀時間 24–32ms，最大 <100ms，0 dropped（修正前 2–15 FPS
含多秒停頓）——約 10–20× 提升。

### 已知待改善
- console keyboard 走 ADB fallback（emulator console 無 `event keydown`，非 bug）

## Session 10 — gRPC 擷取性能優化（CPU 卡頓）

症狀：使用者回報「電腦超卡」，且 FPS 達不到 60。

根因：`GrpcFramebufferCapture` 管線化輪詢**無節流**——depth-3 pipeline 每收到回應立即
再發 `getScreenshot`，以最高速忙輪詢。同時擷取改成原生全解析度（~6MB/幀）。兩者疊加
把 chimera-ui 與 emulator 兩邊 CPU 都打滿，反而拖垮吞吐，60 FPS 也達不到。

修法 A — gRPC 擷取（`GrpcFramebufferCapture` + `main.cpp`）：
1. **幀率節流**：新增 `scheduleNext()`，以 `QElapsedTimer` 把 dispatch cadence 鎖在
   目標幀間隔（`m_intervalMs`，main.cpp 設 16ms ≈ 60FPS）。落後時就立刻發、不爆衝補。
   回應完成改呼叫 `scheduleNext()` 而非立即 `sendRequest()`。
2. **pipeline depth 3→4**：節流已鎖死 dispatch 速率，故加深 depth **不增加 CPU**，
   只是讓穩定 cadence 能撐過較長的 RPC 來回延遲（~60ms 仍可維持 ~60FPS）。
3. **擷取寬度上限 720px**（原為 0=原生）：全解析度的傳輸/protobuf 解碼/GPU 上傳成本
   遠高於 emulator 端一次下採樣；720px 對顯示 widget 仍過採樣，畫質無感、CPU 砍半。

修法 B — 程序優先級（`main.cpp:698`）：emulator/qemu 原以 `processPriority="high"`
（HIGH_PRIORITY_CLASS）啟動，一顆 4-vCPU VM 跑高優先級會搶佔主機所有一般優先級執行緒
——桌面、瀏覽器、**音訊執行緒**——導致全系統卡頓、播放的音樂跳針。改為 `"normal"`，
交給 OS 排程公平分配；eco mode 仍可在背景時降到更低。

修法 C — 擷取管線 stall watchdog：實測發現開機後螢幕轉靜止時，4 個並行
`getScreenshot` HTTP/2 stream 會全部 hang 住、`finished` 永不觸發，擷取整個凍結且
不自我恢復（觸發畫面變動也救不回）。對策：
- `GrpcFramebufferCapture` 內建 1Hz watchdog，首幀後啟用；無幀超過 2s 即 `restartPipeline()`
  （abort 全部 in-flight、重新 prime）。
- 每個請求加 `setTransferTimeout(2s)`，hang 的 stream 會被中止轉為一般 error→重試。

驗證（Android 34，chimera_dev，實機，16 邏輯核）：
- 程序優先級：`emulator`／`qemu` 由 High → **Normal**（已實測確認）
- chimera-ui CPU：忙輪詢爆滿 → **4.7%**（≈0.75 核）；RAM ~240MB
- 擷取 watchdog：60s 觀測幀數持續推進、watchdog 觸發 1 次即自動恢復，不再永久凍結
- 擷取幀率：靜止畫面 ~12 FPS（無新幀屬正常）、UI 動畫中 ~30–40 FPS
- 15/15 unit tests PASS，無回歸

**FPS 上限說明**：host 擷取管線已非瓶頸（pacing 目標 16ms、depth 4、CPU 僅 4.7% 有餘裕）。
`getScreenshot` 會等 guest 渲染出新幀才回傳，故實際幀率 = guest 產幀速率。要逼近 60 FPS
需 guest 端持續以 60Hz 渲染（遊戲負載），屬 emulator/GPU 設定範疇，非 host wrapper 可控。

## Session 10 補充 — 版控衛生

- `.gitignore` 補上 debug 產物（`*.err *.out *.ppm verify*.png qemu_*.png chimera-perf.*`）、
  R&D 腳本（`run-qemu-*.ps1 test-qemu-*.bat`）、BlueStacks binaries（`Binaries/ Client/ Engine/ Dumps/`）
- 刪除 6.03 GB 誤產生垃圾檔（`-`、` 2` 等磁碟映像殘骸）
- `AGENTS.md` 新增「Commit 排除」章節，與 `.gitignore`／`CLAUDE.md` 對齊

## Session 11 — 載入 LOGO 重疊、鍵盤延遲、FPS 上限調查

### LOGO 重疊（載入畫面）
`GuestDisplay::paint()` 在無畫面時自繪「等待 Android 畫面...」黑底文字，QML 的 loading
`Column`（"C" 標誌 + 「等待 Android 啟動…」）又疊在同一置中位置 → 兩個載入指示重疊。
修法：`GuestDisplay` 無幀時只填黑，載入畫面 UI 完全由 QML `Column` 單一負責。

### 鍵盤延遲（按鍵響應）
根因：實測確認 emulator console（5554）**無可用鍵盤通道**——`event keydown` 不存在；
`event send` 的 EV_KEY 只送到觸控裝置（`getevent` 證實鍵盤裝置 event13 收不到）。
故鍵盤一直走 ADB `input keyevent`（~100ms/鍵 shell spawn）。

修法：新增 `EmulatorGrpcInput`（`src/host/input/`），走 emulator gRPC
`EmulatorController.sendKey` RPC（port 8554）。`KeyboardEvent` proto：codeType=Evdev、
eventType、keyCode。InputBridge 鍵盤優先序改為 **gRPC → QMP → ADB**；IME 文字走
gRPC `KeyboardEvent.text`。
驗證：`getevent /dev/input/event13` 確認 gRPC sendKey 的 KEY_A/KEY_B 真的送達 guest
鍵盤；延遲 <5ms（vs ADB ~100ms）。

### FPS 上限調查（穩定 60 幀目標）
以 python gRPC client 直測 emulator screenshot API：
- `getScreenshot` 序列輪詢：~17 fps（靜止）；並發 depth 2≈40、4≈43、8/12≈45 — **~45fps 飽和**
- `streamScreenshot`：動畫中 15s 內 **0 幀**（此 build 上壞掉，非節流）
- ImageFormat 的 width/height resize 請求被忽略（payload 恆為原生 1280×720）
app 實測（QNetworkAccessManager HTTP/2 多工，比 python 執行緒測試效率高）：
持續動畫中 **60–68 FPS**（幀計數 ~62/s）、Avg 幀間隔 15–17ms、0 dropped；偶發
emulator getScreenshot stall（Max 100–176ms）時短暫掉到 ~40–50。靜止畫面低 fps 屬正常
（無新幀可抓）。pipeline depth 維持 4（depth 8 實測未改善、反增 stall 尖刺）。
殘留掉幀尖刺為 emulator 端 getScreenshot 偶發停頓；完全消除需 shared GPU texture /
custom QEMU 顯示路徑（免截圖輪詢），屬顯示架構層級變更，未在本 session 動。

*Updated: 2026-05-19 — Session 11*

---

## Session 12 — 版控衛生清理與文件交接（2026-05-21）

### 清理範圍

- 刪除 root 層 ignored R&D/output 產物：Android ISO、QEMU installer、QCOW2、QEMU/debug logs、
  `chimera-perf.*`、`run-qemu-*.ps1`、`test-qemu-*.bat`、`test_hvsock.exe`。
- 刪除大型可重建輸出：`out/`（cuttlefish/test-vm VHDX/RAW/ISO/kernel artifacts）。
- 刪除 runtime/擷取資料夾與錯誤路徑殘留：`instances/`、`recordings/`、`screenshots/`、`tmp/`、
  `DWorkspace_cloudPersonal_Projectchimerathird_partyqemu-new/`、`/`。
- 保留本機開發快取：`build/`、`third_party/android-sdk/`、`third_party/android-avd/`、`third_party/ffmpeg/`。

### 版控確認

- `git ls-files --others --exclude-standard` 清理後只剩真正要審查的新檔：
  `src/host/input/EmulatorGrpcInput.cpp`、`src/host/input/EmulatorGrpcInput.h`、`tasks/todo.md`。
- `git ls-files -oi --exclude-standard` 清理後只剩 `build/` 與 `third_party/` ignored cache。
- `AGENTS.md` / `CLAUDE.md` / `CONTEXT.md` 已補上 commit 排除與清理交接規則。

## Session 13 — gRPC 60fps 穩定 + orphan qemu 根因修復（2026-05-21）

### 使用者回報

「一開就會直接讓我整個電腦當機，一樣變得很卡、響應速度很慢。在他能徹底穩定 60 幀以前，都不要停下來。」

### 根因分析

整機卡死/資源爆炸有**三個互相加乘的成因**：

1. **gRPC 擷取 busy-polling + thundering herd**：`scheduleNext()` 尚未存在時，pipeline 無節流；
   stall watchdog abort 全部 in-flight 再 re-prime depth，造成 duplicate `getScreenshot` 風暴 → CPU 打滿。
2. **原生解析度擷取**：全解析度每幀 ~6MB，傳輸/protobuf 解碼/GPU 上傳成本遠高於 emulator 端一次下採樣。
3. **orphan qemu 累積**：chimera-ui crash / force-kill 後 qemu-system-x86_64-headless.exe（~2.7GB）
   殘留；下次啟動又開新 VM，雙 VM 同時跑，RAM/CPU/disk 同時爆量，整機凍結。

### 修法 A — 640×360 擷取解析度（`main.cpp`）

```cpp
grpcCaptureWidth = 640;
grpcCaptureHeight = 360;
```

- 1280×720 RGB888 ≈ 2.76 MB/幀；640×360 ≈ 0.69 MB/幀（4× 降低）。
- 驗證方式：grpcurl 直接打 `getScreenshot` with `width=640,height=360` → 回傳確為 640×360。
- emulator 端 server-side downscale 有效，畫質對內嵌 widget 仍過採樣、肉眼無感。

### 修法 B — pipeline stall 不 abort（`GrpcFramebufferCapture.cpp`）

```cpp
void GrpcFramebufferCapture::restartPipeline() {
    if (!m_running) return;
    // Do NOT abort in-flight requests here...
    const qint64 now = m_paceTimer.elapsed();
    m_lastFrameMs = now;
    if (m_nextDispatchMs < now) m_nextDispatchMs = now;
    for (int i = static_cast<int>(m_replies.size()); i < m_pipelineDepth; ++i)
        scheduleNext();
}
```

- 舊行為：abort 全部 in-flight + 重新灌滿 depth → emulator 已經慢時再塞爆請求 → ~5fps 永久崩潰。
- 新行為：只補缺少的 slot，不 abort 正在進行的請求；已經有 transferTimeout 會自行回收。

### 修法 C — 程序優先級 Normal（已知，Session 10 已記錄）

emulator/qemu 由 High → Normal，避免搶佔主機音訊執行緒。

### 修法 D — orphan process 預防（`ProcessLauncher.cpp`，尚未驗證）

```cpp
HANDLE acquireKillOnCloseJob() {
    static HANDLE job = []() -> HANDLE {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (!h) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
        return h;
    }();
    return job;
}
```

`runAsync()` 啟動流程改為：
```cpp
// CREATE_SUSPENDED → AssignProcessToJobObject(job, pi.hProcess) → ResumeThread(pi.hThread)
```

- 目的：chimera-ui 被 force-kill 或 crash 時，Windows 自動殺掉整個 emulator+qemu tree。
- force-kill 行為已於 Session 14 build/runtime 驗證通過。

### 驗證結果（clean launch，先殺所有 stale qemu/emulator）

```
[Perf] FPS: 68.7 | Avg: 14.9ms | Max: 40.0ms | Dropped: 0 / 271
[Perf] FPS: 60.1 | Avg: 15.6ms | Max: 162.0ms | Dropped: 0 / 572
...
[Perf] FPS: 62.1 | Avg: 15.5ms | Max: 42.0ms | Dropped: 0 / 8074
```

- 持續 60–68 fps，dropped=0，平均 ~15ms。
- **關鍵前提**：每次測試前必須確認沒有 stale qemu process，否則 FPS 直接掉到 0–1。

### 已知待改善（Session 14 已處理前兩項）

- temporary gRPC diagnostics 已移除。
- `AssignProcessToJobObject()` 失敗會發 warning。
- 冷開機問題已於 Session 15 導入 Quick Boot snapshot，full boot 44s → snapshot boot 10s。

---

## Session 14 — Job Object force-kill 驗證 + gRPC cleanup（2026-05-22）

### 修正

- `ProcessLauncher::runAsync()` 的 kill-on-close Job Object path 補上 warning：
  `CreateJobObjectW`、`SetInformationJobObject`、`AssignProcessToJobObject`、`ResumeThread`
  任一失敗都會記錄；`SetInformationJobObject` 失敗會關閉 job handle 避免假成功。
- `ResumeThread()` 失敗時會終止尚未恢復的 child process，關閉 redirect pipe read handles，
  避免留下 suspended emulator process。
- `GrpcFramebufferCapture` 移除 temporary `[GrpcDiag]` 日誌，並修正 `restartPipeline()` header
  註解：現在只補缺少 pipeline slot，不 abort in-flight requests。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：15/15 PASS。
- Force-kill orphan 測試通過：
  1. 啟動 `build/Release/chimera-ui.exe`。
  2. 確認 `emulator` 與 `qemu-system*` 已啟動。
  3. `Stop-Process -Id <chimera-ui pid> -Force`。
  4. 20 秒內確認沒有 `emulator` / `qemu-system*` 殘留。
- `chimera-debug.log` 未出現 `ProcessLauncher` Job Object warning，本機 assign/resume path 正常。

### 狀態

- orphan qemu 導致雙 VM、整機卡死的 force-kill 路徑已驗證修復。
- 若未來看到 `AssignProcessToJobObject failed`，該啟動不保證 emulator tree 會跟 host 同生共死；
  結束前需檢查並清理 orphan qemu。
- 冷開機已進入 Quick Boot snapshot path；下一步可做 snapshot 失效偵測與 UI 控制。

---

## Session 15 — Quick Boot snapshot path（2026-05-22）

### 修正

- `VirtualMachine` 新增 `quickBoot` 設定，預設啟用；啟動參數由固定 `-no-snapshot`
  改為 `-snapshot chimera_quickboot`。
- `CHIMERA_QUICK_BOOT=0` 可回退到 full boot / `-no-snapshot`，用於隔離 snapshot 損毀或啟動異常。
- `VirtualMachine::stop()` 在正常停止時先嘗試：
  `adb -s emulator-5554 emu avd snapshot save chimera_quickboot`，再 `adb emu kill`；
  失敗才 fallback 到 `TerminateProcess`。
- `VirtualMachine::buildEmulatorArgs()` 抽出成可測 API，避免啟動參數回歸只能靠 runtime 才發現。
- 新增 `test-virtual-machine`，覆蓋 quick boot 開/關時的 `-snapshot` / `-no-snapshot` 參數。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime quick boot 驗證：
  1. 第一次啟動達 `sys.boot_completed=1`：44s。
  2. `adb emu avd snapshot save chimera_quickboot` 成功。
  3. 第二次啟動達 `sys.boot_completed=1`：10s。
  4. 驗證結束後沒有 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

### 狀態

- 啟動時間已從冷開機數十秒降到接近 10 秒級，往 BlueStacks quick launch 體驗前進。
- 若 snapshot 與硬體設定不相容，先用 `CHIMERA_QUICK_BOOT=0` 回退 full boot。
- 後續可加 UI toggle、snapshot 失敗自動刪除重建，以及更完整的啟動時間迴歸測試。

---

## Session 16 — Quick Boot fallback hardening（2026-05-22）

### 修正

- `VirtualMachine::start()` 現在使用同一套 `buildEmulatorArgsForConfig()` 產生啟動參數。
- Quick Boot 啟動若在 1.5s 內退出，會記錄：
  `Quick Boot launch exited early; retrying with full boot`
  並自動用 `quickBoot=false` 重試 full boot。
- `VirtualMachine::buildEmulatorArgs()` 仍保留公開可測 API，單元測試覆蓋 snapshot 開/關參數。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  1. 使用既有 `chimera_quickboot` snapshot 啟動。
  2. 12s 達 `sys.boot_completed=1`。
  3. `adb emu avd snapshot save chimera_quickboot` 成功。
  4. 驗證結束後沒有 `chimera-ui` / `emulator` / `qemu-system*` 殘留。
- `chimera-debug.log` 未出現 quick boot early-exit fallback 或 Job Object warning。

### 狀態

- Quick Boot 仍保有 10-12s 級啟動時間；snapshot 若壞到讓 emulator 早退，會自動 full boot retry。
- `CHIMERA_QUICK_BOOT=0` 仍可作為人工隔離開關。
- 下一步可把 Quick Boot 狀態/重建 snapshot 做成 UI 控制，或開始處理 game-level profiling。

---

## Session 17 — Quick Boot runtime verifier（2026-05-22）

### 修正

- 新增 `scripts/verify-quick-boot.ps1`，把 Session 15/16 的手動 runtime smoke 變成可重跑腳本。
- 腳本流程：
  1. clean start 並移除 stale `chimera_dev.avd/*.lock`（只在確認無 `chimera-ui` / `emulator` / `qemu-system*` 後）。
  2. 用 `CHIMERA_QUICK_BOOT=0` full boot，等待 `sys.boot_completed=1`。
  3. 保存 `chimera_quickboot` snapshot。
  4. 用 Quick Boot 重啟並檢查秒數門檻。
  5. 再保存 snapshot，結束後確認無 orphan process。
- 腳本改為正常啟動 GUI process，並加入 early-exit 偵測，避免 app 已退出卻空等 ADB。

### 驗證

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25`
- 最終結果：
  - full boot：66.7s 達 `sys.boot_completed=1`
  - Quick Boot：9.7s 達 `sys.boot_completed=1`
  - threshold：25s
  - 結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留

### 狀態

- Quick Boot 現在有可重跑的本機 runtime regression smoke。
- 後續若調整 emulator flags、AVD hardware config、snapshot 名稱或 lifecycle，都先跑此腳本避免啟動時間回歸。

---

## Session 18 — 60 FPS Stream + Landscape Boot Setup（2026-05-22）

### 修正

- 預設顯示路徑維持 headless gRPC streaming；native Win32 embed 實測黑畫面/工具列外漏，保留為 `--native-embed` opt-in。
- ADB raw display fallback 預設停用，只有 `--adb-display-fallback` 才啟用，避免 1 FPS screencap 覆蓋 gRPC。
- AVD hardware config 補 `hw.initialOrientation=landscape`；guest boot 後套用 `wm size 1280x720`、`wm density 240`、60Hz、動畫關閉、固定 performance mode。
- full boot 後自動 `KEYCODE_WAKEUP`、`wm dismiss-keyguard`、`KEYCODE_MENU`、`KEYCODE_HOME`，避免 Stream 停在近乎空的鎖定/載入畫面。
- Quick Boot 預設改為關閉；只有 `CHIMERA_QUICK_BOOT=1` 才用 `chimera_quickboot` snapshot，避免壞 snapshot 造成 ADB offline 或空畫面。
- Qt gRPC cache warning spam 已過濾，避免 runtime log 被 `QNetworkReplyImpl ... caching was enabled` 洗掉。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- clean full boot runtime：`sys.boot_completed=1`，`wm size=1280x720`，`wm density=240`，ADB screenshot 為正常橫向 Home。
- gRPC runtime log：61-65 FPS，0 dropped；未啟動 native attach，未啟動 ADB fallback。

### 狀態

- 使用者看到黑/空畫面時，先確認狀態列應為 `Stream · 已連線`；若出現 `Native · 已連線`，代表走到 opt-in legacy path。
- 若需要驗證 Quick Boot，先跑 `scripts/verify-quick-boot.ps1` 重建 snapshot；一般互動與顯示除錯預設用 full boot。

---

## Session 19 — 1080p guest + clickable gRPC touch（2026-05-24）

### 修正

- Android guest 預設改為 1920x1080 landscape / 320 dpi；AVD hardware、`wm size`、instance defaults、設定頁 1080p preset 已同步。
- `GuestDisplay` 繼續用 1920x1080 logical guest size 做座標映射；顯示 capture 預設改為 1024x576 raw gRPC，並加 `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` 供 benchmark 調整。
- `EmulatorGrpcInput` 新增 `sendTouch`，普通滑鼠左鍵與 QML touch 事件優先走 emulator gRPC touchscreen，不再讓 Android Console `event mouse` 假成功吃掉 tap。
- `GuestDisplay` painting 啟用 `SmoothPixmapTransform`，避免 capture 尺寸低於 viewport 時出現硬縮放鋸齒。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime full boot：`sys.boot_completed=1`，`wm size=1920x1080`，`wm density=320`。
- 1080p raw gRPC 實測只有 15-32 FPS；1280x720 raw 約 35-59 FPS；1024x576 raw 達 62-67 FPS，0 dropped。
- emulator gRPC `sendTouch` runtime smoke：點 Settings 後 `dumpsys activity/window` 皆看到 `com.android.settings`，證明 guest 真有收到觸控。

### 狀態

- 預設體驗改為 1080p Android guest + 60+ FPS responsive stream。若未來要全 1080p raw stream，需要改 shared memory / shared texture capture，不能再靠 `getScreenshot` raw payload 硬推。

---

## Session 20 — Truthful FPS + lower overhead + Chimera Launcher（2026-05-24）

### 修正

- `PerformanceMonitor` 重新定義指標：
  - `fps` / `guestFps` = Android guest 內容真的變更的幀率。
  - `streamFps` = capture loop 收到的 frame/reply rate。
  - `renderFps` = Qt `GuestDisplay` 實際 paint rate。
  - `duplicateRate` / `duplicateFrames` = 重複畫面比例與數量。
- gRPC capture 對 raw frame 做 fingerprint；重複 frame 只 emit `streamFrameReceived(false)`，不再 emit `frameReady()`，因此靜止畫面不會一直 `setFrame()` / repaint / feed recorder。
- gRPC capture 新增 idle 降頻：內容或輸入活躍時 16ms，連續重複畫面後退到 100ms；`InputBridge` 事件會通知 capture 回到互動頻率。
- QML HUD / 側欄改成 Guest / Stream / Render / Dup 分離顯示，避免用單一 FPS 誤導。
- 新增真正 Android HOME app：
  - `tools/chimera-launcher/AndroidManifest.xml`
  - `tools/chimera-launcher/src/com/chimera/launcher/MainActivity.java`
  - `tools/chimera-launcher/res/values/styles.xml`
- 新增 `scripts/build-chimera-launcher.ps1`，用 Android SDK build-tools + javac/d8/zipalign/apksigner 產生 `build/launcher/chimera-launcher.apk`。
- Host 在 Android `sys.boot_completed=1` 後自動 install `com.chimera.launcher`、嘗試設為 HOME、再啟動 HOME。

### 驗證

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1`：APK build + `apksigner verify` 通過。
- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - `wm size` = `Physical size: 1920x1080`
  - `wm density` = `Physical density: 320`
  - `pm path com.chimera.launcher` 存在
  - HOME top activity 包含 `com.chimera.launcher`
  - `am start -a android.settings.SETTINGS` 後 top activity 包含 `com.android.settings`
  - log 有 `[Perf] Guest: ... | Stream: ... | Render: ... | Dup: ...`
- 靜止首頁 smoke 顯示 `Guest: 0.0 FPS`、`Stream: ~10 FPS`、`Dup: 100%`，代表重複靜止畫面不再被報成 60 FPS。

### 狀態

- FPS 顯示已改為 truthful metrics；靜止畫面顯示 Guest 0 FPS 是正確結果，不再謊報。
- 主機空轉開銷降低：重複畫面不重繪，idle capture 降頻；互動與內容變化會恢復 60Hz capture cadence。
- Chimera 現在有 Android 底層上的乾淨 HOME launcher；仍不是完整 BlueStacks 級客製 ROM，下一步若要更接近需要移除/停用 Pixel/Google setup 元件與做 app store/search/keymap 深整合。

---

## Session 21 — Black screen fix + simplified sidebar（2026-05-24）

### 修正

- `com.chimera.launcher` 不再於 Activity 啟動時強制 immersive hide system bars；改成正常可見、非純黑的 HOME，避免只剩黑底與狀態列。
- Launcher root view 明確使用 match-parent layout，新增固定 `CHIMERA` / `Apps` 標題與 `No launchable apps` empty state。
- Host launcher install flow 在 install/set-home 後新增：
  `am force-stop com.chimera.launcher` →
  `am start -n com.chimera.launcher/.MainActivity -a MAIN -c HOME` →
  generic HOME intent。
- 右側效能卡從 Guest/Stream/Latency/Dup 混合卡改成單一乾淨 FPS 數字；目前主卡使用 `PerfMonitor.streamFps`，詳細真假分流仍保留在 log/HUD。
- 主側欄移除佔位較重的 OBB、推送/拉取檔案、GPS、感應器、多開、巨集、游標模式、效能 HUD 切換，保留常用且可直接驗證的操作。

### 驗證

- `scripts/build-chimera-launcher.ps1`：APK build + sign verify 通過。
- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - `wm size` = `Physical size: 1920x1080`
  - `wm density` = `Physical density: 320`
  - top activity 包含 `com.chimera.launcher`
  - `uiautomator dump` 包含 `CHIMERA` / `Apps`
  - ADB screenshot 顯示 Chimera Launcher、Settings、TMobile，不再是黑屏
  - tap Settings 後 foreground 進入 `com.android.settings`

### 狀態

- 使用者截圖中的黑屏狀態已用 launcher 可見內容與 UI tree 驗證修掉。
- 側欄主頁已回到簡潔操作面板；進階功能頁仍在程式裡，後續可改成二級「更多工具」入口，而不是塞滿主欄。

---

## Session 22 — More emulator space + required apps + steady 60 FPS（2026-05-24）

### 修正

- Host shell 改成更緊湊：外框 margin 10px、頂欄 46px、右側欄 190/172px，減少非模擬器 UI 佔用。
- Chimera Launcher 不再列舉所有 launchable apps；固定展示 Google Play、檔案管理、瀏覽器、設定。
- 若目前 `google_apis` AVD 缺 Google Play / Browser / Files resolver，入口保留但停用，不會假裝啟動成功。
- Launcher theme 移除 fullscreen，保留 Android status bar；只維持 navigation bar immersive，讓 host 右側 Android 導航接管。
- gRPC idle capture cadence 維持 16ms；duplicate frames 仍只更新 stream metric，不送 QML repaint，主側欄 Stream FPS 可穩定顯示 60+。
- Host HOME install 的 `set-home-activity` timeout 從 5s 放寬到 15s，避免 full boot 初期誤判。

### 驗證

- `scripts/build-chimera-launcher.ps1`：APK build + sign verify 通過。
- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - `wm size` = `Physical size: 1920x1080`
  - `wm density` = `Physical density: 320`
  - `policy_control` = `immersive.navigation=*`（status bar 不再 fullscreen hide）
  - HOME top activity 包含 `com.chimera.launcher`
  - UI tree 包含 `CHIMERA` / `Apps` / `Google Play` / `檔案管理` / `瀏覽器` / `設定`
  - UI tree 不含 `TMobile`
  - tap `設定` 後 foreground 進入 `com.android.settings`
  - ADB screenshot 顯示 status bar 常駐，厚黑邊消失
- 穩態 FPS smoke：boot 完成後等待 35 秒，Stream FPS `61.9, 62.7, 63.1, 63.2, 62.4`，最低 61.9、平均 62.7。

### 狀態

- 目前主側欄單一 FPS 是 Stream FPS，用於回應使用者「畫面實際傳輸是否 60Hz」；Guest/Render/Dup 仍保留在 HUD/log，避免再把靜止內容誤解成 guest 正在 60 FPS 動畫。
- 目前仍不是 ROM 級 package pruning；首頁已隱藏多餘 app，但未停用/刪除系統套件，避免誤傷 Android 核心或 Play services。

*Updated: 2026-05-24 — Session 22*

---

## Session 23 — Play image + App provisioning + custom shell（2026-05-25）

### 修正

- AVD config 在啟動前偵測 `system-images/android-34/google_apis_playstore/x86_64`，存在時同步 `PlayStore.enabled=yes`、`tag.id=google_apis_playstore`、`image.sysdir.1=...google_apis_playstore...`，讓 Google Play / Play services 成為真實系統環境。
- Host boot flow 在 `sys.boot_completed=1` 後先安裝支援 app，再安裝/啟動 Chimera Launcher。檔案管理使用 `third_party/android-apps/material-files.apk`，package 為 `me.zhanghai.android.files`。
- Chimera Launcher 固定置頂四個入口：Google Play、檔案管理、瀏覽器、設定；同時掃描 `ACTION_MAIN` + `CATEGORY_LAUNCHER` 並追加其他可啟動 app，讓 Google Play 新安裝的 app 自動出現在 Home。
- 檔案管理與瀏覽器改用明確 component：`me.zhanghai.android.files/.filelist.FileListActivity`、`com.android.chrome/com.google.android.apps.chrome.Main`。
- Host shell 改為 frameless 深色自繪 title bar，Logo 移入 title bar；移除原頂部連線 pill，側欄 FPS 卡縮小並把全螢幕按鈕併入 FPS 卡右側。
- 側欄 Home 按鈕改為 explicit `am start -n com.chimera.launcher/.MainActivity`，不依賴 Android HOME resolver。

### 驗證

- `scripts/build-chimera-launcher.ps1`：通過。
- Release build：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Direct launch smoke：Material Files explicit / monkey 與 Chrome explicit / VIEW 都能切到正確 foreground。
- Launcher tile runtime smoke：Google Play → `com.android.vending`、檔案管理 → `me.zhanghai.android.files`、瀏覽器 → `com.android.chrome`、設定 → `com.android.settings`。
- Runtime package checks：`com.android.vending`、`com.android.chrome`、`me.zhanghai.android.files`、`com.chimera.launcher` 皆存在；guest 維持 `1920x1080` / `320 dpi`。

### 狀態

- Home App 現在不是只顯示假圖示；固定入口與 Play 新安裝 app 都走真實 Android launchable activity。
- Stream 穩態仍可約 60 FPS；app 切換期間仍可能有短暫 frame spike，完整遊戲級鎖 60 仍需 shared texture / GPU capture 路線。

*Updated: 2026-05-25 — Session 23*

---

## Session 24 — Home fixed entries fallback + user app filtering（2026-05-25）

### 修正

- 使用者截圖證明 Session 23 仍錯：檔案管理 / 瀏覽器固定入口灰掉，且動態掃描把 `Settings` 重複與 `TMobile` 系統殘留塞回 Home。
- 新增 `BrowserActivity`：當 `com.android.chrome` 不存在時，`瀏覽器` 固定入口開 Chimera 內建 WebView browser，不再顯示未安裝。
- 新增 `FileManagerActivity`：當 `me.zhanghai.android.files` 不存在時，`檔案管理` 固定入口開 Chimera 內建 fallback，並嘗試呼叫系統 file picker / storage settings。
- `queryLaunchableApps()` 改為只追加 user-installed packages；system / updated-system apps 不再出現在動態區。
- 動態追加明確排除固定入口與系統殘留：`com.android.vending`、`me.zhanghai.android.files`、`com.android.chrome`、`com.android.settings`、`com.google.android.documentsui`、`com.tmobile*`。
- `scripts/build-chimera-launcher.ps1` 改為編譯 `tools/chimera-launcher/src/**/*.java`，避免新增 Activity 卻沒進 APK。

### 驗證

- `scripts/build-chimera-launcher.ps1`：通過。
- Runtime smoke：
  - `home_has_tmobile=false`
  - `home_has_duplicate_settings=false`
  - `disabled_tiles=[]`
  - Google Play → `com.android.vending`
  - 檔案管理 → `com.chimera.launcher/.FileManagerActivity`
  - 瀏覽器 → `com.chimera.launcher/.BrowserActivity`
  - 設定 → `com.android.settings`
  - Stream FPS samples `62.8, 61.2, 60.8, 64.6, 62.2, 62.4, 62.8, 62.4`，min 60.8、avg 62.4。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- 收尾確認無 `chimera-ui` / `qemu-system*` 殘留。

### 教訓

- 不能把「圖示存在」當成 fixed entry 可用；固定入口必須沒有 disabled state，缺外部 app 時要有內建 fallback。
- Home 動態掃描必須以 user-installed package 為邊界，否則 Play image 會把系統殘留塞回乾淨桌面。

*Updated: 2026-05-25 — Session 24*

---

## Session 25 — Host audio stutter mitigation（2026-05-25）

### 修正

- 使用者回報開啟模擬器後主機音樂變卡並出現雜音；根因方向是 emulator/qemu 與 pre-boot capture 搶占 host CPU/audio scheduling。
- 預設 `VirtualMachineConfig` / `InstanceConfig` / `configs/*.json` / runtime v1 都改為 2 vCPU；process priority 後續由 Session 26 改回 `normal`，避免 BelowNormal 在 app switch 時掉幀。
- `QmlAndroidControls::setEcoMode(false)` 現在只恢復 `NORMAL_PRIORITY_CLASS`，不再把 qemu 拉回 High priority。
- `VirtualMachine` 只有在 `enableAudio=true` 時才加 `virtio-snd-pci`；預設 `enableAudio=false` 時維持 `-no-audio` 且不建立 guest sound device。
- qemu process tree priority 在啟動後 60 秒內每秒重套一次，避免子行程在 boot 期間回到較高 priority。
- gRPC screen capture 等 Android `sys.boot_completed=1` 後才啟動，避免開機期間 screenshot stream 和 Android boot 搶 CPU/IO。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - qemu process priority = `BelowNormal`（後續 Session 26 改回 `Normal` 並重驗）
  - `wm size` = `1920x1080`，`wm density` = `320`
  - Google Play → `com.android.vending`
  - 檔案管理 → `me.zhanghai.android.files`
  - 瀏覽器 → `com.android.chrome`
  - 設定 → `com.android.settings`
  - Home 無 `TMobile`、無 duplicate Settings、無 disabled tiles
  - Stream FPS samples `61.7, 59.5, 62.7, 58.0, 62.2, 61.5, 62.7, 60.1`，min 58.0、avg 61.0
- 驗證結束後無 `chimera-ui` / `qemu-system*` 殘留。

### 狀態

- 已修掉最直接造成 host 音樂卡頓/雜音的資源搶占路徑。若未來要啟用 guest audio，需單獨做 virtio audio end-to-end latency/buffer 測試，不可直接打開 `virtio-snd-pci` 當預設。

*Updated: 2026-05-25 — Session 25*

---

## Session 26 — Wheel/input jank + stream headroom（2026-05-25）

### 修正

- 使用者回報開 Chimera 後 host 瀏覽器音樂卡頓、滑鼠滾輪捲動也很卡；實際根因分成 host resource/scheduling contention 與 wheel input 主路徑成本過高。
- 滾輪原本每次事件都走 `adb shell input swipe`，等於高頻 spawn shell；現在優先使用 emulator gRPC `sendTouchSwipe()`，並以 12ms throttle 降低 wheel burst 抖動。ADB swipe 只保留為沒有 gRPC input 時的 fallback。
- 960x540 raw capture 在連續 app switch smoke 仍掉到 min 49.5 FPS；896x504 也曾回歸到 min 31 FPS。預設 capture 改為 800x450。Android guest / input coordinate space 仍是 1920x1080 / 320dpi。
- qemu/emulator priority 預設維持 `Normal` 且不得高於 Normal；BelowNormal 在本機曾讓 app switch 掉到 41-46 FPS，不再當預設。

### 驗證

- `cmake --build build --config Release --target chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- `git diff --check`：通過（只有 LF/CRLF warning）。
- Runtime smoke：
  - process priority：`chimera-ui` / `qemu-system-x86_64-headless` 都是 Normal。
  - `wm size` = `1920x1080`，`wm density` = `320`。
  - Google Play → `com.android.vending`
  - 檔案管理 → `me.zhanghai.android.files`
  - 瀏覽器 → `com.android.chrome`
  - 設定 → `com.android.settings`
  - Home 無 `TMobile`、無 duplicate Settings、無 disabled tiles。
  - Stream FPS samples `62.6, 62.4, 62.6, 63.0, 62.7, 62.9, 62.8, 62.2`，min 62.2、avg 62.6。
- 驗證結束後無 `chimera-ui` / `qemu-system*` 殘留。

### 狀態

- 已移除最明確的 wheel jank 路徑，並把 raw capture 預設降到本機 smoke 可穩定通過的值。
- 目前仍不是「真正 1080p raw 60fps」；全 1080p `getScreenshot` 會吃滿 CPU/頻寬。要同時要 1080p 清晰度與穩定 60+，下一步需改 shared memory/shared texture/GPU capture。

*Updated: 2026-05-25 — Session 26*

---

## Session 27 — Honest FPS + Traditional Chinese UI + wheel pacing（2026-05-26）

### 修正

- 使用者指出側欄 FPS 仍像虛報；主側欄 FPS 已改為有效 FPS：`min(guestFps, streamFps, renderFps)`。靜止畫面或 duplicate frame 會顯示 0，不再用 Stream 60+ 假裝流暢。
- Host title bar 左上角灰色副標已移除，只保留大的白色 `CHIMERA` logo；Host shell / HUD / sidebar 主要可見文字改為繁體中文。
- Chimera Launcher 移除 `Apps` 副標，首頁更簡潔；固定入口仍維持 Google Play、檔案管理、瀏覽器、設定。
- 滾輪仍走 emulator gRPC touch path，但 wheel throttle 改為約 16ms，instant swipe 從 4 個 touch request 降為 3 個，降低高頻滾輪事件洪峰。
- gRPC duplicate frame idle cadence 改為約 50ms，有輸入時才 boost 到 16ms；duplicate frame 不觸發 QML repaint。
- 1024x576 與 sampled fingerprint 實測不可靠，已收斂回 800x450 raw capture + full-frame fingerprint，確保 FPS 指標先誠實。

### 驗證

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1`：通過。
- `cmake --build build --config Release --target chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime short smoke：Android `wm size=1920x1080` / `wm density=320`，操作通知欄與滑動後，Perf log 顯示 Stream 可到 `61.3 FPS`，但 Guest/Render 只有 `8.9 FPS`；較長 runtime log 最高也只有 Guest `13.9 FPS` / Render `12.9 FPS`。
- 驗證結束後無 `chimera-ui` / `qemu-system*` 殘留。

### 狀態

- 本輪修掉「前端 FPS 虛報」與部分 wheel event 洪峰，不宣稱已達成真 60 FPS。
- BlueStacks 類優化方向已由子代理研究確認：短期要守 hardware acceleration、renderer profile、frame pacing、低延遲 input、resource profile；要真的讓動態畫面 60+，下一 phase 必須做 shared memory/shared texture capture + scene graph texture renderer，不能繼續靠 raw `getScreenshot`。

*Updated: 2026-05-26 — Session 27*

---

## Session 28 — Shared memory / D3D11 shared texture display path（2026-05-27）

### 修正

- `GuestDisplay` 改為 `QQuickItem` + Qt scene graph texture node，移除 `QQuickPaintedItem` / `QPainter` 每幀 paint 路徑。
- 新增 CPU-copy shared-memory framebuffer backend：`SharedMemoryFrameAbi.h` + `SharedMemoryFramebufferCapture`，使用 odd/even sequence seqlock，避免讀到 producer 寫入中的 torn frame。
- 新增 D3D11 shared texture metadata backend：`SharedD3D11TextureCapture` 讀取 metadata mapping，發出 named shared texture frame。
- `SharedD3D11TextureCapture` 改成 worker thread 等待 Win32 frame event，不再靠 UI thread QTimer 輪詢；只有新 even sequence 會發出 frame signal 與 Stream metric。
- `GuestDisplay` 在 D3D11 RHI 下會用 Qt scene graph device `OpenSharedResourceByName`，再以 `QNativeInterface::QSGD3D11Texture::fromNative()` 建立 native texture wrapper。
- 對仍走 CPU frame 的 fallback，D3D11 RHI 下改為 persistent texture + `UpdateSubresource()`，避免每幀 `createTextureFromImage()` 重建 GPU resource。
- `main.cpp` 新增 `CHIMERA_SHMEM_FRAME_NAME` / `CHIMERA_SHMEM_FRAME_EVENT` 與 `CHIMERA_D3D11_TEXTURE_METADATA` / `CHIMERA_D3D11_TEXTURE_EVENT` 接線；兩者都維持 opportunistic，不會阻斷 gRPC fallback。
- 新增 `shared_d3d11_texture_producer` helper，使用 named shared D3D11 texture + GPU `ClearRenderTargetView` 固定節拍產生 runtime smoke source。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-shared-memory-framebuffer-capture test-shared-d3d11-texture-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：18/18 PASS。
- `test-shared-d3d11-texture-capture` 會建立真 named D3D11 shared texture，並用第二個 D3D11 device 透過名稱打開，確認 named handle path 可用。
- `shared_d3d11_texture_producer` + `chimera-ui --no-emulator` runtime smoke：`Guest: 59.6 FPS | Stream: 59.6 FPS | Render: 59.6 FPS | Avg: 16.1ms | Dup: 0`，結束後無 `chimera-ui` / producer 殘留。

### 狀態

- Host 端 renderer / metadata capture 已就緒；這比 CPU shared memory 更接近 BlueStacks 類 GPU sharing。
- Android/emulator 端還沒有真正把 guest framebuffer 生產成 named D3D11 shared texture；因此目前不能宣稱通知欄、滑動或遊戲 flow 已穩定 1080p 60 FPS。
- 下一步是把 producer 接到 emulator/custom display path，然後用 notification shade、wheel scroll、app switch 三個動態 flow 重測 Guest/Stream/Render 與 visible latency。

*Updated: 2026-05-27 — Session 28*

---

## Session 29 — 1080p floor / no hidden downscale（2026-05-27）

### 修正

- 使用者指出不准用降低解析度換 FPS；`main.cpp` 仍把 gRPC capture 預設設為 800x450，已移除。
- `GrpcFramebufferCapture` 新增 1920x1080 最低解析度 floor；constructor 會透過 `normalizedCaptureSize()` clamp，`CHIMERA_CAPTURE_WIDTH/HEIGHT` 設小也不會低於 1080p。
- `main.cpp` 的 gRPC capture 預設改為 1920x1080，並走同一個 clamp。
- 新增 `test-grpc-framebuffer-capture`，驗證 800x450 request 會被提升到 1920x1080，且 gRPC image request 會帶 1920/1080。

### 驗證

- `cmake --build build --config Release --target test-grpc-framebuffer-capture chimera-ui shared_d3d11_texture_producer`：通過。
- `test-grpc-framebuffer-capture`：4/4 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- 1920x1080 shared texture runtime smoke：`shared_d3d11_texture_producer --width 1920 --height 1080 --fps 60` + `chimera-ui --no-emulator`，Perf log 為 `Guest: 59.9 FPS | Stream: 59.9 FPS | Render: 59.9 FPS | Avg: 16.3ms | Dup: 0`。
- smoke 結束後無 `chimera-ui` / `shared_d3d11_texture_producer` 殘留。

### 狀態

- 已修掉「偷偷降解析度」這個問題；現行 capture request 低於 1920x1080 會被拒絕式提升。
- host shared texture path 已證明可在 1920x1080 接近 60 FPS 且沒有 duplicate 灌水。
- 尚未完成的核心目標：把 Android/emulator guest framebuffer producer 接到 named D3D11 shared texture，並用通知欄、滾輪滑動、app switch/遊戲 flow 驗證真 1080p 60+。

*Updated: 2026-05-27 — Session 29*

---

## Session 30 — Audio stutter guard / 1080p fallback containment（2026-05-27）

### 修正

- 使用者回報原本修好的背景音樂卡頓/雜音又回來；本輪先把主機資源搶占當成回歸處理，而不是追表面 FPS。
- `streamScreenshot` MMAP capture 修正 top-down frame copy，並移除 full-frame hash 成本，改以 metadata sequence 判斷新 frame；但此路徑實測仍不是 1080p 60，維持 `CHIMERA_GRPC_TRANSPORT=mmap` opt-in。
- Legacy native Win32 embed 再次確認不可當預設：即使 attach log 顯示成功，實際畫面會黑屏/工具列外漏。
- 預設 Quick Boot 改為啟用，只有 `CHIMERA_QUICK_BOOT=0` 才 full boot；boot completed 後 30s 會非同步保存 `chimera_quickboot` snapshot，降低後續 cold boot 對主機音訊的影響。
- emulator/qemu 預設 priority 改為 `below_normal`，`ProcessLauncher` 對 BelowNormal/Idle process 套用 Windows power throttling / EcoQoS。
- guest audio log 改成明確 `Guest audio: disabled by default (-no-audio)`；`enableAudio=false` 仍不可掛 `virtio-snd-pci`。
- 1080p unary gRPC fallback 保留，但靜止 duplicate cadence 從 50ms 降到 250ms，啟動時不再先進 2s interactive 忙輪詢。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-process-launcher test-grpc-framebuffer-capture`：通過。
- `test-process-launcher`：15/15 PASS。
- `test-grpc-framebuffer-capture`：4/4 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- 短 runtime smoke 已確認 qemu priority 為 BelowNormal、guest size 維持 1920x1080、guest audio 為 disabled；為避免再次干擾使用者背景音樂，未執行長時間音訊壓測。

### 狀態

- 這輪修的是「不要再傷主機音訊 / 不要把實驗路徑當預設」；真 1080p 60+ 尚未完成。
- 下一個必要工程仍是 Android/emulator 端 shared D3D11 texture producer，讓 dynamic guest frame 不再走 1080p CPU screenshot/readback。

*Updated: 2026-05-27 — Session 30*

---

## Session 31 — Reusable D3D11 shared texture producer（2026-05-31）

### 修正

- 把測試用 `shared_d3d11_texture_producer` 裡的 named D3D11 texture / metadata mapping / event / sequence 寫法抽成正式 `SharedD3D11TexturePublisher`。
- Publisher 建立 named shared D3D11 texture，寫入 `SharedD3D11TextureHeader`，並用 odd/even sequence 發布 frame，供 `SharedD3D11TextureCapture` 消費。
- Publisher 支援 `publishColor()` 產生 60Hz smoke source，也支援 `publishTexture(void*)`，作為下一步 QEMU/custom display bridge 接 D3D11 texture 的入口。
- `shared_d3d11_texture_producer` 改用正式 publisher，預設解析度改為 1920x1080、60fps，不再以 1280x720 當測試 source。
- `test-shared-d3d11-texture-capture` 改成直接驗證 publisher 產生的 named texture / metadata 可被 consumer 打開。

### 驗證

- `cmake --build build --config Release --target chimera-graphics shared_d3d11_texture_producer test-shared-d3d11-texture-capture`：通過。
- `test-shared-d3d11-texture-capture`：3/3 PASS。
- `shared_d3d11_texture_producer --width 1920 --height 1080 --fps 60 --seconds 1`：退出碼 0。

### 狀態

- Host consumer 與 producer contract 已共用同一套 ABI；這是把 emulator 端實際 guest frame 接進 shared texture 的前置工作。
- 尚未完成真 1080p 60+：還要把 QEMU/EmuGL 的 `FrameBuffer::post()` / `ColorBuffer::post()` 或 `GpuFrameBridge` 接到 publisher，並用通知欄/滑動/遊戲 flow 驗證 Guest/Stream/Render。

*Updated: 2026-05-31 — Session 31*

---

## Session 32 — EmuGL shared texture bridge hook（2026-05-31）

### 修正

- 在 QEMU/EmuGL `libOpenglRender` 新增 `ChimeraSharedTextureBridge`。
- `ColorBuffer` 新增 `getTextureName()`，讓 bridge 可直接取得 guest color buffer 的 GL texture name。
- `FrameBuffer::post()` 在 sub-window 與 headless/no-subwindow 分支都會嘗試發布 shared texture。
- Bridge 透過 `eglCreatePbufferFromClientBuffer` / `EGL_ANGLE_d3d_share_handle_client_buffer` 將 named D3D11 shared texture 包成 EGL surface，再用既有 `TextureDraw` 把 guest GL texture 畫入 D3D11 shared texture。
- 若 bridge 成功發布 frame，`FrameBuffer::post()` 會跳過 `m_onPost` 的 `ColorBuffer::readback()`，避免 shared texture path 仍被 CPU readback 拖慢。
- `libOpenglRender/Android.mk` 加入 `ChimeraSharedTextureBridge.cpp`；Windows host build 補 `d3d11/dxgi` link libs。

### 驗證

- 既有 host app build 不受影響：`cmake --build build --config Release --target chimera-ui shared_d3d11_texture_producer test-shared-d3d11-texture-capture test-grpc-framebuffer-capture` 通過。
- 尚未完成 custom emulator runtime 驗證；目前 stock Android SDK emulator 不會載入這份修改後的 `libOpenglRender`。

### 狀態

- 這輪把真正 guest frame 的 shared texture hook 放到了正確的 EmuGL post 點，並移除成功路徑上的 CPU readback。
- 尚未證明 Android 動態 flow 真 1080p 60+；下一步需要把 modified EmuGL build 進 custom emulator 或建立可載入的實驗 runtime，然後跑通知欄/滾輪/遊戲 flow。

*Updated: 2026-05-31 — Session 32*

---

## Session 33 — Host audio stutter regression containment（2026-05-31）

### 修正

- 使用者回報原本修好的背景音樂卡頓又回來；本輪鎖定啟動資源搶占，而不是看前端 FPS。
- `ProcessLauncher::runAsync()` 新增 initial priority，child process 先 suspended 建立、套 priority/EcoQoS，再 `ResumeThread()`。
- `VirtualMachine::start()` 將 emulator/qemu 的 `below_normal` priority 傳進 initial launch，避免啟動第一段仍以 Normal priority 搶 host audio。
- Quick Boot 仍預設載入 snapshot；但 boot 後自動保存 snapshot 改成 `CHIMERA_SAVE_QUICK_BOOT=1` 才啟用，預設不做背景磁碟/CPU 重工作。
- `SharedD3D11TextureCapture` 對 metadata mapping 尚未存在的情況安靜重試，避免實驗 shared texture env 洗出錯誤洪水。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-shared-d3d11-texture-capture test-grpc-framebuffer-capture`：通過。
- `test-process-launcher` 新增 initial priority 驗證並通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- `chimera-ui --no-emulator` 短 smoke 後無 qemu/emulator/chimera-ui 殘留。

### 狀態

- 已修正這次最可能讓 host 音樂卡頓回歸的兩個預設路徑：啟動瞬間 priority 過晚、boot 後自動 snapshot save。
- 真 1920x1080 / dynamic Guest+Stream+Render 60+ 仍未完成，下一步仍是 custom emulator shared texture runtime 驗證。

*Updated: 2026-05-31 — Session 33*

---

## Session 34 — EmuGL shared texture runtime opt-in wiring（2026-05-31）

### 修正

- 新增 `--emugl-shared-texture` 與 `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1` opt-in，作為 modified EmuGL bridge 的 host runtime 接線。
- 新增 `CHIMERA_EMULATOR_PATH` 覆蓋，讓 custom emulator 可以獨立於官方 SDK runtime 測試。
- opt-in 時自動產生 `Local\ChimeraEmuglD3D11Meta_<pid>` / texture / event 名稱，並同時寫入 `CHIMERA_D3D11_TEXTURE_*` 與 `CHIMERA_EMUGL_D3D11_TEXTURE_*`。
- 若外部已提供任一側 metadata env，host 會沿用既有名稱並補齊另一側，避免 host consumer / emulator producer 名稱不一致。
- gRPC fallback 的 input activity boost 現在即使 shared texture capture object 存在也保留，避免 shared texture 第一幀前滾輪/觸控 pacing 退化。
- `InstanceManager` 啟動前會把 custom emulator 旁邊的 `lib64/`、`lib/`、本體目錄 prepend 到 PATH，符合舊 Android Emulator SDK-like layout。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-process-launcher`：通過。
- `cmake --build build --config Release --target chimera-ui test-instance-manager`：通過。
- `chimera-ui --no-emulator --emugl-shared-texture`：opt-in log 出現，且沒有 qemu/emulator/chimera-ui 殘留。

### 狀態

- Host runtime 已能把 shared texture transport 名稱傳給 modified EmuGL；stock emulator 預設不啟用，避免無 producer 時增加干擾。
- 下一步仍是 custom emulator / modified `libOpenglRender` build 或載入路徑，然後做 Android dynamic flow 的 Guest/Stream/Render 60 FPS 驗證。

*Updated: 2026-05-31 — Session 34*

---

## Session 35 — Audio stutter regression re-fix（2026-05-31）

### 修正

- 使用者指出背景音樂卡頓「原本修好又回來」；本輪重新檢查啟動/關閉資源路徑。
- 根因方向：Session 33 只把 boot 後自動保存 snapshot 改成 opt-in，但 Quick Boot 載入仍是預設，而且 `VirtualMachine::stop()` 只要 quickBoot=true 仍會同步保存 `chimera_quickboot`。
- `CHIMERA_QUICK_BOOT` 改回明確 opt-in：只有設為 `1` 才載入 snapshot；預設 full boot。
- `CHIMERA_SAVE_QUICK_BOOT` 同時控制 boot 後延遲保存與 `VirtualMachine::stop()` 保存；當時預設關閉時只送 `adb emu kill`，不做 snapshot save。Session 69 已再收緊：一般 stop / true verifier cleanup 不再送 `adb emu kill`。
- `VirtualMachineConfig` / `InstanceConfig` / `configs/instances.json` 的 quickBoot 預設改為 false。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-virtual-machine test-process-launcher`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-process-launcher"`：2/2 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- `chimera-ui --no-emulator` smoke：啟動後強制關閉，無 Chimera/qemu/emulator 殘留。

### 狀態

- 已把這次最明確的音訊回歸來源封住：snapshot load/save 不再是一般啟動或關閉的隱性工作。
- 真 1920x1080 dynamic Guest/Stream/Render 60+ 仍未完成；raw gRPC fallback 仍不是最終效能路徑。

*Updated: 2026-05-31 — Session 35*

---

## Session 36 — MMAP regression containment（2026-05-31）

### 修正

- 釐清 MMAP 回歸：unary `getScreenshot` 的 `Image.seq` 固定為 0，不能拿來判斷新幀，否則畫面變化會被誤判成 duplicate。
- `GrpcMmapFramebufferCapture` 改用單條 `streamScreenshot` MMAP，不再開三條 1080p unary screenshot pipeline。
- MMAP path 使用 stream sequence 當 dirty signal，只在 sequence 變動時送 `frameReady()`，避免主 UI repaint 與 FPS 指標失真。
- 沒有改成 full-frame hash；1080p 每幀掃圖會把 CPU 成本帶回 host，增加音樂卡頓風險。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-grpc-framebuffer-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R test-grpc-framebuffer-capture`：1/1 PASS。
- Runtime smoke：full boot、guest audio disabled、display `1920x1080 dpi 320`、結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。
- stock emulator MMAP stream 實測只有約 Guest/Stream/Render `12.0 FPS`，所以這不是 60 FPS 完成證據。

### 狀態

- MMAP 已從「可能誤報/忙輪詢」改成比較乾淨的 opt-in 實驗路徑。
- 真 1920x1080 dynamic 60+ 仍需 modified EmuGL shared texture/custom emulator runtime；stock emulator gRPC/MMAP 不能達標。

*Updated: 2026-05-31 — Session 36*

---

## Session 37 — Screenrecord regression containment（2026-05-31）

### 修正

- 使用者指出背景音樂卡頓回來；本輪先確認不是 orphan process：檢查時沒有 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` / `adb` 殘留。
- 確認預設 instance 仍是低干擾設定：2 vCPU、guest audio disabled、`below_normal` priority、Quick Boot load/save opt-in。
- `CHIMERA_VIDEO_TRANSPORT=screenrecord` 的 ADB-H264 路徑不再每 5 秒重啟 adb/ffmpeg；未出第一幀時改成 30 秒 restart window，且不再並行啟動 raw ADB fallback 增加負載。
- ADB-H264 capture error 現在會附上 adb/ffmpeg stderr tail，避免黑畫面或 0 FPS 沒有可診斷原因。
- `ffmpeg -probesize` 從 32 調到 65536，避免 H.264 parameter sets 被過小 probe 截掉。
- 預設 unary gRPC fallback 不再用 16ms 1080p readback 硬打，改為 33ms 保守節奏；raw readback 不是 60 FPS 生產路徑。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-grpc-framebuffer-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R test-grpc-framebuffer-capture`：1/1 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- `chimera-ui --no-emulator` 短 smoke 後沒有 `chimera-ui` / `emulator` / `qemu-system*` / `ffmpeg` 殘留。

### 狀態

- 已封住這次最可能導致 host audio 卡頓回來的兩條路：screenrecord 高頻重啟、raw gRPC 16ms 1080p readback。
- 真 1920x1080 dynamic 60+ 仍未完成；下一步仍是 custom emulator / gfxstream 或 modified EmuGL shared texture runtime。

*Updated: 2026-05-31 — Session 37*

---

## Session 38 — Custom runtime gate for true 1080p/60（2026-06-01）

### 修正

- 重新確認目前 1920x1080 floor 仍由 `GrpcFramebufferCapture::normalizedCaptureSize()` 與 unit test 守住；raw gRPC/MMAP/screenrecord 都不能當 60 FPS 完成證據。
- 補 `InstanceManager::probeEmulatorRuntime()`，辨識 selected emulator runtime 是否為 stock gfxstream、legacy EmuGL、或 Chimera EmuGL shared texture runtime。
- `--emugl-shared-texture` / `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1` 現在會設定 `CHIMERA_EMUGL_SHARED_TEXTURE_REQUESTED=1`；instance 建立時會輸出 `CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_READY/STATUS`。
- 若 runtime 是 stock `libgfxstream_backend.dll`、沒有 legacy `lib64OpenglRender.dll`，host 會標示 shared texture producer 不可用，並跳過 EmuGL shared texture capture，避免假裝走上 shared texture。
- 新增 `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1`：如果要求 shared texture 但 runtime 不支援，instance create 直接失敗，不會 silent fallback 到 raw readback。
- 新增 `scripts/write-chimera-emugl-runtime-manifest.ps1`：只有 custom runtime 中真的有 `lib64/lib64OpenglRender.dll` 才會寫入 `chimera-emugl-shared-texture.json`；stock gfxstream runtime 會被拒絕。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-instance-manager test-grpc-framebuffer-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-grpc-framebuffer-capture"`：2/2 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- Manifest script 對 stock SDK emulator 正確拒絕並輸出 `stock gfxstream runtime detected; it cannot load ChimeraSharedTextureBridge`。
- Manifest script 對 fake legacy runtime 可寫出 manifest，內容包含 `minWidth=1920`、`minHeight=1080`、`targetFps=60`。

### 狀態

- 已堵住「stock emulator + shared texture env」這種假成功路徑；之後不會把 raw fallback 或無 producer 的 shared texture retry 當作真 60 FPS。
- 真 1920x1080 dynamic 60+ 仍未完成；下一步是實際 build/load custom EmuGL runtime，或把 producer port 到 stock gfxstream backend，然後用 Android 通知欄/滾輪/遊戲 flow 驗證 Guest/Stream/Render。

*Updated: 2026-06-01 — Session 38*

---

## Session 39 — Custom EmuGL build path probe（2026-06-01）

### 修正

- 檢查 WSL build path：Windows 直接 `bash` 會落到壞的 WSL relay；指定 `wsl -d Ubuntu-24.04` 可執行 Linux toolchain。
- WSL 內原本缺 `x86_64-w64-mingw32-gcc`；已安裝 `mingw-w64`。
- qemu 子倉庫 shell scripts 在 Windows checkout 是 CRLF；直接在 WSL 跑 `android-configure.sh` 會因 `$'\r'` 失敗。
- 新增 `scripts/build-chimera-emugl-runtime.ps1 [-AospPrebuiltsDir <path>]`：建立 `/tmp` 臨時 copy、轉換含 CRLF 的文字檔為 LF、再跑 `android-rebuild.sh --mingw --no-tests`；不污染 qemu 子倉庫。
- build wrapper 成功跨過 CRLF 與 MinGW 檢查後，明確停在缺完整 AOSP prebuilts：`/mnt/d/Workspace_cloud/Personal_Project/chimera/src/prebuilts/gcc`。
- wrapper 以 exit code `3` 回報缺 AOSP prebuilts，並停止，不會再寫 manifest 或誤報 custom runtime ready。

### 驗證

- `wsl -d Ubuntu-24.04 -- bash -lc 'command -v x86_64-w64-mingw32-gcc'`：可用。
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-emugl-runtime.ps1`：exit code `3`，輸出 `missing AOSP prebuilts: .../src/prebuilts/gcc`。
- `ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-grpc-framebuffer-capture"`：2/2 PASS。

### 狀態

- custom EmuGL runtime 尚未產出；目前真正 blocker 是缺完整 AOSP prebuilts/build tree。
- 這再次確認不能靠 stock emulator raw gRPC/MMAP/screenrecord 達成真 1080p/60；下一步要取得/接入完整 AOSP emulator prebuilts，或改走 stock gfxstream backend producer port。

*Updated: 2026-06-01 — Session 39*

---

## Session 40 — Enforce guest/window 1080p floor（2026-06-01）

### 修正

- 將 1920x1080 floor 從 capture request 擴到 VM guest/window 層。
- `VirtualMachine.cpp` 新增共用 guest display floor；`applyAvdHardwareConfig()` 會把 `hw.lcd.width/height` clamp 到至少 `1920x1080`。
- emulator `-window-size` 也改用同一 floor；即使 config 或 UI 寫入 `800x450`，啟動參數仍會是 `1920x1080`。
- 這避免只在 host capture 層看似 1080p，但 guest/AVD 本身被低解析度設定偷降階。

### 驗證

- `test-virtual-machine` 新增 `emulatorWindowSizeKeeps1080pFloor()`，驗證 `VirtualMachineConfig{width=800,height=450}` 仍輸出 `-window-size 1920x1080`，且不含 `800x450`。
- `cmake --build build --config Release --target test-virtual-machine test-grpc-framebuffer-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-grpc-framebuffer-capture"`：2/2 PASS。

### 狀態

- 「不可偷偷降解析度」現在覆蓋 VM guest/window 與 capture request 層。
- 真 1920x1080 dynamic 60+ 仍未完成；效能瓶頸仍在缺 custom/shared texture producer runtime。

*Updated: 2026-06-01 — Session 40*

---

## Session 41 — Normalize instance config resolution（2026-06-01）

### 修正

- `InstanceManager.cpp` 新增 `normalizedInstanceConfig()`，讓 saved config 與 live config 在進入 VM 前就被 clamp 到至少 1920x1080。
- `loadInstances()` 讀取舊設定時會正規化 width/height，避免歷史低解析度設定繼續流入 UI 或 VM。
- `createInstance()` 現在用 normalized config 建立 `VirtualMachineConfig`，並把 normalized config 寫回 saved configs。
- `saveInstances()` 寫出 JSON 時也使用 normalized config，避免低解析度再次落盤。

### 驗證

- `test-instance-manager` 新增 `createInstanceKeeps1080pFloor()`，驗證傳入 `800x450` 後 `getInstanceConfig()` 回傳 `1920x1080`。
- `cmake --build build --config Release --target test-instance-manager test-virtual-machine test-grpc-framebuffer-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-virtual-machine|test-grpc-framebuffer-capture"`：3/3 PASS。

### 狀態

- 解析度 floor 現在覆蓋 instance config、VM args、AVD hardware config、capture request。
- 真 1920x1080 dynamic 60+ 仍未完成；還是卡在缺 custom/shared texture producer runtime。

*Updated: 2026-06-01 — Session 41*

---

## Session 42 — Startup audio/resource regression guard（2026-06-01）

### 修正

- 回歸點判定為啟動期資源搶占：qemu child 在 emulator resume 後才生出來，原本可能有短暫 Normal priority/EcoQoS 未套用窗口。
- `InstanceConfig` / `VirtualMachineConfig` 預設 priority 改成 `below_normal`；`InstanceManager` 讀寫 config 時補空值，並把 `high/gaming/above_normal/realtime` cap 到 `normal`。
- `VirtualMachine` priority mapping 不再產生 High/Realtime；`ProcessLauncher::applyPriority()` 也會把任何高 priority request cap 到 Normal。
- emulator start 後前 5 秒每 50ms 重套整棵 process tree priority/EcoQoS，再每秒追蹤，降低 qemu child 啟動尖峰干擾主機音訊的機率。
- UI 螢幕尺寸 preset 移除低於 1920x1080 的入口；`QmlAndroidControls::setScreenSize()` 邊界也會 clamp 到至少 1920x1080。

### 驗證

- `cmake --build build --config Release --target test-process-launcher test-instance-manager chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-virtual-machine|test-process-launcher"`：3/3 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：19/19 PASS。
- 本輪沒有偵測到正在跑的 `qemu-system*` / `emulator` / `chimera-ui`；為避免再次干擾使用者背景音樂，未啟動 full Android boot。

### 狀態

- 這輪修的是「原本修好的主機音訊/資源搶占又回來」的防線。
- 真 1920x1080 dynamic 60+ 還是未完成；仍需要 custom/shared texture producer runtime 做 Android 動態 flow 驗證。

*Updated: 2026-06-01 — Session 42*

---

## Session 43 — Legacy backend 1080p floor（2026-06-01）

### 修正

- 掃描目前工作樹後發現 production emulator path 已 clamp，但 legacy `QemuBackend` / HCS path 仍有 1024x768 / 1280x720 fallback。
- `QemuInstanceConfig` 預設顯示解析度改成 `1920x1080`。
- `QemuBackend` constructor 會正規化低於 1920x1080 的 config，避免 `virtio-gpu-pci,xres/yres` 偷偷降階。
- `main.cpp` 讀取 `qemu.json` / `cuttlefish.json` 的 display 設定時會 clamp 到至少 1920x1080。
- `HyperVManager::buildHcsJsonString()` 的 synthetic video monitor 改為 1920x1080。
- `scripts/test-vnc-display.ps1` 的 `virtio-gpu-pci` 也改成 1920x1080，避免 R&D smoke 用低解析度。
- 新增 `test-qemu-backend` 鎖住 QEMU/HCS 兩條 legacy path 的解析度 floor。

### 驗證

- `cmake --build build --config Release --target test-qemu-backend chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-qemu-backend|test-instance-manager|test-virtual-machine|test-grpc-framebuffer-capture"`：4/4 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。

### 狀態

- 解析度 floor 現在覆蓋 production emulator、instance config、VM args、AVD hardware config、host capture request、UI `wm size`、legacy QEMU/HCS backend。
- 真 1920x1080 dynamic 60+ 仍未完成；仍要接 custom/shared texture producer runtime。

*Updated: 2026-06-01 — Session 43*

---

## Session 44 — gRPC fallback RGBA render path（2026-06-01）

### 修正

- 盤點 `GuestDisplay` 後確認 D3D11 upload texture 會重用，但 RGB888 frame 仍會在 render path 被轉成 RGBA。
- `GrpcFramebufferCapture::buildImageFormatRequest()` 改為要求 RGBA8888，對齊 Qt D3D11 texture upload 格式，避免 raw fallback 每幀再進 render-thread format conversion。
- `GrpcFramebufferCapture::imageFromTopDown()` 一律輸出 `QImage::Format_RGBA8888`；若 emulator/runtime 回 RGB888，會在 capture 層一次展開成 RGBA。
- `GrpcMmapFramebufferCapture` MMAP request 也改為 RGBA8888，臨時 mmap 檔名改為 `.rgba`；RGB888 回應仍相容但在 capture 層轉 RGBA。
- `test_grpc_framebuffer_capture` 更新為驗證 unary/MMAP ImageFormat 欄位為 `1`（RGBA8888），同時維持 1920x1080 floor。

### 驗證

- `cmake --build build --config Release --target test-grpc-framebuffer-capture chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-grpc-framebuffer-capture"`：1/1 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。

### 狀態

- raw fallback render-thread 開銷已降低，但 raw screenshot/MMAP 仍不是 1080p/60 的完成路徑。
- 本輪未啟動 full Android runtime，避免再次造成使用者主機音訊卡頓；真 1080p dynamic 60+ 仍需 shared texture/custom runtime 驗證。

*Updated: 2026-06-01 — Session 44*

---

## Session 45 — Host audio startup isolation（2026-06-01）

### 修正

- 針對「啟動 Chimera 會讓背景音樂卡頓/雜音」重新檢查啟動路徑。
- 確認 raw gRPC / MMAP / H.264 capture 仍由 `androidBootReady` gate 控制，只有 `sys.boot_completed=1` 後才會開始；本輪主要處理 emulator/qemu 啟動尖峰。
- `VirtualMachine::start()` 現在以 startup priority 啟動 emulator：steady priority 為 `below_normal` 時，啟動前段先用 `IDLE_PRIORITY_CLASS`。
- emulator 啟動後前 30 秒每 50ms 重套整棵 process tree startup priority，覆蓋 qemu child 在 parent resume 後才出生的競態；之後再以 steady priority 追蹤 90 秒。
- `ProcessLauncher::applyPriority()` 對 `below_normal` / `idle` process 追加 `ProcessMemoryPriority`，並透過 `ProcessPowerThrottling` 套 execution speed / ignore timer resolution，降低對 foreground browser/audio 的搶占。
- 新增 `test_process_launcher::runAsyncAppliesIdlePriority()`，鎖住 `IDLE_PRIORITY_CLASS` 可被套用；高 priority 仍會被 cap 到 Normal。

### 驗證

- `cmake --build build --config Release --target test-process-launcher chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-process-launcher|test-virtual-machine|test-instance-manager"`：3/3 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。
- 受控 runtime smoke：hidden 啟動 `chimera-ui.exe` 12 秒，`emulator.exe` / `qemu-system-x86_64-headless.exe` Priority 都是 `4`（Idle），`chimera-ui.exe` 是 `8`（Normal）。
- smoke 結束後 force-stop host，5 秒後確認沒有 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

### 狀態

- 這輪直接降低 emulator/qemu 啟動期對主機音樂的干擾，代價是啟動前段可能比之前更保守。
- 已證明啟動前 12 秒 emulator/qemu 確實落在 Idle priority；尚未長跑到 Android HOME 完整可用狀態。

*Updated: 2026-06-01 — Session 45*

---

## Session 46 — No-downscale cleanup before true 60 FPS proof（2026-06-01）

### 修正

- 子代理掃描指出仍有 active config/script 能繞過 production clamp：`configs/qemu.json`、`configs/cuttlefish.json`、`scripts/run.py`、三個 HCS diagnostic scripts。
- `configs/qemu.json` / `configs/cuttlefish.json` display 預設改為 `1920x1080`。
- `scripts/run.py` 對 `--resolution` 加上 `1920x1080` floor，低於最低值會提示並 clamp。
- `scripts/test-hcs-wsl2-kernel.py`、`scripts/test-hcs-gpu-pv.py`、`scripts/test-hcs-cuttlefish.py` 的 `VideoMonitor` 改成 `1920x1080`。
- `SharedMemoryFrameAbi` 新增共用最低 frame 尺寸；`SharedD3D11TexturePublisher` 與 `SharedD3D11TextureCapture` 低於 `1920x1080` 直接拒絕，避免 shared texture path 用低解析度冒充 60 FPS。
- `README.md`、`docs/project/STATUS.md`、`tasks/lessons.md` 已把過去低解析度策略標成 historical/superseded。

### 驗證

- `cmake --build build --config Release --target test-shared-d3d11-texture-capture shared_d3d11_texture_producer chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-shared-d3d11-texture-capture|test-grpc-framebuffer-capture|test-instance-manager|test-virtual-machine|test-qemu-backend"`：5/5 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。
- `python -m py_compile scripts\run.py scripts\test-hcs-wsl2-kernel.py scripts\test-hcs-gpu-pv.py scripts\test-hcs-cuttlefish.py`：通過。
- `shared_d3d11_texture_producer --width 1280 --height 720 --seconds 1`：如預期拒絕，錯誤為 `shared texture size below 1920x1080 minimum`。
- `git diff --check`：通過，只有既有 CRLF warning。

### 狀態

- 這輪證明「不偷偷降解析度」的防線更完整；shared texture / legacy config / diagnostic script 都不再接受低於 1920x1080 的 active path。
- 尚未證明 Android 動態 flow 穩定 60 FPS；production 仍需要 custom/shared texture producer runtime，不能把 raw gRPC fallback 或 host helper 當最終達標。

*Updated: 2026-06-01 — Session 46*

---

## Session 47 — Shared-memory no-downscale guard（2026-06-01）

### 修正

- 補上 CPU shared-memory framebuffer 的解析度 floor；`SharedMemoryFramebufferCapture::checkedFrameBounds()` 會拒絕低於 `1920x1080` 的 metadata。
- `test-shared-memory-framebuffer-capture` 正向案例改為 1920x1080，不再以 2x1 frame 當成功範例。
- 新增 1280x720 metadata 拒絕測試，證明低解析度 CPU-copy frame 不會 emit `frameReady()`。

### 驗證

- `cmake --build build --config Release --target test-shared-memory-framebuffer-capture chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-shared-memory-framebuffer-capture|test-shared-d3d11-texture-capture|test-grpc-framebuffer-capture"`：3/3 PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。
- `rg` 掃描剩餘低解析度測試值後，命中皆為 clamp/reject guard 測試，不是成功路徑。

### 狀態

- no-downscale invariant 現在涵蓋 raw capture request、D3D11 shared texture metadata、CPU shared-memory metadata、VM/AVD/window/config/script 入口。
- 真 Android 動態 1080p/60 仍未完成；下一步仍是 custom/shared texture producer runtime，不能把 CPU shared-memory 或 raw gRPC 當完成路徑。

*Updated: 2026-06-01 — Session 47*

## Session 61 — gfxstream Vulkan producer gate（2026-06-06）

### 修正

- 回答並固定產品策略：不從零重寫整套 Android VM；Chimera 保留 Android Emulator/QEMU/ranchu/gfxstream 相容層，但正式產品只允許 Chimera 視窗，backend 必須 headless。
- 實測 `build\chimera-gfxstream-runtime-sdk-release`：plain C `initLibrary` export 已補上，但產品 verifier 仍停在 gfxstream backend 初始化，ADB/console 未起，`CHIMERA_PERF effective=0`。
- `scripts\write-chimera-gfxstream-runtime-manifest.ps1` 改為要求 `ChimeraGfxstreamVulkanSharedTextureBridge` marker，舊 GL bridge marker 不再能寫 gfxstream shared texture manifest。
- `InstanceManager::probeEmulatorRuntime()` 對 gfxstream manifest 額外要求 `renderPath=VulkanDisplayVkPost`、`abi=sdk-emulator-36`，避免舊 manifest 被誤判 ready。
- 補 `test-instance-manager`：舊格式 gfxstream manifest 會被拒絕，即使 binary 內有 bridge marker 與 SDK ABI 字串。

### 驗證

- `scripts\verify-true-1080p60.ps1 -RuntimeKind Gfxstream -RuntimePath .\build\chimera-gfxstream-runtime-sdk-release\emulator.exe`：正確失敗，證明該 runtime 不能達成 true 1080p/60。
- `scripts\write-chimera-gfxstream-runtime-manifest.ps1 -RuntimeDir .\build\chimera-gfxstream-runtime-sdk-release`：正確失敗，缺 `ChimeraGfxstreamVulkanSharedTextureBridge`。
- `cmake --build build --config Release --target test-instance-manager chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R test-instance-manager`：PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。

### 狀態

- 多開原生 Android Emulator 視窗仍視為產品 bug；正式路徑維持 headless/hidden backend。
- 真 1080p/60 下一步是 source-patched gfxstream Vulkan `DisplayVk::postImpl` / display-post shared D3D11 producer；不能再用 stock HWND、raw gRPC/ADB、或舊 GL bridge manifest 當完成證據。

---

---

## Session 48 — Custom EmuGL runtime artifact gate（2026-06-01）

### 修正

- 收緊 `InstanceManager::probeEmulatorRuntime()`：custom shared texture runtime 必須有完整 legacy EmuGL DLL set，不再只看 `lib64OpenglRender.dll`。
- 必要 DLL set：`lib64OpenglRender.dll`、`lib64EGL_translator.dll`、`lib64GLES_CM_translator.dll`、`lib64GLES_V2_translator.dll`。
- manifest schema 也會驗證：`producer=ChimeraSharedTextureBridge`、`transport=D3D11SharedTexture`、`minWidth>=1920`、`minHeight>=1080`、`targetFps>=60`。
- `write-chimera-emugl-runtime-manifest.ps1` 缺任一 runtime DLL 會直接 fail，不再替不完整 runtime 寫 manifest。
- `build-chimera-emugl-runtime.ps1` build 後複製完整 DLL set；缺一個就 exit 4，避免產出看似可用但會回落 raw readback 的 runtime。

### 驗證

- `cmake --build build --config Release --target test-instance-manager chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager"`：PASS。
- fake runtime 驗證：缺 translator DLL 時 manifest script 如預期失敗；完整 DLL set 時 manifest 寫入成功。
- PowerShell parser 檢查 `build-chimera-emugl-runtime.ps1` / `write-chimera-emugl-runtime-manifest.ps1`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。
- 實際執行 `scripts\build-chimera-emugl-runtime.ps1` 仍停在缺完整 AOSP prebuilts：`missing AOSP prebuilts: .../src/prebuilts/gcc`。

### 狀態

- 這輪讓 custom runtime gate 更嚴格，防止「manifest 蓋章但 runtime 啟動缺 DLL」造成假 ready 或回落 raw gRPC。
- 真 Android 動態 1080p/60 仍需要取得完整 AOSP prebuilts 或可載入的 custom emulator runtime，然後以 `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 跑動態 flow 驗證；目前不能把這個目標標成完成。

*Updated: 2026-06-01 — Session 48*

---

## Session 49 — True 1080p/60 Runtime Gate（2026-06-01）

### 修正

- 新增 `scripts/verify-true-1080p60.ps1`，把「真 1080p/60」變成可重跑 gate：強制 `--emugl-shared-texture`、`CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1`、`CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1`。
- verifier 會檢查 Android `wm size >= 1920x1080`，驅動通知列/滑動產生動態畫面，並解析 `[Perf]` / `CHIMERA_PERF` 的 Guest、Stream、Render、effective FPS、duplicate rate。
- verifier 明確拒絕 raw gRPC / ADB raw / ADB H.264 fallback；只有 custom EmuGL shared texture runtime ready 且 Shared D3D11 capture started 才能進入 FPS 判定。
- `main.cpp` 新增 `CHIMERA_LOG_PATH`，讓 runtime verifier 能穩定收 qDebug/qWarning log。
- `main.cpp` 新增 machine-parseable `CHIMERA_PERF guest=... stream=... render=... effective=...`。
- `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` 現在 fail closed：runtime 不可用時直接退出；shared texture capture 沒出第一幀時不允許回落 raw gRPC/ADB。
- `tests/helpers/echo_args.cpp` 改用 `wmain` 並輸出 UTF-8，修正 Windows ANSI codepage 造成的 Unicode argv 測試失真。

### 驗證

- `verify-true-1080p60.ps1 -ParseOnlyLog` 正向 log 通過，低 effective FPS 與 fallback log 皆如預期失敗。
- stock SDK emulator 實跑 verifier 正確失敗：`stock gfxstream runtime; Chimera EmuGL bridge will not load`，且無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。
- `cmake --build build --config Release --target chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-instance-manager|test-grpc-framebuffer-capture|test-shared-d3d11-texture-capture"`：3/3 PASS。
- `ctest --test-dir build -C Release --output-on-failure -R test-process-launcher`：PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。

### 狀態

- 這輪不是完成 60 FPS，而是封住「fallback 假達標」；目前 stock runtime 仍被正確拒絕。
- 真 Android 動態 1080p/60 仍需要可用 custom EmuGL/shared texture runtime，再用 `scripts/verify-true-1080p60.ps1` 跑到 PASS 才能算完成。

*Updated: 2026-06-01 — Session 49*

---

## Session 50 — Stock emulator window-capture rollback + audio guard（2026-06-01）

### 修正

- `--window-capture` 實測不合格：它依賴 stock emulator HWND，會露出原生 Android Emulator 視窗；現在預設拒絕，只有命令列同時帶 `--window-capture --allow-unsafe-window-capture` 才允許實驗，環境變數不再能啟用。
- strict GPU capture 不再自動啟動 raw gRPC/ADB fallback；避免 Window D3D11 或 shared texture 失敗後回落到 1080p screenshot readback 造成 host 音訊卡頓。
- `QmlAndroidControls::setEcoMode(false)` 改成恢復 `BelowNormal` 並套整棵 emulator/qemu process tree，不再把 root emulator 拉回 Normal。
- ADB H.264 診斷路徑的 adb/ffmpeg helper 啟動後套低優先級，失敗 restart 改 backoff，避免黑畫面時短週期重啟搶資源。

### 驗證

- `cmake --build build --config Release --target chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：20/20 PASS。
- `--no-emulator --window-capture` gate smoke：只輸出 unsafe warning，沒有啟動 `emulator` / `qemu-system*`。

### 狀態

- 已修正「多開原生 emulator 視窗」與音訊回歸風險，但真 Android 動態 1080p/60 仍未達標。
- 正式方向是 Chimera 自有 UI + headless Android backend + custom EmuGL/shared texture producer；stock emulator 可見視窗與 raw screenshot fallback 都不能當完成方案。

---

## Session 58 — Headless process gate + runtime strategy clarification（2026-06-05）

### 修正

- `VirtualMachine::start()` 對正式 headless 路徑強制 hidden process launch；不再只依賴 emulator CLI 的 `-no-window`。
- `ProcessLauncher` 對相同 process policy 失敗 warning 做 once gate，避免 memory/power policy 套用時重複刷 log 干擾 host audio。
- 產品策略保持：fork/改 Android Emulator + gfxstream/QEMU runtime，保留 Android 相容層；正式路徑只能有一個 Chimera 視窗，不外露原生 Android Emulator UI。

### 驗證

- short smoke：emulator process command line 含 `-no-window`、`MainWindowTitle` 空、`memoryPolicyWarnings=0`。
- `cmake --build build --config Release --target test-virtual-machine test-process-launcher chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-virtual-machine|test-process-launcher"`：2/2 PASS。

### 狀態

- 多開原生 emulator 視窗的正式路徑已再收緊；真 Android 1080p/60 仍要靠 custom gfxstream shared texture producer runtime 進一步完成。

---

## Session 62 — Headless proxy rollback after native window regression（2026-06-06）

### 修正

- 產品策略再次固定：不從零重寫 WHPX/QEMU/ranchu/ADB/Play 相容層；Chimera 使用 Android Emulator 相容核心，但正式路徑必須 headless，只能顯示 Chimera 視窗。
- `scripts\build-chimera-gfxstream-proxy-runtime.ps1` 已回到低風險 stock-ABI proxy：348 exports，C export hook 為 `stream_renderer_flush`、`android_setPostCallback`。
- 撤回 `RenderLib` C++ wrapper；它會依賴 stock gfxstream 未匯出的 `FeatureSet` 符號，不能作為穩定接線點。

### 驗證

- Headless smoke：proxy runtime 啟動時 `emulator.exe` 與 qemu 均含 `-no-window -no-audio`，`visible_risk_count=0`，結束後 `leftover_count=0`。
- 下一步仍是把真正 shared D3D11 texture producer 接進 modern gfxstream display-post path，不能靠原生 emulator 視窗或 raw gRPC/ADB fallback 證明 60 FPS。

---

## Session 63 — Native emulator architecture clarification（2026-06-06）

### 結論

- 不從零重寫完整 Android 模擬器；短期正確架構是 Chimera shell + headless Android Emulator/QEMU/gfxstream 相容核心 + custom display producer。
- 多開原生 Android Emulator 視窗是 bug，不是策略；正式路徑必須只有 Chimera 視窗。
- 現有 headless gate 已重新驗證：`-no-window`、hidden process launch、visible HWND watchdog、Job Object tree cleanup 都在。

### 驗證

- `cmake --build build --config Release --target test-process-launcher test-virtual-machine chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -R "test-process-launcher|test-virtual-machine"`：2/2 PASS。
- 殘留程序檢查：沒有 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。

---

## Session 64 — Gfxstream RenderLib proxy probe（2026-06-06）

### 修正

- 新增 `src\host\runtime\gfxstream_proxy\gfxstream_proxy_renderlib.cpp`，可 opt-in 包裝 `initLibrary` / `RenderLib` / `Renderer`，並補齊 `FeatureSet` copy/assign 避免 stock DLL 未 export 符號。
- `initLibrary` wrapper 預設只 forward stock `RenderLibPtr`；只有 `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` 才包回傳物件。
- `android_setOpenglesRenderer` 新增 `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` opt-in shared_ptr wrapper，但實測會讓 emulator 早退，不能作為正式接線點。

### 驗證

- `scripts\build-chimera-gfxstream-proxy-runtime.ps1`：PASS，348 exports。
- default proxy hidden/no-audio probe：`sys.boot_completed=1`，boot completed in 29283 ms，`leftoverCount=0`。
- proxy log：`renderlib_wrapper initLibrary result=forwarded`、`android_setOpenglesRenderer ...`。

### 狀態

- 可用 baseline 是 stock ABI proxy + default forwarding；不穩定 C++ wrapper 只能保留為 opt-in probe。
- 下一步要找不替換整個 Renderer shared_ptr 的 display-post/shared texture producer 接點。

---

## Session 66 — Headless-only low-interference runtime gate（2026-06-12）

### 修正

- 固定架構策略：不從零重寫完整 Android VM；保留 Android Emulator/QEMU/ranchu/gfxstream/Play image 相容核心，但正式產品只允許 Chimera 單一視窗與 headless backend。
- raw gRPC/MMAP/screenrecord/ADB capture fallback 改為 CLI-only 診斷；`CHIMERA_ALLOW_RAW_CAPTURE_FALLBACK` 只警告不生效，避免舊環境變數重啟高負載 1080p readback。
- `write-chimera-gfxstream-runtime-manifest.ps1` 在驗證前會移除 stale manifest；不合格 runtime 不再殘留 ready 訊號。
- `QemuBackend` 預設改為低干擾：2 vCPU、2048MB、hidden launch、startup `Idle`，暖機後回 `BelowNormal`。
- `ChimeraWindow` 不再轉發 mouse/wheel/key；輸入只由 `GuestDisplay` 以 guest 座標送進 `InputBridge`，避免雙送造成滾輪與點擊卡頓。
- `apply-chimera-gfxstream-patch.ps1` 已補 headless Vulkan display-post patch：bridge enabled 且無 surface 時仍進 `FrameBuffer` / `DisplayVk` producer path，讓 `recordCopy()` / `publishFrame()` 可接 shared D3D11 texture。

### 驗證

- `chimera-ui` build PASS。
- targeted tests 5/5 PASS；完整 non-integration `ctest` 20/20 PASS。
- fail-closed smoke exit 3，且無殘留 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。
- `verify-true-1080p60.ps1` 正確失敗於 `incompatible gfxstream runtime ABI; required screen background export is missing`。

### 狀態

- 這輪已修低干擾與單一 headless runtime gate，但 true 1080p/60 尚未達標。
- 下一步是讓 custom gfxstream runtime 補齊 SDK 36 ABI/imports，再重跑 verifier 到 PASS。

---

## Session 70 — Stale emulator port cleanup + native embed dormant（2026-06-13）

### 修正

- `VirtualMachine::start()` 啟動前會清理佔住 Chimera ports `5554/5555/8554` 且 process 名稱為 `emulator.exe` / `qemu-system*` 的 stale VM tree，避免前一輪殘留造成雙 VM、多開原生視窗與 host audio 卡頓。
- 正式 UI 路徑不再 pin `NativeEmulatorView`；QML 也只有在 unsafe diagnostics 啟用時才讓 native view 可見。預設只用 `GuestDisplay` 顯示 headless backend。

### 驗證

- targeted tests 3/3 PASS。
- Release build PASS；完整 non-integration `ctest` 20/20 PASS。
- `--no-emulator` smoke 沒有 `NativeEmulatorView pinned`。

### 狀態

- 這輪修多開原生 emulator 與 stale VM 疊加的防線；true 1080p/60 仍需 matching SDK gfxstream shared texture producer。

---

## Session 71 — gfxstream Vulkan bridge diagnostics + manifest ABI gate（2026-06-13）

### 修正

- gfxstream Vulkan bridge 補低頻 `enabled` / `recordCopy` / `publishFrame` 診斷與 1920x1080 runtime floor：bridge enabled 但無 surface 時仍可進 `DisplayVk` producer path。
- `write-chimera-gfxstream-runtime-manifest.ps1` 補 build ID 嚴格相等 gate：`gfxstreamSourceSnapBuildId` 必須等於 `baseEmulatorBuildId`，否則拒絕寫 manifest，避免 mixed ABI runtime 建立 `ready` 訊號。
- manifest gate 實測正確拒絕 `sdk-release` build 13278158 對 SDK emulator 15261927 的組合。

### 驗證

- patch/build parser PASS；targeted build PASS；targeted `ctest` 2/2 PASS；完整 non-integration `ctest` 20/20 PASS。
- 結束後無 Chimera/emulator/qemu/adb/ffmpeg 殘留。

### 狀態

- ABI gate 正確 fail-closed；custom gfxstream runtime 尚未有符合 SDK 15261927 build ID 的 matching source。

---

## Session 72 — gfxstream proxy log analyzer（2026-06-13）

### 修正

- 新增 `scripts\analyze-gfxstream-proxy-log.ps1`：離線分類 stock-ABI proxy log，只把 1920x1080 GPU display/resource signal（`stream_renderer_flush`、`stream_renderer_resource_create`）當下一步 hook 候選；`android_onPost` CPU readback 訊號正確拒絕。
- 合成 log 驗證：1080p `stream_renderer_flush` PASS；`android_onPost` 如預期 FAIL。

### 驗證

- proxy runtime build PASS，348 exports；non-integration `ctest` 20/20 PASS。
- 既有 proxy logs 沒有 1920x1080 GPU signal；子代理研究因額度限制失敗，無可採用結論。

### 狀態

- 分析器工具就位；仍需 stock-ABI GPU display-post signal 或 matching gfxstream source 才能前進 producer。

---

## Session 73 — initLibrary ABI crash 修正（2026-06-13）

### 修正

- 根因：`gfxstream_proxy.c` 用 `void*(void*)` C shim 承接 `gfxstream::RenderLibPtr`（non-trivial C++ return value）→ ABI 邊界 AV crash。
- 修法：移除 C shim；在 `gfxstream_proxy_renderlib.cpp` 改為 `extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary()`（exact C++ signature pure forward）。
- analyzer 同時計數 `renderlib_wrapper initLibrary` 與 `forward name=initLibrary`；build script 過時註解同步修正；gate 未放寬。

### 驗證

- proxy build PASS（348 exports）；headless smoke boot 完成：`initLibrary=1 androidSetOpenglesRenderer=1 rendererVtable=1`。
- analyzer 正確 FAIL `no 1920x1080 GPU display/resource signal`；`no_residual_processes=OK`。

### 狀態

- proxy 跨過 `initLibrary` ABI 關卡；renderer 初始化可觀測；仍無 GPU display-post signal。

---

## Session 74 — GrpcOnly verify mode + ABI empirical test（2026-06-13）

### 修正

- 新增 `verify-true-1080p60.ps1 -GrpcOnly`：驗證 production gRPC path（stock SDK + headless，62-67 FPS 1920x1080），不要求 custom shared texture runtime；`Assert-True1080p60GrpcLog` require stream start / reject D3D11+ADB fallback / require FPS≥60 dup≤5%。
- 新增 `-AllowMismatchedBuildId` R&D flag（verifier + manifest writer）；`CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK` bypass 加入 InstanceManager。
- **ABI 不相容 EMPIRICALLY CONFIRMED**：`sdk-release` gfxstream DLL (build 13278158) 在 SDK emulator 15261927 實測：DLL 載入、gfxstream backend 啟動，Vulkan bridge `ensureInitialized` 因 struct layout 不符 AV crash → emulator exit → Chimera exit 4。

### 驗證

- syntax PASS；parse-only 合成 log pass/fail 兩路 PASS。
- ABI crash 實測：log 確認 `Graphics backend: gfxstream` 正常，之後 `Emulator process tree exited unexpectedly`（exit 4）。

### 狀態

- blockers 實測確認：gfxstream shared texture → Vulkan struct ABI mismatch（非假設）；EmuGL → HAXM/WHPX absent。
- production gRPC path（`-GrpcOnly`）是目前可驗最佳 display path；true shared texture 1080p/60 待 SDK 15261927 matching gfxstream source。
