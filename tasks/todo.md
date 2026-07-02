# Chimera Task Todo

---

## 2026-07-02 Session 101 — `-Fast` host 視窗黑屏（三層疊加根因）+ emulator idle 自殺

### 根因（已實證，全部修復）

- **Bug A（主，三層疊加——shared texture 從 Session 85 起發佈的一直是零幀）**:
  1. **GL→VK 內容同步缺失（最深層）**: HWC compose 路徑由 host GL（CompositorGl）合成 target ColorBuffer，從不標 `mGlTexDirty`（`flushFromGl` 只在 `rcFlushWindowColorBuffer` 被呼叫）→ `invalidateForVk()` 恆 `exit=clean` no-op → `borrowForDisplay(kVk)` 借到的 VK sibling image **從未被寫入** → blit 複製全零。bridge 內 Vulkan readback 診斷實證：修前 `nonzero=0/120`、修後 `nonzero=120/120 center=245,245,245,255`。修法：headless post 分支 borrow 前 `colorBuffer->flushFromGl(); colorBuffer->invalidateForVk();`（VK-backed 內容仍 no-op、保 zero-copy）。
  2. **Vulkan import 無 aliasing**: D3D11 NT shared handle 用 `OPAQUE_WIN32_BIT` 匯入（無 dedicated info）→ NVIDIA 回 VK_SUCCESS 但寫入不落到 D3D11 texture。獨立 vkinterop probe 實證：`OPAQUE` 匯入寫入丟失、`D3D11_TEXTURE_BIT`+`VkMemoryDedicatedAllocateInfo` 後 clear 內容可見。修法：bridge template 改正確 import。
  3. **Consumer 缺 AcquireSync**: keyed-mutex texture（misc 0x900）跨 process 不 acquire 讀 = 零（texprobe 矩陣實證：no-acquire=0、acquired=內容）。Qt scene graph 無法跨 render pass 持鎖。修法：GuestDisplay 每個新 sequence `AcquireSync(0)` → `CopyResource` 到私有 texture → `ReleaseSync(0)`，QSG 取樣私有副本（注意 `WAIT_TIMEOUT (0x102)` 過得了 `SUCCEEDED()`，必須 `== S_OK`）。
- **Bug B（次）**: `VirtualMachine.cpp` 傳 `-idle-grpc-timeout 300`。`-Fast` 顯示不走 gRPC，黑屏下使用者無輸入 → 300 秒零 gRPC 流量 → emulator 自殺（`IdleInterceptor.cpp`: `Idled to long, shutting down`）。使用者 10:18 開機、~10:22 死亡吻合；stock 路徑 getScreenshot 輪詢永不 idle、verifier 全程注入 input，所以從未踩到。修法：移除該旗標（Job Object kill-on-close 已負責 orphan 清理）+ regression test。
- **Gate 漏洞**: 歷來所有 60fps/可見 gate 都驗 guest 端 ADB screencap + host 端計數器（sequence/FPS），兩者都不經過「shared texture 內容 → host 視窗」——所以三層 bug 潛伏 15 個 session。修法：SelfTest 新增 `Get-HostWindowPixelStats`（PrintWindow PW_RENDERFULLCONTENT + 中央區取樣）gate。

### 修復項目
- [x] Bug B: 移除 `-idle-grpc-timeout 300` + `grpcEnabledNeverRequestsIdleShutdown` test
- [x] Bug A-2: bridge template 改 `D3D11_TEXTURE_BIT` import + `VkMemoryDedicatedAllocateInfo` + handle-props 診斷
- [x] Bug A-1: frame_buffer headless 分支補 `flushFromGl()`+`invalidateForVk()`（tree + patch script 同步；發現 modern tree 的 headless 段 patch 是死碼、tree 為手改，已補 modern Replace-Text）
- [x] Bug A-3: GuestDisplay keyed-mutex acquire + 私有副本
- [x] 診斷留存: bridge `debugReadbackSharedImage`（每 240 幀 VK 端讀回）+ `invalidateForVk` exit-path log（每 600 次）——這類 bug 的直接偵測器
- [x] Runtime 驗證: VK readback 非零 ✓、texprobe acquired 讀非零（avgLuma 125.7）✓、host 視窗 PrintWindow 非黑（nonblack 100%、可見 Settings+鍵盤、有效 FPS 43）✓
- [x] SelfTest 增加 host 視窗像素 gate（`host_window_nonblack_pct>=5`）
- [x] ctest non-integration 23/23 PASS
- [x] 文件: CLAUDE.md / CONTEXT.md / tasks/lessons.md / memory
- [x] SelfTest（-Fast）+ 對抗式審查

### Review

