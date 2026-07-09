## 2026-07-09 — Session 111：「點不動」要先證「畫面在不在更新」；fallback 只有在會被觸發時才算存在

- **使用者說「點不動」時，先分「輸入沒送達」與「畫面沒更新」——後者長得一模一樣**。連續三個 session（S106 埠、S107 凍結、S108 breaker）都往輸入方向修，因為沒人在同一次現場同時量三件事。本輪一次量完就結案：host log `sendTouch ... http200 grpc-status0`（送出）＋ guest `adb shell getevent -lt` 收到 `virtio_input_multi_touch_1` 的 `ABS_MT_POSITION/PRESSURE/TRACKING_ID` down/up（送達 kernel）＋ `am start` 能改 guest screencap 但 host `CHIMERA_PERF total` 不動（畫面死）。**Rule**：症狀是「點了沒反應」時，最先做的不是讀輸入碼，而是**用 ADB 從 guest 側製造一個必然的畫面變化**（`am start` Settings），看 host total 有沒有前進；不前進＝顯示問題，讀輸入碼是浪費。
- **`getevent` 是輸入送達的唯一 oracle，且它便宜**。S108 已用它證出 console 是幻象通道，但本輪一開始我仍先去讀 `mapToGuest`/breaker/埠推導。**Rule**：輸入鏈只要能開 adb，第一個動作就是 `getevent`；它一次同時否證「Qt 沒收到」「座標算錯」「埠打歪」「breaker 誤開」四個假設。
- **fallback 的存在性不等於可達性——要驗「它真的會被跑到」**。第一版修法移除了 `!sharedTextureCapture` 的 wiring 排除，live 測也看到 `fallback=grpc-unary` 且 total 前進，看起來成功；但 code review 抓到 retry timer 裡還有第二條 `grpcCapture->stop()`。我的現場沒炸只是因為 gRPC 先拿到第一幀讓 timer 提早 `return`＝**時序僥倖**。**Rule**：改 fallback 時 grep 所有會 `stop()`/停用它的地方（不只你改的那一處）；「live 測有動」可能是別的原因造成的，要能說出它為什麼被觸發。
- **兩個 capture source 不能同時餵同一個 sink**：`GuestDisplay::setFrame()` 清 `m_sharedD3D11TextureName`、`setSharedD3D11Texture()` 清 `m_frame`。原本「第一幀就停 gRPC」不是偷懶，是這個互斥的必要條件。所以正解是 **stall watchdog 二選一切換**，不是「兩條都常駐」。**Rule**：想讓 fallback「一直開著保命」前，先確認 sink 支不支援多來源；不支援就做 watchdog 切換，別讓兩個 source 互相清狀態。
- **「沒有新幀」無法區分 idle 與 dead——任何 stall 門檻都是取捨，要量了才知道**。閒置 Home 本來就不重繪（S102/S103 已記），2s 門檻在閒置時每 2–3 秒誤觸一次（實測 5 次啟停），6s 降到 1 次。**Rule**：做 liveness watchdog 時，先量「健康 idle 的最長無幀間隔」再定門檻，並在文件寫清楚「最壞情況會看到 N 秒舊畫面」；別假裝門檻是根治。
- **subagent 在 worktree 裡讀到的是修補前的樹**：第一個 reviewer 回報「修補不存在」，因為 `isolation: worktree` 給的是乾淨副本。**Rule**：要 review 未提交的工作樹改動，要嘛不開 worktree 隔離，要嘛先 commit；否則 reviewer 會誠實地告訴你「找不到你說的修補」。

## 2026-07-07 — Session 109b：gfxstream known-size pNext struct 必須五路對稱支援；generator 要內容冪等

- **gfxstream pNext extension struct 不能只補 size/marshal/reserved，`deepcopy_extension_struct` 也必須同步補 case**。本輪 GravityMark / Vulkan capability query 先撞 `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT` unknown abort；補 mesh shader + POD autogen 後，code review 抓到更深的洞：generator 把 538 個 POD struct 加進 `goldfish_vk_extension_struct_size*`，但 deepcopy 仍缺其中 380 個 case。這會把原本「size=0 跳過」變成「alloc 了 pNext payload，deepcopy default 直接 return，留下未初始化資料」。**Rule**：在 gfxstream cereal 補 known pNext struct 時，五條路要同一個 universe 對稱：`goldfish_vk_extension_struct_size*`、`reservedunmarshal_extension_struct`、`marshal_extension_struct`、`unmarshal_extension_struct`、`deepcopy_extension_struct`；缺任何一條都不是修好，只是把 crash/錯位延後。
- **不要盲 skip unknown pNext，也不要刪 abort 假裝穩定**。這個 stream 格式沒有未知 struct byte length；不知道 handler 就不能安全跳過，否則讀指標對齊會壞。**Rule**：未知/不支援 struct 預設 fail-fast 是對的；要支援就照 header 欄位順序產生完整 marshal/unmarshal/deepcopy。POD-only 篩選可接受（scalars/fixed arrays；pointer/union/handle 仍跳過），但 size table 只能包含有完整支援的 struct。
- **patch generator 的「idempotent」要同時包含內容與 build 行為**。原 generator 重跑內容相同仍無條件覆寫三個大型 cereal TU，mtime 變動會觸發不必要 rebuild。修成 `write()` 先比對內容，相同不開檔。**Rule**：會被 build script 每次呼叫的 generator，內容相同就不要寫檔；否則增量 build 會被假變更拖慢。

## 2026-07-06 — Session 109：專案搬移打斷啟動鏈（隱形殺手）；GLESDynamicVersion 與 -Fast 顯示互斥；3DMark 拒絕模擬 GPU 是它的反作弊非平台缺陷

