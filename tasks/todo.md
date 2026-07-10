# Chimera Task Todo

---

## 2026-07-10 Session 112 — /goal：修好整個專案讓它真正可投入使用；研究並超越 BlueStacks

目標拆解（依「真正可用」的阻斷程度排序）：

### P0 — 根治 `-Fast` shared-texture producer 停更（S111 只做了 watchdog 保命）
- [x] 讀 gfxstream tree：post 鏈＝guest rcFBPost/compose → `postImpl`（frame_buffer.cpp:3100）→ headless 分支 kVk borrow → `bridge.postFrameDirectGpu`。**發現唯一無 log 的靜默失敗點**：`postImpl` 開頭 `m_colorbuffers` 找不到 posted handle 直接 `return FAIL`（無任何 log）＝guest 持續 post 已被 host 銷毀的 handle 時 producer 永久停更且無診斷痕跡。已補 throttled ERROR（tree + patch script 同步）＋ rebuild runtime。
- [x] 「published sequence=240」非確定性訊號（log 節流 %240，S107/S111 撞同數字是巧合）。
- [x] 直開 emulator 診斷 harness（`diag-producer-stall.ps1`）：注意 **`-gpu swiftshader_indirect` 會把 VK ICD 也拉成 SwiftShader**→bridge D3D11 import 走 null fn ptr＝RIP=0 崩潰（生產是 `-gpu host`，headless GLES 由 DLL 內退 SwiftShader、VK 留 NVIDIA）。baseline（-gpu host）60s 全程健康：seq 240→1200、kvk 100→600、droppedPosts=0、screen hash 變化。
- [x] GLESDynamicVersion 135s 實測**健康**（S109 的 ES3 破壞敘事在 producer 層不成立，可能被 S109b 修復連帶解掉）——ES3 不是重現器。
- [x] 15.5 分鐘 production soak（chimera-ui + synthetic scroll）：hostTotal 453→35089 全程健康、無 stall；但 **`invalidateGl` 對缺失 handle（34/37/39/41）累計 45000 次（~45/s）**＝SF 每幀 `rcBindTexture` 綁到已被 host 銷毀的 layer CB＝CB 生命週期失衡的旁證。
- [x] **RefCountPipe 失衡發現**：DLL 因 cross-DLL FeatureSet no-op 把 `m_refCountPipeEnabled` 硬編 false，但 emulator `advancedFeatures.ini RefCountPipe=on` 且 guest gralloc 走 pipe 模式（不送 rcOpen/Close）→ legacy refcount 機制對不參與的 guest 亂記帳（post-O create refcount=0、headless post 每幀瞬時歸 0 進 10s delayed-close）。已修：`Impl::Create` force `m_refCountPipeEnabled=true`（kill switch `CHIMERA_GFXSTREAM_NO_REFCOUNT_PIPE=1`）＋patch script 同步。
- [x] **確定性重現達成（重大）**：直開 emulator（-gpu host）→ boot 後有一段 ~10-20s 完全靜止窗 → **producer 永久停更**（3 輪 idle repro 全中：seq 卡住、guest screencap/focus 完全健康＝S111 現場複製）。droppedPosts=0＝post 指令根本沒到 postImpl（否證「handle 消失被丟」形態）。forensics：apps 持續渲染（app_time_stats 活躍）、SF 有 latch、SystemUI swap 曾卡 16s、SF `pendingHwVsyncState=Disabled`＝**display present（rcFBPost）鏈死掉**，疑 display chain fence/vsync 卡住。
- [x] 判別完成：killswitch A/B 顯示 rcFBPost/postImpl **照走**、seq/kvk 凍結＝publish 段死（非 guest 停送）；fix 模式三計數全程 lockstep。
- [x] **修根因 RED→GREEN 定案**：未修 3/3 停更；fix 3/3 GREEN；**同 build kill switch（`CHIMERA_GFXSTREAM_NO_REFCOUNT_PIPE=1`）2/2 RED＝A/B 因果證明**。patch script 同步＋冪等驗證（首版手改與 script 不一致造成 block 重複＋em-dash 編碼壞掉，已 dedup/ASCII 化並重建乾淨 runtime）。
- [x] 驗證：unit **24/24 PASS**；SelfTest **result=pass**（boot 35s、visible_home 48s、guest+host nonblack 100%、interactivity ok、residual 0）；**idle-recovery gate `pass-idle-recovery`**（production stack 30s 放置×2 輪、恢復操作 host total 118→228→266）＝使用者案發情境直接驗證。
- [x] 使用者插件需求：啟動鈴聲＝guest 充電提示音/解鎖音 → `applyGuestFirstBootSetup` 加 `charging_sounds_enabled=0`（secure+global）、`lockscreen_sounds_enabled=0`、`sound_effects_enabled=0`；chimera-ui rebuild 過、SelfTest 全綠（音效設定為持久 /data，下次 boot 生效）

