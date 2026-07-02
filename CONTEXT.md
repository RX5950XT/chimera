# Project Chimera — CONTEXT.md

> 開發歷程記錄。供下一個 Agent 快速接手用，不需要從 git log 重建脈絡。當前狀態以 `CLAUDE.md` 為準；本檔是歷程與根因記錄。

## 專案目標

Windows Android 模擬器，競品目標是 BlueStacks。純 open-source 元件，無雲端依賴、無廣告、無遙測。
**引擎決策（最重要）**：生產引擎 = `emulator.exe`（Google QEMU+WHPX fork）；`--qemu-backend`（stock QEMU 11 + Cuttlefish）與 `--hcs-backend`（Hyper-V HCS）= legacy R&D，保留不刪。BlueStacks 輸入路徑更正：`BstkDrv.sys` 是 network/filter driver 非 input driver；BlueStacks 走 `HD-Bridge-Native.dll` → virtio-input，Chimera 等效路徑是 emulator gRPC + Console `event` protocol。

## 2026-07-02 — Session 102 — 畫面糊根因修復（Nearest 縮小取樣）

- **使用者回報**（S101 修復後）：性能顯著改善但不穩 60fps、畫面糊（「1080p 會這麼糊嗎」）。
- **根因（糊）**：producer 端 texture 實證 1920×1080 無誤（log `size 1920 1080 format 28`）；糊在 host 呈現端——預設視窗 1480×860 扣側欄/工具列後 GuestDisplay item 只有約 0.65×，1080p texture 縮小顯示時 filtering 是 **Nearest**（整行整列丟像素→文字筆畫殘缺）。**陷阱**:`QSGSimpleTextureNode` 的 material filtering 預設 Nearest 且 render 時會覆寫 per-texture `setFiltering()`——原代碼三處 `texture->setFiltering()` 全是 no-op，必須設在 node 上。
- **修復**（`GuestDisplay.cpp`，commit `a80dcee`）：node 建立時 `setFiltering(QSGTexture::Linear)`（1:1 對齊時 Linear 取樣 texel 中心＝Nearest，無損）；三處 `setRect` 改經 `snapRectToDevicePixels()`（letterbox 置中的小數座標對齊 device-pixel 格，消除 1:1 時半像素模糊）。
- **驗證**：build + ctest 23/23 + `-Fast -InteractiveFirst -SelfTest` PASS（1920×1080、`host_window_nonblack_pct=100`、interactivity ok、0 residual）；host 視窗截圖對比修復前——文字/圖示邊緣平滑完整。
- **殘餘限制（誠實）**：縮小顯示本質上會損失細節，Linear 只是把「殘缺」變「柔和」；要完全銳利需視窗 ≥1:1 顯示（全螢幕於 ≥1080p 內容區）或未來做 guest 解析度跟隨視窗。60fps 不穩＝已知 GLES 同步成本邊界（見 S101），非回歸。

## 2026-07-02 — Session 101 — `-Fast` host 視窗黑屏三層根因全修 + emulator idle 自殺修復