- **接手一個「之前能跑」的專案，第一件事驗「現在還能不能 boot」——路徑搬移會靜默打斷一切**。本專案從 `D:\Workspace_cloud\...` 搬到 `D:\Workspace\...`，所有硬編碼絕對路徑失效：`configs/android_sdk.json`（sdk_root/avd_home/emulator/adb）、AVD `*.ini`（`path=`）、`hardware-qemu.ini`（disk paths + `android.avd.home`）、CMakeCache（source dir）。症狀＝emulator `FATAL | Broken AVD system path`。**Rule**：CLAUDE.md 寫的路徑（`路徑` 表）是事實查核點不是裝飾——開工前 grep 舊根目錄字串（`Workspace_cloud`）掃全 repo，config/ini/cache 一起修；別假設「文件說能跑就能跑」。
- **`qputenv("ANDROID_SDK_ROOT")` 不夠——emulator 31+ 優先讀 `ANDROID_HOME`，會被 user 層繼承變數蓋掉**。main.cpp 只設 ANDROID_SDK_ROOT，但使用者 User 環境變數 `ANDROID_HOME=C:\Users\…\AppData\Local\Android\Sdk`（Android Studio）勝出→emulator 去那找 system image→FATAL。修＝同時 `qputenv("ANDROID_HOME", sdk_root)`。**Rule**：設 SDK 根給子行程時 ANDROID_SDK_ROOT + ANDROID_HOME 都要設（後者優先序更高），否則繼承的 IDE 變數會贏。
- **`GLESDynamicVersion` 與 -Fast 共享貼圖顯示互斥——實測記住別再開**。guest 被 `advancedFeatures.ini GLESDynamicVersion=off` 鎖在 GLES 2.0（app `glGetString(GL_VERSION)` 回 "OpenGL ES 2.0"，即使 host EmulationGl 選了 3.0）。`-feature GLESDynamicVersion`（cmdline 覆蓋 ini，log「Feature 'GLESDynamicVersion' (54) is overridden to 'enabled'」）能讓 guest 報 ES 3.0，但**破壞 host 顯示**：guest 自身仍正常合成（`adb screencap` 渲染），但 -Fast bridge 的 host producer 凍結（`CHIMERA_PERF total` 卡死、effective=0）——ES3 HWComposer 合成路徑改變 bridge `flushFromGl/invalidateForVk` 捕捉的 ColorBuffer。**這就是專案 ini 預設 off 的真正原因**。**Rule**：-Fast 路徑下 GLESDynamicVersion 一律 off；判「顯示壞沒壞」用 host `CHIMERA_PERF total` 有無前進，**不是** guest screencap（guest 永遠會渲染，host producer 才是 -Fast 的真相）。
- **第三方 benchmark app 的「device not compatible」可能是它自己的反作弊，不是你的平台缺陷——先分清 gate 在哪一層**。3DMark 所有測試顯示不相容，拆帳：RAM gate（12GB 解除但仍擋）→ GL gate（SwiftShader CPU、ES2）→ **伺服器端 `MyDeviceNotRecognizedException`**（`www.futuremark.com` 連得到但拒絕識別：`GL_RENDERER` 洩漏 "SwiftShader"＝模擬器、Vulkan device="NVIDIA RTX 3070 Ti"＝桌面 GPU 非行動）。3DMark 主動拒絕模擬/軟體 GPU 以防無效跑分上榜。**Rule**：模擬環境跑第三方 benchmark 卡「不相容」，先撈 logcat 找是本地能力 gate（GL/Vulkan feature、RAM）還是伺服器裝置驗證（`*NotRecognized*`）；後者非平台可修（除非偽造完整 GPU 指紋騙過伺服器交叉驗證＝高風險投機，要使用者拍板）。別把第三方 app 的反作弊誤判成 Chimera bug 去瞎改 gfxstream。
- **guest GLES 全程是 SwiftShader CPU 軟體渲染，跑 GL benchmark 量的是 CPU 不是 GPU**。Chimera 真 GPU 只在 Vulkan 路徑（NVIDIA passthrough）。「用 GL 版 3DMark 測 GPU 效能」概念上就錯——即使解鎖 ES3，Sling Shot（GL）測的是 SwiftShader CPU fill。有意義的 GPU 測試只能走 Vulkan（而 Vulkan 測試被 3DMark 裝置驗證擋）。**Rule**：談「GPU 效能」先確認該 workload 真的走 GPU（Vulkan）而非 CPU-GL。

## 2026-07-05 — Session 108：否證 S107「-Fast 凍結」；單張 byte-identical ≠ 凍結；接手「待修」先驗證其前提

- **單張（或兩張）byte-identical 螢幕截圖不能證明「顯示凍結」——idle-static 是常態不是病**。guest 內容不變（fling settle、靜止頁面）→ 正確不重繪 → host 像素當然 identical。S107 就這樣把健康的 -Fast 判成「凍結」切去 stock；本次我自己的偵測器同樣誤標 2 次「DIVERGENCE」，下一 tick 就自行追上。**Rule**：判凍結必須是**時間序列上的三指標背離**——(1) producer 幀計數（emulator stderr `kVk GPU path (frame N)`）持續爬、(2) host `CHIMERA_PERF total` 卻長期不動、(3) 螢幕 BitBlt hash 在**主動驅動 guest 變化下**持續 identical，三者同時成立且跨多個觀測窗才算。任一指標恢復＝不是凍結。
- **接手前一輪的「待修」項，先花 10 分鐘驗證其前提，再決定要不要開工**。S107 留下「-Fast ColorBuffer 凍結，需 deep gfxstream 重建 runtime」——但逐一讀 `Failed to find ColorBuffer` 全部 call site 就會發現它們全是 log+`return false` 跳過單次 invalidate（有 throttle），**沒有任何路徑停 producer**＝敘事在 code 上就站不住；150s 出貨組態實測（producer 1→4800、host total ~1:1、螢幕 hash 30 distinct）徹底否證。省下一次盲目的 gfxstream 重建。**Rule**：CLAUDE.md 的「待修＋根因描述」是假設不是事實；動大刀（重建 runtime/改 hot path）前先用 code 檢視 + 一次受控重現驗證前提。這已是連續第三個被否證的根因（S106 埠、S107 producer freeze）——本專案「無法點擊」症狀至今 0 次受控重現，任何新根因宣稱都要附重現步驟。
- **throttled ERROR log ≠ 故障訊號**。`Failed to find ColorBuffer`（throttled count 到 5400）在 150s 完全健康的 run 裡照樣出現＝guest 對已銷毀 handle 的 invalidate/flush 噪音。**Rule**：把某條 ERROR 當根因前，先讀它的 call site 確認後果（fatal？skip？retry？）＋在健康 baseline run 裡看它出不出現；「錯誤訊息在凍結時出現過」不等於「它造成凍結」（相關≠因果）。
- **診斷腳本樣板**（scratchpad `diag-fast-freeze.ps1`）：MaxFrame（regex emulator stderr 幀標記）＋ HostTotal（`CHIMERA_PERF total`）＋ ScreenHash（`CopyFromScreen` 中央取樣 md5）三函式隨時間對照；emulator stderr 用 `cmd /c "exe 1>out 2>err"` 包起來就拿得到（gfxstream fprintf 都在 stderr）。下次「畫面/輸入死當」直接套。
- **probe「指令回 OK」≠「事件送達」——幻象通道會擋住真 fallback**。console `event mouse`/`event send` probe 全 OK、`isMouseReady()`=true，但 guest `getevent` **零 kernel 事件**（縮放座標也無效）＝本 build console `event` 整組是裝飾品；它站在 fallback 鏈裡就把點擊吃掉、讓實證可用的 ADB 永遠輪不到。**Rule**：任何「通道可用」判定必須至少一次用行為 oracle（getevent / focus 變化）驗過送達；語法級 probe 只能證「服務在聽」，不能證「事件進 guest」。已從 pointer 鏈移除 console，勿再加回。
- **fire-and-forget 通道必須有 per-request timeout，否則 hang 型死亡對 error-counting 隱形**。劫持者只 accept 不回話（TCP backlog）＝reply 永不 finished＝breaker 的失敗計數永遠是 0。`setTransferTimeout(2000)` 讓掛死變成可數的 OperationCanceledError（健康 reply 個位數 ms，2s 只在死亡時觸發）。**Rule**：設計 health breaker 時先問「什麼失敗模式不會產生 callback？」——connection refused 會、hang 不會；沒有 timeout 的 breaker 只防快死不防慢死。
- **breaker/fallback 這類「災難路徑」只有 E2E 抓得到缺陷**：unit 24/24 全綠時，E2E 連抓兩個真缺口（hang-blind、幻象 console）。E2E oracle 設計自身也要防坑：synthetic scroll 的 fallback taps 會灌爆 ADB 佇列（cap 64×~200ms）害 focus oracle 超時＝測試假陰性。**Rule**：災難路徑改動必附一次真實故障注入 E2E（劫持/斷線），oracle 用行為（focus/window token 變化）且等待要涵蓋降級通道的延遲。

