# Project Chimera — CLAUDE.md

> AI 工作階段快速參考。每次重大變更後更新。開發歷程與 per-session 詳細記錄一律在 `CONTEXT.md`，本檔不重複保留。

## 當前狀態（2026-07-07 / Session 109b）

- **Session 109b——gfxstream Vulkan pNext / mesh shader abort 修復**：GravityMark Vulkan capability enumeration 會帶 `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT` 等 pNext；本 gfxstream snapshot 的 size table 已認得大量 extension struct，但 cereal switches 不完整，known-size struct 仍可能在 `reservedunmarshal_extension_struct()` / marshal / deepcopy default abort 或半套處理。新增/擴充 `scripts/generate-chimera-vk-ext-handlers.py`：從 `vulkan_core.h` 對 POD-only extension struct 自動補 `goldfish_vk_extension_struct_size*`、reserved marshal、regular marshal/unmarshal、**deepcopy** cases；mesh shader EXT 特例保留手寫 transform/deepcopy；`apply-chimera-gfxstream-patch.ps1` 每次 runtime build 自動套用。code review 抓到首版 coverage mismatch（size 擴了 538 POD structs、deepcopy 缺 380 cases→alloc 未初始化 pNext payload），已修成同 universe 五路對稱；generator `write()` 內容相同不覆寫，避免假 mtime rebuild。驗證：generator `size+96/+96, reserved+380, marshal+380, deepcopy+380`；custom gfxstream runtime rebuild PASS；`chimera-ui` build PASS；`ctest -LE integration` **24/24 PASS**；二次 code review APPROVE。誠實邊界：此修復解 gfxstream capability enumeration crash；3DMark 跑分仍受其裝置驗證/反作弊拒絕模擬 GPU（S109 前半段）。

