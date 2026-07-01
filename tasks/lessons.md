# Chimera Lessons

## 2026-07-01 — general-UI 60 達成（真實輸入路徑）：瓶頸是 HOST 端，不是 guest gfxstream；兩個 host 改動，零 DLL 重建

- **結論翻轉**：跨 ~15 session 認定「general-UI 60 卡在 guest SurfaceFlinger/gfxstream frame-pacing、屬深層 R&D」是**錯的**。真實輸入路徑實測，瓶頸全在 **host 端**，用兩個 host-only 改動就達 60（**沒動 gfxstream DLL**）。`verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -SyntheticScroll` 的**誠實 strict gate** 印出 `result=pass-gpu-direct-60`：`guestFps=60.1 / streamFps=59.6 / renderFps=59.8 / effFps=59.6 / effMin=58.1 / dup=0`，**重現 2×**（59.6/58.1、59.6/58.2）。gl60 連續渲染 60 非回歸（`min 59.9/avg 60.0`）、ctest 23/23、0 residual。
- **修法 1：`QSG_RENDER_LOOP=threaded`（main.cpp，QGuiApplication 前設）**。root cause＝**host→guest backpressure**：Qt Quick 落到 basic（GUI-thread）render loop 時，scroll 期間 GUI thread 的工作拖慢 host 消費 shared texture → gfxstream 對 guest 施加 backpressure → guest 跳過 vsync → **guest cadence 被 host 拖到 55.9**。強制 threaded render loop（自有 render thread）後 guest 立刻升到 60.1。**教訓：guest cadence 低不一定是 guest 慢；先排除 host 消費端 backpressure。** Session 95 已量到 guest 每幀只 7–9ms（~110fps capable）＝guest 是被 pacing/backpressure 限制，非 compute。
- **修法 2：GuestDisplay present timer 16ms→200ms**。root cause＝**GUI-thread contention**：`onFrameReceived`（streamFps 計數）在 GUI-thread slot 處理 queued per-frame texture 訊號（main.cpp:1339）；一個 62Hz present timer 的 `update()` wakeups 與這些 per-frame 訊號搶 GUI thread → 只處理 ~54/59.4 幀。present timer 對 shared-texture path 是**冗餘**（event-driven `setSharedD3D11Texture→update()` 已 per-frame 驅動；timer 只需當 idle 安全網）。降到 200ms 後 stream/render 升到 59.6/59.8。**我先前記「present timer 不是 host gap 主因（Qt coalesce render）」是錯的**——它確實不多產生 *render*，但它的 GUI-thread wakeups 排擠 *frame-signal 處理*（streamFps），那才是 min。
- **量測前提**：用 host 內部 `CHIMERA_SYNTHETIC_SCROLL` fling 注入器走 production 輸入路徑（`InputBridge::onTouchPoint→gRPC sendTouch`）量的，**不搶實體滑鼠**。這是唯一能同時「真實路徑 + 不碰滑鼠 + 可重現」的方法；adb-swipe 完全量不到（0）、host mouse-drag 會搶滑鼠。**先修量測（synthetic 注入器）才看得到真實瓶頸並驗證修復**。
- **殘留 caveat**：custom gfxstream runtime 仍偶發 black-boot（本輪一次 boot ADB screencap 13KB near-black，非本次 host 改動造成——host present timer 不影響 guest 的 ADB screencap；屬既有 intermittent boot flakiness）。60 的重現需 boot 成功（visible gate 過）。

## 2026-07-01 — general-UI「~20fps not-60」大半是 adb-swipe 量測污染；真實輸入路徑實測 ~53 eff/~56 guest（近 60）

- **`adb input swipe` 不只「≠ 真實手感」，還可能量到 0**：本輪 GuestVulkan 互動 verifier 用 adb swipe 跑 sustained-scroll，`guestFps=0.0 renderFps=0.0 dupPct=0.0`（連 duplicate 都 0＝完全沒幀事件）——adb swipe 根本沒驅動 guest 重繪。過去多個 session 記的「general-UI ~20fps」很大程度是這條非真實路徑的污染數字（對照 Session 95 一次性 host mouse-drag 曾量到 `render=57.4`，但那會搶實體滑鼠）。
- **正解：host 內部 synthetic 輸入注入器（lessons 早就開的處方）**。新增 `CHIMERA_SYNTHETIC_SCROLL`（main.cpp，diagnostics-only、預設關）：在 boot 後用 QTimer 走 **production 輸入路徑** `InputBridge::onTouchPoint → EmulatorGrpcInput::sendTouch`（guest 座標的連續 fling），**完全不呼叫 SendInput/SetCursorPos、不碰實體滑鼠**。`verify-interactive-ui.ps1 -SyntheticScroll` 設此 env 並在 scroll 段跳過 adb swipe。實測 real-path sustained-scroll `guestFps=55.9 / renderFps=52.6 / effFps=52.6 / effMin=51.0 / dup=0 / bottleneck=render`＝**一般 UI 在真實輸入路徑上 ~53 eff/~56 guest，近 60，不是 ~20**。
- **Rule**：任何「general-UI 幾 fps」的結論都要用真實輸入路徑（host synthetic 注入器）量，不能用 adb swipe 下架構結論；adb swipe 只代表測試注入路徑且可能完全不驅動渲染。量到低數字先懷疑量測法，再懷疑管線。
- **剩餘 gap（精準）**：① guest 55.9 < 60（SurfaceFlinger 在 gfxstream marshalling 下每 ~15 幀漏 1 個 vsync）＝guest 側深層 frame-pacing；② render 52.6 < guest 55.9（host 掉 ~3fps）＝host Qt render/vsync timing under load。present timer 16ms **不是** host gap 主因（Qt 把 vsync 間多次 `update()` coalesce 成一次 render，加 timer 不會多 render）。兩個 gap 都小但屬已知硬前緣。

## 2026-07-01 — 同類 bug 要全鏈搜；workflow verify 中斷時主 loop 接手；handle ownership 修法不能製造 double-close