## 2026-07-04 — Session 107：「有畫面但無法點擊」真根因＝顯示凍結（非輸入）〔凍結診斷已被 S108 否證；輸入取證方法仍有效〕；FPS≠輸入送達≠畫面 liveness；別在前一輪未驗證的根因上疊修法

- **「pass-gpu-direct-60」量的是 render 節奏，不證明「點擊有送達 guest」也不證明「host 畫面在更新」——這是 S106 修錯的根本原因**。S106 用 synthetic scroll 跑出 `guestFps=60 pass-gpu-direct-60` 就宣稱「點擊確實進 guest」，但那只是 FPS 計數；synthetic scroll 直接呼叫 `InputBridge::onTouchPoint`（繞過 GuestDisplay），且 60fps 可以是 guest 一邊渲染靜態畫面一邊把觸控全丟。**Rule**：驗「輸入有沒有進 guest」要有**行為 oracle**（`mCurrentFocus` 變化 / screencap md5 變 / dumpsys），不是 FPS；驗「畫面有沒有更新」要**對 host 視窗做螢幕 BitBlt（`CopyFromScreen`，抓 DWM 實際合成）在 guest 明確變化前後比對**——PrintWindow 對 D3D11/GPU 合成常抓不到即時幀會誤判。三件事（FPS／輸入送達／畫面 liveness）互相獨立，別用一個代另一個。
- **前一輪的「根因」可能是錯的；接手先對現場重新取證再決定，別直接在它上面疊修法**。S106 結論「gRPC 埠 8554 被搶」——本輪現場 `netstat` 乾淨、`EmulatorGrpcInput` 加診斷日誌後 **3138 個 POST 全 `http200 grpc-status0`**＝根因不成立。真根因是 **-Fast 共享貼圖顯示凍結**（emulator `postFrameDirectGpu` 在 ~seq240 後噴 `Failed to find ColorBuffer` 停止發佈新幀；host `CHIMERA_PERF total` 卡死、螢幕像素 byte-identical）。**Rule**：`CLAUDE.md` 寫「RESOLVED」不等於真的解決；症狀復發時把舊根因當「待否證假設」，用逐邊界注入 + 程式化 oracle 重新拆帳，別因為它有 commit＋通過某 gate 就信。
- **輸入路徑可用 Win32 `PostMessage(WM_LBUTTONDOWN/UP)` 到 chimera-ui HWND 端到端驗，不搶實體滑鼠**：PostMessage 直接塞訊息佇列（繞過 OS hit-test），配合 `EmulatorGrpcInput` 診斷日誌 + guest `mCurrentFocus` 變化，實證「真實滑鼠事件→GuestDisplay→mapToGuest→onMouseButton→sendTouch→guest 開 Activity」全通。要驗實體點擊會不會落到別的視窗，另用 `WM_NCHITTEST`(=HTCLIENT)＋`WindowFromPoint`(=本視窗)＋monitor DPI 比對排除 hit-test/DPI/覆蓋。**Rule**：不能動使用者實體滑鼠時，PostMessage + 行為 oracle 是輸入路徑的合法端到端測法；區分「軟體路徑通不通」與「OS 有沒有把實體點擊路由進來」用不同 API。
- **一鍵預設要對使用者實際痛點負責：不可用的「smooth」不如可用的「slow」**。-Fast 顯示會凍結＝畫面死當；stock gRPC 顯示實測 live、可正常點擊（~10-19fps）。使用者選「先切 stock＋再修 -Fast」＝`start-chimera.cmd` 拿掉 `-Fast`。同時拿掉 `-InteractiveFirst`（normal priority＝最傷 host audio、stock 慢路徑用它幾乎沒 FPS 好處）順手修「開模擬器音樂雜音」。**Rule**：一鍵預設選「當下實測可用且不傷使用者其他體驗（音訊）」的組合，實驗性 smooth 路徑留成 opt-in flag，待其根因修好再考慮改回預設。

## 2026-07-06 — Session 106（根因已被 S107 否證，挑埠強化保留）：「有畫面但無法點擊」誤判為輸入通道（gRPC 埠）被搶

- **「有畫面但完全無法點擊」先分「顯示邊界」與「輸入邊界」——它們是兩條獨立管道**。Chimera 顯示走 shared texture、輸入走 gRPC；顯示正常**不代表**輸入正常。除錯順序：① 先證 guest 觸控本身活著（headless 直開同一 /data，`input swipe` 下拉 shade + screencap md5 前後比對 + `mCurrentFocus` 變化＝guest dispatch OK），把問題夾到 host→guest 路徑；② 再查 host 輸入路徑。**Rule**：別因為「畫面會動」就假設輸入該通、也別一頭鑽顯示碼；逐邊界注入 + 程式化 oracle（md5/焦點/dumpsys）拆帳。
- **`m_grpcInput` 一旦 `setGrpcInput` 就恆非 null → `InputBridge` 的 `else if(ADB)` fallback 成死碼**。gRPC 是 fire-and-forget POST 到 `127.0.0.1:8554`，**沒有健康檢查、失敗不回退**：埠打錯＝每次點擊靜默丟。**Rule**：凡「主通道 + fallback」設計，fallback 分支必須真的可達（主通道健康檢查失敗才用），否則就是騙自己的死碼；fire-and-forget 對「時間敏感、可丟」的訊號 OK，但當它是**唯一**輸入通道時要有 liveness 驗證。
- **固定埠假設在有 orphan/多實例時會靜默中毒**：正常啟動用固定 5554→8554，`emulator -grpc 8554` 遇埠被占**不保證失敗**——Windows 允許具體位址（`127.0.0.1:8554` 舊 listener）與 wildcard（`0.0.0.0:8554` 新 emulator）並存，且 **連 127.0.0.1 命中較具體的舊 listener**→emulator log 照印「Started GRPC server」但 chimera 的 loopback POST 打到殭屍。兩個真 emulator 同埠則第二個 `WSA 10048 bind fail`。**Rule**：host↔emulator 的每條連線（capture/input/console/gRPC）都要對「自己啟動的那個 emulator」有把握；最穩健是**每次啟動挑一組驗證過空閒的埠**（含衍生 gRPC 埠，不只 console+adb），而非硬寫常數。挑埠檢查漏掉 gRPC 埠＝等於沒檢查（輸入就是走它）。
- **腳本直開 emulator（無 kill-on-close Job Object）會留 orphan＝下一輪的地雷**：診斷用 `emulator.exe … &` 直開的行程不受 chimera-ui 的 Job Object 管，session 結束若沒殺就卡住埠。**Rule**：診斷直開 emulator 後務必自己 teardown（`adb emu kill` + taskkill + `adb kill-server` + netstat 確認埠釋放）；把「挑空埠」當防線讓殘留 orphan 不再能毒害正常啟動。
- **修使用者實際路徑優先於「理論最完整」**：使用者只走 `start-chimera.cmd`＝腳本挑埠即根治；直開 `chimera-ui.exe` 的 app 端 gRPC 健康檢查/ADB 回退是更完整的 defense-in-depth，但需 C++＋重編且沒人走那條路＝YAGNI，記著待需要再做。**Rule**：ponytail——在所有相關 caller 匯流的地方修一次（此處＝挑埠邏輯），別為沒人走的路徑先蓋 scaffolding。