- **Session 108（本次）第二部分——輸入自癒（defense-in-depth 完成）**：gRPC 輸入從「唯一通道＋fire-and-forget＋掛了全丟」升級為 **3-strike transport breaker + `getStatus` 探測自癒 + ADB fallback**。① `EmulatorGrpcInput`：連續 3 次 transport 失敗（netErr 或 http≠200；grpc-status 拒絕不算通道死）→ `isHealthy()=false` → 2s `getStatus` 探測（無副作用 RPC）成功即恢復；**每個 POST 加 2s transferTimeout**——E2E 實測抓到的缺口：**hang 型劫持（accept 不回話）會讓 reply 永不 finished、breaker 全盲**，timeout 使掛死＝可數失敗。② `InputBridge` 全部 8 個 gRPC 分支改 gate `grpcUsable()`（=non-null 且 healthy）→ 既有 QMP/ADB fallback 從死碼變活路。③ **Console 從所有 pointer 鏈移除（幻象通道實證）**：本 build console `event mouse`/`event send` 回 OK 但 **guest `getevent` 零 kernel 事件**（縮放 0..32767 座標也無效；`event keydown`/`event multitouch` 直接 KO）＝「Console (5554) 觸控鏈」歷來只驗過語法從未驗過送達；phantom 通道會擋住真正可用的 ADB。console 的 clipboard/geo/power 服務不受影響；keyboard 自有 probe gate；`onTextInput` 改 gate `isKeyboardReady()`（原 `isConnected()` 也是幻象）。④ **驗證三層**：unit 24/24（新 `test-emulator-grpc-input` 含真網路 dead-port 測試）；**劫持 E2E `fallback-pass`**（hang-hijack 8554 + 直開 chimera-ui：2 個犧牲點擊 4 POST timeout→breaker 開→第 3 點擊走 ADB tap→guest window 換新＝送達）；健康路徑回歸 `pass-gpu-direct-60`（gating 零影響）。「有畫面但無法點擊」不論成因從此**結構性自癒**（degraded mode＝ADB tap ~200ms 延遲，但永遠可點）。
- **Session 108 第一部分**：**S107「-Fast 顯示凍結」被否證（連三個錯根因：S106 埠、S107 freeze）；一鍵改回 `-Fast`＋保留 below_normal**。使用者回報 stock 路徑「性能不穩有夠卡」（stock ~10-19fps 本質，非新 bug）→ 重查 -Fast：① 逐 code 檢視 `Failed to find ColorBuffer` 全部 call site（frame_buffer.cpp 4382/4412/4605/5168/5180）＝**非致命**：log+`return false` 跳過該次 invalidate/flush，有 throttle（1/60/每600），不會停 producer。② **雙重實測否證凍結**：(a) 60s adb 驅動——producer `kVk GPU path` 1→500 穩定爬升、`postFrameDirectGpu` 每幀 0.2-1.5ms、零 ColorBuffer 錯誤；(b) **150s 出貨組態**（-Fast + below_normal + `CHIMERA_SYNTHETIC_SCROLL` 真實 gRPC 輸入）——producer 1→**4800**、host `CHIMERA_PERF total` 110→**4593（~1:1 緊跟）**、螢幕 BitBlt hash **30 distinct/58 樣本、nonblack 100%**；ColorBuffer 錯誤有出現（throttled 至 count 5400）但全程照跑＝**確認無害背景噪音**。S107 的「byte-identical凍結」＝把 **fling-settle 靜止幀誤讀成凍結**（本次偵測器同樣誤標 2 次 DIVERGENCE、下一 tick 即自行追上＝同一陷阱的直接示範）。③ **修法**：`start-chimera.cmd` 一鍵預設**改回 `-Fast`**（順、可點），**保留 S107 正確的部分**＝不加 `-InteractiveFirst`（below_normal 護 host audio；S107 輸入路徑端到端證明、gRPC 診斷日誌、挑埠強化皆仍有效）。④ **端到端驗證**：`-Fast -SelfTest` 全過（boot 35s、visible_home 48s、host 視窗 nonblack 100%/spread 716、Settings 互動 ok、residual 0）。⑤ 使用者原始「無法點擊」至今**無法在受控測試重現**——若再發生，用 `CHIMERA_GRPC_INPUT_DIAG` + 本次三指標 liveness 法（producer frameN vs host total vs 螢幕 hash **隨時間對照**）當場拆帳。教訓：**單張 byte-identical 不能證凍結；liveness 必須三指標隨時間看背離**。
- **Session 107（凍結診斷已被 S108 否證；輸入證明與音訊修法仍有效）**：否證 S106「gRPC 埠被搶」（netstat 乾淨、gRPC POST 3138 個全 `http200 grpc-status0`）。**仍有效的部分**：① 輸入路徑端到端證實完好——真實 Windows `WM_LBUTTONDOWN`（PostMessage，不搶實體滑鼠）→`GuestDisplay::mousePressEvent`→`mapToGuest`→`sendTouch`→guest 開 SearchActivity；排除 hit-test/DPI/前景/覆蓋。② `EmulatorGrpcInput` gRPC POST 錯誤浮出＋`CHIMERA_GRPC_INPUT_DIAG` 診斷。③ 音訊雜音修法＝一鍵拿掉 `-InteractiveFirst`→below_normal（S108 保留）。**已否證的部分**：當時判「-Fast 顯示在 ~240 幀後凍結（ColorBuffer 失效停 producer）」並切 stock——S108 證實該判讀把 fling-settle 靜止幀誤讀成凍結、ColorBuffer 錯誤是無害噪音，一鍵已改回 -Fast。詳見 `CONTEXT.md`。
- **Session 106（已否證）**：「有畫面但完全無法點擊」曾誤判為 gRPC 輸入埠 8554 被 orphan 搶（固定埠假設）→ 加了 `start-chimera.ps1` 自挑空埠 + `Get-FreeEmulatorConsolePort` 驗衍生 gRPC 埠（此挑埠強化無害、保留），但**該修法沒解決問題**（S107 證真根因是顯示凍結）。原始逐邊界拆帳：① guest 觸控**正常**（headless 直開同一 /data，注入 swipe→NotificationShade 開、md5 變、BACK 還原；焦點在 `com.chimera.launcher`、0 ANR、locale zh-Hant-TW 存活）＝guest 沒掛。② 輸入路徑實證：`EmulatorGrpcInput` 對 `127.0.0.1:grpcPort` fire-and-forget POST，`m_grpcInput` 一旦接上就**恆非 null→ADB fallback 是死碼**；顯示走 shared texture（獨立於 gRPC）＝**gRPC 埠錯＝畫面在、點擊全丟且無回退**。③ **實機重現**：占用 8554 後開 emulator，log「Started GRPC server at [::]:8554」仍**回報成功**、netstat 同時有 `0.0.0.0:8554`(emulator)＋`127.0.0.1:8554`(舊 listener)＝Windows 具體位址勝 wildcard→chimera 的 loopback POST 打到**舊 listener**＝點擊全失；兩個真 emulator 都 `-grpc 8554`＝第二個 `WSA 10048 bind fail`「Failed to start grpc service」。④ **根因**：`start-chimera.cmd` 正常路徑用**固定 5554→8554**（只有 `-SelfTest` 才挑空埠），且 `Get-FreeEmulatorConsolePort` 只驗 console+adb **沒驗 gRPC 埠**；S105 多次直開 emulator（無 Job Object）留的 orphan 卡住 8554 即觸發。⑤ **修法（非破壞、免重編）**：`ChimeraVerifyCommon.ps1` 挑埠時**加驗衍生 gRPC 埠**（`Get-EmulatorGrpcPort`＝main.cpp 同式 `8554+((c-5554)/2)*2`）；`start-chimera.ps1` 正常啟動也**自動挑空埠**（去掉 `-SelfTest` 條件）＝每次啟動免碰撞、順帶乾淨支援多開。⑥ **端到端驗證**：故意占用 8554→修後啟動自挑 console **5580→gRPC 8580**、`sharedTexture=yes`、synthetic scroll 走真實 gRPC 路徑量到 `guestFps=60.0 effFps=58.6 effMin=57.2 dup=0% result=pass-gpu-direct-60`＝點擊確實進 guest（修前同狀態全丟）。收尾清乾淨無 orphan。誠實邊界：修的是使用者實際路徑（腳本）；直開 `chimera-ui.exe`（無腳本）仍走固定埠＝app 端 gRPC 健康檢查/ADB 回退屬未做的 defense-in-depth（使用者不走此路，YAGNI）。
- **Session 105**：使用者 8 項體驗清單，全部完成並冷重開實測驗證。① **滾輪**：gRPC wheel 走 `sendTouchSwipe(...,durationMs=0)`＝爆速 fling（滾一格到底）+ 可能判點擊 → 改 `kWheelSwipeMs=80`（速度有界）+ throttle `16→90ms`（防同 touchId 重疊）。② **系統繁中**：非 root user image 無 CLI 改系統 locale（`-prop`/`setprop persist.*` 皆 SELinux 擋，`cmd locale` 僅 per-app）→ Settings UI 自動化（搜尋 Chinese→繁體中文→台灣→移除 English），寫 `persist.sys.locale=zh-Hant-TW` 到 /data 持久；VirtualMachine.cpp 的 `-prop` 已回退（無效 dead code）。③ **App 崩潰**：dropbox 找出 Google 相簿反覆崩潰＝`Apps may not schedule more than 150 distinct jobs`（持久 job 累積撞上限）→ debloat 停用即取消其 job＝根因修復（冷重開 crash buffer 0 FATAL）。④ **資料持久**：實測 marker+locale+停用清單全部跨 `adb reboot` 存活＝**/data 本來就持久**（config.ini `<temp>` 只是模板佔位符，看 hardware-qemu.ini 為真）；「全新」感＝Google 帳號沒登入（無 root 登入卡）非資料被清。⑤ **新 app 上首頁＝YES**（`onResume→loadApps` 每次重查；HOME 截圖 GL60 佐證）。⑥ **debloat**：`pm disable-user` 停 20 個純 bloat（MemFree 153→697MB），系統整合類（`as`/`settings.intelligence`）留著（執行中停會觸發 system_server ANR）；codify `scripts/debloat-guest.ps1`（`-Restore` 可還原）。⑦ **清理**：刪 stale `tmp/aosp*`/`gfxstream-{build,src,build-system}` **回收 3.21GB**（保留 active pair）。⑧ **儲存**：qcow2 稀疏動態成長只佔實際用量、封頂＝`disk.dataPartition.size`；「只增不減」是 qcow2 特性（刪檔要離線 `qemu-img convert` 才縮，與上限無關）。**依使用者要求上限 6GB→128GB**：先壓縮（8.95→6.0GB、丟 stale snapshot），但加密 fs（dm-default-key）無法原地擴→ `-wipe-data` 重格成 128G（使用者確認無帳號/個人 app）→ df 126G/1%，重設繁中+debloat+launcher，冷重開全留存；刪舊 .bak，新 qcow2 4.07GB 實體。guest 變更存持久 AVD /data，下次啟動即生效。
- **Session 104**：穩定 60fps。全鏈逐幀實測**否證**「host vsync 量化」假設（每 sample `guest==stream==render`、host consumer 恆 0.1ms、avgMs 出現 16.2ms 非 144Hz 6.94ms 倍數＝host present 1:1 追 guest 非量化）→ swapInterval-0 不套用（量測擋下無效改動）。定調：**使用者一鍵 `start-chimera.cmd`=`-Fast -InteractiveFirst` 已用 normal priority，實際已穩定 ~57–60fps**（`pass-gpu-direct-60`、effMin=54、avgMs 16.5）；我最初量到的抖動是 verifier 預設 below_normal 的假象。瓶頸＝`glToVkSync`（GL→VK readback 3.5–8.8ms，SwiftShader-CPU-GL↔NVIDIA-VK 不同 device 的 CPU round-trip＝架構 floor），非 host present。**改動（hot-path 衛生）**：`postFrameDirectGpu` 的 `debugReadbackSharedImage`（S101 零幀診斷）原每 240 幀(~4s)無條件跑昂貴 GPU readback，改 `CHIMERA_GFXSTREAM_DIAG_READBACK`（預設 off）gate；重建 runtime 驗 `vkReadback=0`、仍 pass。誠實：gate 後 maxMs 未明顯降＝非 spike 主因，屬正確衛生非「修好卡頓」。使用者側最大平滑度＝144Hz 螢幕改 **120Hz**（60fps 完美 2:1 pulldown、零 judder，屬系統設定）。
- **Session 103**：① 載入畫面拿掉中間「C」圖標，改 CHIMERA 字標 + 客製 indeterminate 進度條（`--no-emulator` 截圖驗證，✅ 完成）。② 底部手勢 home handle 閃爍——**guest 內容側已用兩個程式化測試排除**（ADB screencap ×7 + host PrintWindow ×14 byte-identical；`dumpsys SurfaceFlinger --latency` NavigationBar 幀數 0/3s）→ 是 **host present pulldown artifact**：guest ~57-60fps 內容在**使用者的 144Hz 螢幕**上呈現＝2.4× 不規則 pulldown 抖動，細白橫條最明顯。使用者證實**主要在切換 app／回主畫面時**＝視窗轉場動畫的時刻。根因：`-Fast`（`CHIMERA_GUEST_VULKAN`）把 window/transition 動畫 scale 開回 1（base 是 0）→ 60fps 動畫在 144Hz 上 judder。**修法：main.cpp 移除該 re-enable，動畫維持關**（轉場即時、無動畫運動＝無 pulldown 抖動；也減少 idle 無謂重繪）。實證：新 build 開機後 3 個 animation scale 皆 0、HOME→Settings 轉場 distinct frames=3（即時）、host 視窗 nonblack 100%。殘留 idle pulldown（若有）＝S102 host-present boundary，桿子＝全螢幕(F11)/present-pacing。移除的 dead `policy_control` 設定另計為清理。