- **一個根因修過一處後，要 grep 同 pattern 的所有 sibling**：Session 93 修了 capture gRPC port 硬寫 8554，但同一段的 `--adb-display-fallback` 仍硬寫 ADB `5555`（Session 98 才抓到）；Session 83-84 修了 production bridge 的 `CreateSharedHandle` `GENERIC_ALL→DXGI_SHARED_RESOURCE_*`，但 `gfxstream_proxy_d3d11.cpp` 漏修。**Rule**：修「硬寫常數 / 錯誤 flag」這類 bug 後，立刻 `grep` 整 repo 同 API/同常數（`CreateSharedHandle`、`waitForExit`、derived-port 公式），把 sibling 一起修，否則下個 session 又抓同一類。
- **PowerShell harness 的 sibling-grep 要涵蓋所有 `*.ps1`，不能只掃「主 verifier」**：Sessions 96-97 把全域 `Get-Process -Name emulator,qemu-system* | kill` 改成 cmdline-filtered，但只修了主 verifier；R&D probe `run-proxy-smoke-test.ps1` 漏網，殘留掃描仍 `Get-Process -Name "emulator","qemu-system*"` + `$rp.Kill()`，會殺到 Android Studio / BlueStacks / 其他 instance（Session 99 才抓到）。**Rule**：修「全域 kill / 硬寫 port」這類 harness bug，grep 範圍是 `scripts\*.ps1` 全部，包含 probe / smoke / 一次性腳本；它們同樣要 dot-source `ChimeraVerifyCommon.ps1` 走 `Get-ChimeraProcesses`，且本來就 `taskkill /T` 過自己 PID tree 的腳本，殘留掃描更不該再全域補刀。順帶刪掉 shadow 共用版的 local `Stop-/Wait-ChimeraProcesses` 冗餘 re-def（drift 源）。
- **`ProcessLauncher::waitForExit` 在 WAIT_OBJECT_0 會 `CloseHandle`，呼叫端不可再關**：這個「wait 順手關 handle」的隱性 ownership 讓 `QemuBackend::stop()`/`onHealthCheck()` double-close；`VirtualMachine` 反而靠它避免 leak（wait 後只 null 不關）。**Rule**：呼叫 `waitForExit` 後只在「回傳 < 0（沒關到）」或「process 已不在、根本沒走 wait」時才 `CloseHandle`；要修 handle leak 時先確認誰已經關過，避免把 leak 修成更糟的 double-close。
- **背景 thread 與 stop() 共享的 handle 要原子 claim，不能只補 CloseHandle**：`VirtualMachine` exit-monitor 偵測 process tree 消失後 `m_processHandle=nullptr` 會 leak handle；但若天真地「加一行 CloseHandle」，會與 stop() 的 close 形成 double-close（兩條 thread 都可能拿到同一非 null 指標）。**Rule**：用 `InterlockedExchangePointer(&m_processHandle, nullptr)` 在每個 dispose 點原子取走，誰拿到非 null 誰負責關；對齊指標的 plain read（state()/processId()）屬 benign，不必為此整個改 `std::atomic` 大改面。
- **無法 runtime 驗證 / dead-code 的 finding 要誠實分級**：`AudioBridge` 的 WASAPI forced-format 無 `AUTOCONVERTPCM` 是真 bug，但 `AudioBridge::instance()` 產線從未被呼叫（emulator 直接路由 Goldfish→WASAPI），改它無法用無音訊裝置驗證又有 cbSize/EXTENSIBLE 風險 → 記錄不改；gfxstream `mStagingBuffer` 死分支同理（移除動 member、DLL 不重建驗不了）。**Rule**：在「真 bug」與「該不該現在改」之間分清；dead/unreachable code 的潛在 bug、或需重建無法驗證的改動，寧可記錄成 latent，不要為了「修好看」塞進不可驗證的改動（尤其音訊這種敏感路徑）。對照已做 RED→GREEN 的可驗證修復，這是誠實邊界而非偷懶。
- **多 agent 審查 workflow 的 verify 階段可能中途吃 session limit**：本輪 8 維 find 跑完、verify 只完成 1 個就斷。**Rule**：workflow 中斷時，find 階段的 finding preview（在 agent transcript 的 StructuredOutput）仍可救；主 loop 要自己逐一開檔做對抗式 verify（預設 REJECTED）+ TDD，不能因為 workflow 沒跑完就只修那 1 個 CONFIRMED，也不能把未驗證 finding 直接當 bug 全修。
- **「無法驗證」要再分清「環境限制」與「不可驗證」；deferred finding 重判前先寫測試實跑一次**（Session 99 更正 Session 98 的兩個 deferral）：① `AudioBridge` 的 WASAPI bug 被判「dead code + 無法驗證」而 deferred，實際 (a) AudioBridge 有 QtTest exe、可在 host build 跑；(b) 真正 bug 比想像嚴重——`GetMixFormat` 多回 `WAVE_FORMAT_EXTENSIBLE`(cbSize=22)，code 改 `wFormatTag=IEEE_FLOAT` 卻沒重設 `cbSize`，shared-mode `Initialize` 對**任何** rate 都失敗（不只非原生）；加 `cbSize=0` + `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|SRC_DEFAULT_QUALITY` 後修好。**寫的測試先 probe 48000/2 判 device 是否存在**：修前連 probe 都失敗 → 測試 `QSKIP("no endpoint")`（看似無音訊裝置）；修後 probe 成功、非原生 22050/mono 也成功 → PASS。**同一 SKIP↔PASS 差異本身就是 RED→GREEN**，且證明「環境沒裝置」的假設是被 bug 偽裝的——不要把 init 失敗直接當「headless 無音訊」結案。② gfxstream `mStagingBuffer` 死分支：**全 repo grep 證明無 `vkCreateBuffer(&mStagingBuffer)`、無非 null 賦值** → 分支恆 false、靜態可證死碼，移除（含 member/destroy/reset）不可能改變 runtime 行為，**靜態證明即驗證**，不屬「為好看塞不可驗證改動」。**Rule**：deferral 前先問「是真的不可驗證，還是只是這個 sandbox 缺裝置/缺 rebuild」；可寫 SKIP-aware 測試的就寫、跑一次看 SKIP↔PASS；純靜態可證死碼用 grep 證明後可安全刪。template（patch script `Copy-TextTemplate` verbatim 複製）的死碼刪除會原樣流入下次 DLL rebuild，不破壞 patch 套用。