### P1 — 啟動速度（冷 boot ~36s、visible_home ~49s；Quick Boot 實測 9.5s 但 opt-in）
- [ ] 評估 Quick Boot 設為安全一鍵預設（失敗自動退 full boot）或可靠 opt-in

### P2 — BlueStacks 對照盤點（研究已有 docs/references/competitor-emulator-smoothness.md）
- [ ] 更新競品對照：目前差距清單（guest GPU ✅、輸入 ✅、present ✅；剩：穩定性、啟動速度、日常可用性）
- [ ] 定義「超越」的可量測指標並記錄現狀 baseline

### 完成前驗證
- [ ] ctest 24/24、SelfTest pass、文件同步（CLAUDE/CONTEXT/lessons/todo）

---

---

## 2026-07-06 Session 109 — /goal：迭代優化超越 BlueStacks；3DMark 實測 + 16:9 橫向適配

目標拆解（AVD 已是 1920×1080 landscape 16:9，適配重點在 host 呈現 + 3DMark 實跑）：

- [x] **盤點＋修好啟動**：發現專案已從 `D:\Workspace_cloud\...` 搬到 `D:\Workspace\...`，所有硬編碼路徑失效→emulator FATAL「Broken AVD system path」。修 `configs/android_sdk.json`、AVD `chimera_dev.ini`/`chimera_pie64.ini`、`hardware-qemu.ini`（Workspace_cloud→Workspace）；`main.cpp` 補 `qputenv("ANDROID_HOME", sdk_root)`（原本只設 ANDROID_SDK_ROOT，被 user 層 ANDROID_HOME＝Android Studio SDK 蓋掉，emulator 36+ 優先 ANDROID_HOME）。CMakeCache 重生成（舊 cache 指向 Workspace_cloud）。emulator 恢復可 boot。
- [x] **16:9 橫向適配＝已正確**：AVD `hw.lcd 1920x1080`、`hw.initialOrientation=landscape`＝16:9 橫向；`ChimeraWindow.qml` `guestAspect: 16/9` letterbox 呈現。3DMark 在此 16:9 橫向下正確渲染（guest screencap 佐證，非 portrait 塞中間）。無需改動。
- [x] **3DMark 跑分＝結構性受阻（3DMark 拒絕模擬 GPU）**：所有 benchmark 頁面顯示「Sorry, your device is not compatible with this test」。逐層拆帳：① RAM——把 guest 從 4GB→12GB（`CHIMERA_INTERACTIVE_RAM_MB=12288`）解除「need 8GB」訊息，但仍不相容＝RAM 非唯一 gate。② GL——guest GLES 是 SwiftShader **CPU 軟體**渲染（`ANDROID_EMU_gles_max_version_2` 鎖 ES 2.0；GL 測試需 ES 3.1），即使解鎖也是量 CPU 非 GPU、且極慢。③ Vulkan（真 NVIDIA passthrough）——OMM 明確缺光追 feature；其餘 Vulkan 測試被 **`MyDeviceNotRecognizedException`** 擋：UL 伺服器連得到（`www.futuremark.com` 解析成功）但**拒絕識別此裝置**——`GL_RENDERER="…SwiftShader"` 洩漏是模擬器 + Vulkan device="NVIDIA GeForce RTX 3070 Ti"（桌面 GPU 非行動）。這是 3DMark 反造假結果的伺服器端驗證，非 Chimera bug、非本地可修。
- [x] **GLESDynamicVersion 實驗（否證＋回退）**：guest 被 `advancedFeatures.ini GLESDynamicVersion=off` 鎖在 ES 2.0。試 `-feature GLESDynamicVersion`（VirtualMachine.cpp）拉到 ES 3.0。結果：① **對 3DMark compat 零影響**（名單與改動前逐項相同——證實 blocker 不是 GLES 版本回報而是模擬 GPU 被拒）；② **破壞 -Fast 共享貼圖顯示**——guest 仍正常合成（adb screencap 渲染 Settings 繁中雙欄），但 host 端 producer 凍結（`CHIMERA_PERF total` 卡在 204、effective=0），因 ES3 HWComposer 路徑改變 bridge GL→VK 同步捕捉的 ColorBuffer。**這就是專案原本關掉它的真正原因**。已回退 + code 註解記錄。
- [x] 文件同步：todo/lessons/CONTEXT/CLAUDE/memory

