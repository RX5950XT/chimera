# Chimera Task Todo

---

## 2026-06-30 Session 95 — 實際手感卡頓修正：雙擊改最快路徑 + GuestVulkan/skiavk 正式接線

### Context
使用者回報實際打開仍跟原本一樣卡。根因：`start-chimera.cmd` 雙擊雖註解說 custom runtime，實際呼叫 `start-chimera.ps1` 無旗標 → 走 stock gRPC 慢路徑；GuestVulkan/skiavk 仍只在 verifier 內，不在正常啟動路徑。

### Plan
- [x] Stage 0：Fast+GuestVulkan 現況量測，確認 path=gpu-direct、skiavk 生效，但 adb swipe verifier 仍 ~20fps。
- [x] Stage 1：用 gfxinfo / chimera-timing 拆解，確認每幀 render 7–9ms、GPU 1ms、postFrameDirectGpu 0.6–1.8ms，非 GPU/post 瓶頸。
- [x] Stage 2：接 GuestVulkan/skiavk 到 host 正常 boot；GuestVulkan 時重開動畫；`start-chimera.cmd` 預設 `-Fast -InteractiveFirst`。
- [x] Stage 3：不用實體滑鼠的 SelfTest + unit tests；記錄 host-drag 一次性結果但停用該測法。

### Review
- **雙擊慢路徑修正**：`start-chimera.cmd` 現在預設 `-Fast -InteractiveFirst`；`scripts/start-chimera.ps1 -Fast` 自動設 `CHIMERA_GUEST_VULKAN=1`。`-Stock` 仍可手動強制 stock gRPC fallback。
- **GuestVulkan 正式接線**：host boot completed 後自動 set runtime props `debug.renderengine.backend=skiavk` + `debug.hwui.renderer=skiavk` 並 framework restart 一次；restart 後才跑性能設定、launcher provisioning、first-boot setup。GuestVulkan 時重開 Android animations（1/1/1），避免硬體路徑還用舊軟體路徑的「關動畫」權宜配置。
- **重要實證**：adb swipe verifier 仍約 `effAvg=20.4`，但它不是實際操作路徑；一次性 host mouse-drag probe 量到 `guestMax=116.7 / render=57.4 / dup=0`，證明 production input + GPU-direct path 有接近 60 headroom。該 probe 會搶實體滑鼠，使用者要求後已停用，不再使用。
- **驗證**：`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` PASS：`display=gfxstream-shared-texture (-Fast; normal UI via GuestVulkan/skiavk)`、`priority=normal`、1920×1080、screenshot 246,563 bytes、Settings foreground `interactivity=ok`、0 residual、exit 0。`ctest -LE integration` 20/20 PASS。
- **誠實邊界**：正式路徑已改到目前最快；adb verifier 仍不能代表真實手感。後續若要再量 host input，必須做不移動使用者游標的 host 內部 synthetic touch/test hook。

---

## 2026-06-30 Session 94 — roadmap item 2（真實連續渲染重測）+ item 4（GuestVulkan 預設化評估）

### Context
roadmap 剩 item 2 / item 4。/goal「完成未完成事項自排序」→ item 2 先（便宜、為 item 4 前置）。

### Plan
- [x] item 2：在 gl60 加 `heavyIters` intent extra（additive、重 GLES fill 探針）+ verifier `-HeavyIterations` passthrough。
- [x] item 2：light 60 非回歸 + heavy96 量測，定 gpu-direct 在真實負載下的 60 能力。
- [x] item 4：ctest 20/20 + gl60(`CHIMERA_GUEST_VULKAN=1`) 60 非回歸 + interactive Fast `-GuestVulkan` robustness（當前 DLL）。
- [x] item 4：定預設化決策（gated vs 翻預設）。
- [x] 順手修 `verify-interactive-ui.ps1` baseline/observe/pass exit-code 契約（255→0）。
- [x] 文件：CONTEXT/todo/CLAUDE/AGENTS/lessons 同步。

