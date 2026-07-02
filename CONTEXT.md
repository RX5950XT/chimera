# Project Chimera — CONTEXT.md

> 開發歷程記錄。供下一個 Agent 快速接手用，不需要從 git log 重建脈絡。

## 2026-07-02 — Session 101 — `-Fast` host 視窗黑屏三層根因全修 + emulator idle 自殺修復

- **使用者回報**（Session 100 修復後仍黑）：啟動等很久還是黑畫面、有效 FPS 0。取證：guest ADB screencap 75KB 正常、producer `published sequence` 跳動、host `total` 幀數增長、host 視窗 PrintWindow 中心像素 (2,5,4)=純黑——**counters 全綠、像素全黑**。且 AVD 檔案時間軸顯示使用者那次 run 的 emulator 在 boot 後 ~4 分鐘死亡。
- **Bug B（先破案）：emulator idle 自殺**——`VirtualMachine.cpp` 對 emulator 傳 `-idle-grpc-timeout 300`（qemu binary strings 定位到 `IdleInterceptor.cpp`、log `Idled to long, shutting down`）。`-Fast` shared-texture 顯示**不走 gRPC**，黑屏下使用者無從輸入 → 300s 零 gRPC 流量 → emulator 優雅自殺 →「等多久都黑」。stock 路徑 getScreenshot 輪詢永不 idle、verifier 全程注入 input，所以 15 session 沒踩到。修：移除旗標（ProcessLauncher 的 kill-on-close Job Object 已負責 orphan 清理）+ `grpcEnabledNeverRequestsIdleShutdown` unit test。
- **Bug A（主）：shared texture 從 Session 85 起發佈的一直是零幀**。三層獨立 bug 疊加，用三支 probe 分層定案（缺一不可，只修一層整條仍黑會誤判）：
  1. **GL→VK 內容同步缺失（最深層）**：SurfaceFlinger→HWC→`PostCmd::Compose` 由 host GL（CompositorGl）合成 target ColorBuffer，**從不標 `mGlTexDirty`**（只有 `rcFlushWindowColorBuffer`/eglSwapBuffers 路徑會標）→ `invalidateForVk()` 恆 `exit=clean` no-op → `borrowForDisplay(kVk)` 借到的 VK sibling image **從未被寫入** → `postFrameDirectGpu` blit 複製全零。bridge 內新診斷 `debugReadbackSharedImage`（VK 端讀回 mImage 中線）實證：修前 `nonzero=0/120`、修後 `nonzero=120/120 center=245,245,245,255`。修：headless post 分支 borrow 前 `colorBuffer->flushFromGl(); colorBuffer->invalidateForVk();`——兩者對 VK-backed／GL-VK 共享記憶體內容都是 no-op，真 Vulkan 內容保持 zero-copy；GLES 合成內容誠實付出每幀 GL readback（SwiftShader CPU）+ VK upload。
  2. **Vulkan import 無 aliasing**：bridge 把 `IDXGIResource1::CreateSharedHandle` 的 D3D11 NT handle 用 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT` 匯入且無 dedicated info——spec 違規，NVIDIA 全回 VK_SUCCESS 但寫入落在 driver 私有記憶體。獨立 probe `vkinterop.exe`（窮舉匯入型別×keyed-mutex×跨 process 矩陣）5 分鐘定案：OPAQUE=寫入丟失、`D3D11_TEXTURE_BIT`+`VkMemoryDedicatedAllocateInfo`=可見；NTHANDLE 不配 KEYEDMUTEX 根本建不出 texture（E_INVALIDARG）。修：bridge template 改正確 import + `vkGetMemoryWin32HandlePropertiesKHR` 診斷。
  3. **Consumer 缺 AcquireSync**：keyed-mutex texture（misc 0x900）跨 process 不 acquire 讀=零（`texprobe.exe` 矩陣實證：no-acquire=0、acquired=內容；producer 端不帶 `VkWin32KeyedMutexAcquireReleaseInfoKHR` 沒關係）。Qt QSG 無法跨 render pass 持鎖 → `GuestDisplay` 每個新 sequence `AcquireSync(0,4ms)`→`CopyResource` 到私有 texture→`ReleaseSync(0)`，QSG 取樣私有副本。**陷阱**：`AcquireSync` 回 `WAIT_TIMEOUT (0x102)` 能通過 `SUCCEEDED()`，必須 `== S_OK`。
- **Gate 漏洞修補（為何潛伏 15 session）**：歷來所有「可見/60fps」gate 都驗 guest 端 ADB screencap（走 SurfaceFlinger、不經 bridge）+ host 端 counters（sequence/FPS，零幀也照跳）。新增 `ChimeraVerifyCommon.ps1::Get-HostWindowPixelStats`（PrintWindow `PW_RENDERFULLCONTENT` 抓 D3D swapchain + 中央顯示區取樣，避開恆亮側欄）並接入 `start-chimera.ps1 -SelfTest`（gate `host_window_nonblack_pct>=5`，30s retry）。
- **patch script drift**：modern tree（`frame_buffer.cpp` 小寫）的 headless 段被前 session 直接手改（加 timing 碼），patch script 對應段（`$hasLegacyGlDisplay` guard 內）從此死碼、靜默跳過；改 script 完全無效卻印「Applied」。本輪 tree+script 同步修（modern 段用 `Replace-Text` 含 already-applied check），教訓入 lessons.md：改 patch script 後必 grep tree 確認落地。
- **驗證**：ctest 23/23 PASS；`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` **PASS**（`screenshot_nonblack=100%`、**`host_window_nonblack_pct=100.0`（新 gate）**、`interactivity=ok`、0 residual）；host 視窗實截（PrintWindow）顯示 Settings+IME 鍵盤、側欄有效 FPS 43——使用者可見的真畫面。
- **誠實邊界**：① 歷史 GPU-direct「60fps」（S85/89/99 的 gl60/interactive 數字）量的是零幀 blit 的節奏，修復後 GLES 內容每幀多付 SwiftShader GL readback+VK upload，真實數字需重新基準（本輪互動實測 43 eff FPS）。② `boot_seconds` 本輪 87s vs S100 33s——當日連續 6+ 次 boot/build 的環境噪音成分大，空機重測再定論。③ 真 Vulkan-backed 內容（Vulkan 遊戲）仍走 zero-copy 直通路徑不受同步成本影響，但其可見性尚未單獨驗證。
- **診斷留存**（此類 bug 的直接偵測器，皆低頻）：bridge `debugReadbackSharedImage`（每 240 幀 VK 端讀回+log）、`invalidateForVk` exit-path log（每 600 次）、init 時 `D3D11 handle props` log。probe 工具原始碼在 session scratchpad（`texprobe.cpp`/`vkinterop.cpp`），需要時可重編。

## 2026-07-02 — Session 100 — `-Fast` 啟動黑屏根因修復（skiavk 半套用）+ 載入加速

- **使用者回報**：雙擊啟動後中間模擬畫面全黑（側邊 UI 正常）、載入慢。
- **根因（多重實證）**：Session 95 加入的 host 端 skiavk 切換（boot 後 `setprop debug.hwui.renderer=skiavk` + `stop`/`start`）在此 `google_apis_playstore` **user image**（`ro.debuggable=0`）上 `stop`/`start` 回 **"Must be root"** 被 `runAdbShell` 靜默吞掉 → framework 從未 restart、SF 永遠 SkiaGL；但 `debug.hwui.renderer=skiavk` **有**寫入 → setprop 之後才首次 HWUI init 的 process（launcher 被 `pm install -r` 重啟）走 Vulkan → guest VK surface 在 host 是 NVIDIA memory、SF 合成在 host 是 SwiftShader-ES → **無法取樣 → app 視窗全黑**；pre-setprop 的 SystemUI 維持 GLES 可見 → 「狀態列有、中間黑」。可見與否取決於 process 啟動時序 → 過去被誤判為「偶發 black-boot」。
- **skiavk 不可行的完整證明**（別再試）：獨立 probe `-systemui-renderer skiavk` boot → `debug.hwui.renderer=skiavk` 生效（init 有翻譯 hwui）、`Pipeline=Skia (Vulkan)` 實證，但 init **不翻譯** `ro.boot.debug.renderengine.backend`（getprop 空）→ SF 留在 SkiaGL → home 10.7KB / Settings 10.6KB spread=0 全空白。SF 要切 Vulkan 三條路全部實證死路：① root restart（`stop`→"Must be root"）；② boot-time prop（init 不翻譯 `ro.boot.debug.renderengine.backend`）；③ `setprop ctl.restart surfaceflinger` → **SELinux denied**（`avc: denied { set } property=ctl.restart$surfaceflinger scontext=u:r:shell:s0 permissive=0`，SF pid 不變）→ **skiavk UI 在此 playstore user image 完全不可行，定案**。歷史「GuestVulkan 對照 ~2×/hwui=skiavk active」數字全是採樣 pre-setprop GLES process 或 getprop 回顯（invalid comparison），不可引用。
- **修復**：① `main.cpp` 移除 `applyGuestVulkanHardwareUi()` + poller 的 skiavk apply/12s grace（黑屏源 + 省時）；`CHIMERA_GUEST_VULKAN=1` 現在**只**代表 `-feature Vulkan`（Vulkan app/遊戲直達 NVIDIA；gl60 非回歸 Session 94）。② `installChimeraLauncher()` 以 md5 比對跳過重複 `pm install -r`/force-stop/relaunch（省 ~5-8s + 消除 home 重啟閃爍）。③ `QmlAndroidControls.bootReady` + `ChimeraWindow.qml` `guestReady`：「等待 Android 啟動…」placeholder 保持到 boot poller 完成（`-no-boot-anim` 下 boot 期間全是黑幀，舊行為第一張黑幀就撤 placeholder → 使用者看黑）。④ `start-chimera.ps1 -SelfTest` 補 home screenshot **內容 gate**（bytes≥20000/nonblack≥10%/spread≥40）——舊 SelfTest 只驗 dumpsys focus，讓黑屏 bug 假 PASS 過多個 session。⑤ `verify-interactive-ui.ps1 -GuestVulkan` 移除 setprop/stop/start 虛構流程與 `CHIMERA_GUEST_VULKAN_HOST_SETUP`。⑥ VirtualMachine.cpp/AGENTS.md/start-chimera.ps1 註解與文件全面改為「禁止 skiavk UI 切換」+ 實證理由。
- **驗證**：build PASS；`ctest -LE integration` 23/23 PASS；`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` **PASS**：`boot_seconds=33`、`visible_home_seconds=49`、screenshot 76,219 bytes / nonblack 100% / spread 716（真 home，人眼確認 CHIMERA launcher + 5 入口）、`interactivity=ok`、0 residual。載入時間從舊「正常」路徑 ~80–110s（boot+restart+grace+reinstall）降到 **~40-49s 可見 home**，且全程有 placeholder 不再裸黑。
- **殘留 R&D 清理**：repo 根目錄 2026-05 的 `runtime-*.log/out/err`、`chimera-mmap1080-ui.*`（gitignored 舊輸出）刪除。
- **第二個真 bug：背景手把輸入漏進 guest**（驗證 60fps 時抓到）：使用者在主螢幕玩 P5R（手把），gl60 量測反覆在「隨機時間」凍結。sidecar 凍結瞬間取證：guest 前景=launcher、gl60 process 活著但 activity backgrounded、螢幕 Awake → 有東西送 HOME/BACK 進 guest。根因：`GamepadManager` 以 `XInputGetState`（system-wide）60Hz 輪詢並無條件轉發（`main.cpp` `gp.poll()`），使用者在**別的視窗**的手把輸入全部進 guest。修：poll 前 `QGuiApplication::applicationState()==ApplicationActive` 才轉發（BlueStacks 同款）。教訓：全域輸入 API 轉發必須 focus-gate；「guest 前景 app 無故回 HOME」先查 host 全域輸入。verifier 視窗改副螢幕（`CHIMERA_VERIFY_WINDOW_ORIGIN=-2520,45` User env）：main.cpp 啟動時讀該 env 直接 setPosition（**從第一幀就在副螢幕**——舊行為是 boot/gate 階段停在主螢幕預設位置、量測才被釘到副螢幕，期間蓋住使用者遊戲會被合理地關掉→run 中斷）；`Ensure-HostWindowVisible` 對 exited process 的 `MainWindowHandle`（ETS 回 $null → `[IntPtr]$null` cast 炸）加 try/catch 防禦。量測方法教訓：使用者同機玩 AAA 遊戲時，嚴格 `min≥57` gate 會被遊戲負載尖峰打穿（量測窗內曾連續 150s 60.0fps 但 min 掉到 8.6）；乾淨嚴格 PASS 要機器空閒時跑。

## 2026-07-01 — Session 98 — audit-driven concrete bug 修復（8 維 workflow → 對抗式 verify → TDD）

- **背景**：使用者「有很多問題修復它們」。ultracode 開，跑全 repo 8 維審查 workflow（ui/instance/graphics/input/gfxstream/utils-audio/scripts/consistency）。verify 階段中途吃 session limit（只 1 finding 跑完 CONFIRMED），剩餘 find findings 由主 loop 從 agent transcript 救出、逐一手動對抗式驗證 + TDD 修。
- **修掉 8 個（皆先 RED 再 GREEN，除 gfxstream template 無法 runtime 驗）**：① `main.cpp:1597` `--adb-display-fallback` 硬寫 ADB `5555`（Session 93 gRPC port 同類漏網）→ `g_runtimeCfg.adbPort`；② `FileUtils::ensureDir` 目錄已存在回 false → `|| is_directory`；③ `MacroEngine` Tap playback 只送 press 不送 release（guest 永久按住）→ 補 release；④ `QemuBackend` stop()/onHealthCheck() 在 `waitForExit`（WAIT_OBJECT_0 已關 handle）後又 `CloseHandle` double-close → 只在 wait<0/非 running 時關；⑤ `gfxstream_proxy_d3d11.cpp:208` `CreateSharedHandle(GENERIC_ALL)`（Session 83-84 對 production bridge 修過、proxy 漏，consumer E_INVALIDARG）→ `DXGI_SHARED_RESOURCE_READ|WRITE`；⑥ `VirtualMachine.cpp:733` exit-monitor null handle 不 close（leak + 與 stop() close 競態）→ 兩處 `InterlockedExchangePointer` 原子 claim；⑦ `ChimeraGfxstreamVulkanSharedTextureBridge.cpp:491` vkBindImageMemory 失敗漏 `CloseHandle(sharedHandle)`（sibling 都有）→ 補；⑧ `LocationSimulator::setLocation` 走 route throttle 被丟（explicit teleport 第二次被 1Hz/移動門檻擋）→ `emitGeoFix(pt, force=true)`。
- **新增/改 6 個測試**：新 `test_file_utils.cpp` / `test_ui_main_port_contract.cpp` / `test_runtime_source_contract.cpp`（後者含 proxy D3D11 + VirtualMachine handle-ownership source contract）；改 `test_macro_engine.cpp`（InputBridge callback 觀測 tap 的 down+up）/ `test_qemu_backend.cpp`（double-close source contract）/ `test_location_simulator.cpp`（explicit set 不被 throttle 丟）。
- **刻意跳過 2 個（記錄不改）**：`AudioBridge.cpp:93` WASAPI forced format 無 `AUTOCONVERTPCM`（`AudioBridge::instance()` 產線從未被呼叫 — emulator 直接路由 Goldfish→WASAPI；改 init 無法無音訊裝置驗證 + cbSize/EXTENSIBLE 風險 → dead code latent）；`ChimeraGfxstreamVulkanSharedTextureBridge.cpp:587` staging-copy 死分支（`mStagingBuffer` 從未非 null；移除動 member 又無法重建驗 DLL → 無害跳過）。
- **驗證**：完整 Release build PASS；**`ctest -LE integration` 23/23 PASS**。誠實邊界：correctness/leak/race/contract 修復，非 FPS；gfxstream template 兩處未經 DLL runtime 重驗（patch script verbatim copy，下次 rebuild 帶入）。

## 2026-06-30 — Session 97 — PowerShell harness port / cleanup 小修

- **背景**：延續 Session 96 後再掃同類 bug，發現 `start-chimera.ps1 -SelfTest` 與 `verify-hardware-ui.ps1` 仍有舊式 port/cleanup 邏輯；`verify-interactive-ui.ps1` 雖自動挑 port，但明確傳 odd `-ConsolePort` 時仍可進入錯誤 console/ADB pairing。
- **修正**：`ChimeraVerifyCommon.ps1` 新增 `Resolve-EmulatorConsolePort`（0=自挑 free console/ADB pair；非 0 必須 5554–5680 even）；`verify-interactive-ui.ps1`、`start-chimera.ps1`、`verify-hardware-ui.ps1` 全改走 resolver。`start-chimera.ps1 -SelfTest` 未明確指定 port 時會自挑 free pair；`verify-hardware-ui.ps1` 不再硬寫 `emulator-5554`、不再移除 `CHIMERA_EMULATOR_CONSOLE_PORT`。
- **cleanup**：`start-chimera.ps1 -SelfTest` 與 `verify-hardware-ui.ps1` 移除本地 `Kill-All` / 全域 `Get-Process emulator,qemu-system*`，改用 shared cmdline-filtered `Stop-ChimeraProcesses` / `Wait-NoChimeraProcesses` / `Get-ChimeraProcesses`，避免殺掉非 Chimera emulator。
- **驗證**：PowerShell parse OK；`verify-interactive-ui.ps1 -ConsolePort 5555` 與 `verify-hardware-ui.ps1 -ConsolePort 5555` 都 fail fast，錯誤明確指出 console port 必須是 even。本輪是 harness robustness，非 FPS/perf 改動；不使用任何會搶實體滑鼠的測試。

## 2026-06-30 — Session 96 — audit-driven bug 修復（先 bug/回歸，後性能 R&D）

- **使用者**：「還有很多問題修復它們」。ultracode 開：跑 14-agent 審查 workflow（4 維 find → 對抗式 verify）掃整 repo 具體 bug/回歸/矛盾，再逐一修高信心、可小修者。先決定本輪不把 general-UI 60 當承諾（仍 R&D），只修能重現驗證的明確問題；全程不用會搶實體滑鼠的測試。
- **修掉 7 個（皆 P2，對抗式 verify 後）**：① `start-chimera.ps1` `-RequireSharedTexture` runtime 缺失時靜默退 stock → 加 fail-closed `throw`；② `ConsolePort` `ValidateRange` 收 odd port（Android 要 even）→ `ValidateScript` 限 even；③ `README.md:16` 「host input 已量到接近 60 headroom」過度表述（引用已禁用 mouse-drag probe）→ 改誠實 ~20fps + general-UI 60 仍 R&D；④ `verify-quick-boot.ps1` 硬寫 `emulator-5554`/不挑 free port → dot-source `ChimeraVerifyCommon.ps1` + `Get-FreeEmulatorConsolePort` + 設 `CHIMERA_EMULATOR_CONSOLE_PORT`；⑤ `verify-interactive-ui.ps1 -GuestVulkan` 從不設 `CHIMERA_GUEST_VULKAN=1` → emulator 沒帶 `-feature Vulkan`、skiavk 靜默退 GLES 但 getprop 仍回 skiavk（**假對照**）→ 補 env + TouchedEnv 還原；⑥ `verify-quick-boot.ps1` `Get-ChimeraProcesses` 無 cmdline filter，cleanup 會殺機器上任何 emulator/qemu（含 Android Studio/BlueStacks）→ 改用 shared cmdline-filtered 版；⑦ `AGENTS.md:66` 一個 0x0B（VT）控制字元把 `\verify-true-1080p60.ps1` 變 `\v`+`erify…` → 還原。
- **對抗式 verify 否證 3 個（沒亂改）**：main.cpp `boot_completed` gate（刻意 robustness）、`$LASTEXITCODE` 洩漏成 exit 255（實機重現實際 exit 0）、CLAUDE.md:331「預設 stock」（dated changelog 非現況）。
- **audit 前先手修的口徑矛盾**：`start-chimera.ps1` header 仍說「default stock」、`start-chimera.ps1`/`main.cpp` 註解「general-UI feel ~60」、README SelfTest 範例未帶 `-Fast -InteractiveFirst`。
- **驗證**：三支改過 .ps1 `Parser::ParseFile` 無錯；ctest 20/20（C++ 僅改註解、無行為變更、不重建）；`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` PASS（1920×1080、Settings `interactivity=ok`、`residual_processes=0`、exit 0）。
- **實機確認 fix #5（重要副作用）**：detached（獨立 console，避開 `STATUS_CONTROL_C_EXIT`）跑 `verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -AllowBaseline`，log 證 `Feature 'Vulkan' (21) is overridden to 'enabled'` + 選 NVIDIA RTX 3070 Ti + `path=gfxstream-shared-texture fallback=none`。sustained-scroll **`guestFps=48.6 / streamFps=40.5 / renderFps=40.3 / effFps=40.3 / effMin=26.6 / dup=0 / bottleneck=render`**、`result=baseline`、0 residual。→ **先前記錄的「GuestVulkan 一般 UI ~20fps」是被 bug #5 污染**（verifier 沒真的開 `-feature Vulkan`、skiavk 退 SwiftShader 軟體）；Vulkan 真開後一般 UI scroll ~40 effFps（約 2×），瓶頸從 guest 移到 **host Qt render**（本機尚有並行負載 `qemuCpuPctAvg=32.6`）。
- **誠實邊界**：本批改動本身是 robustness / 驗證正確性 / 文件誠實修復，**非** FPS 演算法提升；但 fix #5 讓 GuestVulkan 量測首次反映真實 Vulkan 路徑（~40 而非 ~20）。general-UI 全程 60 仍未達（host render + push cadence），仍 R&D。

## 2026-06-30 — Session 95 — 實際手感卡頓修正：雙擊改最快路徑 + GuestVulkan/skiavk 正式接線

- **使用者回報**：實際雙擊打開仍跟原本一樣卡。根因是 `start-chimera.cmd` 實際呼叫 `start-chimera.ps1` 無旗標 → 走 stock gRPC 慢路徑；GuestVulkan/skiavk 只活在 verifier，未接進正常啟動。
- **修正**：`start-chimera.cmd` 預設 `-Fast -InteractiveFirst`；`start-chimera.ps1 -Fast` 自動設 `CHIMERA_GUEST_VULKAN=1`；`main.cpp` boot completed 後（custom runtime + `-feature Vulkan`）自動 runtime `setprop debug.renderengine.backend=skiavk` + `debug.hwui.renderer=skiavk`，framework restart 一次後再跑 launcher/setup；GuestVulkan 時 Android animations 改回 1/1/1，移除舊軟體路徑「關動畫」造成的跳動感。
- **重要校正**：只設 `debug.hwui.renderer=skiavk` 不重啟 SF 會空畫面；HWUI + SurfaceFlinger 必須一起 skiavk 並 restart。verifier `-GuestVulkan` 會設 `CHIMERA_GUEST_VULKAN_HOST_SETUP=0`，由 verifier 自己管理 restart/re-gate，避免與 host setup 雙 restart race。
- **性能實證**：adb swipe verifier 仍約 `effAvg=20.4`，但 adb swipe 不是實際操作路徑。一次性 host mouse-drag probe（之後禁止再用，因會搶使用者滑鼠）量到 `guestMax=116.7 / render=57.4 / dup=0`，證明 production input + GPU-direct path 有接近 60 headroom。
- **驗證**：`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` PASS：1920×1080、screenshot 246,563 bytes、Settings foreground `interactivity=ok`、0 residual、exit 0；`ctest -LE integration` 20/20 PASS。後續若要再量 host input，必須新增不移動實體游標的 host-internal synthetic touch/test hook。

## 2026-06-30 — Session 94 — roadmap item 2（真實連續渲染重測）+ item 4（GuestVulkan 預設化評估）收尾

- **背景**：roadmap 剩 item 2 / item 4。/goal「完成未完成事項自排序」。item 2 先（便宜、為 item 4 前置證據）。

- **item 2 — gpu-direct path 在真實遊戲級 GPU 負載下的 60 能力（COMPLETE）**：
  - **關鍵架構前提**：custom runtime headless 下 guest GLES 落到 host **SwiftShader（軟體）**（Session 87/88），gl60 能 60 是因 trivial fill + `postFrameDirectGpu` 繞過合成器。直接做「重 GLES fill」量到的是 SwiftShader 填色牆，**非** gpu-direct post path 極限——故設計成顯式量測該牆。
  - **作法（reproducible 探針）**：gl60 app 加 intent extra `heavyIters`（additive、預設 0 不變 light 路徑）→ 1080p 全螢幕 plasma quad、per-pixel N-iter sin/cos loop（ES1.00 常數 loop bound 由 `__ITERS__` splice）。verifier 加 `-HeavyIterations` passthrough（`--ei heavyIters N`）。
  - **量測**：light（trivial fill）`effective min 59.2 / avg 60.0 / dup 0` → 60 ✓；heavy96（96-iter 全螢幕）`effective min 5.8 / avg 6.6 / dup 0`。**同一條 gpu-direct path + host pipeline，只加重 per-frame GLES fragment 成本 → 60→6.6**。dup=0 表示 guest 真的只產出 ~6 unique fps。
  - **結論**：① gl60 的 60 **依賴 trivial fill**（GLES 路徑）；② 瓶頸是 **guest GLES backend = SwiftShader 軟體填色**，非 post path；③ 重遊戲不該走 GLES path，HW 加速重遊戲須走 **guest Vulkan→NVIDIA**（Session 91）。**誠實 scope-cut**：HW Vulkan **連續渲染** 60 仍未量（repo 無 Vulkan game-loop app；寫 native Vulkan 連續渲染 app 超出 item-2「便宜」範圍）。

- **item 4 — GuestVulkan 預設化評估（COMPLETE，決策＝維持 gated）**：
  - **安全性證據**：`ctest -LE integration` 20/20 PASS（無 emulator，flag 不影響）；gl60 + `CHIMERA_GUEST_VULKAN=1` → `min 57.9 / avg 59.7 / dup 0` → `-feature Vulkan` **不回歸** gpu-direct 60；interactive Fast `-GuestVulkan`（當前 DLL）→ `guest_vulkan_hwui_prop=skiavk` active、過 framework restart + scroll **無 MSVCP140 crash**、`guestFps=21.7`（skiagl ~9-18 的 ~2×）、path=gpu-direct、sharedTexture=yes、fallback=none、dup=0、helperSpawns=0、residual=0。→ flag 安全、workload robust。
  - **決策＝維持 gated（不翻預設）**，理由：① `-feature Vulkan` 單獨**無使用者好處**——HWUI-on-Vulkan 需 runtime `debug.hwui.renderer=skiavk` + framework restart（只有 verifier 注入），未接進正常 boot；② 翻預設會影響**每次 boot 含 stock 日常路徑**（`start-chimera` 預設非 `-Fast`），而 skiavk/Vulkan robustness 只在 **custom runtime** 驗過，stock emulator + `-feature Vulkan` 是另一個 backend、未驗；Session 91 刻意 default-off；③ 真正的 GuestVulkan 預設 = 一個 **feature**（enable flag + 把 skiavk runtime props 接進正常 boot + stock/custom 雙路徑 regression），非一行翻 flag。→ `CHIMERA_GUEST_VULKAN=1` 維持 opt-in（已證安全+robust，作為 HW-Vulkan UI 的支援選項）。item 4 **無 code 變更**（評估結論＝翻預設淨負面）。

- **順手修（verifier 契約 bug）**：`verify-interactive-ui.ps1` 的 `-AllowBaseline`/baseline/observe/pass 應 exit 0，但 success path `return` 後 `finally` 的 cleanup native（taskkill/adb，process 已不在時回非零）洩漏 `$LASTEXITCODE` → exit 255（會讓自動化把成功誤判失敗）。修：4 個 documented-success `return` → `exit 0`（實測 `try` 內 `exit` 仍先跑 `finally`，cleanup 不被跳過）；失敗路徑維持 `throw`（nonzero）。run B 的 255 即此 bug，非真錯（residual=0）。

- **md5 對帳**：部署 `build\chimera-gfxstream-runtime\lib64\libgfxstream_backend.dll` md5＝`c81d2092ef0311c83a87756ac525a92b`（與 Session 91 記錄的 `FDF55A3E…` 不同；build 非 byte-deterministic 故 md5 漂移）。此 DLL 通過 manifest buildId gate + gpu-direct 60（light）+ 60-with-Vulkan + guest-Vulkan robustness，**功能完全驗過**。教訓：md5 **不是 gate**，manifest buildId + runtime 60 才是。

- **驗證彙整**：ctest 20/20；gl60 light 60 非回歸；gl60+Vulkan 60 非回歸；heavy96 探針可重現；interactive GuestVulkan robust；每輪 0 residual；verifier 契約修 parse OK + 隔離語義實測。

## 2026-06-30 — Session 93 — stock gRPC `total=0` 真正根因:hardcoded capture port（非 readback 延遲）

- **背景**：roadmap「先修 stock 0-frame，再攻 general-UI 60」。原假設（Session 90）是「1080p readback 延遲 > 無幀 watchdog(2s) → 反覆 restart 拿不到首幀」。
- **實證根因（推翻原假設）**：`verify-true-1080p60.ps1 -GrpcOnly` 失敗 log 顯示 ADB 可見 gate 過（Home 67KB 非黑），但 gRPC capture 全程 `total=0`、`grpc_active_samples<2`、**無** `restarting stream` 行。追 source:`main.cpp:1124` 正確 derive `g_runtimeCfg.grpcPort = 8554 + ((console-5554)/2)*2`（console 5560 → 8560，keyboard wired 8560 log 為證），但 capture 建構（`main.cpp:1518/1523` 的 `GrpcMmapFramebufferCapture`/`GrpcFramebufferCapture`）**硬寫 `8554`**。verifier auto-pick console 5560 → emulator 聽 8560、capture 連 8554 → 幀永遠到不了。與 Session 86 ADB-port hardcode 同類 bug。
- **修法（根因）**：capture 改用 `g_runtimeCfg.grpcPort`。default console 5554 → 8554 不變（零回歸），非預設 port 才修好。
- **次要硬化（負載下真實慢 readback，非 0-frame 根因）**：`GrpcFramebufferCapture` per-request `setTransferTimeout` 從硬綁 `m_stallTimeoutMs`(2s) 解耦成 `m_requestTimeoutMs`(預設 6000，`CHIMERA_GRPC_REQUEST_TIMEOUT_MS` 可調，clamp ≥ stall watchdog)，讓慢 readback 能完成而非 2s 被砍;`main.cpp` 外層 retry timer 盲目 `stop()/start()` 前查 `FramebufferCapture::hasInFlight()`（base virtual 預設 false，gRPC override `!m_replies.isEmpty()`），request 合法在途時不 reset stream。ADB-H264/MMAP 取 base default → restart 行為不變。
- **驗證**：`ctest -LE integration` **20/20 PASS**（含 3 個新 gRPC 守門 test:request timeout 解耦於且 > stall watchdog、env override、env 低於 stall 被拒）；`verify-true-1080p60.ps1 -GrpcOnly` 修前 `fewer than 2 active gRPC samples`（fail）→ 修後 `result=pass`、`grpc_active_samples=9`、`grpc_stream_fps_avg=4.4`、exit 0;`start-chimera.ps1 -Fast -SelfTest` PASS（1920×1080、Launcher→Settings interactivity ok、screenshot 76,175 bytes、residual=0、exit 0）。
- **誠實邊界**：這是 **robustness（非預設 port 不再凍黑）不是 FPS 提升**——stock gRPC 1080p 仍 ~4–17 FPS（量到 4.4）。真正流暢仍需 custom shared-texture path（不經 gRPC、不受此影響）。roadmap 下一步:item 1 general-UI 60 可行性量測 → item 2 真實遊戲重測 → item 4 GuestVulkan 預設化評估。
- **item 1 可行性量測（本輪續做，結論=無 host 槓桿）**：`verify-interactive-ui.ps1 -Mode Fast -GuestVulkan`（HWUI skiavk on NVIDIA 確認）sustained-scroll `guestFps=18.7 / streamFps=14.2 / renderFps=13.8 / effFps=13.6 / dup=0%`、path=gpu-direct、`bottleneck=render`、`qemuCpuPctAvg=12%`。即使 guest Vulkan 把 guest 拉到 ~19 unique fps（vs skiagl ~9），一般 UI scroll 仍是 **push-based SurfaceFlinger cadence ~19fps**，非 CPU/priority/host-pipeline 可調（qemu 才 12%、host 1:1 跟上）。**決策 gate：無 host-side headroom 到 60，天花板是架構性 guest render cadence；general-UI 60 維持 out-of-scope（需 guest 連續渲染 / gfxstream compositor R&D，非本輪可交付）**。host render 比 guest 略低（13.8 vs 18.7）是次要小損，補滿也只到 ~19fps，不值得深做。

## 2026-06-30 — Session 92 — 修 Session 91 code review 抓到的 PostWorker::block hang regression

- **背景**：Session 91 把 gfxstream 的 `std::promise<void>` MSVCP140 crash 改成自訂 `WorkerWaitable`（Lock+CV）。最後一個 review agent 在 session 撞 usage limit 前回報一個未處理的 **HIGH**：`PostWorker::block()`（`host/post_worker.cpp`）在 `m_mainThreadPostingOnly` 時 early-return，舊版靠 `std::promise` 析構的 broken_promise 喚醒 `blockPostWorker()` 的 `scheduledSignal.wait()`（`frame_buffer.cpp:2017`），改成 `WorkerWaitable` 後析構不 auto-complete → 等候端永久 hang。
- **查證（屬實但 Windows 不可達）**：`postOnlyOnMainThread()` 只有 `__APPLE__ && !QEMU_NEXT` 回 true（`frame_buffer.cpp:125-131`），Windows 恆 false；`PostWorkerVk` 顯式 `PostWorker(false,…)`、`PostWorkerGl` 收 `postOnlyOnMainThread()`=false。故 `m_mainThreadPostingOnly` 在 Chimera headless Windows 生產路徑恆 false，hang 屬 **latent / macOS-only regression**，對已部署 DLL（Windows）行為零影響。
- **修法（最小）**：early-return 前補 `scheduledSignal->signal();`（與同函式 task body 既有寫法一致），恢復「工作被跳過也算完成」語義。改 ① source `tmp/aosp-github/.../host/post_worker.cpp`、② **durable**：`scripts/apply-chimera-gfxstream-patch.ps1` 的 `"post_worker block WorkerWaitable implementation"` replacement（fresh tree 才會正確套用）。
- **不重建 DLL**：改動只在 Windows 不可達分支，rebuild 出的 DLL 行為 byte-identical，重跑 gfxstream build + 120s gl60 + ctest 只會重證 Session 91 已驗證狀態。已部署 DLL 維持正確；下次 rebuild 自動帶入此修正並讓 macOS path 也正確。
- **驗證**：patch script `ParseFile` 0 errors；source `block()` 確認含 signal-before-return；ctest 20/20 仍 PASS（Session 91 已驗，未動 Windows 路徑）。lessons.md 加「自訂 signal 取代 promise，每條 early-return 都要顯式 signal」通則。

## 專案目標

Windows Android 模擬器，競品目標是 BlueStacks。純 open-source 元件，無雲端依賴、無廣告、無遙測。

## 最新狀態（2026-06-29 Session 91）— clean 60fps strict PASS 重現 + 競品平滑度研究

- **動機**：使用者要「把 Session 90 沒拿到的 clean 60fps PASS 拿到」，並「參考一堆抄 BlueStacks 的中國模擬器」。
- **clean PASS 重現**：`verify-true-1080p60.ps1 -WarmupSeconds 15 -MeasureSeconds 120` `result=pass`、`effective_fps_min=59.5 / avg=60.0 / dup=0`、`perf_samples=25`、`PWSH_EXIT=0`（detached）。
- **根因（Session 90 微差 floor 的真相）**：不是 GPU-direct path 回歸（同 run `guest=stream=60` solid、`postFrameCpu=0`），是 host 端 ① Qt Quick render thread 被其他視窗遮擋時 occlusion-throttle 到 0；② verifier 每 tick 無條件 `SetWindowPos`+`SetForegroundWindow` 自誘週期性 render hitch，單一 sample 破 floor。
- **修法（`ChimeraVerifyCommon.ps1` `Ensure-HostWindowVisible`）**：改 **HWND_TOPMOST 釘 z-order**（occlusion 靠 z-order 不靠 foreground；背景 process `SetForegroundWindow` 不可靠）+ **idempotent tick**（`GetWindowLongPtr(GWL_EXSTYLE)&WS_EX_TOPMOST` + `IsIconic`，已 topmost 就 return）。量測迴圈刷新 5s→2s。min 從 54.1 → 59.5。
- **harness 教訓**：foreground pwsh 啟動的 chimera-ui 與工具共用 console → Ctrl+C → `STATUS_CONTROL_C_EXIT (-1073741510)` boot 前被殺；emulator-boot verifier 一律 **detached background** 跑。`crashpad_handler` 殘留多是 Corsair iCUE 等第三方、非 Chimera 洩漏。跑前清 `hardware-qemu.ini.lock` stale lock 否則 boot 即 crash。
- **競品研究交付**：`docs/references/competitor-emulator-smoothness.md`。結論：BlueStacks/LDPlayer/MuMu 與 Chimera **同源**（fork QEMU+Android、GPU passthrough、virtio-input 連續輸入）；Chimera **host pipeline 已同級**（`postFrameDirectGpu` 直接 present、postFrameCpu=0）。一般 UI ~20fps 的**唯一落差是 guest 自身繪製跑在 SwiftShader（軟體）**——競品 guest 繪製在實體 GPU。最可行攻堅：**virtio-gpu Venus（guest Vulkan → host Vulkan passthrough）**，host Vulkan 已驗證可用（Session 80），最可能突破 Session 87 的 headless host-GLES 三重死結。
- **Venus/Vulkan 三層實測（探測 `tmp/venus-*.ps1`）**：① **guest Vulkan→實體 NVIDIA 成立**（`-feature Vulkan` → gfxstream `Selecting Vulkan device: NVIDIA GeForce RTX 3070 Ti`，非 SwiftShader）；② SF Vulkan RenderEngine（`debug.renderengine.backend=skiavk`）**穩定但 realistic 效益小**——只能 runtime `setprop`+`ctl.restart surfaceflinger` 生效（emulator `-prop` 設 `androidboot.*` 是 no-op），end-to-end ≈ 持平（guest 23 vs 25），因 app HWUI 才是天花板；③ **app HWUI Vulkan（`debug.hwui.renderer=skiavk`）崩 gfxstream host backend**：`DeviceOpTracker::PollAndProcessGarbage`（`device_op_tracker.cpp:72` use-after-free）。general-UI 60 的牆從「未知深層 R&D」精準定位成「一個有 crash stack 的 gfxstream Vulkan lifetime bug」。`VirtualMachine.cpp` 的 `-prop` gate 已 revert（留 documented comment）。詳見 `docs/references/competitor-emulator-smoothness.md`。
- **Venus 後段 — 修 3 個 MSVCP140 future crash → app HWUI Vulkan 硬體渲染達成 ✅**：所謂的「hang」其實是第三個 crash（raw crash 格式被 grep 漏抓）。根因是 **Session 76 同款 MSVCP140 `_Associated_state` crash**（兩個不相容 MSVCP140.dll；`std::promise::set_value`/被 invoke 的 `packaged_task`/`future` shared-state null-deref `MSVCP140.dll+0x12c10`）。HWUI Vulkan 大量 fence/enqueue 流量驅動而崩。crash stack 逐一指路修三處：① `device_op_tracker.cpp/.h` `shared_future`+`promise::set_value` → `shared_ptr<atomic<bool>>`；② `sync_thread.cpp/.h` `packaged_task<int(WorkerId)>` → `std::function`+stack `Lock+CV`；③ `WorkerThread.h`（threadpool primitive）`Command::mCompletedPromise` `std::promise<void>`+`enqueue()->future<void>` → 共用 `Lock+CV` 的 `WorkerCompletion`，`frame_buffer.cpp` `sendPostWorkerCmd` 邊界用安全 deferred-async 橋接。第三處用 `llvm-symbolizer --obj=<dll> <0x180000000+offset>` 定位。
- **突破結果**：crash 全清、無第四 site，**app HWUI Vulkan 真渲染**（`Pipeline=Skia (Vulkan)`、responsive、非黑）。end-to-end（chimera-ui、host gRPC input、gpu-direct）同 session 對照：**Vulkan HWUI guest=18.4/20.5 vs skiagl guest=9.7**——~2× guest throughput，bottleneck 從 guest（SwiftShader）移到 render（host Qt）。**guest 軟體渲染牆打破**（競品的硬體 UI 渲染做法）。
- **可用**：`VirtualMachine.cpp` `CHIMERA_GUEST_VULKAN=1` gate（只加 `-feature Vulkan`）+ `verify-interactive-ui.ps1 -GuestVulkan`（runtime setprop skiavk SF/HWUI + framework restart）。
- **誠實邊界**：general-UI 全程 60 仍未達成（剩 host render contention + push cadence + 每幀 gfxstream Vulkan marshalling），但「軟體 vs 硬體渲染」這道牆已破，guest 不再是瓶頸。
- **驗證**：final runtime rebuild PASS（verified source commit `d60d3457ac1f1188b5782ccc23bde2c124a7c77b` → SDK build id `15261927`）；**gl60 60fps 非回歸 `min 59.6/avg 60.0` PASS**（含全部 3 crash 修正 + WorkerThread threadpool 改寫）；ctest 20/20 PASS；Fast SelfTest PASS（1920×1080、Settings interactivity、0 residual）；final DLL md5 `FDF55A3EF314262F5BEA76760B9D454B`；3 crash 修正 codified 進 `apply-chimera-gfxstream-patch.ps1`。

## 2026-06-28 Session 90 — 誠實互動量測 + 可設定 priority + path 觀察

- **動機**：使用者回報實際操作仍卡（~1–2 FPS）且干擾背景音樂。Session 85/89 的 60fps 是 GL60 **synthetic 連續渲染**證據，不代表日常互動。本輪建立誠實互動量測、把 priority 變可設定、加 path 觀察 log。
- **新 verifier `scripts/verify-interactive-ui.ps1`**：量測真實互動（Home→Settings→sustained scroll→app switch），分段量 guest/stream/render/dup + bottleneck，分類 display path，並採 emulator priority/CPU%/helperSpawns telemetry。輸出 `CHIMERA_DISPLAY`/`CHIMERA_INT`/`CHIMERA_INT_PRIO`。Stock 永遠 `result=baseline`（不宣稱 60）；Fast 只有 `gpu-direct` 且 sustained eff≥門檻才 `pass-gpu-direct-60`，否則 `fast-ui-visible-not-60`+root-cause。共用 harness 抽到 `scripts/ChimeraVerifyCommon.ps1`，`verify-true-1080p60.ps1` 改 dot-source（純搬移、regression 由 proven verifier 經共用 lib 執行確認）。
- **Fast 一般 UI 實測**：sustained Settings scroll `guestFps=25.7 / streamFps=20.6 / renderFps=20.2 / effFps=20.2 / dup=0`，path=**gpu-direct**（`postFrameDirectGpu`×7、`postFrameCpu`=0）。`guest≈stream≈render`+dup=0 → host pipeline 1:1 跟上；瓶頸是 **guest SurfaceFlinger render cadence**（push-based ~20 unique fps）+ cold-launch hitch（早期 `maxMs` 達 8.4s、steady ~400ms）。一般 UI 60 仍需 gfxstream compositor R&D（out of scope，使用者已確認範圍）。
- **priority/cpus/ram 可設定**：`main.cpp` 移除硬寫（cpus/ram 本來就被 normalizer floor 回 4/4096，只有 priority 真正生效），改 env→default resolver（`CHIMERA_INTERACTIVE_PRIORITY/CPUS/RAM_MB`，預設等於原生效值）。`start-chimera.ps1` 加 `-AudioFirst`(idle)/`-InteractiveFirst`(normal) sugar。新增 `CHIMERA_DISPLAY path=… sharedTexture=… fallback=… priority=… cpus=… ramMB=…` 一行 qInfo 觀察 log（已在 live log 證實）。
- **audio churn 量測結論**：healthy 互動 run `helperSpawns=0`、`qemuCpuPctAvg≈22%`、priority ramp `BelowNormal,Idle`。boot-probe `guestPerfTimer` boot_completed 即停、retry timer 首幀即停 → steady-state churn 已 ~0，**不需** churn 改動；audio 調節桿是 priority + 1080p readback。
- **stock gRPC 0 幀（既有、負載敏感）**：`-Mode Stock` 與 proven `-GrpcOnly`（無 telemetry）皆 `total=0` 整輪 0 幀；1080p `getScreenshot` 延遲 > 無幀 watchdog 反覆 restart 拿不到首幀。證明非本次改動造成；custom shared-texture path 不經 gRPC、不受影響。標為 OPEN（既有）。
- **驗證**：`ctest` 20/20 PASS；`start-chimera.ps1 -Fast -SelfTest` PASS（1920×1080、Launcher+Settings 互動、screenshot 75,673 bytes、0 residual）；`verify-interactive-ui.ps1 -Mode Fast` 完整跑通並誠實 gating；每輪 0 residual。
- **extraction 非回歸（gl60 regression）**：`verify-true-1080p60.ps1 -MeasureSeconds 60` 經共用 lib 完整跑通（build/boot/gate/perf），`guest=stream=60.0` solid、`postFrameDirectGpu=49`、`GL readback=0` → producer+pipeline 60 完好；host Qt render 在並行負載（active Claude session）下抖到 54–57，`min=56.2 / avg≈58.8` 微差 strict floor 57/59。屬 host-render jitter，非 extraction 回歸（安靜機器 Session 89 為 min 59.9）。**未在本輪重跑出 clean 60fps PASS**，但 60fps 路徑與 verifier 結構均證實完好。
- **誠實邊界**：交付的是「誠實量測 + 可設定 priority + 降 churn 結論 + path 觀察」，**非** general-UI 60（後者需深層 gfxstream compositor R&D）。

## 歷史 Sessions 64–89（精簡 changelog；完整見 git log / 早期版本 CONTEXT）

- **89 (06-28)** — 嚴格可見 120s 1080p/60 PASS（`min 59.9/avg 60.0/dup 0`）；verifier 自動挑 free port pair、post-warmup `effective<=0` 直接 fail、量測期間釘 host window 前景。
- **88 (06-26)** — custom runtime 一般 UI 黑屏修復：`CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1` 關閉 core-profile shader emission（renderer identity / direct-VK path 不變）；ANGLE/D3D11 可 init 但 draw AV，不作正式路徑。
- **87 (06-24)** — host GLES 硬體路由 BLOCKED：headless SwiftShader host GLES + renderer=HOST → `#version 330 core` → ES 拒 → 黑；native WGL/CLI angle_indirect/DLL-內 ANGLE 三條 headless 全擋（init hang）。gl60 因 `postFrameDirectGpu` 繞過合成器不受影響。
- **83-84 (06-22)** — D3D11 DXGI shared handle fix（`CreateSharedHandle` 改 `DXGI_SHARED_RESOURCE_READ|WRITE`，解 `E_INVALIDARG`）+ bridge namespace 對齊。
- **82 (06-19)** — shmem consumer 吞吐 ceiling ~50fps；`BELOW_NORMAL` 移除 EcoQoS（triangle demo 7→24fps、Settings scroll 16→25fps）。
- **81 (06-19)** — shmem delivery 路徑確認（非 GuestVulkanOnly 開機 + shmem PASS）。
- **80 (06-18)** — NVIDIA Vulkan loader 調查收斂：失敗根因是測試 harness `-gpu swiftshader_indirect` 污染（`emugl_config` 強制 `ANDROID_EMU_VK_ICD=swiftshader`），改 `-gpu host` 後 NVIDIA instance/device OK。
- **78-79 (06-17)** — 音訊啟用（移除 `virtio-snd-pci`）+ gRPC display 解鎖 + GrpcOnly verifier 修正；AdbH264 screenrecord headless 死路確認、priority 改 `idle`。
- **76-77 (06-16~17)** — `std::promise` MSVCP140 ABI crash 改 `Lock+CV` → Android headless boot CONFIRMED；CPU readback → Win32 shmem 管道打通，Android 畫面首次送達 host。
- **71-75 (06-13~14)** — gfxstream bridge diagnostics + 1080p floor；proxy probe/analyzer；`initLibrary` ABI fix；GrpcOnly verify mode + ABI 不相容實測；Vulkan backend 確認（headless 不 present → proxy frame capture 死路）。
- **64-70 (06-06~13)** — RenderLib/proxy probe；bad-runtime gate + VM hardening；單視窗 + 低干擾 fallback；可見原生 Emulator 雙 gate；strict shared-texture fail-closed；headless runtime 邊界 + snapshot I/O；stale port cleanup + native embed 休眠。

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

