# 競品平滑度參考 — BlueStacks / LDPlayer / MuMu 如何做到順暢一般 UI

> 目的：使用者問「一堆中國模擬器抄襲 BlueStacks，我們何不好好參考參考」。
> 本文釐清競品「一般 UI 順」的真正技術來源，對照 Chimera 現況，列出可採用的方向與被擋的根因。
> 所有 Chimera 側結論都有 log/file:line 佐證；競品側為公開技術資料 + 行為推論。

## 1. 競品架構 = 與 Chimera 同源（fork Android Emulator/QEMU + GPU passthrough）

- BlueStacks、LDPlayer、MuMu(網易) 全部是 **fork 自 AOSP Android Emulator / QEMU**，搭配 **host 硬體 GPU passthrough**（gfxstream / virtio-gpu 家族）。不是從零寫 Android VM，也不是純軟體渲染。
- 輸入走 **自家 virtio-input HAL**（BlueStacks `HD-Bridge-Native.dll` → virtio-input），不是 kernel driver、也不是 `adb input`。連續滑動/fling 產生連續 guest frame。
- 這跟 Chimera 的生產引擎（`emulator.exe` = Google QEMU+WHPX fork）+ gRPC `sendTouch` + custom gfxstream shared-texture runtime **是同一條技術路線**。差別不在「要不要 fork Android」，而在 **guest GPU 有沒有打到實體 GPU**（見 §3）。

## 2. 競品「順」的三個真正來源

1. **Guest GPU 跑在實體 GPU 上**：guest 的 SurfaceFlinger 合成 + app 自身繪製，透過 virtio-gpu（Venus=guest Vulkan→host Vulkan，或 host-GL）落到 host 的 NVIDIA/AMD GPU。整條 UI pipeline 硬體加速，每幀 < 16.6ms。
2. **直接 swapchain present，不是螢幕輪詢**：host 視窗顯示走 guest compositor → shared surface → host swapchain 的 **直接 present**，不是週期性 `getScreenshot` readback。這對應 Chimera 的 `postFrameDirectGpu`（custom runtime shared-D3D11-texture）路徑，**不是** stock gRPC unary capture。
3. **精簡 ROM + 連續輸入**：自家 gaming ROM 拔掉多餘系統元件、輕量 launcher；輸入 HAL 連續餵 motion event，讓 SurfaceFlinger 持續以 60 渲染（而非 push-based 靜止 0 幀）。

## 3. Chimera 對照：哪一段已對齊、哪一段是真正落差

| 環節 | 競品 | Chimera 現況（佐證） |
|------|------|----------------------|
| VM 核心 | fork QEMU+Android | ✅ 同（`emulator.exe` QEMU+WHPX） |
| 輸入 | virtio-input HAL 連續 | ✅ gRPC `sendTouch`（連續），verifier 用 `adb input swipe` 是離散測試假象 |
| Host 顯示 present | 直接 swapchain | ✅ custom runtime `postFrameDirectGpu`（Session 89：`path=GPU-direct=79`、`postFrameCpu=0`、`GL readback=0`） |
| **Guest GPU 繪製** | **實體 GPU passthrough** | ❌ **headless build 落到 SwiftShader ES（軟體）** — Session 87 三條硬體路徑全被擋 |

**結論（reframe）**：Chimera 的 **host 端 pipeline 已和競品同級**（GPU-direct present、postFrameCpu=0、輸入連續）。一般 UI 只有 ~20–25 effFps（Session 90 實測 `guest≈stream≈render`、dup=0 → host 1:1 跟上）的 **唯一瓶頸是 guest 自身繪製跑在 SwiftShader（CPU 軟體）**，不是 host 顯示路徑、不是輪詢、不是輸入。gl60 能 60 是因為一個三角形即使軟體繪製也夠快、且 post 走 GPU-direct；整個 SurfaceFlinger UI 合成在軟體下就掉到 ~20。

> 即：**競品順 = guest 繪製在實體 GPU；Chimera 卡 = guest 繪製在 SwiftShader。** 這正是 Session 87 記錄、被標為 out-of-scope 的深層 gfxstream R&D。