### Review
- **item 2（COMPLETE）**：light `effective min 59.2/avg 60.0/dup 0`（60 ✓）vs heavy96（1080p 全螢幕 plasma、96-iter/px）`min 5.8/avg 6.6/dup 0`。同一 gpu-direct path + host pipeline，**只加重 per-frame GLES fragment → 60→6.6**，dup=0 表示 guest 真只產 ~6 unique fps。→ ① gl60 的 60 依賴 trivial fill；② 瓶頸是 guest GLES backend=**SwiftShader 軟體填色**，非 post path；③ HW 重遊戲須走 guest Vulkan→NVIDIA。**scope-cut（誠實）**：HW Vulkan 連續渲染 60 未量（無 Vulkan game-loop app）。reproducible 探針：`verify-true-1080p60.ps1 -HeavyIterations N`。
- **item 4（COMPLETE，決策＝維持 gated）**：證據 ctest 20/20；gl60+Vulkan `min 57.9/avg 59.7/dup 0`（flag 不回歸 60）；interactive `-GuestVulkan` `hwui=skiavk` active、無 crash、`guestFps=21.7`（~2× skiagl）、gpu-direct、0 residual（flag 安全+robust）。**決策維持 gated**：① `-feature Vulkan` 單獨無使用者好處（skiavk 需 runtime props，未接進正常 boot）；② 翻預設影響 stock 日常路徑（未驗 stock+Vulkan）；③ 真正預設＝feature（接 runtime props + 雙路徑 regression），非翻 flag。**無 code 變更**。
- **順手修**：`verify-interactive-ui.ps1` success path `return`→`exit 0`（`finally` cleanup native 洩漏 `$LASTEXITCODE` 致 baseline 誤 exit 255）；失敗維持 throw。實測 `try` 內 `exit` 仍跑 `finally` + parse OK。
- **md5 對帳**：部署 gfxstream DLL md5＝`c81d2092…`（≠ doc 的 `FDF55A3E`；build 非 byte-deterministic）。功能全驗過（manifest gate + 60 + Vulkan robust）。md5 非 gate。
- **非回歸/衛生**：每輪 0 residual；ctest 20/20。

---

## 2026-06-30 Session 93 — stock gRPC `total=0` 根因:hardcoded capture port

### Context
roadmap「先修 stock 0-frame，再攻 general-UI 60」（使用者選完整 roadmap、先 3 再 1）。原假設是 readback 延遲 > watchdog;實證推翻 → 真正根因是 capture 硬寫 gRPC port 8554，與 emulator derived port（非預設 console 時 = 8560）不符 → 0 幀。

### Plan
- [x] 探索 gRPC capture watchdog 路徑 + 驗證 harness（2 Explore agents）。
- [x] 從 `-GrpcOnly` 失敗 log 實證根因:ADB 可見 gate 過但 gRPC `total=0`、無 restart 行 → 追到 `main.cpp:1518/1523` 硬寫 8554 vs `g_runtimeCfg.grpcPort`(8560)。
- [x] 根因修法:capture 改用 `g_runtimeCfg.grpcPort`（default 5554→8554 零回歸）。
- [x] 次要硬化:per-request transfer timeout 解耦 stall watchdog（`m_requestTimeoutMs`/`CHIMERA_GRPC_REQUEST_TIMEOUT_MS` 預設 6000）;外層 retry timer 加 `FramebufferCapture::hasInFlight()` base virtual gate。
- [x] 單元守門:`test_grpc_framebuffer_capture.cpp` 加 3 test（timeout 解耦/env override/env 低於 stall 被拒）。
- [x] 文件:CLAUDE.md（flag 表 + 已知問題）、AGENTS.md（troubleshooting）、lessons.md（修正 Session 90 誤歸因 + 新 port lesson）、CONTEXT.md、todo.md。