- **使用者回報**（Session 100 修復後仍黑）：啟動等很久還是黑畫面、有效 FPS 0。取證：guest ADB screencap 75KB 正常、producer `published sequence` 跳動、host `total` 幀數增長、host 視窗 PrintWindow 中心像素 (2,5,4)=純黑——**counters 全綠、像素全黑**。且 AVD 檔案時間軸顯示使用者那次 run 的 emulator 在 boot 後 ~4 分鐘死亡。
- **Bug B（先破案）：emulator idle 自殺**——`VirtualMachine.cpp` 對 emulator 傳 `-idle-grpc-timeout 300`（qemu binary strings 定位到 `IdleInterceptor.cpp`、log `Idled to long, shutting down`）。`-Fast` shared-texture 顯示**不走 gRPC**，黑屏下使用者無從輸入 → 300s 零 gRPC 流量 → emulator 優雅自殺 →「等多久都黑」。stock 路徑 getScreenshot 輪詢永不 idle、verifier 全程注入 input，所以 15 session 沒踩到。修：移除旗標（ProcessLauncher 的 kill-on-close Job Object 已負責 orphan 清理）+ `grpcEnabledNeverRequestsIdleShutdown` unit test。
- **Bug A（主）：shared texture 從 Session 85 起發佈的一直是零幀**。三層獨立 bug 疊加，用三支 probe 分層定案（缺一不可，只修一層整條仍黑會誤判）：
  1. **GL→VK 內容同步缺失（最深層）**：SurfaceFlinger→HWC→`PostCmd::Compose` 由 host GL（CompositorGl）合成 target ColorBuffer，**從不標 `mGlTexDirty`**（只有 `rcFlushWindowColorBuffer`/eglSwapBuffers 路徑會標）→ `invalidateForVk()` 恆 `exit=clean` no-op → `borrowForDisplay(kVk)` 借到的 VK sibling image **從未被寫入** → `postFrameDirectGpu` blit 複製全零。bridge 內新診斷 `debugReadbackSharedImage`（VK 端讀回 mImage 中線）實證：修前 `nonzero=0/120`、修後 `nonzero=120/120 center=245,245,245,255`。修：headless post 分支 borrow 前 `colorBuffer->flushFromGl(); colorBuffer->invalidateForVk();`——兩者對 VK-backed／GL-VK 共享記憶體內容都是 no-op，真 Vulkan 內容保持 zero-copy；GLES 合成內容誠實付出每幀 GL readback（SwiftShader CPU）+ VK upload。
  2. **Vulkan import 無 aliasing**：bridge 把 `IDXGIResource1::CreateSharedHandle` 的 D3D11 NT handle 用 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT` 匯入且無 dedicated info——spec 違規，NVIDIA 全回 VK_SUCCESS 但寫入落在 driver 私有記憶體。獨立 probe `vkinterop.exe`（窮舉匯入型別×keyed-mutex×跨 process 矩陣）5 分鐘定案：OPAQUE=寫入丟失、`D3D11_TEXTURE_BIT`+`VkMemoryDedicatedAllocateInfo`=可見；NTHANDLE 不配 KEYEDMUTEX 根本建不出 texture（E_INVALIDARG）。修：bridge template 改正確 import + `vkGetMemoryWin32HandlePropertiesKHR` 診斷。
  3. **Consumer 缺 AcquireSync**：keyed-mutex texture（misc 0x900）跨 process 不 acquire 讀=零（`texprobe.exe` 矩陣實證：no-acquire=0、acquired=內容）。Qt QSG 無法跨 render pass 持鎖 → `GuestDisplay` 每個新 sequence `AcquireSync(0,4ms)`→`CopyResource` 到私有 texture→`ReleaseSync(0)`，QSG 取樣私有副本。**陷阱**：`AcquireSync` 回 `WAIT_TIMEOUT (0x102)` 能通過 `SUCCEEDED()`，必須 `== S_OK`。
- **Gate 漏洞修補（為何潛伏 15 session）**：歷來所有「可見/60fps」gate 都驗 guest 端 ADB screencap（走 SurfaceFlinger、不經 bridge）+ host 端 counters（sequence/FPS，零幀也照跳）。新增 `ChimeraVerifyCommon.ps1::Get-HostWindowPixelStats`（PrintWindow `PW_RENDERFULLCONTENT` 抓 D3D swapchain + 中央顯示區取樣，避開恆亮側欄）並接入 `start-chimera.ps1 -SelfTest`（gate `host_window_nonblack_pct>=5`，30s retry）。
- **patch script drift**：modern tree（`frame_buffer.cpp` 小寫）的 headless 段被前 session 直接手改（加 timing 碼），patch script 對應段（`$hasLegacyGlDisplay` guard 內）從此死碼、靜默跳過；改 script 完全無效卻印「Applied」。本輪 tree+script 同步修（modern 段用 `Replace-Text` 含 already-applied check），教訓入 lessons.md：改 patch script 後必 grep tree 確認落地。
- **驗證**：ctest 23/23 PASS；`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` **PASS**（`screenshot_nonblack=100%`、**`host_window_nonblack_pct=100.0`（新 gate）**、`interactivity=ok`、0 residual）；host 視窗實截（PrintWindow）顯示 Settings+IME 鍵盤、側欄有效 FPS 43——使用者可見的真畫面。
- **誠實邊界**：① 歷史 GPU-direct「60fps」（S85/89/99 的 gl60/interactive 數字）量的是零幀 blit 的節奏，修復後 GLES 內容每幀多付 SwiftShader GL readback+VK upload，真實數字需重新基準（本輪互動實測 43 eff FPS）。② `boot_seconds` 本輪 87s vs S100 33s——當日連續 6+ 次 boot/build 的環境噪音成分大，空機重測再定論。③ 真 Vulkan-backed 內容（Vulkan 遊戲）仍走 zero-copy 直通路徑不受同步成本影響，但其可見性尚未單獨驗證。
- **診斷留存**（此類 bug 的直接偵測器，皆低頻）：bridge `debugReadbackSharedImage`（每 240 幀 VK 端讀回+log）、`invalidateForVk` exit-path log（每 600 次）、init 時 `D3D11 handle props` log。probe 工具原始碼在 session scratchpad（`texprobe.cpp`/`vkinterop.cpp`），需要時可重編。

## 2026-07-02 — Session 100 — `-Fast` 啟動黑屏根因修復（skiavk 半套用）+ 載入加速

- **使用者回報**：雙擊啟動後中間模擬畫面全黑（側邊 UI 正常）、載入慢。
- **根因（多重實證）**：Session 95 加入的 host 端 skiavk 切換（boot 後 `setprop debug.hwui.renderer=skiavk` + `stop`/`start`）在此 `google_apis_playstore` **user image**（`ro.debuggable=0`）上 `stop`/`start` 回 **"Must be root"** 被 `runAdbShell` 靜默吞掉 → framework 從未 restart、SF 永遠 SkiaGL；但 `debug.hwui.renderer=skiavk` **有**寫入 → setprop 之後才首次 HWUI init 的 process（launcher 被 `pm install -r` 重啟）走 Vulkan → host 上 NVIDIA VK surface 無法被 SwiftShader-ES 合成取樣 → **app 視窗全黑**；pre-setprop 的 SystemUI 可見 → 「狀態列有、中間黑」。可見與否取決於 process 啟動時序 → 過去被誤判為「偶發 black-boot」。
- **skiavk 不可行定案（三路 probe 實證全死，別再試）**：① root restart（`stop`→"Must be root"）；② boot-time prop（`-systemui-renderer skiavk` 只翻譯 hwui prop，init **不翻譯** `ro.boot.debug.renderengine.backend` → SF 留 SkiaGL；HWUI-only Vulkan 實證全黑）；③ `setprop ctl.restart surfaceflinger` → **SELinux denied**（`scontext=u:r:shell:s0 permissive=0`、SF pid 不變）。歷史「GuestVulkan 對照 ~2×/hwui=skiavk active」數字全是採樣 pre-setprop GLES process 或 getprop 回顯，不可引用。
- **修復**：① `main.cpp` 移除 `applyGuestVulkanHardwareUi()` + poller skiavk apply/12s grace；`CHIMERA_GUEST_VULKAN=1` 現在**只**代表 `-feature Vulkan`。② launcher 安裝 md5 比對跳過重複 reinstall/force-stop/relaunch（省 ~5-8s + 消除閃爍）。③ `QmlAndroidControls.bootReady` + QML `guestReady`：placeholder 保持到 boot 完成（不再第一張黑幀就消失）。④ SelfTest 補 home screenshot **內容 gate**（bytes≥20000/nonblack≥10%/spread≥40）——舊 SelfTest 只驗 dumpsys focus，黑屏假 PASS 多個 session。⑤ verify-interactive-ui 移除 skiavk 虛構流程。⑥ `Invoke-CheckedTool` EAP=Continue（javac stderr Note 假失敗）。
- **第二個真 bug：背景手把輸入漏進 guest**（60fps 驗證時抓到）：使用者在主螢幕玩 P5R（手把），`GamepadManager` XInput（system-wide）60Hz 輪詢無條件轉發 → 手把 B/HOME 打進 Android 把前景 app 切到背景 →「guest 隨機停止渲染」。sidecar 凍結瞬間取證（guest 前景=launcher、gl60 process 活、螢幕 Awake）。修：`gp.poll()` 前 `QGuiApplication::applicationState()==ApplicationActive`（BlueStacks 同款 focus-gate）。
- **驗證環境改進**：chimera-ui 啟動即讀 `CHIMERA_VERIFY_WINDOW_ORIGIN`（"x,y"，負值=副螢幕）setPosition——測試視窗從第一幀就在副螢幕，不再蓋住使用者畫面被關掉；`Ensure-HostWindowVisible` 對 exited process 防禦；env 已存 User scope `-2520,45`。
- **驗證**：build PASS；ctest 23/23；`-Fast -InteractiveFirst -SelfTest` PASS（`boot_seconds=33`、`visible_home_seconds=49`、screenshot 76,219B/nonblack 100%/spread 716、interactivity=ok、0 residual）。載入從舊 ~80–110s → **~40-49s 可見 home**，全程 placeholder。60fps：與 AAA 遊戲同跑量到 gl60 窗內連續 150s 60.0（min 59.5/dup 0）、互動 scroll eff 49.1/dup 0；嚴格 gate 被遊戲負載尖峰打穿屬預期（**注意：此處 gl60 數字量測於 S101 修復前，屬零幀節奏，見 S101 更正**）。

## 2026-07-01 — Session 99 — sibling-grep 補洞 + general-UI 60 host 根因（⚠ 60 數字被 S101 重新定性）

- **sibling-grep 複查**（lessons 規則：修過一類 bug 要 grep 全 repo sibling）：C++ production host 5 類已修 bug class（硬寫 port、`CreateSharedHandle` flag、`waitForExit` double-close、env `stoi/atoi`、tap press/release pairing）全乾淨；修 2 個 PowerShell harness sibling（`run-proxy-smoke-test.ps1` 全域 kill → cmdline-filtered；`verify-quick-boot.ps1` 刪 shadow re-def）。
- **實修 Session 98 deferred 的 2 個**：① `AudioBridge.cpp` WASAPI forced-format bug 比原判嚴重——`GetMixFormat` 回 `WAVE_FORMAT_EXTENSIBLE`(cbSize=22)、改 `wFormatTag=IEEE_FLOAT` 沒重設 `cbSize` → shared-mode `Initialize` 對任何 rate 都失敗；修 `cbSize=0`+`AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|SRC_DEFAULT_QUALITY`，新測實機 RED(QSKIP 被 bug 偽裝)→GREEN。② gfxstream `mStagingBuffer` 死分支 grep 靜態證明後移除（member/destroy/reset）。
- **量測方法修正**：過去「general-UI ~20fps」大半是 **adb-swipe 量測污染**（GuestVulkan scroll 量到 `guestFps=0.0 dup=0.0`＝完全沒驅動重繪）。新增 `CHIMERA_SYNTHETIC_SCROLL`（diagnostics-only）走 production 輸入路徑 `InputBridge::onTouchPoint→gRPC sendTouch` 連續 fling，不碰實體滑鼠；`verify-interactive-ui.ps1 -SyntheticScroll`。
- **兩個 host-only 修（零 DLL 重建，仍有效保留）**：① `QSG_RENDER_LOOP=threaded`（main.cpp）解 host→guest backpressure；② GuestDisplay present timer 16ms→200ms 解 GUI-thread contention（present timer 對 shared-texture path 冗餘，event-driven 已 per-frame 驅動）。
- **⚠ Session 101 重新定性**：本輪 strict gate `pass-gpu-direct-60`（effFps 59.6）與 gl60 非回歸數字，量測時 shared texture 發佈的是零幀（host 視窗黑），60 是零幀 blit 節奏，**不可引用**；但「瓶頸在 host 端」的兩個修法本身有效（S101 修復後互動 43 eff FPS 含此二修）。
- **驗證**：完整 Release build PASS；ctest 23/23 PASS（test-audio-bridge 含新 GREEN）；0 residual。

## 歷史 Sessions 90–98（精簡 changelog；詳見 git log 與 tasks/todo.md）

- **98 (07-01)** — audit-driven 8 維 workflow → 對抗式 verify → TDD 修 8 個 concrete bug：`--adb-display-fallback` 硬寫 ADB 5555、`FileUtils::ensureDir` 已存在回 false、MacroEngine Tap 缺 release、QemuBackend double-`CloseHandle`、proxy `CreateSharedHandle(GENERIC_ALL)`、VirtualMachine exit-monitor handle leak/race（`InterlockedExchangePointer` 原子 claim）、bridge vkBindImageMemory 失敗漏 CloseHandle、LocationSimulator explicit teleport 被 throttle 丟。新增 3 test exe（20→23）。
- **97 (06-30)** — harness port/cleanup 修：`Resolve-EmulatorConsolePort`（0=auto free pair；非 0 必須 even）；`start-chimera.ps1 -SelfTest`/`verify-hardware-ui.ps1` 改共用 cmdline-filtered cleanup，不再全域殺 emulator/qemu。
- **96 (06-30)** — 14-agent 審查修 7 個 P2（`-RequireSharedTexture` fail-closed throw、ConsolePort 限 even、README 誠實化、verify-quick-boot 共用 harness、`-GuestVulkan` 補設 env〔先前假對照〕、cmdline-filtered kill、AGENTS.md VT char）；對抗式 verify 否證 3 假陽性。修 #5 後 GuestVulkan scroll 實測 effFps 40.3（先前 ~20 是污染數字）。
- **95 (06-30)** — 雙擊改最快路徑：`start-chimera.cmd` 預設 `-Fast -InteractiveFirst`；GuestVulkan/skiavk 接進正常 boot（**此 skiavk 接線即 S100 黑屏根因，已移除**）。禁用會搶實體滑鼠的 host mouse-drag 測法。
- **94 (06-30)** — gl60 `-HeavyIterations` GLES fill 探針：light 60 vs heavy96 6.6（dup=0）→ 重 GLES 瓶頸是 guest GLES=host SwiftShader 軟體填色，非 post path；HW 重遊戲須走 guest Vulkan→NVIDIA。GuestVulkan 預設化評估＝維持 gated。verifier success path `return`→`exit 0`（`$LASTEXITCODE` 洩漏）。md5 非 gate（build 非 byte-deterministic）。
- **93 (06-30)** — stock gRPC `total=0` 真根因＝capture 硬寫 gRPC port 8554（須用 derived `g_runtimeCfg.grpcPort`；推翻 S90「readback 延遲>watchdog」歸因）；次要硬化 `CHIMERA_GRPC_REQUEST_TIMEOUT_MS` 解耦 + `hasInFlight()` gate；GrpcOnly 修後 pass（4.4 FPS，robustness 非 FPS）。
- **92 (06-29)** — `PostWorker::block()` early-return 補 `scheduledSignal->signal()`（macOS-only latent；Windows DLL byte-identical，不重建）。
- **91 (06-29)** — clean gl60 strict PASS 重現（根因＝host 視窗 occlusion-throttle + verifier 自誘 hitch → `Ensure-HostWindowVisible` 改 HWND_TOPMOST + idempotent tick）；競品研究 `docs/references/competitor-emulator-smoothness.md`。後段修 3 個 MSVCP140 future crash（`device_op_tracker`/`sync_thread`/`WorkerThread.h`→`Lock+CV`）→ app HWUI Vulkan 可渲染不崩（**skiavk production 接線部分被 S100 否定**；`-feature Vulkan` 本身仍有效）。
- **90 (06-28)** — 誠實互動量測：新增 `verify-interactive-ui.ps1` + `ChimeraVerifyCommon.ps1` 共用 harness；priority/cpus/ram 改 env resolver（`CHIMERA_INTERACTIVE_*`）+ `-AudioFirst`/`-InteractiveFirst` sugar + `CHIMERA_DISPLAY` 觀察 log；audio churn 量測結論 helperSpawns=0（不需改動）；量測工具改低干擾 telemetry。

## 歷史 Sessions 64–89（精簡 changelog）

- **89 (06-28)** — 「嚴格可見 120s 1080p/60 PASS」（**S101 重新定性：零幀 blit 節奏**）；verifier 自動挑 free port pair、post-warmup `effective<=0` 直接 fail、量測期間釘 host window；ColorBuffer log storm 節流。
- **88 (06-26)** — custom runtime 一般 UI 黑屏修復：`CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1` 關閉 core-profile shader emission；ANGLE 可 init 但 draw AV，不作正式路徑。
- **87 (06-24)** — host GLES 硬體路由 BLOCKED：headless SwiftShader + renderer=HOST → `#version 330 core` → ES 拒 → 黑；native WGL/CLI angle_indirect/DLL 內 ANGLE 三條 headless 全擋。
- **85–86 (06-22~23)** — gl60 連續渲染 verifier 首次 PASS + `postFrameDirectGpu` direct GPU path（**S101 重新定性：發佈零幀**）；main.cpp 修 hardcoded runtime ports；一鍵啟動器 `start-chimera.cmd`。
- **83-84 (06-22)** — `CreateSharedHandle` `GENERIC_ALL`→`DXGI_SHARED_RESOURCE_READ|WRITE`（解 E_INVALIDARG）+ bridge namespace 對齊。
- **82 (06-19)** — shmem consumer 吞吐 ceiling ~50fps；`BELOW_NORMAL` 移除 EcoQoS（triangle demo 7→24fps）。
- **81 (06-19)** — shmem delivery 路徑確認（NVIDIA VkEmulation + `chimeraPublishFrameToShmem` 送達 host）。
- **80 (06-18)** — NVIDIA Vulkan loader 調查收斂：失敗根因是 harness `-gpu swiftshader_indirect` 污染（強制 `ANDROID_EMU_VK_ICD=swiftshader`），改 `-gpu host` 後 NVIDIA instance/device OK。
- **76-79 (06-16~17)** — `std::promise` MSVCP140 ABI crash 改 `Lock+CV` → headless boot CONFIRMED；shmem 管道打通（Android 畫面首次送達 host）；音訊啟用（移除 `virtio-snd-pci`）；gRPC display 解鎖 + GrpcOnly verifier；AdbH264 screenrecord headless 死路確認。
- **70-75 (06-13~14)** — stale port cleanup；gfxstream bridge diagnostics + 1080p floor；proxy probe/analyzer + `initLibrary` ABI fix；GrpcOnly mode；ABI 不相容實測（`sdk-release` 13278158 vs SDK 15261927 → AV）；stock headless 純 Vulkan、proxy frame capture 死路定案。
- **64-69 (06-06~13)** — RenderLib/proxy probe；bad-runtime gate + VM hardening；單視窗 + 低干擾 fallback；可見原生 Emulator 雙 gate；strict shared-texture fail-closed；headless runtime 邊界 + snapshot I/O 政策。