## 2026-07-05 — Session 105：非 root user image 的 guest 設定邊界（locale/debloat）；app crash-loop 根因＝持久 job 累積；停用系統整合元件會觸發 system_server ANR

- **非 root google_apis_playstore 沒有 CLI 改「系統 locale」——`-prop persist.sys.locale` 與 `adb shell setprop persist.*` 都被 SELinux 擋（實測 prop 恆空、setprop 回 `Failed to set property`）**。`cmd locale` 只有 `set-app-locales`（per-app）無系統 locale。唯一可靠路：**Settings UI（新增 繁體中文（台灣）→ 移除 English）**，它以系統權限寫 `persist.sys.locale` 到 /data，冷開機留存。**Rule**：要改非 root guest 的系統 locale，別寫 launch flag（會是 dead code），走 Settings 一次性設定 + 靠 /data 持久；程式碼裡若已加 `-prop persist.sys.locale` 要回退（無效）。
- **UI 自動化多步 Settings：用 screencap 逐步取座標 + 善用畫面內「搜尋框」跳過長列表**。改語言要進「Add a language」的字母長列表找中文——直接 `input tap` 搜尋圖示、`input text "Chinese"`（`input text` 不支援 CJK，但英文語言名可過濾出「简体中文/繁體中文」）→ 一跳到位。reorder 用「移除 English」比拖曳 drag-handle 穩健（無 root、無 draganddrop 依賴）。`am get-config` 的 `config: ...b+zh+Hant+TW` 是設定成功的程式化 oracle。
- **「應用程式時常停止運作」根因＝持久化 JobScheduler job 累積撞每-app 150 上限**：`dumpsys dropbox --print | grep Process:` 排序找出反覆崩潰的 process（本次 Google 相簿 51 次），trace ＝ `IllegalStateException: Apps may not schedule more than 150 distinct jobs`。job 存在 /data（跨開機累積、crash→reschedule 滾大）→ 撞上限→ boot receiver 崩潰迴圈。**`pm disable-user` 會取消該 app 的持久 job ＝根因修復**（非只是關掉它）。**Rule**：guest app 反覆崩潰先撈 dropbox 找 process + exception type，別憑感覺歸因記憶體；「150 jobs」/持久狀態損壞用 disable（或 `pm clear`）重置。
- **停用「系統整合」套件（`com.google.android.as`、`settings.intelligence`、`googlequicksearchbox`）在執行中會觸發 `Process system isn't responding`（system_server ANR）**——system_server 綁定它們。**Rule**：debloat 只停「純使用者面向」bloat（Photos/YouTube/Gmail/Maps/Wellbeing/wallpaper/Pixel Launcher…）；系統整合類留著（要清其累積 job 就 disable→enable 重置，別長期停用）。ANR 出現點 **Wait 不點 Close app**（Close app 會重啟 system_server）。停用前 `cmd package resolve-activity -a android.intent.action.MAIN -c android.intent.category.HOME` 確認 Chimera launcher 才是 HOME，再停 Pixel Launcher。
- **/data 確實跨冷開機持久（實測）**：hardware-qemu.ini 顯示 `disk.dataPartition.path=…userdata-qemu.img`、`userdata.useQcow2=true`、`forceColdBoot=false`；`-no-snapstorage/-no-snapshot*` 只關 snapshot **不清 userdata**。實測：dropbox 有 3 天前記錄 + 寫 marker→`adb reboot`→marker 存活。**config.ini 的 `<temp>`/`<build>` 是 AVD 模板佔位符，非真的暫存**（啟動時解析成 avd-dir 真實路徑，看 hardware-qemu.ini 不看 config.ini）。**Rule**：使用者說「每次全新」先實測 marker 持久性，別直接信 config.ini 的佔位符；持久成立時「全新」感是帳號/登入態（無 root Google 登入卡）而非資料被清。
- **滑鼠滾輪「滾一格飛到底」＝gRPC swipe `durationMs=0`**：0ms 讓 press→終點→放開背對背送出，Android VelocityTracker 看到「全距離/~0ms」＝爆速 fling（且無中間 move 可能被判點擊）。**修：給有限時長（`sendTouchSwipe` 內插 move + 終點停留再放＝速度有界＝小幅 scroll 非 fling）；throttle 要 > 手勢長度否則同 `kWheelTouchId` 手勢重疊**。ADB fallback 早就用 100ms，只有 gRPC 生產路徑漏帶時長。
- **qcow2「只增不減」與「分割區上限」是兩件事——改上限不修膨脹**：使用者以為把 `disk.dataPartition.size` 6GB→128GB 能解「實體檔只增不減」。錯：上限是邏輯天花板；只增不減是 qcow2 特性（guest 刪檔釋放的叢集不還給 host 實體檔）。**真正縮小只有離線 `qemu-img convert -O qcow2 src dst`**（攤平 backing 鏈＋丟內部 snapshot＋回收零叢集；本次 8.95→6.0GB，主要來自丟棄 stale `chimera_quickboot` 內部 snapshot）。但 convert **不回收「已刪但未 TRIM」的 block**（ext4 不歸零刪除塊，convert 照抄）——要真正瘦身得先 guest `fstrim`（需 discard 支援）。**Rule**：`qemu-img info` 先看 `disk size` vs `virtual size` 差距 + Snapshot list；壓縮前先備份（rename 成 `.bak`，瞬間、失敗可還原）。
- **加密的 userdata（dm-default-key metadata encryption）無法原地擴大——擴容 = wipe**：fstab `keydirectory=/metadata/vold/metadata_encryption` ＝ /data 走 dm-default-key（vdc→dm-40，磁碟上密文）＋FBE。要 df 顯示更大容量得 resize2fs 那顆 ext4，但 guest 無 `resize2fs`/root、host 看到的是密文、**emulator 也只在建立/`-wipe-data` 時決定 userdata 大小、絕不原地擴既有 fs**。踩過的坑：先 `qemu-img resize` 把 image 撐到 128G→emulator 看到 image==config 就不 resize2fs（df 仍 5.8G）；縮回 6G 讓 image<config 也沒用（emulator 對既有加密 fs 不 resize）。**唯一可靠路＝`-wipe-data`（config 先設好目標 size）重新格式化**；重格後 `df /data`=126G。**Rule**：要放大加密 userdata 容量，別想原地 resize，直接 `-wipe-data`（先確認無真資料損失＝無帳號/個人 app，或先備份可還原的部分），重格後再 scripted 重設（launcher/locale/debloat）。停用 Pixel Launcher 前先把 Chimera launcher 裝好+`set-home-activity`，否則 wipe 後無 HOME。