### Review
- **根因實證**：`-GrpcOnly` 修前 `fewer than 2 active gRPC capture samples`（exit 1，`total=0` 整輪）;keyboard log `port 8560` 但 capture 連 8554 為鐵證。Session 90 的「readback 延遲 > watchdog」歸因已證偽。
- **修後 PASS**：`verify-true-1080p60.ps1 -GrpcOnly` `result=pass`、`perf_samples=13`、`grpc_active_samples=9`（0→9）、`grpc_stream_fps_avg=4.4`、`unique_content_fps_max=1.0`、exit 0。
- **非回歸**：`ctest -LE integration` 20/20 PASS（含 3 新守門）;`start-chimera.ps1 -Fast -SelfTest` PASS（booted、1920×1080、Launcher→Settings `interactivity=ok`、screenshot 76,175 bytes、`residual_processes=0`、exit 0）。custom shared-texture path 未動。
- **誠實邊界**：robustness 修復（非預設 port 不再凍黑），**非 FPS 提升**;stock gRPC 1080p 仍 ~4–17 FPS（量到 4.4）。次要 timeout/gate 硬化對真實慢 readback 有益但非 0-frame 根因。
- **item 1 可行性量測（續做）**：`verify-interactive-ui.ps1 -Mode Fast -GuestVulkan` sustained-scroll `guestFps=18.7/streamFps=14.2/renderFps=13.8/effFps=13.6/dup=0%`、path=gpu-direct、`bottleneck=render`、`qemuCpuPctAvg=12%`。guest Vulkan 把 guest 拉到 ~19fps（vs skiagl ~9）但一般 UI scroll 是 push-based ~19fps，非 host/priority 可調 → **無 host 槓桿到 60，general-UI 60 維持 out-of-scope（架構性 guest cadence，需 gfxstream compositor R&D）**。item 2/4 未開工。

---

## 2026-06-28 Session 90 — 誠實互動量測 + 可設定 priority + path 觀察

### Context
使用者回報實際操作仍 ~1–2 FPS 且干擾背景音樂；Session 85/89 的 60fps 只是 GL60 synthetic 連續渲染證據，不代表日常互動。目標（使用者已確認）：誠實量測 + 修音訊（可設定 priority、降 churn）+ 最佳可用路徑 + 觀察性；general-UI 60 標為 out-of-scope 深層 R&D。

### Plan
- [x] 抽 `scripts/ChimeraVerifyCommon.ps1` 共用 harness；`verify-true-1080p60.ps1` dot-source（純搬移）。
- [x] 新增 `scripts/verify-interactive-ui.ps1`：真實互動分段量測、display path 分類、`CHIMERA_INT`/`CHIMERA_INT_PRIO` telemetry、誠實 gating、`-Observe`/`-Priority`。
- [x] `main.cpp`：硬寫 priority/cpus/ram → env→default resolver（`CHIMERA_INTERACTIVE_PRIORITY/CPUS/RAM_MB`）；加 `CHIMERA_DISPLAY` 觀察 log。
- [x] `start-chimera.ps1`：`-AudioFirst`/`-InteractiveFirst` sugar。
- [x] churn：measurement-gated；量到 helperSpawns=0 → 無需改動。
- [x] 量測工具不干擾被測對象：telemetry 改 per-PID `Get-Process` + 2s/4s cadence。
- [x] 文件：lessons.md / CLAUDE.md / AGENTS.md / CONTEXT.md / README.md / todo.md。

### Review
- **Fast 一般 UI 實測**：sustained Settings scroll `effFps≈18–20、guest≈25、dup=0`，path=**gpu-direct**（`postFrameDirectGpu`、`postFrameCpu=0`）。`guest≈stream≈render`+dup=0 → host pipeline 1:1 跟上，瓶頸=**guest SurfaceFlinger render cadence**（push-based ~25 unique fps）+ cold-launch hitch（早期 maxMs 8.4s、steady ~400ms）≈ 使用者感受「1–2 FPS」。
- **observability**：`CHIMERA_DISPLAY path=grpc-unary/gfxstream-shared-texture … priority=below_normal cpus=4 ramMB=4096` 在 Stock/Fast live log 皆證實；resolver 預設等於原生效值（cpus/ram 被 normalizer floor 回 4/4096，只有 priority 真正生效）。
- **audio**：`helperSpawns=0`、`qemuCpuPctAvg≈22%`、priority ramp `BelowNormal,Idle` 實證；steady-state churn 已 ~0，調節桿是 priority（`-AudioFirst`=idle/EcoQoS）+ 1080p readback。
- **stock gRPC 0 幀（既有）**：`-Mode Stock` 與 proven `-GrpcOnly`（無 telemetry）皆 `total=0`，證明非本次改動；1080p getScreenshot 延遲 > 無幀 watchdog。custom shared-texture 不受影響。
- **驗證**：`ctest` 20/20 PASS；`start-chimera.ps1 -Fast -SelfTest` PASS（screenshot 75,673 bytes、Settings 互動、0 residual）；`verify-interactive-ui.ps1 -Mode Fast` 完整跑通 + 誠實 gating（reproducible effFps 18–20）；每輪 0 residual。
- **extraction 非回歸（gl60 regression 證據）**：`verify-true-1080p60.ps1 -MeasureSeconds 60` 完整經共用 lib 執行（gl60 build/boot/gate/perf 全跑通），`guest=stream=60.0` solid、`postFrameDirectGpu=49`、`GL readback=0`、`GPU-direct D3D11 import OK=1` → producer+pipeline 60 完好；但 host **Qt render** 在本機並行負載（active Claude session）下抖到 54–57，`effMin=56.2 / avg≈58.8` 微差 strict floor（57/59）。此為 host-render jitter，非 extraction 回歸；安靜機器（Session 89）為 `min 59.9`。
- **誠實邊界**：general-UI 60 未交付（需 gfxstream compositor R&D，使用者已確認 out-of-scope）。

