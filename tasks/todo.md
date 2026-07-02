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
- [x] commit `4f616e4` 到 master

### Review

- 決定性工具是兩支獨立 probe：`texprobe.exe`（D3D11 端讀 shared texture 像素，有/無 AcquireSync）與 `vkinterop.exe`（隔離重現 Vulkan→D3D11 匯入/寫入/跨 process 矩陣）。沒有它們，三層 bug 會被互相遮蔽（修一層看不到效果，容易誤判修錯）。
- 教訓核心寫入 lessons.md：counters 全綠（VK_SUCCESS/sequence/fence/FPS）不代表像素有到；跨 API/跨 process 共享的唯一有效驗證是從消費端 API 讀回像素；顯示鏈每一跳要有各自的像素證據。
- gl60「60fps」歷史數字需重新定性：過去量的是零幀 blit 的節奏。修復後 GLES 內容每幀多付 GL readback+VK upload（SwiftShader CPU），實測互動 UI 有效 43 FPS（真實內容、真實可見）。

### 後續候選（未指派）
- [ ] gl60 GLES 同步成本優化（readback buffer 重用/節流）
- [ ] Vulkan-backed 內容（zero-copy 直通）可見性單獨基準
- [ ] 空機重測 boot 時間（S101 87s vs S100 33s 的環境噪音成分）

---

## 2026-07-02 Session 100 — -Fast 啟動黑屏根因修復（skiavk 半套用）+ 載入加速

### 根因（已實證）

使用者雙擊 `start-chimera.cmd` 後中間模擬畫面全黑、側邊 UI 正常。skiavk 切換在 `google_apis_playstore` **user image**（`ro.debuggable=0`）上 `stop`/`start` 回 "Must be root" 被靜默吞掉 → framework 從未 restart、SF 永遠 SkiaGL；但 `debug.hwui.renderer=skiavk` 有寫入 → setprop 後首次 HWUI init 的 process 走 Vulkan → SF（SwiftShader-ES）無法取樣 NVIDIA VK surface → 視窗全黑；pre-setprop process（SystemUI）可見。「偶發 black-boot」＝process 啟動時序決定可見與否。skiavk 三路（root restart / boot-prop / `ctl.restart` SELinux denied）probe 實證全死，定案禁再試。

### 修復項目（全部完成）
- [x] main.cpp 移除 `applyGuestVulkanHardwareUi()` 與 skiavk apply/grace；`CHIMERA_GUEST_VULKAN`＝只加 `-feature Vulkan`
- [x] launcher md5 比對跳過重複 reinstall（省 ~5-8s + 消除閃爍）
- [x] QML placeholder 綁 `bootReady`（boot 完成才撤）
- [x] SelfTest 加 screenshot 內容 gate（bytes≥20000/nonblack≥10%/spread≥40）
- [x] **第二個真 bug：背景手把輸入漏進 guest** — `GamepadManager` XInput 全域輪詢無條件轉發（使用者玩 P5R 按 B/HOME 關掉 guest 前景 app）→ `gp.poll()` focus gate（`ApplicationActive` 才轉發）
- [x] 驗證環境：chimera-ui 啟動即讀 `CHIMERA_VERIFY_WINDOW_ORIGIN` 開副螢幕；`Ensure-HostWindowVisible` exited-process 防禦；`Invoke-CheckedTool` EAP=Continue
- [x] Build/ctest 23/23/SelfTest PASS（`boot_seconds=33`、`visible_home_seconds=49`、nonblack 100%、interactivity=ok、0 residual）

### Review
- 載入 ~80–110s → ~40-49s 可見 home（移除 skiavk restart+12s grace、launcher md5 跳過 reinstall）；placeholder 全程覆蓋不再裸黑。
- 量測紀律教訓（詳 lessons.md）：量測崩潰先查使用者在用電腦跑什麼；全域輸入 API 必 focus-gate；sidecar 凍結瞬間取證；dup=0 低 fps=producer 上限。
- 本輪 60fps 數字（gl60 150s 60.0、scroll eff 49.1）於 S101 被重新定性為零幀節奏，不可引用。

---

## 更早 Sessions（精簡 changelog；根因與 Review 詳見 CONTEXT.md 與 git log）