## 2026-07-04 — Session 104：量測前先問「量的是不是使用者的配置」；vsync 量化假設用逐幀 avgMs 當場否證；聚合 FPS 被 idle gap 汙染；診斷碼別留在生產 hot path

- **「不穩」的基線要用使用者實際的啟動配置量，別用 verifier 預設**：我用 `verify-interactive-ui.ps1`（**預設 priority=below_normal**）量到 avgMs 16.2–17.9/maxMs 48＝抖，差點歸因成 pipeline 問題。但使用者一鍵 `start-chimera.cmd = -Fast -InteractiveFirst` **已設 `CHIMERA_INTERACTIVE_PRIORITY=normal`**，normal 下收緊到 avgMs 16.1–17.0/maxMs 34＝穩定 60（通過 gpu-direct-60 gate）。**抖動是 verifier 預設造成的假象，不是使用者體驗**。**Rule**：優化「使用者感覺不穩」前，先確認量測配置＝使用者實際跑的（priority/flags/一鍵腳本帶什麼），否則會修一個使用者根本沒遇到的問題。measured lever（priority）使用者可能已經在用。
- **host 有沒有 vsync 量化，看 avgMs 是不是 refresh 週期的整數倍**：假設「144Hz vsync-block 把 60fps 量化成 ~57」——逐幀 `CHIMERA_PERF` 當場否證：`guest==stream==render` 每 sample 相等、avgMs 出現 **16.2ms(=61.7fps)**。若真被 144Hz(6.94ms) vsync 鎖，avgMs 會量化成 6.94 倍數（13.9/20.8ms），不會是 16.2。→ host present 事件驅動 1:1 追 guest，非量化，`setSwapInterval(0)` 不套用。**Rule**：疑 vsync 量化，算 `avgMs mod (1000/refresh)`；非整數倍＝不是 vsync-locked，是內容 cadence 驅動。假設在動手前用既有逐幀資料就能否證，別急著改 swapchain。
- **聚合 FPS（streamFps/effFps 平均）會被 idle gap 嚴重汙染，用 per-sample + effMin 才誠實**：同一 build，兩次 run 聚合 streamFps 10.8 vs 57.8——差別不是修改，是 synthetic-scroll 的 fling-settle idle gap 分佈不同（settle 期 guest 正確 0 幀被平均進去）。**Rule**：互動負載報 fps 用 per-sample 分佈 + `effMin`（最差窗）+ 只取 active（guest≥40）樣本算 avgMs/maxMs；別引用單一聚合平均下結論，尤其 A/B。
- **診斷用的昂貴 GPU readback 別留在生產 post hot path**：`postFrameDirectGpu` 裡 `debugReadbackSharedImage`（S101 零幀診斷）以 `frameIndex%240==0` **無條件**每 ~4s 跑一次完整 buffer alloc+submit+等 fence+map+free。改 `envTruthy("CHIMERA_GFXSTREAM_DIAG_READBACK")`（預設 off）gate。**Rule**：hot path 裡任何 `%N==0` 的「診斷/驗證」要 env-gate 預設 off；它只增不減 post thread 工作。**但誠實**：gate 後 `maxMs` 未明顯降＝它不是 maxMs spike 主因（主因是 GL→VK/SwiftShader 合成變異＝架構 floor）；移除它是正確衛生，不可宣稱成「修好了卡頓」。
- **guest post 各階段成本要逐項量，別籠統說「2 次 VK submit+wait 是瓶頸」**：實測 `postFrameDirectGpu total=0.5–1.3ms`（fence 等待僅 0.1–0.4ms）、真正大塊是 `glToVkSync（GL→VK readback）3.5–8.8ms`（SwiftShader CPU-GL ↔ NVIDIA VK 不同 device＝必經 CPU round-trip）。**Rule**：報「瓶頸在 X」前把 X 拆成有計時的子階段；先前 S102 的「2 次 submit+wait」其實 submit+fence 才 ~1ms，瓶頸是 readback——籠統歸因會導向錯的優化目標。

## 2026-07-03 — Session 103：用「該元件的 SF layer 幀數」分辨 guest 重繪 vs host 呈現時序；present-timing artifact 對內容截圖隱形；假設要被自己的取證否證就別留成「fix」

- **「畫面在閃」但 guest ADB screencap 與 host PrintWindow 都 byte-identical → 呈現/掃描時序 artifact，兩種內容截圖本質抓不到**。手勢列閃爍：guest screencap ×7、host PrintWindow ×14（HOME+Settings）全 spread=0。`adb screencap` 讀 SurfaceFlinger 回讀、`PrintWindow PW_RENDERFULLCONTENT` 讀 Qt scene-graph 目前 node——**都是「已合成一幀內容」不是螢幕掃出時序**；present beat / tearing / refresh 對它們隱形。**Rule**：內容截圖 spread=0 只證「合成內容穩定」，不證「使用者沒看到閃爍」；present-timing 的 oracle 是眼睛 / Desktop Duplication（讀 DWM 合成後畫面）/ GPU present trace，不是 PrintWindow/screencap。截不到 ≠ 沒問題。
- **分「guest 有沒有在重繪」用該元件的 SF layer 幀數：`dumpsys SurfaceFlinger --latency-clear` → 等 N 秒 → `--latency "<LayerName#id>"` 數非零 actual-present 列**。手勢列閃爍量 `NavigationBar0#N` 三階段（含強制 `immersive.navigation`）幀數皆 **0/3s**＝guest 端 layer 從不重繪 → 閃爍 100% 在 host 呈現側。**Rule**：懷疑某 UI 元件在閃/動，先量它的 SF layer 幀數；0 幀＝guest 靜態、往 host present 查，別在 guest 端瞎改設定。layer 名用 `--list` 撈（取 `Name#id` 那條，非 `animation-leash`/`Surface(name=` 前綴那些）。
- **假設被自己的取證否證，就不能留成「fix」commit**：我先假設 `policy_control immersive.navigation`（開機強制）是閃爍成因、改成 delete 並 commit「fix: flicker」；隨後兩個程式化測試（screencap byte-identical + SF layer 0 幀、強制 immersive 零效果）**證明它 guest 側完全靜態、該設定在此 image（gesture-nav / 疑 Android 12+，`policy_control` 已無作用）根本 no-op**。**Rule**：commit 前先讓「決定性測試」跑完再定 commit type/訊息；已 disproven 的假設，把 commit 改回誠實（此處＝`chore` 清理 dead no-op 設定，非 flicker 修復），別讓下個 agent 讀到假的「已修」。呼應 memory `completion-claims-must-match-evidence`。
- **`policy_control immersive` 在近代 Android（12+）已移除/無作用**：強制它幀數 0、screencap 不變＝既沒隱藏 nav bar 也沒任何效果。要在無 root user image 隱藏 gesture handle 沒有可靠 settings 途徑；閃爍的真桿是 host present-pacing（S102 boundary）或全螢幕（繞過 windowed DWM 合成）。
- **「present-timing 閃爍」要先問使用者的螢幕更新率——它是隱藏變因**：guest 卡在 ~57-60fps（SwiftShader 上限），使用者螢幕是 **144Hz**（`Get-CimInstance Win32_VideoController | Select CurrentRefreshRate`）→ 60-on-144＝2.4× 不規則 pulldown（每幀顯示 2 或 3 個 refresh 交替）＝judder，在細高對比橫條上眼睛最敏感、其他內容不明顯。60Hz 螢幕上 60fps 是 1:1 完全順、不會有此問題。**Rule**：present/judder 類回報先量 host refresh；60fps 內容只在「非 60 整數倍」的螢幕（144/165Hz）上 judder。修法排序：先問「哪個情境在動」（使用者說主要切換 app/回主畫面＝轉場動畫）。
- **高刷新螢幕上，動畫 scale 開著會放大 pulldown judder；-Fast 關動畫是低風險直接解**：`applyGuestPerformanceSettings` base 把動畫 scale 設 0，但舊 `CHIMERA_GUEST_VULKAN` 分支又開回 1（理由是 S99「60」，已被 S101/S102 定性為 ~57 boundary＝理由失效）。動畫期間 60fps 幀在 144Hz judder＝轉場閃爍。移除 re-enable（動畫維持關）＝轉場即時、無運動可 judder。**可程式化驗證**（雖然閃爍本身截圖抓不到）：`settings get global *_animation_scale` 皆 0 + 觸發 app 切換連拍算 distinct frames（動畫關＝~1-3 幀即時、動畫開＝10+ 動畫幀）。**Rule**：present-timing artifact 本身雖 capture-invisible，但「觸發它的運動源」（動畫/轉場）常可程式化量（scale 值、轉場 distinct-frame 數），改機制＋量機制比乾等目視有進展。