- **完成度**：BlueStacks Parity Roadmap v3 P0–P4e + 補強 COMPLETE；核心功能同等級（見下方功能清單）。
- **生產引擎**：`emulator.exe`（Google QEMU+WHPX fork）。`--qemu-backend` / `--hcs-backend` / `--cuttlefish` 為 legacy R&D，保留不刪。
- **Tests**：`ctest -LE integration` **23/23 PASS**；3 個 integration tests 需 emulator 運行中。
- **一鍵啟動**：根目錄 `start-chimera.cmd` = `start-chimera.ps1 -Fast`（custom gfxstream shared texture + `CHIMERA_GUEST_VULKAN=1`＝只加 `-feature Vulkan`；priority 預設 below_normal 護 host audio，S108 定案）；`-InteractiveFirst` 換最順（音訊代價）、`-AudioFirst` 最省音訊、`-Stock` 才回 stock gRPC 慢路徑（~4–17 FPS）。啟動時間 `boot≈33s`、`visible_home≈49s`（Session 100 SelfTest；Session 101 量到 87/103s 含連續 boot 噪音，待空機重測）；boot 期間 QML placeholder 由 `AndroidControls.bootReady` 撐到 boot 完成，launcher 以 md5 比對跳過重複 reinstall。
- **顯示路徑（-Fast）**：source-patched gfxstream `postFrameDirectGpu`——GLES 合成內容先 `flushFromGl()+invalidateForVk()` GL→VK 同步 → GPU `recordCopy` blit → D3D11 NT shared texture（Vulkan `D3D11_TEXTURE_BIT`+dedicated import、keyed mutex）→ host `GuestDisplay` `AcquireSync(0)==S_OK` → 私有副本取樣。互動 UI 實測有效 **~43 FPS**（真實可見內容）。
- **Session 101 重大更正**：`-Fast` shared texture 從 Session 85 起發佈的一直是**零幀**（三層 bug：compose 不標 `mGlTexDirty`→kVk image 從未被寫入；`OPAQUE_WIN32` 匯入無 aliasing；consumer 無 AcquireSync）。歷來 gate 只驗 guest ADB screencap + host counters 所以 15 session 全綠——**歷史 GPU-direct「1080p/60 PASS」（S85/89/99）量的是零幀 blit 節奏，不可引用**。三層已修；SelfTest 新增 host 視窗像素 gate（`Get-HostWindowPixelStats`，`host_window_nonblack_pct>=5`）防再犯。另修 emulator idle 自殺：`-idle-grpc-timeout 300` 已移除（shared-texture 顯示不走 gRPC，黑屏無輸入 300s 即自殺）。
- **FPS 誠實邊界（Session 102 全鏈拆帳定案）**：**~57fps 是 vsync 邊緣的 frame-pacing boundary，非單一可修瓶頸**。逐段計時實證：host consumer（`AcquireSync+CopyResource`）恆 **0.1ms**（最佳、零空間）；guest **34% CPU**（非 compute-bound）；瓶頸在每幀 post 付 glReadPixels(3-4ms)+2 次 VK submit+wait，偶爾超 16.7ms vsync→miss。A/B 換 CPU-direct post（`readToBytes` 3.8-4.6ms）使 guest production 升乾淨 60.0，但 effective 仍 ~57（瓶頸位移到 host windowed-DWM present）。真 60 需 guest **Vulkan-backed** 內容（消 readback，被 skiavk 牆擋）**且** host present pacing 對齊。gl60 連續渲染 53-60/avg 57；push-based idle Home 低 FPS 正常。互動 scroll 於短清單大半是 **idle gap（內容不變＝guest 正確不重繪，非 stall）**，不能當不穩定證據。Session 99 的兩個 host 修（`QSG_RENDER_LOOP=threaded`、present timer 200ms）仍有效。任何「可見/60」宣稱必須含 host 視窗像素證據並分清 workload。
- **skiavk UI 切換禁止再試**（Session 100 定案）：playstore user image 無 root，framework restart 必失敗，半套用（HWUI Vulkan + SF SkiaGL）＝app 視窗全黑；三條替代路（root restart / boot-prop / `ctl.restart`）全 probe 實證死路。
- **Headless 邊界**：正常啟動強制 headless / `-no-window`，emulator/qemu tree 外露可見 HWND 即終止。stock HWND/window capture/native embed 是 unsafe diagnostics：需 unsafe CLI 旗標 + `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` + 同次啟動建立的內部 diagnostics session（`CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1`）。Chimera 走 fork/改 Android Emulator + gfxstream/QEMU runtime，不從零重寫 Android VM，正式路徑不多開原生 Emulator 視窗。
- **Harness 紀律**：verifier/self-test 走 `ChimeraVerifyCommon.ps1`（自動挑空 console/ADB port pair、拒 odd console port、cmdline-filtered cleanup 防誤殺非 Chimera emulator）；post-warmup `effective<=0` 直接 fail；adb-swipe 只代表測試注入路徑，真實路徑量測用 `CHIMERA_SYNTHETIC_SCROLL`；禁止會搶實體滑鼠的測法。