## 2026-06-30 — PowerShell harness port/cleanup 必須共用 resolver，不可硬寫 5554 或全域 kill

- **standalone self-test / R&D verifier 不可再硬寫 `emulator-5554` 或清掉 `CHIMERA_EMULATOR_CONSOLE_PORT`**：`start-chimera.ps1 -SelfTest`、`verify-hardware-ui.ps1` 這類腳本若繞過 shared harness，就會重犯 5554/5555 被占用時 false timeout、或 ADB serial 指到錯 VM 的舊 bug。**Rule**：腳本要 dot-source `ChimeraVerifyCommon.ps1`，用 `Resolve-EmulatorConsolePort`（0=auto free pair；非 0 必須 even）後再 derive `emulator-$port` 與 `CHIMERA_EMULATOR_CONSOLE_PORT`。
- **cleanup 不可用 `Get-Process -Name emulator,qemu-system* | Stop-Process -Force` 全域掃射**：這會殺 Android Studio / BlueStacks / 其他實驗 emulator。**Rule**：runtime verifier / self-test 一律用 cmdline-filtered `Get-ChimeraProcesses` / `Stop-ChimeraProcesses` / `Wait-NoChimeraProcesses`；R&D harness 也不能例外，例外就是下一次漂移源。

## 2026-06-30 — 旗標契約要真的設 env；standalone verifier 必須共用 harness

- **驗證旗標要真的設它承諾的 env，否則量到的是假對照**：`verify-interactive-ui.ps1 -GuestVulkan` 文件說「需 `CHIMERA_GUEST_VULKAN=1` 讓 emulator 帶 `-feature Vulkan`」，但實際只設 `CHIMERA_GUEST_VULKAN_HOST_SETUP=0` + runtime `setprop skiavk`，從未設 `CHIMERA_GUEST_VULKAN=1`。結果 standalone 跑時 emulator 沒開 Vulkan feature，`setprop debug.hwui.renderer=skiavk` 靜默退回 GLES，但 `getprop` 仍回 `skiavk` → guest-Vulkan 對照無效。**Rule**：switch 的 help 說會設某 env，就要在該 block 真的設並加入 TouchedEnv 還原；measurement-integrity 比 gating 更隱蔽，要用「實際送進 emulator 的 args / 真實 backend」反查（log 找 `Feature 'Vulkan' (21) is overridden to 'enabled'`），不能只看 getprop 回顯。**實害量化**：修好後 detached 重跑，一般 UI scroll `effFps=40.3 / guest=48.6 / dup=0 / bottleneck=render`——**先前多個 session 記的「GuestVulkan ~20fps」其實是這個 bug 污染**（退 SwiftShader 軟體），真開 Vulkan 約 2×。污染的 verifier 會讓「天花板」結論整個偏低，修 verifier 前不要拿它的數字下架構結論。
- **emulator-boot verifier 必須 detached（獨立 console）跑，否則 long restart 期間吃 `STATUS_CONTROL_C_EXIT`（`0xC000013A`）**：直接用背景 `powershell -File verify…` 仍與工具 console 同 group，GuestVulkan 的 framework restart 拉長視窗時 chimera-ui 會被 console CTRL 事件殺掉（非 Vulkan crash）。**Rule**：用 `Start-Process powershell …`（不加 `-NoNewWindow`，給它新 console）+ 讓 verifier 自己 `*>` 寫 log，再 `WaitForExit`；`0xC000013A` 是 console 控制死亡不是程式 bug，別誤判成 runtime regression。
- **`-RequireSharedTexture` 這類 fail-closed 旗標不可在 runtime 缺失時靜默退回**：`start-chimera.ps1` 舊版 `-RequireSharedTexture` 只在 `customAvailable` 分支生效，runtime 檔缺失時走 else 直接退 stock gRPC——正是 fail-closed 要擋的情況卻被忽略。**Rule**：fail-closed 旗標要在「正要 fallback」的點先 `throw`，不能讓它只在 happy path 生效。
- **standalone verifier 不要重抄 adb/process/port 邏輯**：`verify-quick-boot.ps1` 自存 `Serial="emulator-5554"` 且自抄 `Get-ChimeraProcesses`（只比 process 名、無 cmdline filter）→ ① 5554/5555 被佔用時 false boot timeout（Session 89 已修過的同類）；② cleanup 會 force-kill 機器上**任何** emulator/qemu（含 Android Studio/BlueStacks）。**Rule**：新/舊 verifier 一律 dot-source `ChimeraVerifyCommon.ps1`，用 `Get-FreeEmulatorConsolePort` 挑 port + 設 `CHIMERA_EMULATOR_CONSOLE_PORT` + 用 cmdline-filtered `Get-ChimeraProcesses`；重抄就是漂移源。
- **多 agent 審查要對抗式 verify**：本輪 14-agent audit 中，find 階段報的 P1（boot_completed gate race、`$LASTEXITCODE` 洩漏成 exit 255、CLAUDE.md 歷史段「預設 stock」）被 verify 階段逐一否證（前者是刻意 robustness、第二個實機重現 exit 0、第三個是 dated changelog 非現況）。**Rule**：find 不等於 bug；每個 finding 要獨立開檔對抗式驗證，預設 REJECTED，才不會把設計或歷史紀錄當回歸修壞。

## 2026-06-30 — 真實滑鼠路徑 ≠ adb swipe；不可用 SendInput 搶使用者滑鼠