## 4. 要追平競品一般 UI，必須解的根因（Session 87 已實測被擋）

讓 guest host-GLES/Vulkan 真正路由到 RTX 3070 Ti，三條路徑現況：

1. **native WGL（`-gpu host`）headless**：無視窗無法建 GL context → 退 SwiftShader。
2. **`-gpu angle_indirect` CLI**：prebuilt `emulator.exe` 的 `gpuChoiceBasedOnGpuOptions` 判 invalid → auto → SwiftShader+lavapipe → exit 4（binary 不可重建）。
3. **ANGLE 經 DLL 內 emugl_config fallback**：emulator init 階段直接 hang（停在 `Found systemPath`，never open console/adb）。

**可採用的下一步候選（對齊競品做法）**：
- **A. virtio-gpu Venus（guest Vulkan → host Vulkan）** 取代 host-GLES：競品新版多走 Vulkan passthrough；避開 headless WGL 無 context 的死結，guest Vulkan 直接打 host Vulkan（我們已能在 host 建 NVIDIA Vulkan instance/device，見 Session 80）。**最有機會**。
- **B. 帶隱藏 host surface 的 windowed emulator**：不用 `-no-window`，改建一個 off-screen / 不可見但有真實 WGL context 的視窗，讓 host-GLES 有 context 可建，再把畫面導進 Chimera 單一視窗（須維持「使用者面只有一個 Chimera 視窗」邊界）。
- **C. 重建 emulator.exe** 放寬 headless GPU 選擇（超出現行範圍，但能解 angle_indirect/host 的 init gate）。
- **D. 精簡 guest ROM**：即使軟體繪製，拔掉系統動畫/多餘合成層可把 ~20 拉高；屬輔助、非根治。

## 5. Session 91 實測：Venus/Vulkan 方向走到底，牆精準定位

使用者授權「執行」Venus 方向後，用一系列 headless 探測（`tmp/venus-*.ps1`）逐層驗證，結果分三層：

**✅ 第 1 層 — guest Vulkan → 實體 NVIDIA（成立）**
- AVD 為 android-34、`/vendor/lib64/hw/vulkan.ranchu.so` 存在、`ro.hardware.vulkan=ranchu`。
- 加 `-feature Vulkan` 後 gfxstream backend log：`Selecting Vulkan device: NVIDIA GeForce RTX 3070 Ti, Version 1.4.325`、`vkCreateInstance res=0`、`Vulkan emulation initialized`。guest Vulkan **確實路由到實體 GPU**，非 SwiftShader/lavapipe。

**✅/△ 第 2 層 — SurfaceFlinger Vulkan RenderEngine（穩定但 realistic 效益小）**
- `debug.renderengine.backend=skiavk` 只能用 **runtime `adb setprop` + `ctl.restart surfaceflinger`** 生效；emulator `-prop X=Y` 設的是 `androidboot.X`（kernel cmdline），**不是** SF/HWUI 讀的 runtime `debug.*`，所以 `-prop` 是 no-op。
- SF skiavk 穩定（30s 連續可互動、非黑）。standalone in-guest fling 下 skiavk 比 skiagl 多 6× app frames（97 vs 16），但那是 in-guest 輸入 JVM 把 CPU 榨乾、放大了 skiagl SF 合成的 backpressure。**end-to-end（host-driven 量測）≈ 持平**（guest 23 vs baseline 25）——因為 realistic 情境下 skiagl SF 本來就跟得上，真正的天花板是 app 自己的 HWUI 繪製。

**❌ 第 3 層 — app HWUI 上 Vulkan（崩潰，這是牆）**
- 真正能破 ~25 天花板的是讓 **app HWUI 用 Vulkan 繪製**（`debug.hwui.renderer=skiavk`）。runtime setprop 生效（`hwui_prop_now=skiavk`），但 app 一旦以 Vulkan HWUI 實際繪製，**gfxstream host backend 崩潰**：
  ```
  gfxstream::host::SyncThread::doSyncThreadCmd()
  gfxstream::host::vk::DeviceOpTracker::PollAndProcessGarbage()  device_op_tracker.cpp:72
  vk_decoder_global_state.cpp:7764
  ```