## BlueStacks Parity 功能清單（production emulator.exe 路徑，皆 ✅）

| 類別 | 功能 |
|------|------|
| 核心 | Android boot + WHPX；headless 顯示內嵌（無原生彈窗）；Multi-instance 批次啟停 |
| 輸入 | Keyboard/mouse/touch/gamepad(XInput, focus-gated)；Multi-touch (MT evdev Type-B)；IME；FPS 鼠標鎖定；十字準心/自訂游標；Key scheme 匯入匯出；Macro 錄製/播放 |
| 顯示 | Screen resize / DPI / rotation；FPS lock (30/60/90/120)；Performance HUD (FPS/Lat/Drop) |
| App | APK/OBB 安裝；launch/stop/uninstall/clear；釘選常用應用；Chimera Launcher HOME |
| 系統 | Root mode；Device spoofing（5 flagship profiles）；Clipboard 同步；File sharing (push/pull)；網路 Proxy；網速模擬 (GPRS→Full) |
| 模擬 | GPS (geo fix+route)；感應器 (acc/gyro/mag)；震動；電池；Shake (Ctrl+Shift+3)；Rotate (Ctrl+Shift+4) |
| 媒體 | Screen recording + screenshot；Audio (WASAPI) |
| 體驗 | Eco mode (Ctrl+Shift+F)；Boss Key (Ctrl+Shift+X)；Trim Memory (Ctrl+Shift+T)；Mute (Ctrl+Shift+M)；Open Downloads (Ctrl+Shift+6) |

## 架構

```
UI Layer          src/host/ui/           Qt 6 QML 視窗、Dock、設定面板、Input Overlay
Config            src/host/config/       JSON ConfigManager (nlohmann/json)
Input             src/host/input/        InputBridge → gRPC Touch/Key、Console/QMP/ADB；CoordinateMapper；Gamepad；Macro
Graphics          src/host/graphics/     FramebufferCapture (Grpc/Adb/Vnc/SharedMemory/SharedD3D11)；SharedD3D11TexturePublisher；AngleBackend；PerformanceMonitor
Audio             src/host/audio/        WASAPI shared-mode
Instance          src/host/instance/     VM lifecycle；ProcessLauncher；DeviceSpoofer；MemoryTrimmer
Integration       src/host/integration/  ClipboardBridge (CF_UNICODETEXT)；LocationSimulator (geo fix)
Utils             src/common/utils/      Logger；ThreadPool；FileUtils；LowInterferenceProcess
Tests             tests/unit/            23 Qt Test executables
                  tests/integration/     emulator-boot / input-inject / screencap (QSKIP guards)
```

**Input priority chain（S108 更新）**: 滑鼠左鍵/觸控 gRPC `sendTouch`（8554，**3-strike transport breaker gate `grpcUsable()`**，2s POST timeout，`getStatus` 探測自癒）→ HvSocket → QMP → ADB tap；
鍵盤 gRPC `sendKey`（同 breaker gate）→ QMP → ADB。**Console (5554) 已從所有 pointer/文字鏈移除**——本 build `event mouse`/`event send` 回 OK 但 guest getevent 零事件＝幻象通道（S108 實證）；console 只留 clipboard/geo/power 等服務
**Display path**: headless GPU/shared texture transports（正式方向，emulator `-no-window` hidden）→ raw gRPC/MMAP/screenrecord/ADB capture（CLI-only 診斷 `--allow-raw-capture-fallback`，不可當 1080p/60 證據）→ legacy Win32 window embed / window capture（unsafe CLI opt-in only）