## 早期歷程（Phase 1–8 / Session 3–63；完整見 git log）

- **Phase 1–4（~2026-05）** — 核心 MVP：Qt6 host shell、ConfigManager、InputBridge（gRPC/Console/QMP/ADB）、CoordinateMapper（`gx = nx * (guestW-1)`；macro 記 normalized events）、WASAPI audio、VM lifecycle（ProcessLauncher `CreateProcessW` + `quoteArg` round-trip）、GamepadManager、MacroEngine、DeviceSpoofer、MemoryTrimmer、ScreenRecorder、ANGLE 動態載入、單元測試骨架。
- **Phase 5–7（05-17）** — Hyper-V/HCS native stack 與 Cuttlefish R&D（legacy）。遺留 OPEN（不影響生產路徑）：`--cuttlefish` SurfaceFlinger crash-loop（gralloc 需 gfxstream cap set 3）與 ADB TCP blocked。
- **BlueStacks Parity Roadmap v3 P0–P4e（05-18~19）** — AndroidConsoleInput 狀態機、InstanceRuntimeConfig（index-based port allocation，移除 `emulator-5554` hardcode）、CoordinateMapper pipeline、ProcessLauncher rewrite、LocationSimulator（geo fix 順序 **lon lat alt**）、ClipboardBridge CF_UNICODETEXT、SharedFolder ADR、integration tests、multi-instance grid、visible latency 量測、APK 安裝/Settings 面板/Root mode/rotation。
- **Session 9–26（05-19~25）** — 顯示改 gRPC streaming（取代 native embed）；orphan qemu 根因（Job Object kill-on-close）；Quick Boot snapshot 政策；1080p floor；truthful FPS（effective=min(guest,stream,render)）；Chimera Launcher（`com.chimera.launcher`）；host audio stutter mitigation；wheel 改 gRPC swipe；shared memory / D3D11 shared texture 雛形。
- **Session 27–63** — shared texture producer、gfxstream ABI gate、host audio isolation、custom runtime artifact gate 等迭代（見上方 changelog 與 git log）。