---

## 2026-06-28 Session 89 — 嚴格可見 1080p/60 穩定 PASS

### Plan
- [x] 修正 verifier port 選擇：自動挑選 console/ADB 皆空的 port pair，避免 `urbanvpnserv` 占用 `5561` 造成 emulator 已 boot 但 ADB 看不到 device。
- [x] 修正 GPU-direct runtime regression：恢復 D3D11 NT shared texture 由 Vulkan external-memory import，`postFrameDirectGpu` 不走 staging `UpdateSubresource`。
- [x] 修正 stale ColorBuffer log storm：`invalidateColorBufferForVk/Gl` 與 bad color buffer handle 改低頻節流，避免 18k+ stderr lines 拉低 producer。
- [x] 修正 verifier 假 PASS：post-warmup `effective<=0` 不再被過濾，直接 fail；量測期間保持 host window 前景避免 occlusion throttling。
- [x] 驗證：嚴格 120 秒可見 verifier PASS、non-integration unit tests 20/20 PASS、`start-chimera.ps1 -Fast -SelfTest` PASS。

### Review
- **嚴格 120 秒 PASS**：`visible_frame_bytes=56133`、`nonblack=83.3%`、`luma_spread=305`、`perf_samples=25`、`effective_fps_min=59.9 / avg=60.0`、`dup=0`、`result=pass`。
- **GPU-direct 證據**：`GPU-direct D3D11 import OK=1`、`path=GPU-direct=79`、`Shared D3D11 texture display capture started=1`、`chimera-raw=0`、`GL readback fallback=0`、`recordCopy unavailable=0`。
- **穩定性修正**：ColorBuffer stale miss 由 18,563 行降到 33 行 throttled diagnostic；不再讓 stderr I/O 破壞長測 FPS。
- **一鍵 Fast 可用**：`start-chimera.ps1 -Fast -SelfTest` PASS：boot completed、1920×1080、Chimera Launcher foreground、screenshot 75,650 bytes、Settings interactivity OK、0 residual process。
- **測試**：`ctest --test-dir build -C Release --output-on-failure -LE integration` → 20/20 PASS。

---

## 2026-06-26 Session 88 — custom runtime 一般 UI 黑屏修復

### Plan
- [x] 用 systematic debugging 重新驗證 Session 87 結論，拆分 ANGLE init / shader version / SurfaceFlinger draw 三層。
- [x] 反查 ANGLE crash stack：`libGLESv2.dll` AV 由 `translator::gles2::glDrawArrays()` 觸發，program 28/31，ANGLE 新舊版本皆可重現。
- [x] 驗證 SwiftShader ES shader path：`CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1` 讓一般 UI boot completed、截圖非黑、shader error=0。
- [x] 收斂正式修法：只在 gate 開啟時關閉 `shouldEnableCoreProfile()`，不改 renderer identity / 不啟用 EglOnEgl，避免 direct-VK 60fps 路徑回歸。
- [x] 接入 `scripts/start-chimera.ps1 -Fast`，更新 README / CLAUDE.md / CONTEXT.md / patch script。
- [x] 驗證：`start-chimera.ps1 -Fast -SelfTest` PASS；`verify-true-1080p60.ps1 -WarmupSeconds 15` PASS；0 residual process。