- **`adb input swipe` 不能代表實際操作流暢度**：Stage 3 量到 `adb` verifier 只有 `effAvg≈20`，但真實 host mouse-drag → Chimera → gRPC `sendTouch` 路徑瞬間量到 `guestMax=116.7 / render=57.4`、dup=0。**Rule**：日常互動性能結論必須區分「adb 測試路徑」與「host input production 路徑」；不能用 adb swipe 的 20fps 直接判定使用者手感。
- **不可用會搶使用者滑鼠的測試**：Win32 `SendInput` / `SetCursorPos` 會移動實體游標，使用者明確要求「不要影響到我的滑鼠」。**Rule**：之後禁止用會搶實體滑鼠的自動化；若要測 host input，必須做不移動使用者游標的工具化路徑（例如 host 內部 synthetic touch/test hook）或先取得明確同意。
- **GuestVulkan/skiavk 需要 HWUI + SurfaceFlinger 一起切 Vulkan 並 framework restart**：只 set `debug.hwui.renderer=skiavk`、不重啟 SF，會出現只剩背景/空畫面；完整路徑要 `debug.renderengine.backend=skiavk` + `debug.hwui.renderer=skiavk` + `stop/start`，再重新 gate 可見畫面。

## 2026-06-30 — 量測「真實負載 60」前先分清 guest backend；md5 非 gate；PS verifier success path 要顯式 exit

- **重 GLES fill 量到的是 SwiftShader 軟體填色牆，不是 host 顯示/post path**（Session 94 item 2）：custom runtime headless 下 guest GLES → host **SwiftShader（軟體）**。gl60 加重 fragment（`-HeavyIterations 96` 全螢幕 plasma）→ 同一 gpu-direct path 60→6.6 fps、dup=0。瓶頸是 guest 軟體填色，非 post path。**Rule**：設計「真實遊戲負載」探針前先確定 guest 繪製跑在哪（GLES=SwiftShader 軟體 / Vulkan=NVIDIA 硬體），否則會把軟體填色慢誤判成顯示路徑慢。GLES heavy 數字只能標成「GLES/SwiftShader fill 天花板」，HW 遊戲負載要走 guest Vulkan 才有意義。
- **dup=0 才能宣稱「guest 真的只產出 N unique fps」**：heavy96 的 6.6 fps dup=0 證明是 guest render cadence 限制（真不重複幀），非 host 重送舊幀。量瓶頸定位時 dup 是關鍵判別。
- **gfxstream DLL 的 md5 不是 runtime gate**：build 非 byte-deterministic，md5 會在功能等價的 rebuild 間漂移（Session 94 部署 md5 `c81d2092…` ≠ doc 記錄）。**Rule**：驗 DLL 健康用 manifest buildId gate + 實測 gpu-direct 60 + guest-Vulkan robustness，不要把 md5 字串當通過條件；docs 記 md5 只作參考。
- **PowerShell verifier 的 documented-success 路徑要 `exit 0` 顯式收尾，不能靠 bare `return`**：`finally` 內的 cleanup native（taskkill/adb，process 已不在時回非零）會洩漏 `$LASTEXITCODE`，讓 baseline/observe/pass 成功卻 exit 255 → 自動化誤判失敗。**Rule**：success 路徑用 `exit 0`，失敗用 `throw`（engine 給 nonzero）；實測確認 `try` 內 `exit` **仍會先跑 `finally`**（cleanup 不被跳過），故安全。

## 2026-06-30 — capture 連線 port 必須用 derived port，不可硬寫；「ADB 有畫面但 gRPC 0 幀」是 port mismatch 的招牌

- **症狀**：可見 gate（ADB `screencap`）拿到正常 Home 畫面，但 `CHIMERA_PERF` 全程 `total=0` / `stream=0`、`grpc_active_samples<2`。容易被誤判成「readback 太慢」或「stock flakiness」。
- **根因**：ADB（console+1）與 gRPC（`8554 + console offset`）是兩條獨立通道。emulator console port 非預設（verifier auto-pick 5560）時，ADB 仍對（serial 已 override），但 capture 若**硬寫** gRPC `8554` 就連到不存在/別的 endpoint → 0 幀。`main.cpp:1124` 已正確 derive `g_runtimeCfg.grpcPort`，但 capture 建構（`GrpcFramebufferCapture`/`GrpcMmapFramebufferCapture`）卻忽略它、硬寫 8554（與 Session 86 的 ADB-port hardcode 同類 bug）。
- **Rule**：任何對 emulator 的連線（capture / input / keyboard / console）都必須從同一個 derived port 公式取值，不可各自硬寫常數。預設 console 5554 → 8554 會「剛好對」而長期遮蔽此 bug；只有非預設 port 才暴露。
- **Rule**：可見性 gate 用 ADB，但 ADB 通就不代表 gRPC display path 通；display-path 驗證要直接看 `total/stream`，不可只靠 ADB 截圖。
- **Rule（歸因紀律）**：把 0-frame 歸因成「負載/延遲」前，先排除「連錯 port / 連不上」這種確定性失敗。確定性 bug（連 0 幀）優先於機率性假說（負載敏感）。

## 2026-06-28 — GL60 synthetic pass ≠ 真實互動 pass；量測工具不可干擾被測對象