## 2026-07-02 — Session 102b：60fps 拆帳——逐段計時定位瓶頸，別假設；瓶頸會「位移」不會「消失」

- **掉幀先分「production gap」vs「consumer stall」，用逐跳計時判別**：互動 scroll 量到 43 個 gap（含一個 2967ms），第一反應是「host 管線 stall」——錯。加 `CHIMERA_HOST_FRAME_TIMING`（GuestDisplay `updatePaintNode` 的 `acquire/copy/sincePaint` + capture worker event gap）實證：每個 gap `acquire=0.1ms copy=0.1ms`（consumer 從不卡），`worker event gap≈paintNode sincePaint`（frame 根本沒被 produce）。**gap 是 production 空檔不是 consumer stall**。**Rule**：顯示鏈掉幀，先在 consumer 端量「我這幀處理花多久」vs「距上一幀多久」——若處理快但間隔大＝上游沒送幀，往 producer/內容查；別在 consumer 端瞎修。
- **synthetic scroll 於短清單大半是 idle gap，會高估不穩定度**：Settings 太短、injector 45-tick(720ms) gesture cycle，gap 緊貼 720ms＝內容不變期間 guest 正確不重繪（push-based）。**Rule**：連續負載 fps 用 gl60（純 clear，無 idle 可能、隔離 fill）量；UI scroll 的低數字先扣掉 idle gap 才是真 cadence。「內容不變＝0 幀」是正確行為不是 bug。
- **counters 全綠 + producer 健康 ≠ 沒問題；但 producer per-frame 快 ≠ 能穩 60**：producer `postFrameDirectGpu` 0.3-2.8ms、`glToVkSync` 4-10ms，好視窗穩 60.0——但整體只 avg 57。因為每幀 total（compose+readback+2×VK submit+wait+present）偶爾超 16.7ms vsync deadline→miss→54。**Rule**：60fps 是 per-frame **budget** 問題（每幀必須 <16.7ms，含所有階段串接），不是「平均夠快」問題；量 avg 沒用，要量 **maxMs / p99** 和 vsync miss。
- **瓶頸會位移不會消失（frame-pacing boundary）**：A/B 換 CPU-direct post（`readToBytes` 4ms vs VK 5-12ms）使 guest production 升乾淨 60.0，但 effective 仍 57——瓶頸從 guest-readback 側**位移**到 host windowed-DWM present 側（`guest=60 render=55`）。**Rule**：當系統多個階段都貼在同一 deadline（vsync 16.7ms）邊緣、各自偶爾 miss，優化單一階段只會把「誰 miss」換人，不改 effective。要嘛消除某階段（guest Vulkan 消 readback），要嘛對齊 pacing——單點優化無效。承認 boundary 比繼續單點優化誠實。
- **A/B 用 env gate 一次 build 比兩次 build 省**：`CHIMERA_GFXSTREAM_FORCE_CPU_POST` 讓同一 runtime 切兩條 post path，一次 rebuild 量兩組。但**跨 API 共享資源改 sync 模型要連帶想 correctness**：CPU-path texture 無 keyed mutex→跨 device tearing race（VK path 的 mutex 正是防此）；「更快但可能撕裂」不是淨勝，不可設預設。
- **`QSGSimpleTextureNode` 縮放品質看 node filtering**（見下條 102a）；readback 成本看用哪個 read：`readToBytesScaled` 恆做 resizer blit（+~5ms）即使 1:1，`readToBytes` 直讀 FBO——1:1 該用後者。
- **多次 boot 後機器會累積 flakiness**：連續 6+ 次 boot/build 後出現 boot timeout（180s 不到 boot_completed）+ 中途 freeze（producer log 健康到突然雙側齊停＝guest hang 非 post deadlock）。**Rule**：這類「跑一半 freeze / boot 超時」先疑環境（清 AVD lock、隔一下重跑），別急著歸因程式回歸；判別：freeze 前 producer 是否還在健康 log（是＝guest/環境 hang，否＝post 側卡）。

## 2026-07-02 — Session 102a：QSGSimpleTextureNode 的 filtering 要設在 node 上，texture 上的會被覆寫

- **`texture->setFiltering()` 對 `QSGSimpleTextureNode` 是 no-op**：render 時 `QSGOpaqueTextureMaterial` 會把自己的 filtering 套回 texture，material 預設 **Nearest** → 三處 per-texture 設定全部無效，1080p texture 縮到 ~0.65× 顯示時整行整列丟像素、文字筆畫殘缺（使用者回報「畫面糊」）。**Rule**：用 `QSGSimpleTextureNode` 時 filtering 一律 `node->setFiltering(...)`；任何「設定看起來有寫但行為像預設值」先查框架是否在後面覆寫。
- **縮放顯示的 texture 用 Linear**：1:1 對齊時 Linear 取樣 texel 中心＝Nearest（無損），縮放時遠優於 Nearest；letterbox 置中會產生小數座標，rect 要 snap 到 device-pixel 格避免 1:1 時半像素模糊。
- **「糊」的排查順序**：先驗 producer 端實際尺寸（log `Opened shared D3D11 texture size W H`），確定來源解析度沒問題，再查 host 呈現端 filtering/縮放/DPI——別上來就懷疑解析度被降。