## 重要決策（不討論不改）

1. **MSVC only** — MSYS2 GCC 在此機器上 `cc1plus.exe` crash，不嘗試 MinGW
2. **emulator.exe 為生產引擎** — BlueStacks 同等級 (QEMU+WHPX)；`--qemu-backend/--hcs-backend` 是 R&D
3. **ANGLE 動態載入** — `libEGL.dll` + `libGLESv2.dll` via QLibrary；不需要 .lib
4. **AVD 用 `google_apis_playstore`** — Play 支援必要；注意這是 user image（無 root），任何需要 `adb shell stop/start` / root 的 guest 操作結構性不可行（skiavk 教訓）
5. **Port 5554 = Android Console telnet**，不是 JSON QMP（JSON QMP 在 `--qemu-backend` port 4445）
6. **BlueStacks input**: `HD-Bridge-Native.dll` → virtio-input，不是 kernel driver；`BstkDrv.sys` 是 network/filter driver
7. **對 emulator 的所有連線（capture/input/keyboard/console）必須用同一 derived-port 公式**，不可硬寫常數（gRPC = `8554 + console offset`）

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration   # 23/23
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-gfxstream-runtime.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25
```

## Feature Flags

| 變數 | 值 | 預設 | 說明 |
|------|----|------|------|
| `CHIMERA_INPUT_BACKEND` | `console\|adb\|qmp\|auto` | `auto` | auto = 嘗試 Console，不 Ready 則退回 ADB |
| `CHIMERA_PROCESS_LAUNCHER` | `legacy\|native\|auto` | `auto` | legacy = `_popen`；native = `CreateProcessW` |
| `CHIMERA_EMULATOR_PATH` | path | config | 覆蓋 `configs/android_sdk.json` 的 emulator（**指向 emulator.exe 檔非目錄**）；custom runtime 自動 prepend 旁邊 `lib64/`、`lib/` 到 PATH |
| `CHIMERA_GUEST_VULKAN` | `0\|1` | 空 | 設 1 時 emulator 加 `-feature Vulkan`（Vulkan app 直達實體 GPU）。**僅此而已**——skiavk UI 切換已移除且禁止再加 |
| `CHIMERA_INTERACTIVE_PRIORITY` | `idle\|below_normal\|normal` | `below_normal` | emulator process priority。`idle`=audio-first（EcoQoS 降頻）；`normal`=interactive-first（最順、最多音訊競爭）。非法值退 `below_normal`；上限被 normalizer/`safePriorityClass` 夾到 `normal` |
| `CHIMERA_INTERACTIVE_CPUS` / `CHIMERA_INTERACTIVE_RAM_MB` | 正整數 | `4` / `4096` | vCPU / RAM override；`normalizedInstanceConfig` 仍 floor `>=4 / >=4096` |
| `CHIMERA_QUICK_BOOT` / `CHIMERA_SAVE_QUICK_BOOT` | `0\|1` | 空 | 皆 opt-in：載入 / 保存 `chimera_quickboot` snapshot；預設 full boot（帶 `-no-snapstorage`）保護 host audio |
| `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` | 正整數 | `1920` / `1080` | gRPC raw capture 尺寸；低於 1080p 會被 clamp 回，不可降解析度換 FPS |
| `CHIMERA_GRPC_REQUEST_TIMEOUT_MS` | 正整數 | `6000` | 單一 `getScreenshot` transfer timeout，解耦於 stall watchdog（2s）；低於 watchdog 被拒。robustness 旋鈕非 FPS |
| `CHIMERA_GRPC_TRANSPORT` | `unary\|mmap` | `unary` | MMAP 實驗路徑（~29 FPS），不可當 1080p/60 證據 |
| `CHIMERA_VIDEO_TRANSPORT` | `screenrecord` | 空 | ADB H.264 實驗路徑；headless 下 screenrecord 0 bytes（死路），僅診斷 |
| `CHIMERA_LOG_PATH` | path | 空 | Qt message handler 同步寫 log；verifier 解析 `CHIMERA_PERF` 用 |
| `CHIMERA_SYNTHETIC_SCROLL` | `0\|1` | 空 | diagnostics-only：boot 後走 production 輸入路徑（`InputBridge::onTouchPoint→gRPC sendTouch`）連續 fling，不碰實體滑鼠；`verify-interactive-ui.ps1 -SyntheticScroll` 使用 |
| `CHIMERA_VERIFY_WINDOW_ORIGIN` | `"x,y"` | 空 | chimera-ui 啟動即 setPosition（負值=副螢幕）；驗證時避免蓋住使用者畫面 |
| `CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES` | `0\|1` | 空 | custom runtime headless 下關閉 core-profile shader emission（SwiftShader-ES 拒 `#version 330 core`）；`-Fast` 自動設 |
| `CHIMERA_HOST_FRAME_TIMING` | `0\|1` | 空 | diagnostics-only：host 端逐幀計時（GuestDisplay `updatePaintNode` 的 `acquire/copy/sincePaint`、capture worker event gap），只在 >60ms/>5ms 異常時 log。用於分辨 production gap vs consumer stall（Session 102 拆帳工具） |
| `CHIMERA_GFXSTREAM_FORCE_CPU_POST` | `0\|1` | 空 | **experimental/tree-only（未進 patch script、有 tearing 風險）**：headless post 走 CPU 路徑（GL readback→D3D11 UpdateSubresource，無 VK submit+wait、無 keyed mutex）。Session 102 A/B 證實 guest production 升 60 但 effective 不變（瓶頸位移 host present）；預設 off，default VK path（keyed-mutex 同步）不受影響 |
| `CHIMERA_GFXSTREAM_DIAG_READBACK` | `0\|1` | 空（off） | diagnostics-only（Session 104）：開啟 `postFrameDirectGpu` 每 240 幀對共享 image 做 VK readback 驗非零（S101 零幀 bug 用）。預設 off——它是昂貴 GPU round-trip（buffer alloc+submit+等 fence+map+free），留在生產 post hot path 會每 ~4s 造成單幀 hitch。只在除錯零幀時設 1 |
| `CHIMERA_SHMEM_FRAME_NAME` / `CHIMERA_SHMEM_FRAME_EVENT` | Win32 object name | 空 | CPU-copy shared-memory framebuffer backend（seqlock header）；無第一幀時 gRPC fallback |
| `CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE` / `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE` | `0\|1` | 空 | 啟用 / fail-closed 要求 modified gfxstream shared texture transport（等同 CLI `--gfxstream-shared-texture`）；stock 或缺 marker/manifest/SDK ABI/imports 的 DLL 不生效，REQUIRE 下不得回落 raw/stock HWND |
| `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE` / `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE` | `0\|1` | 空 | legacy EmuGL 版同上；strict mode 亦禁 `m_onPost`/`readback()` fallback |
| `CHIMERA_D3D11_TEXTURE_METADATA` / `EVENT`（及 `CHIMERA_EMUGL_*`、`CHIMERA_GFXSTREAM_*` 變體） | Win32 object name | auto opt-in | D3D11 named shared texture metadata；opt-in 時 host 自動同步名稱 |