- `verify-true-1080p60.ps1` 的 60 FPS PASS 只證明 **synthetic 連續渲染 app（`chimera-gl60-smoke`）** 在 custom gfxstream GPU-direct shared-texture path 達標；它**不能**代表日常 Home/Settings/遊戲互動。一般 SurfaceFlinger UI 走 SwiftShader ES 合成 + CPU readback（`postFrameCpu`），非 `postFrameDirectGpu`，因此非 60。
- **Rule**：日常可用性要用獨立的互動 verifier（`verify-interactive-ui.ps1`）量測真實互動（Home→Settings→sustained scroll→app switch），並**分類實際 display path**（`gpu-direct` / `gpu-shared-cpu-composited` / `grpc-unary` …）。Stock 永遠回報 `result=baseline`（不可宣稱 60）；Fast 只有 `path=gpu-direct` 且 sustained eff≥門檻才 `pass-gpu-direct-60`，否則 `fast-ui-visible-not-60` + root-cause。
- **idle Home 低 FPS 是 push-based 正常行為**（`effective=min(guest,stream,render)`，guest 只在內容改變時渲染；BlueStacks 亦然）。verifier 不可 gate idle FPS，只 gate sustained-scroll 段。
- **量測工具不可干擾被測對象（Heisenberg）**：互動 verifier 初版每秒做 2–3 次 `Get-CimInstance Win32_Process`（會 join command line，極昂貴）取 CPU/churn，足以與 capture threads 搶 CPU。**Rule**：telemetry 改用 per-PID `Get-Process`（無 WMI）+ 2s cadence，昂貴的 CIM adb-children churn 掃描節流到 ~4s。
- **host 互動會 reset gRPC idle throttle，但 `adb input swipe` 不會**（`notifyInputActivity()` 只由 host `InputBridge` 觸發，main.cpp:848）。不過連續 scroll 產生 unique frames 使 `m_duplicateStreak<2`，仍維持 active interval，所以 adb scroll 量到的是真正天花板（1080p readback 吞吐），不是 idle throttle。手動互動情境用 `-Observe` 模式涵蓋。
- **stock gRPC `getScreenshot` 整輪 0 幀（`total=0`）的真正根因是 hardcoded port，非 readback 延遲**（Session 90 的「延遲 > watchdog」歸因已被 Session 93 證偽）：`main.cpp` 建 `GrpcFramebufferCapture` 時硬寫 gRPC port `8554`，但 emulator gRPC port 是 `g_runtimeCfg.grpcPort = 8554 + ((console-5554)/2)*2`。verifier auto-pick console 5560 → emulator 聽 8560、capture 連 8554 → 幀**永遠**到不了。改用 derived port 後 GrpcOnly `result=pass`（grpc_active_samples 0→9）。次要的負載硬化（transfer timeout 與 stall watchdog 解耦、retry timer `hasInFlight()` gate）對「真實慢 readback」有益，但**不是** 0-frame 的根因。custom runtime shared-texture path 不經 gRPC，不受此影響。
- **emulator config 硬寫值可能被 normalizer 蓋掉**：`main.cpp` 曾硬寫 `cfg.cpus=2; ramMB=2048`，但 `InstanceManager::normalizedInstanceConfig` floor 回 `>=4 / >=4096`，所以實際只有 `processPriority` 生效。改成可設定時要記得：env→default resolver 的預設值要等於 normalizer 後的「實際生效值」，否則 `CHIMERA_DISPLAY` log 會誤報。priority 上限被 normalizer + `ProcessLauncher::safePriorityClass` 雙層夾到 `normal`。

## 2026-06-28 — verifier 不能過濾 post-warmup 零 FPS，且 emulator port 要檢查 pair

- Android Emulator 的 ADB port 是 console port + 1；本機服務可能只占用 odd ADB port（例如 `5561`），造成 emulator 內部 boot completed，但 ADB server 顯示 `no emulators found`。
- **Rule**：verifier 不可硬寫 console port；必須挑選 console/ADB 兩個 port 都可用的 pair，或在失敗時明確提示 `console+1` 被占用。
- 60 FPS verifier 若把 post-warmup `effective=0` 樣本過濾掉，會製造假 PASS；host render 被 Windows/Qt occlusion throttling 時尤其危險。
- **Rule**：warmup boundary 之後任何 `effective<=0` 都必須 fail；需要可見 host GuestDisplay 的測試，要在量測期間保持 host window 可見/前景。
- stale ColorBuffer miss 可以是 benign lifecycle noise，但高頻 per-frame stderr log 會破壞 FPS；保留首次/低頻 throttled diagnostic，不可在 hot path 每幀打 error。

## 2026-06-27 — 60 FPS verifier 不能只相信前端 FPS

- 前端 `CHIMERA_PERF` / effective FPS 只能證明 host pipeline 有 cadence，不能單獨證明「畫面真的可見」。黑屏、空畫面、或 workload 沒在前景時，仍可能量到看似穩定的 60 FPS。
- 使用者指出「要等畫面跑出來再測試；全黑屏 60fps 很正常」是正確 gate 修正。
- **Rule**：任何 1080p/60 完成證據都必須先通過可見畫面 gate：ADB 確認目標 workload 在前景、實際 `screencap` 拉回、解析度達 1920x1080、PNG 大小合理、像素抽樣非黑且有亮度分布；通過後才 reset/mark warmup boundary 並採樣 FPS。
- 若 gate 只能證明 synthetic app，回報必須明說「連續渲染 workload 達標」，不可外推成 idle Home / push-based UI 也固定 60 FPS。

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

## 標準規範（彙整 2026-05-22 ~ 06-02 lessons，依主題）

**解析度 floor（不偷偷降解析度換 FPS）**
- guest `hw.lcd.width/height`、emulator `-window-size`、capture request、UI `wm size` preset、legacy QEMU/HCS backend、CPU shmem、InstanceManager saved config 全部至少 1920×1080；低於就 clamp 或 reject。960x540/896x504/800x450 已被使用者明確否決。
- 測試要直接驗 emulator args 不出現低解析度，不能只看 gRPC request。

**誠實 FPS**
- 主側欄顯示 `effective=min(guest,stream,render)`；靜止/duplicate 顯示 0 是正確。Stream/Guest/Render/Dup 細節留 HUD/log。
- duplicate frame 只更新 stream metric，不送 QML repaint（省 host 開銷）。
- unary `getScreenshot` 的 `Image.seq` 固定 0，不可當 dirty signal；sampled fingerprint 會低估內容變化；要可靠 dirty signal 或 full-frame fingerprint（成本可接受時）。

**輸入路徑**
- 普通點擊走 emulator gRPC `sendTouch`（Console `event mouse` 回 OK 不代表 launcher 當 tap）；驗證要看 tap 後 foreground package 改變。
- wheel/拖曳照 60Hz frame pacing 合併、throttle ~16ms、單次 swipe request 最小化；`adb input swipe` 每次 spawn shell 會抖動，只當 fallback。輸入只由 `GuestDisplay` 做座標轉換後轉發，window 層直接送會雙送/座標錯。