### Review
- **完成**：`-Fast` custom runtime 一般 UI 黑屏已修好。SelfTest：boot completed、1920×1080、Chimera Launcher screenshot ~75–76KB、Settings foreground/interactivity OK、0 residual。
- **根因**：renderer HOST + underlying SwiftShader ES 時 `shouldEnableCoreProfile()` 發桌面 `#version 330 core` shader，SwiftShader ES compiler 拒絕；修法是 gate 下關閉 core-profile shader emission。
- **ANGLE 結論**：ANGLE/D3D11 可 init，但 SurfaceFlinger 後續 draw 會在 ANGLE `libGLESv2.dll` AV；新版 ANGLE 也一樣，因此不作正式路徑。
- **60fps 回歸**：direct-VK path 保持可用；`effective_fps_min=58.8 / avg=59.6 / dup=0`，`postFrameDirectGpu=134`、CPU readback fallback=0。

---

## 2026-06-24 Session 87 — host GLES → 硬體 (原生 GL / ANGLE) + 文件/README 更新

### 目標
1. 更新 + 精簡 CLAUDE.md / CONTEXT.md / AGENTS.md（對齊現況）
2. 更新 README.md（移除過時的 native-embed 預設說明，反映 headless + stock/custom 路徑）
3. 把 host GLES 從 SwiftShader 導到硬體（原生 GL / ANGLE），讓一般 UI 走硬體加速、不再黑屏

### Plan
- [x] 調查（workflow 並行）：emugl_config GPU-mode 對應、isCoreProfile/shader 版本發射、Compositor shader、runtime log 證據（4/5 reader 完成；workflow 因前次 usage limit 中止，已 salvage）
- [x] 根因確認（log 實證）：headless SwiftShader host GLES + renderer=HOST → `#version 330 core` → ES 編譯器拒 → 黑屏
- [x] 實作 + 實測硬體路由：CLI angle_indirect（被 prebuilt binary 拒，已還原）→ DLL 內 emugl_config fallback（重建實測 → init hang）
- [x] 重建 gfxstream DLL（ANGLE patch）+ runtime 驗證（`verify-hardware-ui.ps1`，bridge 與 -GrpcDisplay 兩種模式皆 init hang）
- [x] 還原 verified 60fps DLL（md5 相符），ANGLE patch staged 在 patch script（gate 預設關、標記 hang）
- [x] 文件 + README 誠實更新（BLOCKED 結論）
- [x] unit tests 20/20 + 無殘留 process

### Review
- **結果：hardware host GLES routing BLOCKED（未完成，但根因與 blocker 全部 log 實證）**。
- 根因：headless `emugl_config.cpp:297-353` fallback 到 SwiftShader（軟體），renderer enum 仍 `SELECTED_RENDERER_HOST` → translator 發桌面 `#version 330 core` 合成器 shader → SwiftShader ES 編譯器拒 → SurfaceFlinger 合成空 → 一般 UI 黑屏。gl60 60fps 因 `postFrameDirectGpu` 繞過合成器。
- 三條硬體路徑實測全擋：① native WGL 需視窗（headless 不行）；② CLI `-gpu angle_indirect` 被 prebuilt `emulator.exe` `gpuChoiceBasedOnGpuOptions` 判 invalid → auto → SwiftShader+lavapipe → exit 4；③ ANGLE 經 DLL 內 emugl_config fallback（重建 DLL）→ emulator **init hang**（停在 `Found systemPath`），bridge 與純 gRPC 皆然。
- 動作：`VirtualMachine::emulatorGpuMode` angle 嘗試已還原（保留 `CHIMERA_GPU_MODE` override）；ANGLE patch staged 在 `apply-chimera-gfxstream-patch.ps1`（`CHIMERA_GFXSTREAM_HEADLESS_ANGLE=1` gate，預設關、標記會 hang）；新增 `scripts/verify-hardware-ui.ps1`；deployed gfxstream DLL 還原為 Session 85 verified（md5 相符），無回歸。
- 驗證：`chimera-ui` build PASS；unit 20/20 PASS；deployed DLL md5 == verified；0 殘留 process。
- 已驗證狀態維持：stock 路徑日常可用（`start-chimera.cmd`）、custom runtime 連續渲染 1080p/60。
- 下一步（resume 點）：ANGLE headless init hang 的 verbose-log 調查；或重建 emulator.exe（超出現行範圍）。

---

## 更早 Sessions（精簡 changelog；完整 Plan/Review 見 git log 與 CLAUDE.md/CONTEXT.md）