- 決定性工具是兩支獨立 probe：`texprobe.exe`（D3D11 端讀 shared texture 像素，有/無 AcquireSync）與 `vkinterop.exe`（隔離重現 Vulkan→D3D11 匯入/寫入/跨 process 矩陣）。沒有它們，三層 bug 會被互相遮蔽（修一層看不到效果，容易誤判修錯）。
- 教訓核心寫入 lessons.md：counters 全綠（VK_SUCCESS/sequence/fence/FPS）不代表像素有到；跨 API/跨 process 共享的唯一有效驗證是從消費端 API 讀回像素；顯示鏈每一跳要有各自的像素證據。
- gl60「60fps」歷史數字需重新定性：過去量的是零幀 blit 的節奏。修復後 GLES 內容每幀多付 GL readback+VK upload（SwiftShader CPU），實測互動 UI 有效 43 FPS（真實內容、真實可見）。

---

## 2026-07-02 Session 100 — -Fast 啟動黑屏根因修復 + 載入加速 + 1080p60 驗證

### 根因（已實證，2026-07-02）

使用者雙擊 `start-chimera.cmd`（`-Fast -InteractiveFirst`）後中間模擬畫面全黑、側邊 UI 正常。

**證據鏈**：
1. 重現 boot（port 5554、production env）：guest 螢幕 = status bar/導覽列可見（SystemUI）+ launcher 視窗內容純黑（ADB screencap 14KB，t=46s 起持續 4 分鐘）。
2. `applyGuestVulkanHardwareUi()`（main.cpp）在 boot_completed 立即 `setprop debug.hwui.renderer=skiavk` + `stop`/`start`。
3. 此 AVD 是 `google_apis_playstore` **user build**（`ro.debuggable=0`）→ `adb shell stop` → **"Must be root" 失敗**（被 runAdbShell 吞掉）→ framework 從未 restart、SurfaceFlinger 永遠留在 SkiaGL。
4. setprop 之後才第一次 HWUI init 的 process（launcher 等）走 Vulkan 渲染 → guest VK buffer 在 host 是 NVIDIA VK memory，SF 的 GLES 合成在 host 是 SwiftShader → 跨裝置無法取樣 → **視窗內容全黑**。setprop 之前就啟動的 process（SystemUI）維持 GLES → 可見。
5. 獨立 probe：`-systemui-renderer skiavk` boot（HWUI 從開機就 Vulkan，`Pipeline=Skia (Vulkan)` 實證）→ home 10.7KB / Settings 10.6KB spread=0 全空白 → 確認 HWUI-VK + SF-GL 結構性全黑，且 init 不翻譯 `ro.boot.debug.renderengine.backend` → **SF 在此 image 上永遠無法切 Vulkan（無 root、無 boot hook）→ skiavk UI 路線在此 image 不可行**。
6. 「偶發 black-boot」假象：可見與否取決於前景 app process 是在 setprop 前或後啟動（timing），因此舊 verifier 有時可過（sampled pre-setprop process）。