**CLI 旗標**：`--native-embed` 必須另加 `--allow-unsafe-native-window` + `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`（同次啟動建立 internal diagnostics session）；`--window-capture` 同理需 `--allow-unsafe-window-capture`；殘留 env 只警告不生效。`--no-emulator` 不啟動 emulator；`--gfxstream-shared-texture` / `--emugl-shared-texture` 啟用對應 transport；`--allow-raw-capture-fallback` 才允許 raw gRPC/MMAP/screenrecord/ADB 診斷 fallback（env 版不生效）。

**gfxstream proxy probe**（legacy R&D，已定案死路）：`build-chimera-gfxstream-proxy-runtime.ps1` 建 stock-ABI hook probe；stock headless 純 Vulkan、swapchain 永不呼叫，`sharedTextureProducer=false`，只能當 ABI probe，不可當 1080p/60 證據。

## 已知問題

| 問題 | 狀態 |
|------|------|
| 有畫面但完全無法點擊（看似無反應） | UNREPRODUCIBLE — S108 定案：**兩個歷任根因皆錯**（S106 埠被搶：netstat 乾淨、POST 全 200；S107 -Fast 顯示凍結：150s 出貨組態實測 producer 1→4800、host total ~1:1 緊跟、螢幕 hash 30 distinct＝完全健康，`Failed to find ColorBuffer` 是 throttled 無害噪音）。**輸入路徑端到端證實完好**（S107：真實 WM_LBUTTONDOWN→sendTouch→guest 開 SearchActivity；gRPC 3138 POST 全 200）。受控測試無法重現原始症狀。**S108 defense-in-depth 完成＝不論成因結構性自癒**：gRPC 3-strike breaker（2s POST timeout 抓 hang 型死亡）→ ADB tap fallback → `getStatus` 探測自動復原；劫持 E2E `fallback-pass` 實證（點擊在通道死亡下仍送達 guest）。**若再發生**：設 `CHIMERA_GRPC_INPUT_DIAG` + 三指標 liveness（producer `kVk GPU path` frameN vs `CHIMERA_PERF total` vs 螢幕 BitBlt hash 隨時間對照）當場拆帳，不可只看單張截圖 |
| 底部手勢 home handle 閃爍 | 轉場 case FIX APPLIED（待目視）— Session 103：guest 內容側排除（screencap/PrintWindow byte-identical、SF NavigationBar 幀數 0/3s）＝**host present pulldown**（60fps guest 於 144Hz 螢幕 2.4× judder，細白橫條最明顯）。使用者證實主要在切換 app／回主畫面＝轉場動畫；根因＝`-Fast` 把動畫 scale 開回 1。**修法：移除該 re-enable（動畫維持關）**——動畫 scale 皆 0、轉場即時（distinct frames=3）、無回歸實證。殘留 idle pulldown＝S102 host-present boundary（全螢幕/present-pacing） |
| 畫面糊（1080p texture 縮小顯示文字殘缺） | RESOLVED — Session 102：`QSGSimpleTextureNode` material filtering 預設 Nearest 且 render 時覆寫 per-texture 設定（原 `texture->setFiltering()` 全 no-op）；改 node `setFiltering(Linear)` + letterbox rect snap 到 device-pixel 格。縮小顯示本質損失細節，完全銳利需 ≥1:1 顯示 |
| `-Fast` host 視窗零幀黑屏（S85 起潛伏） | RESOLVED — Session 101 三層修復（`flushFromGl+invalidateForVk` 前置同步、`D3D11_TEXTURE_BIT`+dedicated import、GuestDisplay keyed-mutex acquire+私有副本〔`WAIT_TIMEOUT` 過得了 `SUCCEEDED()`，須 `==S_OK`〕）；SelfTest 新增 host 視窗像素 gate |
| emulator idle 自殺（黑屏「等多久都黑」第二半） | RESOLVED — Session 101 移除 `-idle-grpc-timeout 300` + regression test；orphan 由 Job Object 管 |
| `-Fast` skiavk 半套用黑屏 | RESOLVED — Session 100 移除 skiavk UI 切換（user image 結構性不可行，禁再試）；SelfTest 補 screenshot 內容 gate |
| 載入慢 + boot 期間裸黑無回饋 | RESOLVED — Session 100：`boot≈33s`、`visible_home≈49s`（原 ~80–110s）；placeholder 綁 `bootReady`；Quick Boot 維持 opt-in |
| 背景手把輸入漏進 guest | RESOLVED — Session 100：`GamepadManager` poll 前檢查 `applicationState()==ApplicationActive`（focus-gate） |
| 真 60 FPS | PARTIAL — **Session 104 實測更正：使用者一鍵配置（`-InteractiveFirst`=normal priority）已穩定 ~57–60fps（`pass-gpu-direct-60`、effMin=54、avgMs 16.5、maxMs≤34）**。逐幀否證 S102「host present ceiling」：normal + 動畫關下 render 1:1 追 guest、host consumer 0.1ms、avgMs 非 144Hz vsync 倍數＝**host present 非瓶頸/非量化**。priority 是唯一可量測穩定度槓桿（below_normal 抖 maxMs≤48 是 verifier 預設假象，非使用者體驗）。瓶頸＝`glToVkSync`（GL→VK readback 3.5–8.8ms，SwiftShader-CPU-GL↔NVIDIA-VK CPU round-trip＝架構 floor；`postFrameDirectGpu` 本身僅 0.5–1.3ms，先前「2 submit+wait」歸因過粗）。rock-solid 60 需 guest VK-native 合成（skiavk 牆擋）。互動 scroll 不穩大半是 fling-settle idle（非 stall）。**144Hz 上 60fps 本質 2.4× pulldown judder→顯示端解＝120Hz 模式**。**負載掃描（gl60 heavyIters 0/48/128/256，normal priority）證 frame pacing 對負載不變：每級 `guest==stream==render` lockstep、dup/drop=0、effMin≈effAvg——重 GLES fill 只乾淨降穩定幀率（60→13→5.5，SwiftShader CPU-fill floor）不造成 jitter，極端 256 才熔毀停產；Vulkan 遊戲繞過此牆**。S101「零幀」更正、S99 host 修、S94 GLES fill 牆仍有效 |
| gRPC 截圖慢 / stall / busy-poll | PARTIAL — stock gRPC ~4–17 FPS 為 fallback 本質；watchdog+transferTimeout 解 hang；raw fallback CLI-only；1080p floor 強制。流暢正解是 shared-texture path |
| stock gRPC 整輪 0 幀（`total=0`） | RESOLVED — Session 93 根因＝capture 硬寫 port 8554（須用 derived `g_runtimeCfg.grpcPort`）；次要硬化 transferTimeout 解耦 + `hasInFlight()` gate |
| emulator/qemu 搶 host audio（音樂雜音） | PARTIAL — S107 拿掉一鍵 `-InteractiveFirst`→`below_normal` 預設；S108 改回 -Fast 後**保留 below_normal**（150s 實測 -Fast + below_normal 顯示健康，不需 normal）。要最省音訊加 `-AudioFirst`（idle/EcoQoS），要最順加 `-InteractiveFirst`（換音訊雜音）。helper 走 `LowInterferenceProcess`；調節桿＝priority + readback CPU |
| 滑鼠滾輪「滾一格到底/被判點擊」 | RESOLVED — Session 105：gRPC wheel 走 `sendTouchSwipe(...,durationMs=0)`＝press→終點→放開背對背＝爆速 fling（且無中間 move 可能判 tap）。改 `kWheelSwipeMs=80`（內插 move+終點停留＝速度有界）+ throttle `16→90ms`（>手勢長度防同 `kWheelTouchId` 重疊）。ADB fallback 早就用 100ms |
| App 時常停止運作（背景 crash-loop） | RESOLVED — Session 105：dropbox 找出 Google 相簿反覆崩潰＝`Apps may not schedule more than 150 distinct jobs`（持久 JobScheduler job 跨開機累積撞每-app 上限）。`pm disable-user` 停用即取消其 job＝根因修復；冷重開 crash buffer 0 FATAL。非缺 ARM 橋接（`libndk_translation.so` 在）非 OOM |
| 系統語言非繁中 | RESOLVED — Session 105：非 root user image 無 CLI 改系統 locale（`-prop`/`setprop persist.*` SELinux 擋）→ Settings UI 設 繁體中文（台灣），`persist.sys.locale=zh-Hant-TW` 寫入 /data 持久（`am get-config=b+zh+Hant+TW`）。存於 AVD /data，不需程式碼 |
| 個人資料每次全新 | NOT-A-BUG — Session 105 實測：marker+locale+停用清單全部跨 `adb reboot` 存活＝**/data 本來就持久**（`userdata.useQcow2=true`、`forceColdBoot=false`；config.ini `<temp>` 只是模板佔位符）。「全新」感＝Google 帳號未持久（無 root 登入卡）非資料被清 |
| 精簡不必要 app／省記憶體 | DONE — Session 105：`scripts/debloat-guest.ps1` 停用 20 個純 bloat（Photos/YouTube/Gmail/Maps/Pixel Launcher…，MemFree 153→697MB，`-Restore` 可還原）。系統整合類（`com.google.android.as`/`settings.intelligence`）不停用——執行中停會觸發 system_server ANR |
| 原生 Emulator 視窗外露 / orphan qemu / 雙 VM | RESOLVED — 強制 headless + 可見 HWND watchdog 終止 tree；Job Object kill-on-close；啟動前清 stale port tree |
| FPS 虛報 / 靜止 repaint 開銷 | RESOLVED — 主側欄顯示 effective=min(guest,stream,render)；duplicate frame 不觸發 repaint |
| Chimera Launcher HOME（乾淨首頁/入口/status bar） | RESOLVED — `com.chimera.launcher` 自動 install/set-home；固定四入口+fallback Activity；動態只追加 user-installed；完整 ROM 級精簡仍待後續 |
| 冷開機數十秒 | PARTIAL — Quick Boot opt-in（`CHIMERA_QUICK_BOOT=1` / verifier）；預設 full boot 保護 host audio |
| SurfaceFlinger crash-loop / ADB TCP blocked（`--cuttlefish`） | OPEN — legacy R&D 路徑，不影響生產路徑 |
| emulator `streamScreenshot` 節流/0 幀 | ACCEPTED — 改用 unary `getScreenshot` 管線輪詢 |