## 2026-07-02 — Session 101：counters 全綠不代表像素有到；跨 API 資源共享要驗「另一端讀到什麼」

- **零幀黑屏是三層獨立 bug 疊加**（修一層看不到效果，必須逐層取證）：① compose 路徑不標 `mGlTexDirty` → `invalidateForVk` 恆 no-op → kVk sibling image 從未被寫入（blit 複製零）；② D3D11 NT handle 用 `OPAQUE_WIN32` 匯入 → 無 aliasing → 就算 blit 有內容也寫不進 D3D11 texture；③ consumer 不 AcquireSync → 跨 process 就算 texture 有內容也讀到零。**Rule**：顯示鏈黑屏用「分層讀回」逐跳取證——producer 內部（VK readback）、共享資源（D3D11 staging 讀回，有/無 acquire）、consumer 視窗（PrintWindow）——每跳一個獨立 probe，一次定位所有斷層；只修一層再測整條會誤判「修錯了」。
- **HWC compose 路徑不會標 `mGlTexDirty`**：`flushFromGl()` 只有 `rcFlushWindowColorBuffer`（eglSwapBuffers）呼叫；SurfaceFlinger→HWC→`PostCmd::Compose`→CompositorGl 是 host 端 GL 寫入，沒有任何 dirty 標記 → 任何依賴 `invalidateForVk()` 的 GL→VK 同步對 composed target 恆 no-op。修法：post 前顯式 `flushFromGl()`+`invalidateForVk()`（兩者對 VK-backed/共享記憶體內容都是 no-op）。**Rule**：用 dirty-flag 護欄的跨 API 同步，先查「這條寫入路徑會標 dirty 嗎」；host 端代寫（compositor）最容易漏標。
- **Vulkan 匯入 D3D11 NT shared handle 必須用 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT` + `VkMemoryDedicatedAllocateInfo`**：用 `OPAQUE_WIN32_BIT` 匯入在 NVIDIA 上 `vkAllocateMemory`/`vkBindImageMemory` 全回 VK_SUCCESS、blit/fence 正常，但**不建立 aliasing**。獨立 vkinterop probe 5 分鐘定案（OPAQUE=寫入丟失、D3D11_TEXTURE=可見）。**Rule**：跨 API/跨 process 共享資源，唯一有效的驗證是**從消費端 API 讀回像素**；`VK_SUCCESS`、sequence 前進、fence signal、FPS 計數全部可以在寫入丟失時照常發生。懷疑 driver 層行為時，先寫 100 行隔離 probe 窮舉變因，再動 10 分鐘一輪的 DLL rebuild。
- **keyed-mutex texture 跨 process 讀取必須 AcquireSync**（NTHANDLE 必須配 KEYEDMUTEX，probe 實證 NTHANDLE-only `CreateTexture2D`=E_INVALIDARG）：producer 端不帶 `VkWin32KeyedMutexAcquireReleaseInfoKHR` 沒關係（NVIDIA 上 submit 邊界即 flush），但 consumer 不 acquire 就讀到零。Qt QSG 無法跨 render pass 持鎖 → 每個新 sequence `AcquireSync(0)`→`CopyResource` 到私有 texture→`ReleaseSync(0)`，QSG 取樣私有副本。**陷阱**：`AcquireSync` 回 `WAIT_TIMEOUT (0x102)` 能通過 `SUCCEEDED()`（severity bit 0），必須 `== S_OK`。
- **「可見性」gate 必須驗使用者實際看到的 surface**：guest 端 ADB screencap 驗的是 SurfaceFlinger（不經 bridge），host 端 FPS counters 驗的是 metadata 流；兩者都不經過「shared texture 內容 → Qt 視窗」這段——三層 bug 因此潛伏 15 個 session。SelfTest 現在有 `Get-HostWindowPixelStats`（PrintWindow PW_RENDERFULLCONTENT + 中央區取樣）gate `host_window_nonblack_pct>=5`。**Rule**：顯示鏈每一跳都要有各自的像素證據，宣稱「可見」時說清楚驗的是哪一跳。
- **headless 服務的 idle 自殺開關要跟顯示路徑的流量模型對齊**：`-idle-grpc-timeout 300` 在 stock 路徑安全（getScreenshot 輪詢=持續 gRPC 流量），在 shared-texture 路徑致命（顯示不走 gRPC；黑屏下使用者無輸入 → 300s 零流量 → emulator 靜默自殺，qemu log `Idled to long, shutting down`）。verifier 全程注入 input 所以測不到。**Rule**：生命週期保護由誰負責要明確——Chimera 已有 kill-on-close Job Object，emulator 端 idle 自殺一律不開；任何「N 秒無 X 就關機」的旗標，先問「哪個正常使用情境會 N 秒無 X」。
- **「等很久還是黑」= 兩個獨立 bug 疊加的招牌**：黑屏（顯示鏈斷）讓使用者無法互動 → 無互動觸發 idle 自殺（VM 死）→ 使用者感受「黑屏永遠不好」。debug 時分開驗證：畫面黑 ≠ VM 死；先確認 process/boot 狀態，再查顯示鏈。
- **patch script 的 needle 會與手改的 tree drift**：modern tree（`frame_buffer.cpp` 小寫）的 headless 段被前 session 直接手改（加 timing 碼），patch script 對應段從此死碼（guard 不成立→靜默跳過），改 patch script 完全無效卻回報「Applied」。**Rule**：改 patch script 後必須 grep tree 確認變更真的落地（`grep <新字串> tree 檔`）；發現 drift 時 tree 與 script 同步改，並用 `Replace-Text`（非致命、含 already-applied check）而非會 throw 的變體。

## 2026-07-02 — skiavk UI 在 playstore user image 上結構性不可行；「偶發黑屏」其實是半套用狀態（timing-deterministic）

- **`adb shell` 序列裡的特權命令必須驗證結果，不能靜默吞掉**：`applyGuestVulkanHardwareUi()` 的 `stop`/`start` 在 `google_apis_playstore` user image（`ro.debuggable=0`）回 "Must be root"，`runAdbShell` 不看輸出/exit code → framework restart 從加入當天（Session 95）起**從未成功過**，但 `setprop debug.hwui.renderer=skiavk` 有寫入 → 半套用。**Rule**：含 `stop`/`start`/`su`/非 `debug.*` setprop 的 adb 序列，要麼先驗 `ro.build.type`/root 可用性，要麼檢查命令輸出；任何「必須成功否則狀態不一致」的 guest 操作不可用 fire-and-forget。
- **半套用 skiavk = 結構性黑屏**：HWUI 在 setprop 後首次 init 的 process 走 Vulkan（host NVIDIA memory），SF 合成留在 SkiaGL（host SwiftShader-ES）→ 跨裝置無法取樣 → 那些視窗內容全黑；pre-setprop process（SystemUI）可見 → 使用者看到「狀態列有、中間黑」。**可見與否取決於 process 啟動時序**，所以看起來「偶發」。**Rule**：把問題歸類 intermittent 前，先找 timing-dependent deterministic 機制；「同一 boot 內有的視窗可見有的黑」是 per-process renderer 半套用的招牌。
- **skiavk 在此 image 不可行，別再試**（2026-07-02 probe 實證，三路全死）：① root restart：`stop`→"Must be root"；② boot-time prop：`-systemui-renderer skiavk` 只讓 init 翻譯 `ro.boot.debug.hwui.renderer`→`debug.hwui.renderer`，**不翻譯** `ro.boot.debug.renderengine.backend`（getprop 空、SF "Unrecognized RenderEngineType ; ignoring!" → SkiaGL）；③ `setprop ctl.restart surfaceflinger`：SELinux denied（`avc: denied { set } property=ctl.restart$surfaceflinger scontext=u:r:shell:s0 permissive=0`、SF pid 不變）。HWUI-only Vulkan 必黑（`Pipeline=Skia (Vulkan)` 實證下 home/Settings screencap 全空白）。`CHIMERA_GUEST_VULKAN` 現在只等於 `-feature Vulkan`（Vulkan app 直達 NVIDIA）。
- **驗證 renderer 切換不能用 getprop 回顯，也不能只看「畫面還在」**：畫面可能來自 pre-setprop 的舊 process。歷史「GuestVulkan 對照 ~2×」「hwui=skiavk active」數字全是這兩種假證據（採樣 pre-setprop GLES process / getprop echo），不可引用。**Rule**：要證明 renderer 生效，用 `dumpsys gfxinfo <pkg>` 的 `Pipeline=`，且該 process 必須是 setprop 之後新啟動的；同時必須配 ADB screencap 內容 gate 證明它真的在畫。
- **「可見性」宣告一律要像素 gate**：舊 `start-chimera.ps1 -SelfTest` 只驗 dumpsys focus + log screenshot bytes（不 assert）→ 黑屏 bug 假 PASS 多個 session。**Rule**：任何 self-test/verifier 的可見宣告都要 assert bytes/nonblack/luma spread（共用 `Get-AdbScreenshotStats` 門檻 20000/10%/40）。
- **placeholder 的隱藏條件要綁「真的可用」不是「有第一幀」**：`-no-boot-anim` 下 boot 期間 shared texture 流的是黑幀，QML placeholder 在第一幀就消失 → 使用者看 20-30s 裸黑以為壞掉。**Rule**：載入 UI 的隱藏條件用 boot-ready 信號（`AndroidControls.bootReady`），不用 hasFrame。
- **detached PowerShell 的 terminating error 會逃出 `*>` 落到 hidden console stderr 而遺失**：`powershell -Command "& script *> log"` + `-WindowStyle Hidden` 時，script 的 `throw` 訊息不進 log。**Rule**：detached verifier wrapper 一律 `try { & script *> log } catch { $_ | Out-String | Add-Content log; exit 1 }`。
- **量測崩潰先查「使用者正在用電腦嗎、跑什麼」，再懷疑回歸**：本輪 3 次 gl60 post-warmup 全 0 差點誤判成程式回歸，實際是使用者在主螢幕玩遊戲（P5R），TOPMOST verifier 視窗蓋住遊戲被使用者縮掉 → Qt render 停 → 全 metric 崩。招牌特徵：**崩點時間 run-to-run 不固定** + **producer 端 log 全程健康 60**（`published sequence`/`postFrameDirectGpu` 持續）+ guest/stream/render 一起爬行（0.5-2fps、maxMs 數十秒）。**Rule**：需要可見視窗的量測，跑前先 `Get-Process | Sort CPU` 看前景負載；視窗必須放在不干擾使用者的位置（`CHIMERA_VERIFY_WINDOW_ORIGIN` 副螢幕），否則使用者會合理地把它縮掉、量測必崩。
- **`dup=0` 的低 fps = producer 上限，不是 consumer 崩**：host-only smoke 在遊戲負載下量到 34fps 但 dup=0 → 消費端每幀都跟上，34 是 synthetic producer 被遊戲搶 GPU 的產出上限。判讀 host 消費端健康與否，dup 與「producer log 的發布節奏」比絕對 fps 更決定性。
- **全域輸入 API（XInput）的轉發必須 focus-gate**：`GamepadManager` 用 `XInputGetState`（system-wide，不分視窗）60Hz 輪詢並無條件轉發進 guest → 使用者在**另一個遊戲**（P5R）按手把，B/Home 變 Android BACK/HOME 把 guest 前景 app 關到背景 →「guest 隨機停止渲染」。sidecar 實錄：凍結瞬間 guest 前景=launcher、gl60 process 活著但 activity backgrounded、螢幕 Awake。**Rule**：任何 system-wide input source（XInput/RawInput/global hook）轉發進 guest 前必須檢查 `QGuiApplication::applicationState()==ApplicationActive`；「guest 前景 app 無故回 HOME/BACK」先查 host 側全域輸入轉發。
- **抓「隨機時間 guest 停止」用 sidecar 在凍結瞬間取證**：watch host log 的 CHIMERA_PERF，`effective=0` 且 `total` 重複兩次 → 立刻 ADB screencap + `dumpsys window`(focus) + `dumpsys power`(wakefulness) + `pidof` + crash logcat。一次凍結現場勝過十輪盲跑。

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
- ~~GuestVulkan/skiavk 需要 HWUI + SurfaceFlinger 一起切 Vulkan 並 framework restart~~ **（已被 Session 100 定案推翻：playstore user image 無 root，`stop/start` 必失敗，skiavk UI 切換結構性不可行、禁止再試；見 2026-07-02 lesson）**。

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

## 2026-06-22 — 真 60 FPS 的瓶頸是 guest render cadence，不是 host pipeline（⚠ 數字被 S101 重新定性）

- 先前 7–24 FPS 的結論誤導：boot 動畫 / Settings 滾動 / idle Home 都是 push-based，guest 不連續渲染，量到的低 FPS 是 guest 沒在畫，不是 host 撐不住。要驗 host pipeline 上限，必須用連續渲染 workload（`RENDERMODE_CONTINUOUSLY` 的 GL app），否則永遠誤判。
- `chimera-gl60-smoke` 連續渲染後，direct-Vulkan→D3D11 path 立刻 steady 60（min 59.8 / avg 60.0 / dup 0 / avgMs 16.2ms）。瓶頸定位錯誤會浪費好幾個 session 往錯方向（async PBO、GPU-to-GPU zero-copy）優化。**⚠ Session 101 更正：此 60 量的是零幀 blit 節奏（shared texture 實際發佈全零、host 視窗黑）；「用連續渲染 workload 量 host 上限」的方法論仍成立，但任何 FPS 數字必須配 host 視窗像素證據。**
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