**Review（誠實邊界）**：本輪最大實質產出是**修好被搬移路徑打斷的啟動鏈**（emulator 根本無法 boot→恢復）。3DMark「測試效能」的目標**在此環境結構性無法達成**：3DMark 以伺服器端裝置驗證 + 本地 GL renderer 探測，主動拒絕模擬/軟體 GPU（SwiftShader GL identity + 桌面 NVIDIA Vulkan device），這是它防止無效跑分上榜的設計。唯一理論路徑＝偽造 GL_RENDERER + Vulkan device 字串騙過驗證＝深、脆、成功率低、且伺服器交叉驗證仍可能擋，屬使用者需拍板的高風險投機。16:9 橫向適配本來就正確（AVD 級）。教訓：接手先驗「專案能不能 boot」（路徑搬移是隱形殺手）；GLESDynamicVersion 與 -Fast 顯示互斥（實測記入）；第三方 app 的相容性 gate 可能是它自己的反作弊而非平台缺陷。

### 2026-07-07 Session 109b — gfxstream Vulkan pNext / mesh shader crash 修復

- [x] **根因**：GravityMark / Vulkan capability query 帶 `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT` 等 pNext；gfxstream size table 認得 struct，但 `reservedunmarshal_extension_struct()` / marshal / transform / deepcopy 不完整，known-size struct 走 default abort 或半套 deepcopy。
- [x] **修法**：`scripts/generate-chimera-vk-ext-handlers.py` 產生 POD extension struct 的 size cases、reserved marshal、regular marshal/unmarshal、**deepcopy** cases；mesh shader EXT 特例保留手寫 transform/deepcopy；`apply-chimera-gfxstream-patch.ps1` 每次 runtime build 自動套用 generator。
- [x] **code review 修正**：補上 code review 抓到的 coverage mismatch（size table 擴了 538 POD structs，但 deepcopy 缺 380 cases）；generator `write()` 改內容相同不覆寫，避免假 mtime 造成大型 cereal TU 無謂 rebuild。
- [x] **驗證**：generator 重跑輸出 `universe 538 POD structs; size+96/+96, reserved+380, marshal+380, deepcopy+380`；`goldfish_vk_deepcopy.cpp` autogen deepcopy cases=380；custom gfxstream runtime rebuild PASS；`chimera-ui` build PASS；`ctest -LE integration` **24/24 PASS**；兩輪 code review 最終 APPROVE。

**Review**：known-size pNext struct 必須五路對稱支援（size / reserved / marshal / unmarshal / deepcopy），只消 abort 或只補首個 crash 點會把問題延後到 stream 對齊或未初始化 pNext payload。GravityMark 跑分本身仍受 3DMark 裝置驗證/反作弊阻擋；本修復解的是 gfxstream 因 capability enumeration abort 的平台 crash。

---

## 2026-07-05 Session 108 — 否證 S107「-Fast 凍結」；一鍵改回 -Fast＋保留 below_normal（修「性能不穩有夠卡」）

使用者回報 stock 路徑「輪到性能不穩了有夠卡」（stock ~10-19fps 本質）。

- [x] **驗證 S107「-Fast ColorBuffer 凍結」前提**：code 檢視全部 `Failed to find ColorBuffer` call site＝非致命 skip（log+return false、有 throttle），不停 producer。
- [x] **60s adb 驅動實測**：producer `kVk` 1→500 穩定、每幀 0.2-1.5ms、零 ColorBuffer 錯誤＝無凍結。
- [x] **150s 出貨組態實測**（-Fast + below_normal + 真實 gRPC synthetic scroll）：producer 1→4800、host `total` ~1:1 緊跟 4593、螢幕 BitBlt hash 30 distinct/58、nonblack 100%；ColorBuffer 錯誤 throttled 出現但全程健康＝無害噪音。**S107 凍結診斷否證**。
- [x] **修法**：`start-chimera.cmd` 一鍵改回 `-Fast`（順、可點），保留不加 `-InteractiveFirst`（below_normal 護 host audio＝S107 音訊修法留用）。
- [x] **端到端驗證**：`-Fast -SelfTest` pass（boot 35s、visible_home 48s、host 視窗 nonblack 100%、Settings 互動 ok、residual 0）。
- [x] docs 同步：CLAUDE.md／CONTEXT.md／lessons.md／todo.md。