**Chimera HOME launcher**
- 做真 Android HOME（`com.chimera.launcher`）並 boot completed 後 install/set-home + explicit `am start`，不在 host UI 疊假首頁。固定必要入口（缺套件顯示停用 + fallback Activity），動態只追加 user-installed（排除 TMobile/Setup 系統殘留）。theme 不可 `windowFullscreen`（否則厚黑邊 + status bar 不見）。Google Play 需 `google_apis_playstore` image；file manager 用實際 package（如 `me.zhanghai.android.files`）。驗證用 `uiautomator` tile bounds + screencap。

**host audio 優先於 emulator**
- child 先 suspended 建立 → 套 priority/memory priority/EcoQoS/ignore-timer-resolution → resume；startup 先 `Idle` 高頻重套整棵 tree（覆蓋 qemu child 出生競態），暖機後回 `below_normal`。高 priority 在 InstanceConfig/VirtualMachine/ProcessLauncher 三層封到 ≤ Normal。
- boot completed 前不啟動 gRPC capture；Quick Boot load/save 都 opt-in（`CHIMERA_QUICK_BOOT=1`/`CHIMERA_SAVE_QUICK_BOOT=1`，含 `stop()` 同步 save）；`enableAudio=false` 不掛 `virtio-snd-pci`；raw capture/snapshot I/O/orphan qemu/shared-texture retry 都要盤點。

**shared texture（非 CPU-copy）**
- producer 建 named D3D11 shared handle，consumer 在 render thread `OpenSharedResourceByName` → `QSGD3D11Texture::fromNative()`；persistent texture 逐幀 `UpdateSubresource`，不每幀重建。
- metadata 用 odd/even seqlock（consumer 只收一致 even）；frame event 由 worker 等待，非 UI QTimer。
- 發布 GPU shared texture 後跳過 `m_onPost`/`ColorBuffer::readback()`；`FrameBuffer::post()` 的 sub-window 與 headless 分支都要接。
- opportunistic：沒第一幀讓 gRPC fallback 接手（含 input activity boost）；env `CHIMERA_D3D11_TEXTURE_*`（host）/`CHIMERA_EMUGL_D3D11_TEXTURE_*`（producer）opt-in 自動互補；test 至少建真 D3D11 resource + 第二 device 開啟；producer 自測用 GPU render/clear + 固定 pacing，不用 CPU 全圖填色。

**raw fallback 是診斷非 60fps**
- raw gRPC/MMAP/screenrecord/ADB 只能同次 CLI `--allow-raw-capture-fallback`（env 不生效）；request RGBA8888（RGB888 在 capture 層轉，不丟 format convert 進 render thread）；idle cadence 保守、有輸入才 boost；失敗帶 adb/ffmpeg stderr；都要誠實標「非 60fps 完成」。MMAP stock stream 實測 ~12 FPS。

**custom runtime 需硬證據 / fail-closed**
- standalone DLL 有 marker ≠ 可替換 SDK backend；必須比對 SDK ABI export（如 `gfxstream_backend_set_screen_background`）+ SDK runtime imports + manifest（`VulkanDisplayVkPost`、build id 對齊）。缺則 manifest writer 先刪 stale manifest 並 fail closed，不標 runtime ready。
- `CHIMERA_REQUIRE_*_SHARED_TEXTURE=1` fail closed：runtime 不可用/無第一幀/fallback 啟動都直接失敗，不留空 UI 或假跑。verifier 失敗做 stock 對照 boot（stock 能 boot→問題在 custom ABI/producer）。
- classic `emulator64-x86.exe`+`lib64OpenglRender.dll` 只是 build/probe artifact，非 Android 34 Play production（image 只有 `kernel-ranchu`）；對 classic runtime 不可傳新版 stock flags（`-grpc`/`-window-size`/`-vsync-rate`…）。verifier 鎖 `CHIMERA_EMULATOR_PATH`。
- custom build：Windows 用 `wsl -d Ubuntu-24.04`、qemu subtree CRLF→LF 臨時 copy、需完整 AOSP `prebuilts/gcc`（缺則 fail fast）；Unicode argv 用 `wmain`。

**no native window（headless 強制）**
- native embed 黑屏/破壞 Qt 視窗群組/外漏 toolbar → 只能 opt-in 實驗；`--window-capture`/`--native-embed` 需各自 unsafe flag + `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` + 同次 CLI internal diagnostics session，env 不可單獨放行；嚴格模式失敗不可退 raw gRPC/ADB；Eco 解除最多回 BelowNormal。

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

## 2026-06-22 — 真 60 FPS 的瓶頸是 guest render cadence，不是 host pipeline

- 先前 7–24 FPS 的結論誤導：boot 動畫 / Settings 滾動 / idle Home 都是 push-based，guest 不連續渲染，量到的低 FPS 是 guest 沒在畫，不是 host 撐不住。要驗 host pipeline 上限，必須用連續渲染 workload（`RENDERMODE_CONTINUOUSLY` 的 GL app），否則永遠誤判。
- `chimera-gl60-smoke` 連續渲染後，direct-Vulkan→D3D11 path 立刻 steady 60（min 59.8 / avg 60.0 / dup 0 / avgMs 16.2ms）。瓶頸定位錯誤會浪費好幾個 session 往錯方向（async PBO、GPU-to-GPU zero-copy）優化。
- benchmark 的「真 60」門檻要容許 windowed-counter jitter：真 60 FPS 的 5s 視窗 FPS 會在 58.9–60.2 跳動，硬卡 `min ≥ 60` 永遠 fail。正解是 warmup 排除冷啟 transient + `min ≥ 57 容許 jitter` + `avg ≥ 59 證明 sustained` + `dup ≤ 5%`。

## 2026-06-22 — PowerShell StrictMode + finally：env-restore 不對稱會 mask 結果並洩漏 process