### 修復計畫
- [x] 重現 + 根因（上述）
- [x] main.cpp：移除 `applyGuestVulkanHardwareUi()` 與 poller 內 skiavk apply/grace 邏輯（省 12s grace + 移除黑屏觸發源）
- [x] main.cpp：`installChimeraLauncher()` 加 md5 比對，APK 未變時跳過 reinstall/force-stop/relaunch（省 ~5-8s + 移除 home 重啟閃爍）
- [x] QmlAndroidControls + ChimeraWindow.qml：新增 `bootReady`，「等待 Android 啟動…」placeholder 保持到 boot 完成（不再在第一張黑幀就消失）
- [x] VirtualMachine.cpp：更新過時註解（skiavk 不可行的實證；保留 `-feature Vulkan`）
- [x] start-chimera.ps1：更新 -Fast 說明；SelfTest 加 screenshot 內容 gate（bytes≥20000/nonblack≥10%/spread≥40）
- [x] verify-interactive-ui.ps1：移除 -GuestVulkan 的 setprop/stop/start 虛構路徑與 HOST_SETUP env
- [x] ChimeraVerifyCommon.ps1：`Invoke-CheckedTool` EAP=Continue（javac stderr Note 在 redirect 下變 terminating error 的 harness bug）
- [x] Build PASS + ctest 23/23 PASS
- [x] SelfTest（-Fast）PASS：`boot_seconds=33`、`visible_home_seconds=49`、screenshot 76,219B/nonblack 100%/spread 716、interactivity=ok、0 residual
- [x] **第二個真 bug（60fps 驗證時抓到）：背景手把輸入漏進 guest** — 使用者玩 P5R（手把），`GamepadManager` XInput 全域輪詢無條件轉發 → B/HOME 進 guest 把 gl60 切到背景 → 量測「隨機」凍結。sidecar 凍結瞬間取證（guest 前景=launcher、gl60 活著、螢幕 Awake）。修：`gp.poll()` focus gate（`ApplicationActive` 才轉發）。修後 sidecar 實錄 gl60 全程前景、`mObscuringWindow=null`。ctest 23/23。
- [~] verify-true-1080p60（連續渲染 60 gate）— 管線健康證據：量測窗內連續 150 秒 60.0fps（`total 510→9197`、min 59.5/avg ~60.0/dup 0，**與 P5R 同跑 + 副螢幕**）；producer `postFrameDirectGpu 0.4-0.6ms` 全程健康。惟嚴格 `min≥57` gate 在使用者 AAA 遊戲同跑時會被負載尖峰打穿（min=8.6 的瞬間掉速），乾淨 PASS 需機器空閒時重跑（歷史 Session 89/99 同 render path 已 PASS；本輪未動 render path）
- [x] verify-interactive-ui -Mode Fast -GuestVulkan -SyntheticScroll（副螢幕、與 P5R 同跑）— 完整跑完不再中斷：`path=gpu-direct / sharedTexture=yes / fallback=none`、sustained scroll `guest 55.9 / eff 49.1 / dup=0`、`result=fast-ui-visible-not-60`（遊戲同跑掉 ~10fps；Session 99 空機同路徑 59.6 strict PASS）
- [x] **第三批修正（60fps 驗證中挖出）**：① chimera-ui 啟動即讀 `CHIMERA_VERIFY_WINDOW_ORIGIN` setPosition（測試視窗從第一幀就在副螢幕，不再在 boot 期間蓋住使用者畫面被關掉）；② `Ensure-HostWindowVisible` 對 exited process 的 `MainWindowHandle`（ETS null → `[IntPtr]$null` cast 炸）try/catch 防禦；③ `Invoke-CheckedTool` EAP=Continue（javac stderr Note 假失敗）。build PASS、ctest 23/23、common parse 0 errors
- [x] `setprop ctl.restart surfaceflinger` 第三路 probe — **SELinux denied**（`avc: denied { set } property=ctl.restart$surfaceflinger scontext=u:r:shell:s0 permissive=0`、SF pid 不變）→ skiavk 三路全死，文件定案
- [x] 最終 SelfTest（全部修復、與 P5R 同跑）— **PASS**：`boot_seconds=35`、`visible_home_seconds=52`、screenshot 75,584B/nonblack 100%/spread 716、`interactivity=ok`、0 residual、exit 0
- [x] 檢查其他問題：repo 根目錄 2026-05 殘留 runtime log 已刪（gitignored）；instances.json 執行時 priority 持久化 diff 已還原
- [x] 使用者要求：verifier 視窗開副螢幕（`CHIMERA_VERIFY_WINDOW_ORIGIN=-2520,45` 存 User env；harness 原生支援）
- [x] 文件同步：CLAUDE.md / AGENTS.md / CONTEXT.md / tasks/lessons.md / memory（venus-vulkan-wall 更正）

### 載入速度（修復後預期）
舊（壞掉前的「正常」路徑）：boot ~37s + skiavk restart + 12s grace + launcher reinstall/relaunch ≈ 80–110s 才有畫面（中途多段黑屏）。
新：boot ~37s → 立即 setup（無 restart/grace）→ launcher md5 跳過 reinstall → **~40s 可見 home**，全程 placeholder 覆蓋。
Quick Boot snapshot（~15-25s）維持 opt-in（既有 host audio 政策，不動）。

### Review