### 第二部分 — 輸入 defense-in-depth（gRPC breaker + ADB fallback）

- [x] `EmulatorGrpcInput`：3-strike transport breaker（`isHealthy()`/`recordTransportResult()`）＋ 2s `getStatus` 探測自癒 ＋ **每 POST 2s transferTimeout**（E2E 抓到：hang 型劫持下 reply 永不 finished＝breaker 全盲）。
- [x] `InputBridge`：8 個 gRPC 分支全 gate `grpcUsable()`＝fallback 從死碼變活路。
- [x] **Console pointer 通道實證為幻象並移除**：裸機 telnet 測 `event mouse`/`event send`（MT Type-B/含 BTN_TOUCH/縮放 0..32767 座標）全回 OK 但 guest `getevent` 零 kernel 事件；`event keydown`/`multitouch` 直接 KO。console 只留 clipboard/geo/power；`onTouchPoint`→ADB tap fallback；`onTextInput` gate 改 `isKeyboardReady()`。
- [x] 驗證三層：unit **24/24**（新 `test_emulator_grpc_input.cpp` 含真網路 dead-port 測試）；劫持 E2E **`fallback-pass`**（hang-hijack 8554＋直開 exe：breaker 3-strike 開→點擊走 ADB→guest window 換新＝送達）；健康路徑 **`pass-gpu-direct-60`**（60fps、breaker 未觸發＝零回歸）。

**Review**：S106/S107 的共同結構性缺陷（唯一通道＋fire-and-forget＋fallback 死碼）根治——「有畫面但無法點擊」不論成因從此自癒（降級模式 ADB tap ~200ms 但永遠可點、通道復原自動切回）。E2E 連抓兩個 unit tests 抓不到的缺口（hang-blind、幻象 console）＝災難路徑必須配真實故障注入。

**S108 第一部分 Review**：S107 把 fling-settle 靜止幀（單張 byte-identical）誤讀成顯示凍結——本次自己的偵測器也誤標 2 次 DIVERGENCE、下一 tick 即自行追上＝同一陷阱的活示範。連三個根因被否證（S106 埠、S107 freeze），「無法點擊」原始症狀 0 次受控重現；留 `CHIMERA_GRPC_INPUT_DIAG`＋三指標 liveness 法（producer frameN／host total／螢幕 hash 隨時間對照）備查。改動極小（僅 start-chimera.cmd + docs），免重編。

## 2026-07-04 Session 107 — 「有畫面但無法點擊」拆帳＋音訊雜音〔凍結診斷已被 S108 否證〕

- [x] 否證 S106「埠被搶」：netstat 乾淨、gRPC POST 3138 全 200（`CHIMERA_GRPC_INPUT_DIAG` 新診斷）。
- [x] 輸入路徑端到端證實完好：PostMessage WM_LBUTTONDOWN→GuestDisplay→sendTouch→guest 開 SearchActivity。
- [x] `EmulatorGrpcInput::post` 補錯誤浮出（原 fire-and-forget 靜默吞錯）。
- [x] 音訊雜音：一鍵拿掉 `-InteractiveFirst`→below_normal（S108 保留此修法）。
- [~] ~~「真根因＝-Fast 顯示凍結」＋切 stock~~ → **S108 否證**：誤把 idle-static 當凍結；一鍵已改回 -Fast。

**Review**：輸入取證方法（PostMessage＋行為 oracle）與音訊修法是本輪留下的真資產；凍結診斷是單張截圖陷阱的教訓，已入 lessons。

---

## 2026-07-06 Session 106 — 修「有畫面但完全無法點擊」（gRPC 輸入埠被搶）

使用者回報：啟動後有畫面但完全無法點擊、沒反應。