## 路徑

| 資源 | 路徑 |
|------|------|
| 專案根目錄 | `D:\Workspace\Personal_Project\chimera\` |
| Build 輸出 | `build\Release\` |
| Qt | `C:\Qt\6.8.3\msvc2022_64\` |
| Android SDK | `third_party\android-sdk\` |
| AVD | `third_party\android-avd\chimera_dev.avd\` |
| Third-party APK cache | `third_party\android-apps\` |
| Instance 設定 | `configs\instances.json` |
| Chimera Launcher source / APK | `tools\chimera-launcher\` / `build\launcher\chimera-launcher.apk` |
| ANGLE headers | `third_party\angle\` |
| gfxstream source tree（手改+patch script 同步） | `tmp\aosp-github\hardware\google\gfxstream\`（ignored） |
| Custom gfxstream runtime 輸出 | `build\chimera-gfxstream-runtime\` |

## 參考文件

| 文件 | 用途 |
|------|------|
| `AGENTS.md` | Build、測試、Git、Coding 標準、疑難排解 |
| `CONTEXT.md` | 開發歷程、session 記錄、bug 修正紀錄 |
| `tasks/todo.md` / `tasks/lessons.md` | 當前任務規劃回顧 / 修正教訓規則 |
| `docs/STATUS.md` | 目前狀態快照與已知限制 |
| `scripts/verify-quick-boot.ps1` | Quick Boot smoke（重建 snapshot、驗秒數與 cleanup） |
| `scripts/verify-true-1080p60.ps1` | 連續渲染 runtime gate（gl60 synthetic，非日常 UI）；`-HeavyIterations N` 量 GLES/SwiftShader fill 天花板。**S101 後 GLES 內容嚴格 60 gate 不再通過（同步成本），任何數字要配 host 視窗像素證據** |
| `scripts/verify-interactive-ui.ps1` | 日常可用性 gate（Home→Settings→scroll→app switch；path 分類 + per-segment metrics + telemetry）；`-SyntheticScroll` 走真實輸入路徑；Stock 永不宣稱 60 |
| `scripts/ChimeraVerifyCommon.ps1` | 共用 harness（port 挑選、adb、screenshot/host-window 像素 gate、`CHIMERA_PERF` 解析、cmdline-filtered cleanup） |
| `scripts/build-chimera-gfxstream-runtime.ps1` / `apply-chimera-gfxstream-patch.ps1` | custom gfxstream runtime build / patch codify（改 script 後必 grep tree 確認落地） |
| `scripts/build-chimera-launcher.ps1` | 建置/簽章 Android HOME launcher APK |
| `docs/ADR-001-shared-folder.md` | SharedFolder 技術選型 ADR |
| `docs/references/competitor-emulator-smoothness.md` | 競品（BlueStacks/LDPlayer/MuMu）平滑度研究 |

**禁止 commit**: BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/)、root 層 ISO/QCOW2/installer、QEMU/debug logs、R&D throwaway scripts、runtime output dirs。

---
*Updated: 2026-07-05 — Session 108（二）：**輸入 defense-in-depth 完成**——gRPC 3-strike breaker（2s POST timeout 抓 hang 型劫持）+ `getStatus` 探測自癒 + ADB fallback；**console pointer 通道實證為幻象**（`event mouse`/`event send` 回 OK 但 getevent 零 kernel 事件）從所有 pointer 鏈移除；劫持 E2E `fallback-pass`、unit 24/24、健康路徑 `pass-gpu-direct-60` 零回歸＝「無法點擊」結構性自癒。Session 108（一）：**否證 S107 的「-Fast 顯示凍結」**（150s 出貨組態實測：producer `kVk` 1→4800、host `CHIMERA_PERF total` ~1:1 緊跟到 4593、螢幕 BitBlt hash 30 distinct/58、`Failed to find ColorBuffer` 全為 throttled 無害噪音——code 檢視證實該錯誤路徑只 skip 單次 invalidate、不停 producer）。S107 誤把 fling-settle 靜止幀當凍結（單張 byte-identical 陷阱）。**一鍵 `start-chimera.cmd` 改回 `-Fast`**（順、可點）＋**保留 below_normal**（護 host audio）；`-Fast -SelfTest` 端到端 pass（boot 35s、host 視窗 nonblack 100%、互動 ok）。「無法點擊」原始症狀受控測試無法重現，留 `CHIMERA_GRPC_INPUT_DIAG`+三指標 liveness 法備查。教訓：liveness 要三指標（producer/host total/螢幕 hash）隨時間看背離，單一快照不算證據。前一輪 S107：否證 S106 埠論；輸入路徑端到端證實完好（WM_LBUTTONDOWN→sendTouch→guest 開 SearchActivity；gRPC 3138 POST 全 200）；音訊雜音修法＝拿掉 -InteractiveFirst（此部分 S108 保留）。前一輪 S105：使用者 8 項體驗清單全完成並冷重開實測（滾輪 0ms→80ms 有界 fling；系統繁中走 Settings UI＝非 root 唯一路徑、`-prop` 回退；app 崩潰根因＝持久 job 累積撞 150 上限、debloat 即修；/data 實測本來就持久；debloat 20 app 省 544MB＋`debloat-guest.ps1`；清 stale build cache 3.21GB；儲存＝壓縮 8.95→6.0GB 後依使用者要求 `-wipe-data` 重格 6→128GB〔加密 fs 無法原地擴〕並重設繁中/debloat/launcher、冷重開全留存）。前一輪 S104：穩定 60fps 全鏈實測——逐幀否證「host vsync 量化」假設（省下無效 swapInterval 改動）；定調使用者一鍵配置（`-InteractiveFirst`=normal priority）已穩定 ~57–60（pass-gpu-direct-60/effMin 54），瓶頸是 GL→VK readback 架構 floor 非 host present；移除生產 post hot-path 每 4s 的診斷 GPU readback（`CHIMERA_GFXSTREAM_DIAG_READBACK` gate，預設 off）；使用者側最大平滑度＝144Hz 改 120Hz。負載掃描（gl60 heavyIters 0/48/128/256）證 frame pacing 對負載不變（lockstep、dup0、effMin≈effAvg）——重 GLES fill 只乾淨降穩定幀率非 jitter（SwiftShader CPU-fill floor），Vulkan 遊戲繞過。前一輪 S103：載入畫面進度條 + 手勢列閃爍定調 host present pulldown（動畫關）。詳見 `CONTEXT.md`。*