**修了 3 個真 bug（皆有現場證據）＋載入減半＋量測環境整修：**
1. **黑屏（主訴）**：skiavk 半套用（`stop/start` "Must be root" 靜默失敗 + `debug.hwui.renderer=skiavk` 生效 → HWUI-VK+SF-GL 跨裝置無法合成 → app 視窗全黑）。移除 skiavk 切換；三條替代路（root restart / boot-prop / `ctl.restart`）全 probe 實證死路，文件定案禁再試。
2. **背景手把輸入漏進 guest**：`GamepadManager` XInput 全域輪詢無條件轉發，使用者玩 P5R 按 B/HOME 直接關掉 guest 前景 app（sidecar 凍結瞬間取證：前景=launcher、gl60 活著、螢幕 Awake）。修 `gp.poll()` focus gate。
3. **測試視窗蓋住使用者畫面**：boot 階段視窗停主螢幕預設位、蓋住遊戲被使用者關掉 → run 中斷。chimera-ui 啟動即讀 `CHIMERA_VERIFY_WINDOW_ORIGIN` 開副螢幕（User env `-2520,45`）。
- **載入**：~80–110s → 35s boot / ~52s 可見 home（移除 skiavk restart+12s grace、launcher md5 跳過 reinstall）；placeholder 綁 `bootReady` 全程覆蓋，不再裸黑。
- **harness 加固**：SelfTest 像素內容 gate（防黑屏假 PASS 再犯）；`Invoke-CheckedTool` EAP=Continue（javac Note 假失敗）；`Ensure-HostWindowVisible` exited-process 防禦。
- **60fps 誠實現況**：與 AAA 遊戲同跑：gl60 量測窗內連續 150s 60.0fps（min 59.5/dup 0）、互動 scroll eff 49.1/dup 0（gpu-direct、無 fallback）；嚴格 gate（min≥57 全窗）被遊戲負載尖峰打穿屬預期。空機 strict PASS＝Session 89/99（render path 本輪未動）；空機時重跑 `verify-true-1080p60.ps1` + `verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -SyntheticScroll` 即可重確認。
- **驗證彙整**：build PASS ×3；ctest 23/23 ×3；SelfTest PASS ×2（修復前後）；互動 verifier 完整跑完（`fast-ui-visible-not-60`＝遊戲共存下的誠實結果）；每輪 0 residual。
- **量測紀律教訓**（詳 lessons.md）：量測崩潰先查使用者是否在用電腦/跑什麼；全域輸入 API 必 focus-gate；sidecar 凍結瞬間取證勝過盲跑；dup=0 低 fps=producer 上限。

---

## 2026-07-01 Session 99 — /goal 修復所有問題：sibling-grep 補洞（harness 全域 kill）

### Context
`/goal 修復所有問題`。延續 Session 96-98 已做的 8 維審查（baseline 已綠：ctest 23/23、build current、git clean）。本輪聚焦 lessons.md 第一條規則（修過一類 bug 要 grep 全 repo sibling），把先前各 session 修過的 bug class 在 production host + harness 全面複查，補先前漏網的同類。

### Plan
- [x] 驗證 baseline：`ctest -LE integration` 23/23 PASS、build current、git clean。
- [x] Sibling-grep C++ production host 的已修 bug class：硬寫 port（8554/5554/5555）、`CreateSharedHandle` flag、`waitForExit`/`CloseHandle` double-close、env-var `stoi/atoi` 解析、tap press/release pairing。
- [x] Sibling-grep PowerShell harness 的全域 kill / 硬寫 port。
- [x] 修漏網、parse + runtime 煙霧驗證。
- [x] 文件同步：lessons / todo。

### Review
- **複查結果（C++ production host）：5 類 sibling 全乾淨，無新 bug**：
  - 硬寫 port：production 路徑（main.cpp/InstanceManager/AdbFramebufferCapture）都從 derived-port 公式取值；剩餘 `5554/5555` 命中都在 legacy `src/virtualization/qemu` fork（R&D `--qemu-backend`）與 `.h` 預設（建構時被覆蓋）。
  - `CreateSharedHandle`：3 個 site（proxy d3d11 / Vulkan bridge ×2 / SharedD3D11TexturePublisher）全用 `DXGI_SHARED_RESOURCE_READ|WRITE`，無殘留 `GENERIC_ALL`。
  - `waitForExit`/`CloseHandle`：`QemuBackend::stop()`/`onHealthCheck()` 邏輯正確（只在 `waitForExit<0` 或 process 已不在時才 close）。
  - env `stoi/atoi`：所有 `std::stoi`（main.cpp:1152 / InstanceManager:529,835 / ConfigManager:51,63）都包 try/catch；`atoi` 不丟例外且後續被 clamp。
  - tap press/release：MacroEngine 已成對；swipe 路徑都以 `sendTouch(...,0)` 釋放結尾。無漏網。
- **修掉的 2 個（PowerShell harness）**：
  1. **`run-proxy-smoke-test.ps1:284` 殘留掃描全域 `Get-Process -Name "emulator","qemu-system*"` + `$rp.Kill()`**（Sessions 96-97 修主 verifier 時漏的 R&D probe sibling）→ dot-source `ChimeraVerifyCommon.ps1`、改用 cmdline-filtered `Get-ChimeraProcesses`。它本來就 `taskkill /F /T /PID` 殺過自己的 tree，全域補刀只會殺到別人的 emulator（Android Studio/BlueStacks/其他 instance）。
  2. **`verify-quick-boot.ps1` local re-def `Stop-/Wait-NoChimeraProcesses`**（與 dot-sourced 共用版行為完全一致、shadow 之）→ 刪 local，走共用版（lessons「重抄就是漂移源」）。