- [x] **系統化除錯逐邊界拆帳**
  - [x] guest 邊界：headless 直開同一 /data，`input swipe` 下拉 shade + screencap md5 前後比對 + `mCurrentFocus` 變化＝**guest 觸控 dispatch 正常**（0 ANR、focus 在 chimera launcher、locale 存活）。
  - [x] host 靜態分析：`EmulatorGrpcInput` fire-and-forget POST 到 `127.0.0.1:8554`；`m_grpcInput` 恆非 null＝ADB fallback 死碼；顯示走 shared texture 獨立於 gRPC＝埠錯則畫面在點擊全丟。
- [x] **實機重現根因**：占用 8554→emulator 仍回報 gRPC 成功、Windows 具體位址（舊 listener）勝 wildcard→POST 誤路由；兩真 emulator 同埠則第二個 `WSA 10048` bind fail。
- [x] **修法（非破壞、免重編）**：`ChimeraVerifyCommon.ps1` 挑埠加驗衍生 gRPC 埠（`Get-EmulatorGrpcPort`）；`start-chimera.ps1` 正常啟動也自動挑空埠（去 `-SelfTest` 限制）。
- [x] **端到端驗證**：占用 8554→修後自挑 console 5580→gRPC 8580、`sharedTexture=yes`、synthetic scroll `guestFps=60.0 effFps=58.6 dup=0% result=pass-gpu-direct-60`。收尾無 orphan。

**Review**：根因是**輸入通道（gRPC 埠）被搶**而非顯示 bug——顯示/輸入是兩條獨立管道，「畫面會動」不代表輸入通。固定 5554→8554 + 恆非 null 的 `m_grpcInput`（ADB fallback 死碼）+ orphan 卡 8554＝觸控 POST 靜默全丟。修在**所有 caller 匯流的挑埠邏輯**一次（ponytail）：每次啟動挑一組驗證過空閒的埠（含 gRPC 埠），碰撞結構性消失且順帶乾淨多開。未做＝app 端（直開 exe）的 gRPC 健康檢查/ADB 回退（YAGNI，使用者只走 `start-chimera.cmd`）。腳本改動免 C++ 重編，ctest 不受影響。

---

## 2026-07-04 Session 105 — 使用者 8 項體驗/穩定度清單

使用者一次列 8 項（做完再回答 #5/#8）：

1. **滾輪體驗**（滾一格飛到底／被判點擊）
   - [x] 根因＝gRPC wheel `sendTouchSwipe(...,durationMs=0)`＝爆速 fling（ADB fallback 反用 100ms）。修 `InputBridge.cpp`：`kWheelSwipeMs=80`（速度有界）+ throttle `16→90ms`（防同 `kWheelTouchId` 重疊）。build+ctest 23/23。
2. **系統語言全繁中**
   - [x] 非 root 無 CLI 改系統 locale（`-prop`/`setprop persist.*` SELinux 擋、`cmd locale` 僅 per-app）→ Settings UI 自動化設 繁體中文（台灣）；`persist.sys.locale=zh-Hant-TW` 寫入 /data，`am get-config=b+zh+Hant+TW`、UI+launcher 全中文。VirtualMachine.cpp `-prop` 回退（無效 dead code）。
3. **App 時常停止運作**
   - [x] dropbox 找出 Google 相簿 crash-loop＝`Apps may not schedule more than 150 distinct jobs`（持久 job 累積撞上限）。debloat 停用即取消其 job＝根因修復；冷重開 crash buffer 0 FATAL。
4. **個人資料持久化（每次全新）**
   - [x] 實測 marker+locale+停用清單全部跨 `adb reboot` 存活＝/data 本來就持久（config.ini `<temp>` 只是模板佔位符）。「全新」感＝Google 帳號未持久，非資料被清。
5. **新 app 出現在首頁？**（問題）
   - [x] 程式碼確認 YES：`onResume→loadApps` 每次重查 launchable+user-installed。HOME 截圖 GL60 佐證。
6. **精簡不必要 app／後台省記憶體**
   - [x] `pm disable-user` 停 20 個純 bloat（MemFree 153→697MB）；系統整合類（`as`/`settings.intelligence`）留著（執行中停觸發 system_server ANR）。codify `scripts/debloat-guest.ps1`（`-Restore`）。
7. **持續優化性能／刪垃圾**
   - [x] host：刪 stale `tmp/aosp*`/`gfxstream-{build,src,build-system}` 回收 **3.21GB**（保留 active pair）；修 AGENTS.md stale 路徑。guest debloat 同 #6。