## 早期歷程（Phase 1–8 / Session 3–63；精簡摘要，完整見 git log）

- **Phase 1–4（~2026-05）** — 核心 MVP：Qt6 host shell、ConfigManager、InputBridge（gRPC/Console/QMP/ADB）、CoordinateMapper、WASAPI audio、VM lifecycle、單元測試骨架。
- **Phase 5–7（05-17）** — Hyper-V/HCS native stack 與 Cuttlefish R&D（legacy，`--qemu-backend`/`--hcs-backend` 保留不刪）。
- **BlueStacks Parity Roadmap v3 P0–P4e（05-18~19）** — emulator.exe（QEMU+WHPX）路徑達 BlueStacks 同等功能（見 CLAUDE.md「BlueStacks Parity 功能清單」35+ 項全 ✅）。Session 3–8 補強：多點觸控、IME、gamepad、macro、device spoof、clipboard、file share、proxy、網速、感應器、震動、電池等。
- **Session 9–26（05-19~25）** — 顯示路徑改 gRPC streaming（取代 native embed）；gRPC 60fps 穩定 + orphan qemu 根因（Job Object kill-on-close）；Quick Boot snapshot path/fallback/verifier；1080p guest + clickable gRPC touch；truthful FPS（effective=min(guest,stream,render)）；Chimera Launcher（`com.chimera.launcher`）；black screen fix；host audio stutter mitigation；wheel/input jank 改 gRPC swipe；shared memory / D3D11 shared texture renderer 雛形。
- **Session 27–63** — 見上方 changelog 與 git log（shared texture producer、gfxstream ABI gate、1080p floor 強制、host audio isolation、custom runtime artifact gate 等迭代）。

> 重要 bug 修正的「當前狀態」以 CLAUDE.md「已知問題」表為準；本段僅作歷程索引。