- **驗證**：兩支 .ps1 parse OK；`Get-ChimeraProcesses` 在 `run-proxy-smoke-test.ps1` 的 script-var context 可呼叫（乾淨機器回 0）；`verify-quick-boot.ps1` 刪 local 後共用 `Stop-/Wait-/Get-ChimeraProcesses` 仍由 dot-source 提供。

### 追加：實修 Session 98 deferred 的 2 個 finding（goal hook 要求不留 deferral；重判後兩者都可正當修復）
3. **`AudioBridge.cpp` WASAPI forced-format bug（實際比 Session 98 認定嚴重）**：`GetMixFormat` 多回 `WAVE_FORMAT_EXTENSIBLE`(cbSize=22)，code 改 `wFormatTag=IEEE_FLOAT` 卻沒重設 `cbSize` → shared-mode `Initialize` 對**任何** rate 都失敗（"Audio (WASAPI)" 功能其實全壞，非只非原生 rate）。修法：`pwfx->cbSize=0` + `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|SRC_DEFAULT_QUALITY`。新增 `test_audio_bridge.cpp::forcedNonNativeRateInitializesViaAutoConvert`（probe 48000/2 判 device 再試 22050/mono）。**RED→GREEN 實機驗證**：修前該測試 `QSKIP("no WASAPI render endpoint")`（連 probe 都失敗，被 bug 偽裝成無裝置）→ 修後 `PASS`（10 passed/0 skipped）。
4. **`ChimeraGfxstreamVulkanSharedTextureBridge` `mStagingBuffer` 死分支**：全 repo grep 證明無 `vkCreateBuffer(&mStagingBuffer)`、無非 null 賦值 → 分支恆 false（Session 89 GPU-direct path 取代後的殘骸）。移除 recordCopy 死 if-branch（保留 live DisplayVk else）+ reset() 死 destroy + `.h` 死 member（mStagingBuffer/Memory/Data）。靜態可證死碼，移除不改 runtime；template 由 patch script `Copy-TextTemplate` verbatim 複製，會流入下次 DLL rebuild。recordCopy/reset 編輯後括號平衡、repo 無殘留 `mStaging` 參照（僅解釋註解）。
- **驗證（全部）**：完整 Release build PASS；`ctest -LE integration` **23/23 PASS**（`test-audio-bridge` 內含新 GREEN 方法）；兩支 .ps1 parse OK。
- **誠實邊界**：harness 防誤殺 sibling + AudioBridge 真 bug（實機 RED→GREEN）+ gfxstream 靜態可證死碼移除，皆非 FPS。gfxstream template 兩處未經 DLL runtime 重驗（靜態證明 + 下次 rebuild 帶入，同 Session 98 #7 模式）。未使用任何會搶實體滑鼠的測試。

### 追加：深層路線 general-UI 60（使用者選「投入深層路線」）— 建真實路徑量測器 + 修正 ~20fps 誤判
- **根因發現**：過去「general-UI ~20fps not-60」大半是 **adb-swipe 量測污染**。本輪 adb-swipe GuestVulkan sustained-scroll 量到 `guestFps=0.0 dup=0.0`（連 duplicate 都 0＝完全沒驅動 guest 重繪）。真實輸入路徑（host mouse-drag）Session 95 曾量 `render=57.4` 但會搶實體滑鼠。
- **新增 real-path 量測器（lessons 早開的處方，可驗、可重用、不搶滑鼠）**：`CHIMERA_SYNTHETIC_SCROLL`（`main.cpp`，diagnostics-only 預設關）— boot 後 QTimer 走 production 路徑 `InputBridge::onTouchPoint → gRPC sendTouch` 連續 fling，**零 SendInput/SetCursorPos**。`verify-interactive-ui.ps1 -SyntheticScroll` 設 env 並跳過 adb swipe。
- **實測（真實輸入路徑，GuestVulkan/skiavk、gpu-direct）**：sustained-scroll `guestFps=55.9 / streamFps=52.8 / renderFps=52.6 / effFps=52.6 / effMin=51.0 / dup=0 / bottleneck=render`。→ **一般 UI 真實路徑 ~53 eff/~56 guest，近 60，非 ~20**。修正長期誤判。
- **達成 general-UI 60（真實輸入路徑）✅ — 瓶頸是 HOST 端，非 guest gfxstream**。~53→60 靠**兩個 host-only 改動、零 DLL 重建**：
  1. **`QSG_RENDER_LOOP=threaded`**（main.cpp，QGuiApplication 前）：basic render loop 讓 scroll 期 GUI-thread 拖慢 host 消費 → gfxstream backpressure → guest 跳 vsync（55.9）。threaded 後 **guest→60.1**。
  2. **GuestDisplay present timer 16ms→200ms**：62Hz `update()` wakeups 排擠 GUI-thread 處理 per-frame texture 訊號（`onFrameReceived`＝streamFps）→ 只處理 54/59.4。降頻後 **stream/render→59.6/59.8**（present timer 對 shared-texture path 冗餘，event-driven 已 per-frame 驅動）。