8. **儲存硬限還是動態？**（問題，全做完再答）
   - [x] 答＝qcow2 稀疏動態成長只佔實際用量、封頂＝`disk.dataPartition.size`；「只增不減」是 qcow2 特性（與上限無關，離線 `qemu-img convert` 才縮）。
   - [x] 使用者追加：壓縮（8.95→6.0GB、丟 stale snapshot）＋上限 6→128GB。加密 fs（dm-default-key）無法原地擴→`-wipe-data` 重格 128G（確認無帳號/個人 app）→df 126G，重設 launcher/繁中/debloat、冷重開全留存；刪 .bak、新 qcow2 4.07GB。

### Review
- 一次 headless boot（stock emulator + adb）+ 一次冷重開跑完 8 項，全部程式化取證（dropbox trace / `am get-config` / marker 存活 / HOME 截圖 / MemFree delta），非憑感覺。
- 兩個「以為要寫 code 其實不該」的省事：locale 的 `-prop` 實測無效→回退（非 root 唯一路徑是 Settings UI + /data 持久）；task 4「每次全新」實測否證＝資料本來就持久（不是 bug，是帳號態）。
- 一個 mid-course 修正：aggressive debloat 停到系統整合元件觸發 system_server ANR→點 Wait+re-enable 那兩個，保守化只停純使用者 bloat。教訓入 lessons。
- 淨產出：1 個 host code 修（滾輪根因）、guest 端 locale+debloat（持久 /data）、1 個可還原腳本、3.21GB 清理、文件全對齊。push 待使用者確認。

---

## 2026-07-04 Session 104 — 穩定 60fps：全鏈實測定調 + post hot-path 診斷 readback gate

目標（使用者 /goal）：「性能已大幅改善，持續優化讓它更穩定在 60fps」。

### 初始假設（錯，已被自己的量測否證）
- 猜 host Qt swapchain vsync-block 在 144Hz 量化 60fps → effective ~57。
- [x] 量基線（Fast + `CHIMERA_HOST_FRAME_TIMING`，副螢幕）**否證**：每 sample `guest==stream==render`（59.2/59.2/59.2…）、`dupPct=0`、host consumer acquire+copy 恆 0.1ms、avgMs 出現 16.2ms(=61.7fps)——**若被 144Hz vsync 鎖會量化成 6.94ms 倍數，並沒有**。→ host present 1:1 追上 guest，**不是 vsync 量化**。swapInterval-0 不套用。量測救了我，未做無效改動。

### 實測定調（誠實）
- [x] Producer 逐幀：`postFrameDirectGpu total=0.5–1.3ms`（fence 等待僅 0.1–0.4ms，非瓶頸）、`glToVkSync（GL→VK readback）=3.5–8.8ms`（每幀最大塊，SwiftShader(CPU-GL)↔NVIDIA(VK) 不同 device＝必經 CPU round-trip，架構 floor）。guest post 共 ~5–10ms，能撐 60。
- [x] **priority 是唯一可量測的穩定度槓桿**：below_normal（verifier 預設）avgMs 16.2–17.9/maxMs≤48（抖）；normal avgMs 16.1–17.0/maxMs≤34（穩，通過 gpu-direct-60）。**使用者 `start-chimera.cmd` = `-Fast -InteractiveFirst` 已設 `CHIMERA_INTERACTIVE_PRIORITY=normal`＝實際就在穩定側**；我最初的「抖動」是 verifier 預設 below_normal 造成的假象，非使用者體驗。→ 不改 code 預設（使用者已被 `-InteractiveFirst` 覆蓋；改預設是投機且犧牲 audio 值）。
- [x] synthetic-scroll 的 ~370ms 週期凍結＝fling→lift→動量遞減→列表靜止的 **settle idle**（guest 正確不重繪），**測試假象**，非 stall（S102 早已警告）。

### 修改（hot-path 衛生，已 build+驗證）
- [x] `postFrameDirectGpu` 裡 `debugReadbackSharedImage` **每 240 幀(~4s)無條件跑昂貴 GPU readback**（buffer alloc+submit+等 fence+map+free）——S101 零幀診斷遺留在生產 post thread。改用 `CHIMERA_GFXSTREAM_DIAG_READBACK`（預設 off）gate。重建 custom runtime、驗 `vkReadback=0`、仍 `result=pass-gpu-direct-60`（effMin=54）。**誠實：這是正確衛生（移除週期性診斷 GPU round-trip），但 maxMs 未明顯下降＝非 maxMs spike 主因**（主因是 GL→VK/SwiftShader 合成變異＝架構 floor）。