- `Set-StrictMode -Version Latest` 下，`$hash.MissingKey` 會拋「property cannot be found」。verifier `finally` 若 restore 一個沒在 `$savedEnv` save 的 key，會拋例外，**蓋掉 try 區塊真正的結果**，而且因為例外發生在 `Stop-ChimeraProcesses` 之前，cleanup 不會跑 → emulator/qemu 洩漏佔住 ports。
- 規則：savedEnv 的 save-key 與 finally 的 restore-key 必須一對一對稱。改動時用程式化檢查（grep 兩邊 key 集合做 diff），不要靠肉眼。
- 跑完每個 emulator-boot verifier 都要主動檢查殘留 process / ports（5554/5555/5560/8554），不能假設 finally 一定有跑。

## 2026-06-22 — 連續渲染 verifier 的 install 與量測注意事項

- 反覆 install 同 package 會撞 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`（debug keystore 變動造成簽章不符）。install 前先 `adb uninstall`（IgnoreExit）最穩。
- CHIMERA_LOG_PATH 是 `fopen(path,"w")` 持開 + per-message `fflush`；不要在 run 中途 truncate（offset 會錯亂）。要分 warmup/measure window 就用「記錄 warmup 結束時的 perf-sample 數當 boundary」，Assert 只看 boundary 之後的 samples。

## 2026-06-29 — host Qt render 抖到 floor 以下：是視窗 occlusion 節流 + 自誘 hitch，不是 path 回歸

- strict 60fps gate 偶發 `min=54–56 < 57` 或 render→0，**不是** GPU-direct path 壞掉（同 run `guest=stream=60` solid、`postFrameCpu=0`）。根因是 host 端：① Qt Quick render thread 在視窗被其他視窗遮擋時 occlusion-throttle 到 0；② verifier 每 tick 無條件呼叫 `SetWindowPos`+`SetForegroundWindow` 強迫 window-manager 工作，週期性 hitch render thread，單一 ~1s 視窗 FPS 被拉低就讓 min sample 破 floor。
- 從背景 process 呼叫 `SetForegroundWindow` 在 Windows foreground-lock 下不可靠（常靜默失敗）。要讓視窗不被 occlusion 節流，靠的是 **z-order（HWND_TOPMOST）** 而非 foreground；TOPMOST 釘住就算別人有 focus 也不被遮。
- 週期性「保前景」必須 **idempotent**：先用 `GetWindowLongPtr(GWL_EXSTYLE) & WS_EX_TOPMOST` + `IsIconic` 檢查，已 topmost 且非最小化就 return，不要重複 `SetWindowPos`。修正後 min 從 54.1 → 59.5，120s strict gate `result=pass`（min 59.5 / avg 60.0 / dup 0）且 `PWSH_EXIT=0`。
- foreground vs background 啟動差異會造成假失敗：foreground pwsh 啟動的 chimera-ui 與工具共用 console，會收到 Ctrl+C/console-close → `STATUS_CONTROL_C_EXIT (0xC000013A / -1073741510)` 在 boot 前被殺。emulator-boot verifier 一律走 **detached background** 跑，別在 foreground 跑。
- 殘留檢查要分清「誰的 process」：`crashpad_handler` 多半是 Corsair iCUE / 瀏覽器等第三方的，不是 Chimera 洩漏；過濾殘留只比對 `chimera-ui/emulator/qemu-system`，別把無關 crashpad 當成洩漏而誤判或無限等待。
- 另：跑前要清 `*.avd\hardware-qemu.ini.lock` stale lock，否則 emulator 啟動即 crash（boot 前死）。

## 2026-06-29 — Venus/Vulkan 攻堅：emulator -prop ≠ runtime sysprop；app HWUI Vulkan 崩 gfxstream

- **emulator `-prop X=Y` 設的是 `androidboot.X`（kernel cmdline），不是 runtime `debug.*` sysprop**。SurfaceFlinger/HWUI 讀的是 runtime `debug.renderengine.backend` / `debug.hwui.renderer`，所以想切 RenderEngine 用 `-prop` 是 no-op（實測 `getprop debug.renderengine.backend` 回空、log 只見 `androidboot.debug.hwui.renderer`）。正解是 **runtime `adb setprop` + `ctl.restart surfaceflinger`**（app 層要 `am force-stop` 再開）。寫 feature 前先用 `getprop` 確認真的生效，別假設旗標等於生效。
- **量測前確認「被測旗標真的開了」**：先前一輪 end-to-end「skiavk 持平」其實是 `-prop` 沒生效、根本還在 skiagl 的假結果。任何 A/B 都要在 report 印出實際 backend/pipeline（`dumpsys gfxinfo` 的 `Pipeline=Skia (OpenGL|Vulkan)`），否則會拿到 invalid 對照。
- **app HWUI 上 Vulkan 會崩 gfxstream host backend**：`debug.hwui.renderer=skiavk` 讓 app 以 Vulkan 繪製時，gfxstream 崩在 `DeviceOpTracker::PollAndProcessGarbage`（`device_op_tracker.cpp:72`，poll fence/semaphore 的 use-after-free）。guest Vulkan 做 device init（gl60-style）OK，但 HWUI 大量 fence/semaphore 流量觸發 lifetime bug。general-UI 60 的牆精準定位在此。
- **headless emulator 的 VM process 名是 `qemu-system-x86_64-headless.exe`**（不是 `qemu-system-x86_64.exe`）。cleanup / stale-VM 偵測漏了它 → 上一輪 wedged VM 殘留佔住 AVD lock，下一輪 `-avd` 直接 FATAL「Running multiple emulators with the same AVD」。production verifier 用 `qemu-system*.exe` glob 已涵蓋，但臨時腳本用精確名要記得加 `-headless`。
- **emulator-boot 探測一律 detached background 跑 + 全 adb 帶 timeout（Start-Job + Wait-Job）+ force-kill cleanup**（`adb emu kill` 在 device offline 時會 hang）。skiavk/HWUI 實驗會把 guest 弄 offline，沒有 timeout 護欄整個 probe 會卡死。

## 2026-06-29 — gfxstream MSVCP140 std::future crash 是全域反覆出現的，不只 frame_buffer

- Session 76 只在 `frame_buffer.cpp` 修掉 `std::promise<void>` MSVCP140 crash，但 **gfxstream host tree 還有多處** `std::promise`/`std::future`/`std::packaged_task` 會踩同一個雷（本機兩個不相容 MSVCP140.dll 對 `_Associated_state` layout 不一致 → `_Set_value` null-deref）。重度 guest Vulkan（HWUI Vulkan）一驅動就崩。修法統一：`std::promise/shared_future` → `std::shared_ptr<std::atomic<bool>>`；`std::packaged_task<int(...)>`+`future.get()` → `std::function` + `gfxstream::base::Lock + ConditionVariable`（caller block 時用 stack Result + 指標捕捉，免 heap）。
- **注意區分**：`std::async(std::launch::deferred, []{})` 的 deferred future **不踩雷**（`.get()`/`.wait()` inline 跑，不走 `_Set_value`），codebase 已大量安全使用；別盲目改它。危險的只有 `promise::set_value()` 與被 invoke 的 `packaged_task`。
- crash stack 會逐一指路：修一個、重建、重測，下一個 crash stack 指向下一處。比一次盲改所有檔案安全（deferred-async 那些不該動）。
- **codify 進 `apply-chimera-gfxstream-patch.ps1` 後一定要驗證**：拿已改好的 source 跑一次 patch script（`-SourceDir tmp\aosp-github\...\gfxstream`）。`Replace-FirstAvailable` idempotent——replacement 已在檔案就 no-op，**有 typo（含註解差一字、em-dash）就 throw**。EXIT=0 才代表 patch-script 的 replacement 與實際 source byte-match，clean checkout 才能正確套用。
- 改 `sync_thread.cpp`（每條路徑都用的 sync command）後**務必跑 60fps 回歸**；但要先排除 host 桌面 contention（msedge/FPS overlay 會把 render FPS 壓到 ~22–53，old/new DLL 同樣受影響）——量測期間我自己也不能跑並行 pwsh，否則污染 render 量測。

## 2026-06-29 — 「hang」可能其實是沒符號化的 crash；用 llvm-symbolizer 解 raw stack

- Chimera VEH handler 印的是 **raw** `[chimera-gfxstream-crash] stk[NN] <dll>+0x<offset>`（不符號化），跟 emulator 內建 reporter 的 `.cpp, line` 格式不同。**grep crash 不能只比對符號化格式**，否則會把 crash 誤判成「hang」（device offline / TIMEOUT 兩者表象一樣）。
- 解 raw offset：`llvm-symbolizer --obj=<dll> <addr>`，但 addr 要加 PE preferred image base——**x64 DLL 預設 `0x180000000`**（`dumpbin /headers` 的 "image base" 確認），即 `llvm-symbolizer --obj=gfxstream_backend.dll 0x1800bf111`，且 `.pdb` 要在 DLL 旁邊。offset 直接餵（不加 base）會回 `??:0:0`。llvm-symbolizer 在 `…/VC/Tools/MSVC/<ver>/bin/Hostx64/x64/`。
- 重新量 crash 前要先重建出**對應當前 PDB** 的 DLL（offset 隨 build 變），否則符號化對不上。
- gfxstream 的 MSVCP140 future crash 不只前兩處——**threadpool 核心 `WorkerThread.h` 的 `Command::mCompletedPromise` (`std::promise<void>`) 也是**，SyncThread/post/readback/cleanup worker 全踩。修 primitive（`WorkerCompletion`=Lock+CV；`WorkerWaitable` wrapper 保留舊 `.wait()` / `.wait_for()` API，同時內部可 `->signal()`）一次覆蓋所有 worker pool。回傳型別改了要找出 cascade 的 consumer（`frame_buffer.cpp` 有多處用 `std::future<void>` 接 enqueue；其中 `sendPostWorkerCmd` 要回傳 `std::future<void>`，用 `std::async(std::launch::deferred,[w]{w.wait();})` 橋接——deferred-async 在此環境安全、不踩 `_Set_value`）。
- 改 threadpool primitive（每條路徑都用）後必跑 **gl60 60fps 回歸**：確認 `min≥57`（本次 final rebuild 後 `min 59.6/avg 60.0` PASS）+ ctest 20/20 + Fast SelfTest PASS，才算非回歸。

## 2026-06-30 — 用自訂 signal 取代 std::promise，每條 early-return/skip 路徑都要顯式 signal

- `std::promise<void>` 析構而未 `set_value()` 時，等候端的 `future.wait()` 會因 **broken_promise** 自動解除阻塞（這是 STL 免費給的「工作被跳過也算完成」語義）。換成自訂 `WorkerWaitable`（Lock+CV，析構不 auto-complete）後，**這層保護消失**：任何「不排隊就 return」的捷徑路徑若沒手動 `->signal()`，對應的 `.wait()` 會永久卡死。
- 本例：`PostWorker::block()` 在 `m_mainThreadPostingOnly` 時 early-return，舊版靠 promise 析構喚醒 `blockPostWorker()` 的 `scheduledSignal.wait()`，新版漏 signal → hang。修法是 return 前補 `scheduledSignal->signal()`。**Chimera Windows 不可達**（`postOnlyOnMainThread()` 只有 `__APPLE__ && !QEMU_NEXT` 為 true，PostWorkerVk/Gl 都傳 false），屬 latent/macOS-only regression，故修 source + codify 進 patch script 即可，**不需重建已驗證的 Windows DLL**（該分支對 Windows 行為 byte-identical）。
- 通則：把 promise/future 換成自訂 primitive 時，grep 出該函式所有 `return;` / `continue;` / 提早結束分支，逐一確認等候端不會因少一次 signal 而卡死。code review 抓 correctness 比「能編譯」重要——這個 HIGH 是 review 在最後一刻抓到的。