- 崩在 device-op 垃圾回收的 `pollingFunc.func()`（fence/semaphore poll）——HWUI 大量 Vulkan fence/semaphore 流量下的 **use-after-free / lifetime bug**。

## 6. Session 91 後段 — 攻 app HWUI Vulkan，修 3 個 MSVCP140 crash，硬體 UI 渲染達成 ✅

授權後實際攻 app HWUI Vulkan 崩潰。根因**不是** lifetime UAF，而是 **Session 76 同款 MSVCP140 `_Associated_state` crash**（本機兩個不相容 MSVCP140.dll：SDK-bundled 14.28 vs 系統 14.44，layout 不一致；`std::promise::set_value` / 被 invoke 的 `std::packaged_task` / `std::future` shared-state 都 null-deref 在 `MSVCP140.dll+0x12c10`）。HWUI Vulkan 大量 per-submit fence/enqueue 流量驅動這些路徑而崩。crash stack 逐一指路，修了**三處**：
1. `host/vulkan/device_op_tracker.cpp/.h`：`DeviceOpWaitable` `std::shared_future<void>` + `promise->set_value()` → `std::shared_ptr<std::atomic<bool>>`（header-only atomic flag）。
2. `host/sync_thread.cpp/.h`：`Command::mTask` `std::packaged_task<int(WorkerId)>` + `future.get()` → `std::function` + stack `Lock+ConditionVariable`。
3. `common/base/.../threads/WorkerThread.h`（threadpool 核心 primitive，SyncThread/post/readback/cleanup 全用）：`Command::mCompletedPromise` `std::promise<void>` + `enqueue() -> std::future<void>` → 共用 `Lock+ConditionVariable` 的 `WorkerCompletion`（`shared_ptr`）；`frame_buffer.cpp` 的 `sendPostWorkerCmd` 邊界用**安全的 deferred-async** 橋接回 `std::future<void>`（此環境 `std::async(std::launch::deferred,…)` 不崩）。

> 找第三處的方法：raw crash 格式 `[chimera-gfxstream-crash] stk[NN] dll+offset` 用 `llvm-symbolizer --obj=<dll> <0x180000000+offset>` 解（x64 DLL preferred image base = `0x180000000`）。

**結果（突破）**：crash 全清、無第四 site，**app HWUI Vulkan 真的渲染了**——`dumpsys gfxinfo` 顯示 `Pipeline=Skia (Vulkan)`、guest responsive、畫面非黑。

## 7. End-to-end 量測 — 硬體 Vulkan UI ~2× 快過 SwiftShader，瓶頸移出 guest

`verify-interactive-ui.ps1 -Mode Fast -GuestVulkan`（chimera-ui、host gRPC input 無 in-guest JVM confound、gpu-direct path）同 session 對照：

| Path | guest render | bottleneck |
|------|--------------|-----------|
| **skiagl**（guest 繪製在 SwiftShader 軟體） | **9.7 fps** | **guest**（guest 畫不夠快） |
| **Vulkan HWUI**（skiavk → gfxstream → NVIDIA 硬體） | **18.4 / 20.5 fps**（重現） | **render**（guest 比 host 合成還快） |

**guest render throughput 約 2×，bottleneck 從 guest（SwiftShader-bound）移到 host render（Qt 合成，contention-bound）**——即 **guest 軟體渲染牆被打破**，這正是競品的硬體 UI 渲染做法。仍非 60（剩 host render contention + push cadence），但 **guest 不再是瓶頸**。

## 8. 對使用者的誠實結論（最新）

- Chimera **架構與競品同級**，且 **guest UI 現在可走實體 NVIDIA Vulkan 渲染**（不再被 SwiftShader 軟體繪製卡死）。
- 三個 MSVCP140 crash 修正對**任何重度 guest-Vulkan workload（含 Vulkan 遊戲）**都有 robustness 價值，不只 HWUI。
- **distance to general-UI 60**：guest 軟體牆已破；剩下是 host render（安靜機器可達 60，gl60 已證）+ push-based cadence + 每幀 gfxstream Vulkan marshalling 開銷。要 BlueStacks 級全程 60 仍需收斂這些，但不再卡在「軟體 vs 硬體渲染」這道牆。
- 驗證：final runtime rebuild PASS（verified source commit `d60d3457ac1f1188b5782ccc23bde2c124a7c77b` → SDK build id `15261927`）；gl60 60fps **非回歸**（`min 59.6/avg 60.0`）、ctest 20/20、Fast SelfTest PASS、0 residual、final DLL md5 `FDF55A3EF314262F5BEA76760B9D454B`。