### 定調 + 使用者可行動的最大平滑度來源
- 使用者實際配置（normal priority）**已穩定 ~57–60fps**（render 1:1 追 guest、host consumer 0.1ms）。S102「host present ceiling ~57」在 normal + 動畫關下**不再觀察到**。
- 殘留 sub-60 與 maxMs 27–34 單幀 hitch＝GL→VK readback + SwiftShader 合成變異（架構 floor）；真正 rock-solid 60 需 guest VK-native 合成（skiavk 牆擋，blocked）。
- **144Hz 螢幕上 60fps 本質 2.4× pulldown judder（S103）**；顯示端最大平滑度改善＝若螢幕支援 **120Hz**（2×60＝完美 2:1 pulldown、零 judder）——屬使用者系統設定，非 Chimera code。
- [x] 文件：todo（本檔）/lessons/CONTEXT/CLAUDE。commit（push 待使用者確認）。

### Review
量測 > 假設：一個 vsync 量化假設被逐幀資料當場否證，省下一個會誤導的改動。淨產出＝(1) 移除生產 post hot-path 的週期性診斷 readback（衛生），(2) 全鏈實測定調（使用者配置已穩定 60、瓶頸是 GL→VK 架構 floor 非 host present），(3) 指出 120Hz 顯示模式是使用者側最大平滑度來源。未做投機的 priority 預設變更。

---

## 2026-07-03 Session 103 — 客製化載入畫面（進度條）+ 手勢列閃爍 guest 側排除

使用者回報（S102 修復後）：「性能改善非常多」，但 (1) 底部滑動回主畫面的手勢橫條不斷閃爍，(2) 載入畫面中間 Pixel 圖標想換成進度條、要能客製化。

### Fix 2 — 客製化載入畫面（進度條）✅ 完成
- [x] ChimeraWindow.qml：載入 placeholder 拿掉中間漸層「C」圖標，改 CHIMERA 字標 + 客製 indeterminate 進度條（綠色漸層 pip 掃動）+「正在啟動 Android…」。placeholder 全程覆蓋顯示區＝看不到 guest 預設 Pixel 開機動畫。
- [x] 驗證：`chimera-ui --no-emulator`（guestReady 恆 false→placeholder 持續顯示）PrintWindow 截圖確認進度條、無中間圖標。

### Fix 1 — 手勢列閃爍：定調 host present pulldown（144Hz），修轉場觸發點（動畫關）
- 初始假設（錯，已否證）：`policy_control immersive.navigation` relayout 閃爍。兩測試證 guest 側完全靜態：
  - [x] guest ADB screencap ×7 + host PrintWindow ×14 + post-DWM 螢幕擷取 手勢列區域全 byte-identical。
  - [x] `dumpsys SurfaceFlinger --latency "NavigationBar0#N"` 三階段幀數皆 **0/3s**、強制 immersive 零效果 → `policy_control` 在此 image 無作用。
- 定調：使用者螢幕 **144Hz**（`Win32_VideoController.CurrentRefreshRate=144`）；guest ~57-60fps 內容於 144Hz＝2.4× 不規則 pulldown judder，細白橫條最明顯。內容截圖抓不到（讀合成幀非掃描時序）。
- 使用者回答：閃爍「主要在切換 app／回主畫面時」＝視窗轉場動畫。根因＝`-Fast`（GuestVulkan）把動畫 scale 從 0 開回 1（S99 理由已失效）。
- [x] **修法 main.cpp：移除 GuestVulkan 動畫 re-enable，動畫維持關**（轉場即時、無動畫運動＝無 judder；減少 idle 重繪）。
- [x] 實證（新 build 副螢幕 -Fast）：animation scale ×3 皆 0、HOME→Settings 轉場 distinct-frame=3（即時）、host 視窗 nonblack 100% 無回歸。
- [x] 另計清理：移除 dead `policy_control` 設定。
- [ ] **使用者目視確認**「切換 app／回主畫面」不再閃（present artifact 無法截圖自證）。殘留 idle pulldown＝S102 host-present boundary（全螢幕/present-pacing）。