- **Session 99 (07-01)** — sibling-grep 補洞（2 個 harness 全域 kill/shadow re-def）+ 實修 S98 deferred（AudioBridge WASAPI cbSize/AUTOCONVERT 實機 RED→GREEN、gfxstream `mStagingBuffer` 死碼移除）+ `CHIMERA_SYNTHETIC_SCROLL` 真實路徑量測器 + 兩個 host 修（`QSG_RENDER_LOOP=threaded`、present timer 200ms）。⚠ 60 數字被 S101 重新定性（零幀節奏），host 修本身仍有效。
- **Session 98 (07-01)** — audit-driven 8 維 workflow → TDD 修 8 個 concrete bug（ADB fallback 硬寫 port、ensureDir、Macro Tap release、QemuBackend double-close、proxy GENERIC_ALL、VM handle 原子 claim、bridge CloseHandle leak、LocationSimulator throttle）；tests 20→23。
- **Session 97 (06-30)** — harness `Resolve-EmulatorConsolePort`（auto free pair / even 檢查）+ cmdline-filtered cleanup 全面化。
- **Session 96 (06-30)** — 14-agent 審查修 7 P2（fail-closed throw、even port、README 誠實化、共用 harness、`-GuestVulkan` 補 env〔假對照修正，實測 40.3 effFps〕、cmdline-filtered kill、VT char）；對抗式 verify 否證 3 假陽性。
- **Session 95 (06-30)** — 雙擊改最快路徑（`start-chimera.cmd` = `-Fast -InteractiveFirst`）；GuestVulkan/skiavk 接進正常 boot（**skiavk 接線即 S100 黑屏根因，已移除**）；禁用搶滑鼠測法。
- **Session 94 (06-30)** — gl60 `-HeavyIterations` GLES fill 探針（60→6.6＝SwiftShader 軟體填色牆）；GuestVulkan 預設化評估＝維持 gated；verifier `exit 0` 契約修。
- **Session 93 (06-30)** — stock gRPC `total=0` 真根因＝capture 硬寫 port 8554 → derived port；transferTimeout 解耦 + `hasInFlight()` gate；GrpcOnly 修後 pass。
- **Session 92 (06-29)** — `PostWorker::block()` early-return 補 signal（macOS-only latent）。
- **Session 91 (06-29)** — clean gl60 strict PASS 重現（HWND_TOPMOST + idempotent tick）；競品研究；修 3 個 MSVCP140 future crash → HWUI Vulkan 可渲染（skiavk production 部分被 S100 否定）。
- **Session 90 (06-28)** — 誠實互動量測（`verify-interactive-ui.ps1` + 共用 harness）；priority/cpus/ram env resolver + `CHIMERA_DISPLAY` log；audio churn 結論 helperSpawns=0。
- **Session 89 (06-28)** — 「嚴格可見 120s 60 PASS」（S101 重新定性零幀）；verifier free-port pair + zero-sample fail + host window 釘前景。
- **Session 88 (06-26)** — custom runtime 一般 UI 黑屏修復（`CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1` 關 core-profile shader）；ANGLE draw AV 不作正式路徑。
- **Session 87 (06-24)** — hardware host GLES routing BLOCKED（三條 headless 路徑全擋）；`verify-hardware-ui.ps1`；DLL 還原 verified。
- **Session 85-86 (06-22~23)** — gl60 verifier 首次 PASS + `postFrameDirectGpu`（S101 重新定性：發佈零幀）；main.cpp 修 hardcoded runtime ports；一鍵啟動器。
- **Session 83-84 (06-22)** — `CreateSharedHandle` 改 `DXGI_SHARED_RESOURCE_READ|WRITE`（解 E_INVALIDARG）。
- Session 82 (06-19) — shmem consumer ceiling ~50fps + `BELOW_NORMAL` 移除 EcoQoS（7→24fps）。
- Session 81 (06-19) — shmem delivery 路徑確認。
- Session 80 (06-18) — Vulkan loader 調查收斂（harness `-gpu swiftshader_indirect` 污染）。
- Session 76-79 (06-16~17) — MSVCP140 promise crash 改 Lock+CV → headless boot；shmem 管道打通；音訊啟用；GrpcOnly verifier；screenrecord 死路。
- Session 70-75 (06-13~14) — port cleanup、bridge diagnostics + 1080p floor、proxy probe/ABI fix、GrpcOnly mode、ABI 不相容實測、proxy frame capture 死路定案。
- Session 61-69 (06-05~13) — RenderLib/proxy probe、bad-runtime gate、單視窗+低干擾、可見視窗雙 gate、strict fail-closed、snapshot I/O 收斂。
- Session 31-60 (05-31~06-05) — headless-only gate、ABI gate、runtime build、EmuGL bridge、D3D11 producer、window-capture rollback。
- Session 13-30 (05-21~27) — gRPC 60fps 穩定 + orphan qemu 根因、Quick Boot、1080p + clickable input、truthful FPS、Chimera Launcher、host audio mitigation、wheel jank、shared memory/D3D11 renderer。