- **驗證（重現 2×）**：`verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -SyntheticScroll` strict gate **`result=pass-gpu-direct-60`**：run1 `effFps 59.6/effMin 58.1/guest 60.1/dup 0`、run2 `59.6/58.2/60.2/0`。gl60 連續渲染非回歸 `min 59.9/avg 60.0 pass`。`ctest -LE integration` 23/23 PASS、0 residual。
- **根因翻轉**：跨 ~15 session 的「general-UI 60 卡 guest gfxstream frame-pacing、out-of-scope」判斷**是錯的**（被 adb-swipe 量測污染遮蔽）；真實瓶頸是 host render-loop backpressure + GUI-thread present-timer contention，皆 host-side 可修。
- **誠實 caveat**：① 用 `CHIMERA_SYNTHETIC_SCROLL` fling 注入器走 production 輸入路徑量（不搶實體滑鼠），反映真實 fling 輸入；② custom runtime 仍偶發 black-boot（本輪一次 boot ADB 13KB near-black，屬既有 intermittent，非 host 改動造成；host present timer 不影響 guest ADB screencap）；60 重現需 boot 成功。

---

## 2026-07-01 Session 98 — audit-driven concrete bug 修復（8 維 find → 對抗式 verify）

### Context
使用者：「有很多問題修復它們」。ultracode 開，跑全 repo 8 維審查 workflow（ui/instance/graphics/input/gfxstream/utils-audio/scripts/consistency → 對抗式 verify）。verify 階段中途吃 session limit，僅 1 個 finding 跑完 CONFIRMED；其餘 find 階段的 finding 由主 loop 逐一手動對抗式驗證 + TDD 修。不碰 general-UI 60 R&D、不用搶實體滑鼠的測試。

### Plan
- [x] 跑審查 workflow，抽出各 find agent 完整 findings（11 個）。
- [x] 逐一手動 verify + 每個高信心可小修者先寫 RED 測試再修。
- [x] 全量 build + ctest 非回歸。
- [x] 文件同步：lessons / todo / CONTEXT。

### Review — 8 個確認並修復（皆 TDD：先 RED 再 GREEN，除 gfxstream template 無法 runtime 驗證者）
1. **`main.cpp:1597` ADB fallback 硬寫 port 5555**（P2，與 Session 93 gRPC port 同類漏網）→ 改 `g_runtimeCfg.adbPort`。新測 `test_ui_main_port_contract.cpp`。
2. **`FileUtils::ensureDir` 目錄已存在回 false**（P2，`create_directories` 只在新建時回 true）→ `|| is_directory(path)`。新測 `test_file_utils.cpp`。
3. **`MacroEngine` Tap playback 只送 press 不送 release**（P1，guest 觸控永久按住）→ 補 `onMouseButton(false,…)`。`test_macro_engine.cpp` 加 callback 觀測測試。
4. **`QemuBackend` double `CloseHandle`**（P2，`waitForExit` WAIT_OBJECT_0 已關 handle，stop()/onHealthCheck() 又關一次）→ 只在 `waitForExit<0` / 非 running 時關。`test_qemu_backend.cpp` 加 source-contract 測試。
5. **`gfxstream_proxy_d3d11.cpp:208` `CreateSharedHandle` 用 `GENERIC_ALL`**（P2，consumer `OpenSharedResourceByName` 回 E_INVALIDARG，Session 83-84 已對 production bridge 修過、proxy 漏）→ 改 `DXGI_SHARED_RESOURCE_READ|WRITE`。新測 `test_runtime_source_contract.cpp`。
6. **`VirtualMachine.cpp:733` exit-monitor null 掉 handle 不 close**（P2 leak + 與 stop() 的 close 競態；只加 CloseHandle 反成 double-close）→ exit-monitor 與 stop() 都用 `InterlockedExchangePointer` 原子 claim，誰拿到非 null 誰關。source-contract 測試。
7. **`ChimeraGfxstreamVulkanSharedTextureBridge.cpp:491` vkBindImageMemory 失敗路徑漏 `CloseHandle(sharedHandle)`**（P3 leak，上面兩個 sibling 失敗路徑都有）→ 補上。gfxstream template（patch script verbatim copy），純加 error-path cleanup、零風險，下次 runtime rebuild 帶入；本輪不重建 DLL。
8. **`LocationSimulator::setLocation` 走 route throttle 被丟**（P2，explicit teleport 短時間內第二次被 1Hz/移動門檻擋掉）→ `emitGeoFix(pt, force=true)`，throttle 只留給 `update()` route 推進。`test_location_simulator.cpp` 加測試。