### Build/驗證
- [x] `cmake --build build --config Release --target chimera-ui` PASS（含動畫關改動）。
- [x] 真 boot（-Fast，副螢幕 DISPLAY2 @ -2560）：boot_completed、動畫 scale 0、轉場即時、host 視窗渲染 nonblack 100%＝無回歸。
- [x] 文件：CLAUDE/CONTEXT/lessons/todo（本檔）。commit（push 待使用者確認閃爍）。

---

## 2026-07-02 Session 102 — 畫面糊修復（Part A）+ 60fps 全鏈拆帳定案（Part B）

### Part A — 畫面糊根因（已實證，修復完成）

使用者回報 S101 修復後「性能顯著改善但畫面糊，1080p 會這麼糊嗎」。producer texture 實證 1920×1080 正確；糊在 host 呈現端：預設視窗扣側欄後 item ~0.65×，縮小 filtering 是 Nearest（丟整行整列像素→文字殘缺）。`QSGSimpleTextureNode` material filtering 預設 Nearest 且 render 時覆寫 per-texture `setFiltering()`——原三處全 no-op。
- [x] node `setFiltering(QSGTexture::Linear)` + 三處 `setRect` 經 `snapRectToDevicePixels()`
- [x] Build + ctest 23/23 + SelfTest PASS；commit `a80dcee`
- 殘餘：縮小本質損失細節（Linear 是柔和非殘缺）；完全銳利需 ≥1:1 或 guest 解析度跟隨視窗。

### Part B — 60fps 不穩：逐段計時拆帳（定案 frame-pacing boundary）

新增全鏈計時逐段取證，推翻先前假設：
- **host consumer 恆 0.1ms（acquire+copy）＝最佳零空間**；guest **34% CPU 非 compute-bound**。
- 互動 scroll 的「stall」是假象：gap `acquire=0.1/copy=0.1`、`worker gap≈paint gap`＝frame 沒被 produce，且貼 720ms gesture cycle＝內容不變 guest 正確不重繪（Settings 太短無法連續 scroll，量測大半 idle gap，高估不穩）。
- gl60 連續負載真相：純 clear 振盪 **53-60/avg 57/avgMs 16.1-17.8/maxMs 24-33（無秒級 stall）**。瓶頸＝每幀 glReadPixels(3-4ms)+2 VK submit+wait 偶超 16.7ms vsync→miss。
- **A/B（`CHIMERA_GFXSTREAM_FORCE_CPU_POST`）**：CPU-direct post（`readToBytes` 3.8-4.6ms）使 guest production 升乾淨 60.0，但 **effective 仍 avg 57.3/min 45.5**——瓶頸位移到 host windowed present（`guest=60 render=55`）。且 CPU-path 無 keyed mutex→tearing race。
- **定案**：~57fps 是 vsync frame-pacing boundary，換 post path 只位移「誰 miss vsync」。真 60 需 guest Vulkan 內容（消 readback，被 skiavk 牆擋）+ host present pacing。

修復/保留項目：
- [x] 全鏈計時：producer 每段 + host `CHIMERA_HOST_FRAME_TIMING`（paintNode acquire/copy/sincePaint、worker event gap）
- [x] `invalidateForVk` 重用 8MB readback buffer（消每幀 zero-init jitter）
- [x] `chimeraPublishFrameToD3D11Texture` 1:1 用 `readToBytes`（省 resizer blit）
- [x] A/B 實測 CPU-direct post（readToBytesScaled 8.6ms → readToBytes 4ms；guest 60、effective 不變）
- [x] patch script sync（readToBytes + 修 include 重複 bug）；`FORCE_CPU_POST`/host-timing 為 tree-only diagnostics
- [x] default VK path `-Fast -SelfTest` PASS 無回歸（host_window_nonblack 100%、luma 716、0 residual）
- [x] 文件：CONTEXT / CLAUDE / lessons / todo / memory

### 後續候選（未指派）
- [ ] guest Vulkan-backed 內容 zero-copy 直通（消 producer readback）——被 skiavk 牆擋，需 CompositorVk 或 root image
- [ ] host present pacing 對齊（windowed DWM vsync miss；fullscreen exclusive 或 present pacing 研究）
- [ ] patch script fresh-clone diff 驗證（本輪只驗 idempotent re-apply + default SelfTest；S101 lesson 要求 fresh clone 確認落地）
- [ ] 空機重測 boot 時間（S102 boot 87-104s 含連續 boot 噪音）

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