## 9. 現況對照與「超越 BlueStacks」可量測指標（2026-07-10 S112 盤點）

| 維度 | BlueStacks | Chimera 現況 | 差距定性 |
|------|-----------|--------------|---------|
| 引擎 | fork QEMU+Hypervisor | ✅ 同級（emulator.exe QEMU+WHPX） | 持平 |
| Host 顯示 present | 直接 swapchain | ✅ GPU-direct shared texture；gl60 連續渲染 60fps（normal priority）| 持平 |
| 輸入 | virtio-input HAL | ✅ gRPC sendTouch＋3-strike breaker＋ADB fallback（結構性自癒） | 持平（fallback 韌性反而較好） |
| Guest 3D（遊戲） | 實體 GPU | ✅ Vulkan app 直達 NVIDIA（S91） | 持平（Vulkan 路徑） |
| **Guest 2D UI 合成** | 實體 GPU GLES | ❌ SwiftShader CPU（ES2 pin）；互動 scroll eff ~49-57 | **主要落差**（skiavk 牆） |
| **啟動時間** | ~10-20s | ✅ Quick Boot 預設化：載入 7.5–8.6s（首次/換組態冷開 ~34s；跨 flavor 自動 invalidate 防 brick，S112c） | **持平**（已進 BlueStacks 區間） |
| **穩定性** | 成熟 | ✅ 停更真根因根治（RefCountPipe，S112；A/B 因果定案）＋watchdog 保命＋跨組態 snapshot brick 修（S112c）；35s 放置後真實點擊 gate pass | 接近持平（30-min soak gate 待跑） |
| 乾淨度 | 廣告＋遙測＋捆綁 | ✅ 純 open-source、無廣告無遙測 | **Chimera 勝** |
| 客製性 | 封閉 | ✅ 全開源可改（launcher/debloat/spoof） | **Chimera 勝** |

**「超越」的可量測 gate（誠實版）**：
1. ✅ 開機到可互動 < 15s——S112 預設化實測 7.5–8.6s（unclean exit/換組態自動退冷開＝fallback 安全，S112c flavor marker）
2. 一般 UI 互動 sustained 60fps——**S112c 定性更正**：一般 UI scroll 下 guest 已 60fps（effFps 54.3、限制在 host stream/render＝below_normal 的音訊取捨；normal priority 可 59-60，S104）；ES3（GLESDynamicVersion）A/B 零收益不採用。SwiftShader 只在重 fill 是牆且 Vulkan 遊戲已繞過→深水候選（ANGLE host-GLES draw AV/CompositorVk）對日常 UI 收益上限低。**現有解＝`-InteractiveFirst`（要 60 換音訊競爭）**
3. 30 分鐘連續使用零停更、零 crash（S112 soak 為基準）
4. 點擊→guest 反應 < 50ms（gRPC 路徑已達；ADB fallback ~200ms 屬降級模式）
5. 已達成且競品做不到：無廣告、無遙測、全開源、可完全客製 guest

---
*建立：2026-06-28；更新：2026-06-30 Session 91（前段：Venus 三層實測；後段：修 3 個 MSVCP140 future crash → app HWUI Vulkan 渲染成功、~2× guest throughput、軟體牆打破；DLL md5 FDF55A3EF314262F5BEA76760B9D454B、3 crash 修正 codified 進 patch script；gl60 60fps 非回歸 min 59.6/avg 60.0；ctest 20/20；Fast SelfTest PASS）。探測腳本：`tmp/venus-probe*.ps1`、`tmp/venus-measure.ps1`、`tmp/venus-hwui*.ps1`；end-to-end `verify-interactive-ui.ps1 -GuestVulkan`。*