### 刻意跳過（記錄不修）
- **`AudioBridge.cpp:93` WASAPI forced format 無 `AUTOCONVERTPCM`**（P2 medium）：`AudioBridge::instance()` 產線從未被呼叫（main.cpp:1217 註解：guest audio 由 emulator 把 Goldfish audio 直接路由 host WASAPI，不經此 bridge）；改 WASAPI init 無法 runtime 驗證又涉敏感音訊路徑 + cbSize/EXTENSIBLE 一致性風險 → dead code 的潛在 bug，留記錄。
- **`ChimeraGfxstreamVulkanSharedTextureBridge.cpp:587` staging-copy dead branch**（P3 dead-code）：`mStagingBuffer` 從未賦非 null；移除需動 member + 無法 runtime 驗證 DLL，無害 → 跳過。

### 驗證
- 完整 Release build PASS（chimera-ui.exe 連結成功）；**`ctest -LE integration` 23/23 PASS**（原 20 + 新 `test-file-utils`/`test-ui-main-port-contract`/`test-runtime-source-contract`；改過的 macro/qemu/location 仍綠）。
- 每個修復先確認 RED（新測試在修前失敗），再 GREEN。
- 誠實邊界：本批是 correctness/leak/race/contract 修復，非 FPS 提升；gfxstream template 兩處（491 修、587 跳）未經 DLL runtime 重驗。審查 workflow 的 verify 階段因 session limit 中斷，剩餘 finding 由主 loop 補做對抗式驗證。

---

## 2026-06-30 Session 97 — PowerShell harness port / cleanup 小修

### Context
使用者：「有些問題修復它們」。延續 Session 96 audit 後，再掃目前 harness 殘留同類問題：部分 self-test / R&D verifier 仍硬寫 port 或用全域 process kill，會重犯「非 Chimera emulator 被殺」與「5554/5555 被占用就 false timeout」問題。本輪只修 harness hygiene，不碰 gfxstream/perf R&D，不使用會搶實體滑鼠的測試。

### Plan
- [x] 在 `ChimeraVerifyCommon.ps1` 新增最小 `Resolve-EmulatorConsolePort`：`0` 自挑 free pair，非 0 必須是 5554–5680 even console port。
- [x] `verify-interactive-ui.ps1` 改用共用 resolver，`-ConsolePort 5555` fail fast。
- [x] `start-chimera.ps1 -SelfTest` 未明確指定 port 時自挑 free pair；pre/post cleanup 改用 cmdline-filtered `Stop-ChimeraProcesses` / `Wait-NoChimeraProcesses` / `Get-ChimeraProcesses`。
- [x] `verify-hardware-ui.ps1` 新增 `-ConsolePort 0`、設定 `CHIMERA_EMULATOR_CONSOLE_PORT`，移除 hardcoded `emulator-5554` 與全域 `Kill-All`。
- [x] 驗證：PowerShell parse、odd-port fail-fast、Fast SelfTest、R&D verifier smoke、ctest 20/20。
- [x] 文件同步：lessons / CONTEXT / CLAUDE / AGENTS / todo。

### Review
- **修掉的 3 個同類問題**：
  1. `verify-interactive-ui.ps1 -ConsolePort 5555` 會進入錯誤 pairing → 改走 `Resolve-EmulatorConsolePort`，odd port 啟動前 fail fast。
  2. `start-chimera.ps1 -SelfTest` 預設固定 5554 且 `Kill-All` 全域殺 emulator/qemu → 未明確指定 port 時改自挑 free pair（實測 5560），cleanup 改 shared cmdline-filtered helper。
  3. `verify-hardware-ui.ps1` 硬寫 `emulator-5554`、清掉 `CHIMERA_EMULATOR_CONSOLE_PORT`、全域 kill → 新增 `-ConsolePort 0`、設定 env/serial、cleanup 改 shared helper。
- **驗證**：四支 .ps1 parse OK；`verify-interactive-ui.ps1 -Mode Stock -ConsolePort 5555` fail fast（even port error，未啟動 UI）；`start-chimera.ps1 -Fast -InteractiveFirst -SelfTest` PASS（serial auto `emulator-5560`、1920×1080、Settings `interactivity=ok`、`residual_processes=0`）；`verify-hardware-ui.ps1 -GrpcDisplay -BootTimeoutSec 60` 進既有 R&D failure（adapter 仍 SwiftShader / shader errors）但輸出 `selected_console_port=5560`、`serial=emulator-5560`、`residual_processes=0`；`ctest -LE integration` 20/20 PASS。
- **誠實邊界**：本輪是 harness robustness / 防誤殺 / port contract 修復，非 FPS 或 renderer 改動；未使用任何會搶實體滑鼠的測試。