- **Session 85 (06-22) — TRUE 1080p/60 verifier PASS**：`chimera-gl60-smoke` 連續渲染經 direct-Vulkan→D3D11 path 量到 steady `min 59.8/avg 60.0/dup 0`；瓶頸是 guest render cadence 非 host pipeline。
- **Session 83-84 (06-22) — D3D11 DXGI shared handle fix**：`CreateSharedHandle` 改 `DXGI_SHARED_RESOURCE_READ|WRITE`，`OpenSharedResourceByName` 不再 `E_INVALIDARG`。
- Session 82 (06-19) — shmem 吞吐量確認（consumer ceiling ~50fps）+ `BELOW_NORMAL` 移除 EcoQoS（triangle demo 7→24fps）+ reviewer follow-up。
- Session 81 (06-19) — shmem delivery 路徑確認（非 GuestVulkanOnly 開機 + shmem PASS）。
- Session 80 (06-18) — NVIDIA Vulkan loader 調查收斂（失敗根因是測試 harness `-gpu swiftshader_indirect` 污染，改 `-gpu host` 後 NVIDIA OK）。
- Session 79 (06-17) — AdbH264 screenrecord 死路確認（headless 0 bytes）+ 背景音樂干擾改 priority `idle`。
- Session 78 (06-17) — 音訊啟用（移除 `virtio-snd-pci`）+ gRPC display 解鎖 + GrpcOnly verifier 修正。
- Session 77 (06-17) — CPU readback → Win32 shmem 管道打通，Android 畫面首次送達 host。
- Session 76 (06-16) — `std::promise` MSVCP140 ABI crash 改 `Lock+CV` → Android headless boot CONFIRMED。
- Session 75 (06-14) — Vulkan backend 確認 + GetProcAddress/vkQueuePresentKHR hook（headless 不 present，proxy frame capture 死路）。
- Session 74 (06-13) — GrpcOnly verify mode + gfxstream ABI 不相容實測確認。
- Session 73 (06-13) — `initLibrary` ABI fix（exact C++ signature）+ proxy smoke PASS。
- Session 72 (06-13) — stock-ABI gfxstream proxy display probe + analyzer script。
- Session 71 (06-13) — gfxstream bridge diagnostics + 1080p floor + 自研 VM 邊界。
- Session 70 (06-13) — stale emulator port cleanup + native embed 休眠。
- Session 69 (06-13) — headless runtime 邊界 + snapshot shutdown I/O 收斂（`-no-snapstorage`）。
- Session 68 (06-13) — strict shared-texture fail-closed + SDK source 對齊盤點。
- Session 67 (06-12) — 可見原生 Emulator 雙 gate（unsafe flag + internal diagnostics session）。
- Session 66 (06-12) — 單視窗 + 低干擾 fallback 擷取收斂（`LowInterferenceProcess`）。
- Session 65 (06-12) — gfxstream bad-runtime gate + VM state hardening。
- Session 61-64 (06-05~06) — gfxstream RenderLib/proxy probe、native emulator strategy、headless visible-window watchdog、Vulkan producer gate。
- Session 55-60 (06-03~05) — headless-only launch gate、ABI gate、modified gfxstream runtime build、visible window hard gate、proxy runtime probe。
- Session 50-54 (06-01~03) — window-capture rollback + audio guard、EmuGL DLL artifact、gfxstream attestation/verifier、renderer strict shared texture、runtime gate + low-interference fallback。
- Session 38-49 (06-01) — true 1080p/60 runtime gate、custom EmuGL runtime artifact gate、no-downscale guard、host audio startup isolation、gRPC RGBA fallback、legacy 1080p floor、normalize instance config、enforce 1080p floor、custom EmuGL build probe。
- Session 31-37 (05-31) — screenrecord/MMAP regression containment、audio stutter re-fix、EmuGL shared texture bridge/opt-in、reusable D3D11 shared texture producer。
- Session 26-30 (05-25~27) — wheel/input jank、honest FPS、shared memory/D3D11 renderer、enforce 1080p floor、audio stutter guard。
- Session 13-25 (05-21~25) — gRPC 60fps 穩定 + orphan qemu 根因、Job Object hardening、Quick Boot snapshot/fallback/verifier、60fps display path + landscape、1080p stream + clickable input、truthful FPS + clean launcher、black screen fix、emulator space + required apps、app provisioning、Home app false-positive fix、host audio stutter mitigation。