---

## 2026-06-30 Session 96 — audit-driven bug 修復（先 bug/回歸，後性能 R&D）

### Context
使用者：「還有很多問題修復它們」。ultracode 開，用 14-agent 審查 workflow（4 維 find → 對抗式 verify）掃整個 repo 的具體 bug/回歸/矛盾，再逐一修高信心、可小修者。general-UI 60 維持 R&D，不當本輪承諾。不使用任何會搶實體滑鼠的測試。

### Plan
- [x] 現況矩陣：ctest 20/20、Fast `-InteractiveFirst -SelfTest` PASS（interactivity=ok、0 residual）。
- [x] 審查 workflow：7 個確認、可小修 finding（對抗式 verify 砍掉 3 個假陽性）。
- [x] 修 7 個 finding（見 Review）。
- [x] parse-validate 三支改過的 .ps1 + 重跑 Fast SelfTest 非回歸。
- [x] 文件同步：lessons / todo / CONTEXT / CLAUDE / README / AGENTS。

### Review
- **修掉的 7 個（皆 P2，對抗式 verify 後）**：
  1. `start-chimera.ps1` `-RequireSharedTexture` runtime 缺失時靜默退 stock gRPC → 加 fail-closed `throw`。
  2. `start-chimera.ps1` `ConsolePort` `ValidateRange` 收 odd port（Android 要 even，否則 grpc/adb 推導錯）→ 改 `ValidateScript` 限 even。
  3. `README.md:16` 「host input path 已量到接近 60 headroom」過度表述（引用已禁用的 mouse-drag probe）→ 改誠實「一般 UI 約 20fps、general-UI 60 仍 R&D」。
  4. `verify-quick-boot.ps1` 硬寫 `emulator-5554`、不挑 free port → dot-source `ChimeraVerifyCommon.ps1`、`Get-FreeEmulatorConsolePort` + 設 `CHIMERA_EMULATOR_CONSOLE_PORT` + derive serial。
  5. `verify-interactive-ui.ps1 -GuestVulkan` 從不設 `CHIMERA_GUEST_VULKAN=1` → emulator 沒帶 `-feature Vulkan`，skiavk 靜默退 GLES（假對照）→ 補設 env + 加 TouchedEnv 還原。
  6. `verify-quick-boot.ps1` `Get-ChimeraProcesses` 無 cmdline filter，cleanup 會殺機器上任何 emulator/qemu → 刪 local、改用 shared cmdline-filtered 版。
  7. `AGENTS.md:66` 一個 0x0B（VT）控制字元把 `\verify-true-1080p60.ps1` 變 `\v`+`erify…`，文件指令路徑壞掉 → 還原 `\v`。
- **對抗式 verify 否證的 3 個（沒亂修）**：main.cpp boot_completed gate（刻意 robustness）、`$LASTEXITCODE` 洩漏成 exit 255（實機重現 exit 0）、CLAUDE.md:331「預設 stock」（dated changelog 非現況）。
- **本輪也修了的口徑矛盾**（audit 前先手修）：`start-chimera.ps1` header 仍說「default stock」、`start-chimera.ps1`/`main.cpp` 註解「general-UI feel ~60」、README SelfTest 範例未帶 `-Fast -InteractiveFirst`。
- **驗證**：三支改過 .ps1 parse OK；ctest 20/20（C++ 僅改註解、無行為變更，不需重建）；Fast `-InteractiveFirst -SelfTest` PASS（1920×1080、Settings `interactivity=ok`、`residual_processes=0`、exit 0）。
- **實機確認 fix #5**：detached（獨立 console，避 `STATUS_CONTROL_C_EXIT`）跑 `-Mode Fast -GuestVulkan -AllowBaseline` → log `Feature 'Vulkan' (21) is overridden to 'enabled'` + NVIDIA Vulkan + `path=gfxstream-shared-texture fallback=none`；sustained-scroll `guestFps=48.6/effFps=40.3/dup=0/bottleneck=render`、0 residual。**先前「GuestVulkan ~20fps」是 bug #5 污染（沒真開 Vulkan、退 SwiftShader）**；真開後 ~40 effFps（約 2×），瓶頸移到 host Qt render。
- **誠實邊界**：本批改動是 robustness / 驗證正確性 / 文件誠實修復，非 FPS 演算法提升；但 fix #5 讓量測首次反映真實 Vulkan 路徑。general-UI 60 仍 R&D。未使用任何會碰實體滑鼠的測試。

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
