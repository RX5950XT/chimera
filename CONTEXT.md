# Project Chimera — CONTEXT.md

> 開發歷程記錄。供下一個 Agent 快速接手用，不需要從 git log 重建脈絡。當前狀態以 `CLAUDE.md` 為準；本檔是歷程與根因記錄。

## 專案目標

Windows Android 模擬器，競品目標是 BlueStacks。純 open-source 元件，無雲端依賴、無廣告、無遙測。
**引擎決策（最重要）**：生產引擎 = `emulator.exe`（Google QEMU+WHPX fork）；`--qemu-backend`（stock QEMU 11 + Cuttlefish）與 `--hcs-backend`（Hyper-V HCS）= legacy R&D，保留不刪。BlueStacks 輸入路徑更正：`BstkDrv.sys` 是 network/filter driver 非 input driver；BlueStacks 走 `HD-Bridge-Native.dll` → virtio-input，Chimera 等效路徑是 emulator gRPC + Console `event` protocol。

## 2026-07-10 — Session 112c — Quick Boot 跨組態 snapshot 污染 brick＋人類保真度點擊 gate

使用者：「你要確定現在是人類可以正常使用點擊的模擬器欸」＋「開啟時音樂有雜音，修復時別讓它再出現」。做人類保真度驗證時當場抓到 P1（Quick Boot 預設化）的驗證盲區並根治。

**症狀**：一鍵 `-Fast` 啟動，`Loading snapshot 'default_boot'... Successfully loaded (950ms)`，但 guest 整台掛死——adb `devices` 回 device 卻 `shell getprop` 永久 hang、qemu 713s uptime 只用 4.4s CPU、host `CHIMERA_PERF total` 卡 1。**與 RefCountPipe 停更完全不同**：那是 producer 停更（guest 活的），這是 guest 本體 brick。

**根因＝跨 display flavor 的 snapshot 污染**：`verify-quick-boot.ps1` 直接跑 `chimera-ui.exe`＝stock runtime flavor（無 CHIMERA_EMULATOR_PATH/GUEST_VULKAN），14:57 存下 stock flavor 的 `default_boot`；一鍵 `-Fast` 用 custom gfxstream DLL 載入它→guest 在 gltransport pipe 上等一個狀態對不上的 renderer＝永久 hang。emulator 自身的 default_boot 相容檢查只看 CLI/AVD config，**看不到 DLL 與 env flavor 差異**（`compatible.pb` 0 bytes）。S112 P1 的「7.5s pass」實測其實只驗過 stock flavor＝盲區。

**隔離實驗（qb-fast-experiment）**：清光 snapshots，純 `-Fast` 三階段：冷開 32.5s 活→graceful stop 存檔→載入 8.5s 活→再載入 8.5s 活＝`pass-fast-quickboot-selfconsistent`。**`-Fast` 的 quick boot 本身完全可靠**，問題只在跨 flavor。

**修法（匯聚點，所有啟動路徑生效）**：`VirtualMachine.cpp` 新增 `displayFlavorForConfig`（emulatorPath+`CHIMERA_GUEST_VULKAN`+`CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES`）＋`invalidateQuickBootOnDisplayFlavorChange`：AVD 目錄旁 `chimera_display_flavor.txt` marker，launch 時（quickBoot on）flavor 與 marker 不符→刪 `snapshots/default_boot`＋log `Quick Boot snapshot invalidated`＋改寫 marker＝跨組態自動退一次冷開，永不 brick。

**A/B 驗證（qb-flavor-ab）＝`pass-flavor-marker-ab`**：① marker 缺失＋fast snapshot→stock 啟動：清＋冷開 38.3s 活、marker=stock；② stock snapshot→`-Fast` 啟動（**原 brick 場景**）：invalidation 記錄＋冷開 31.8s 活；③ `-Fast` 再啟動：8.6s 快載活、`snapshot_loaded=True`、無誤清。unit 24/24。

**人類保真度 gate（verify-human-click）**：真實 Windows `WM_LBUTTONDOWN/UP`（PostMessage，不搶實體滑鼠）→兩點探測用 guest `getevent` 原始值回推 host client→guest 仿射映射→`uiautomator dump` 找 launcher「設定」入口 bounds→精準點擊。round1（新鮮）：kernelTouch=True、focus 切到 Settings、host total 90→138；round2（**放置 35s 後再點＝歷史凍結場景**）：guest kernel 捕獲 touch down/up（`TRACKING_ID`/`PRESSURE`/`SYN` @375.2s）、focus 重開 Settings、host total 168→206＝S112 RefCountPipe 修法在真實使用者場景成立。（gate 腳本三個儀器 bug 依序修正：① PS5.1 無 BOM UTF-8＋CP950 console 吃壞「設定」regex→adb pull＋UTF-8 讀檔＋code point 組字串；② guest brick 時 adb shell 永久 hang→等待迴圈包 Start-Job timeout；③ **evdev 對未變化 ABS 值去重**——round2 點同座標，DOWN 不帶 `POSITION_X/Y`，parser 硬要求座標＝誤判 kernelTouch=False；改以 TRACKING_ID down 為送達證據。）

**音訊**：本輪程式碼修改不碰 priority；驗證改用 `CHIMERA_INTERACTIVE_PRIORITY=idle`（AudioFirst）跑以免測試期間干擾使用者音樂。Quick Boot 本身把開機噪音窗口 34s→8.5s。

**教訓**：① verifier 必須跑出貨 flavor（stock-only 驗證蓋不住 `-Fast` 預設）；② guest brick 時 `adb shell` 永久 hang——所有等待迴圈的 adb 呼叫要包 timeout（Start-Job + Wait-Job）；③ 「adb devices 回 device」不等於 guest 活著。

## 2026-07-10 — Session 112 — /goal「修好專案真正可用」：停更真根因根治（RefCountPipe）＋確定性重現器＋消音

使用者 /goal「修好整個專案讓它真正可投入使用、研究並超越 BlueStacks」。本輪把連續六個 session（S106/107/108/111）反覆誤判的「有畫面但點不動」**根治**。

**根因（gfxstream `m_refCountPipeEnabled` 硬編 false 的 RefCountPipe 失衡）**：
- 這棵 DLL 因 cross-DLL FeatureSet copy no-op 把全部 feature 預設 false、只補「馬上壞」的（EglOnEgl/Vulkan/GlDirectMem/QueueSubmitWithCommands）。`RefCountPipe` 因「幾乎能動」潛伏：emulator 端 `advancedFeatures.ini RefCountPipe=on`（hosts refcount pipe service）、API-34 guest gralloc 走 pipe 模式（**從不送 rcOpen/CloseColorBuffer**），host 卻跑 legacy refcount。
- 後果：post-O ColorBuffer 建立時 refcount=0；headless `postImpl` 每幀 `++`→`dec` 瞬時歸 0→排入 **10 秒 delayed-close**；活躍時下一幀 `markOpened` 救回，**guest 靜止 ≥10s 後 display 鏈 CB 被 sweep 銷毀→producer 永久停更**＝使用者「放著回來就點不動」。歷來 verifier 從不讓 guest 靜止 10 秒＝15+ session 的結構性盲區。
- 旁證：15.5 分鐘 production soak 中 `invalidateGl` 對缺失 handle 累計 **45000 次（~45/s）**＝SF 每幀 `rcBindTexture` 綁到已被 host 銷毀的 layer CB。

**修法**：`frame_buffer.cpp Impl::Create` force `m_refCountPipeEnabled = true`（比照其他 force-enabled features；kill switch `CHIMERA_GFXSTREAM_NO_REFCOUNT_PIPE=1`）；patch script 同步（ASCII-only，注意 em-dash 會被 Write-TextFile 編碼壞掉＋Replace-Text 的 needle 若還在 replacement 尾端會重複插入——首版手改與 script 不逐位元一致造成 block 重複，已 dedup 並驗證冪等）。

**驗證（同 build A/B 因果定案）**：
| 項目 | 結果 |
|------|------|
| 確定性重現器（boot→20s 靜止→操作；`svc power stayon true` 排除螢幕睡眠擬態） | 未修 runtime **3/3 停更**（guest screencap/focus 健康、seq 凍結、dropped=0） |
| fix build | **3/3 GREEN**：rcFBPost=postImpl=kvk 全程 lockstep、seq 一路 240→1200 |
| 同 build + kill switch | **2/2 RED**（idle 後 rcFBPost/postImpl 照走、seq/kvk 凍結）＝A/B 因果證明 |
| unit | `ctest -LE integration` **24/24 PASS** |
| SelfTest（-Fast、新 runtime＋新 chimera-ui） | host 視窗截圖健康（launcher 中文首頁、guest 渲染、75/79KB 非黑）；console 輸出被 harness 吞掉故 dedup 重建後重跑一次留檔 |

**新診斷資產**：`rcFBPost` → `postImpl` → `published sequence` 三層 throttled 計數（1/60/%600）＋ `postImpl` missing-CB log（原本唯一無 log 的靜默失敗點）——下次任何停更可直接分層定位（guest 沒送／到了沒進／進了沒發佈）。診斷腳本 `diag-idle-stall.ps1`／`diag-producer-stall.ps1`（scratchpad；直開 emulator 必用 `-gpu host`，`swiftshader_indirect` 會把 VK ICD 拉成 SwiftShader→D3D11 import null fn ptr＝RIP=0 崩潰）。

**其他**：① S109「GLESDynamicVersion 破壞 -Fast 顯示」直開重測 135s 健康＝敘事可能已被 S109b 修復連帶解掉（未進一步改預設，ini 仍 off）。② 使用者插件需求「啟動鈴聲」＝guest 充電提示音（emulator 每次 boot「剛插 AC」）＋解鎖音：`applyGuestFirstBootSetup` 加 `charging_sounds_enabled=0`（secure+global）、`lockscreen_sounds_enabled=0`、`sound_effects_enabled=0`。③ 競品對照與「超越 BlueStacks」可量測指標寫入 `docs/references/competitor-emulator-smoothness.md` §9。

**誠實邊界**：killswitch RED 中「post 完成但 publish 停、handle lookup 不 miss」的最後一段微觀機制未逐指令釘死（疑 guest 同 resource id 重建 CB／外層 else CPU 發佈靜默失敗）；但同 build A/B 已證因果，且 RefCountPipe=true 是 stock 模擬器本來就在跑的正確組態。S111 watchdog 保留為 belt-and-suspenders。

**P1 — Quick Boot 一鍵預設化（開機 34s→7.5s）**：S111 遺留的加速要求。設計關鍵＝**不可用 named `-snapshot`**（load 會回捲 qcow2 磁碟到存檔點＝crash 後資料遺失），改用 **AVD default Quick Boot**（quickBoot=true 時不帶任何 snapshot 旗標；emulator 自管 `default_boot`：關機存、unclean exit 自動冷開不回捲、config 變更自動失效）＋`VirtualMachine::stop()` 對 quickBoot 一律優雅 `adb emu kill`（等 ≤20s 讓 RAM snapshot 寫完；逾時仍 terminate）。`start-chimera.ps1` 預設 `CHIMERA_QUICK_BOOT=1`（`-NoQuick`/SelfTest 除外）。unit contract 改 `quickBootUsesAvdDefaultQuickBoot`（斷言零 snapshot 旗標）；`verify-quick-boot.ps1` 改三階段（full→seed→quick）且 `Stop-Emulator` 先等優雅退出再硬殺（防打斷存檔）。實測 **full 34.3s / seed 30.9s / quick 7.5s，result=pass**；unit 24/24。代價（誠實）：關閉視窗多等幾秒存 snapshot；首次啟動仍冷開。

## 2026-07-09 — Session 111 — 「有畫面但點不動」真根因更正 + fallback 保命 + README 降調

使用者回報再次出現「打開了、有畫面、但完全點不動」，並要求加快開啟速度、最後重寫 README 刪掉冠冕堂皇宣稱，明講此專案目前只是 AI 開發/整活實驗，不能真正投入使用。

**真根因更正（重要）**：這次不是 S106 的埠、不是 S108 的 gRPC input breaker，也不是 Qt hit-test/座標映射。現場以真實一鍵路徑 `start-chimera.cmd`（`-Fast`）加 `CHIMERA_LOG_PATH` + `CHIMERA_GRPC_INPUT_DIAG` 重現：
- host 日誌證實滑鼠進 `GuestDisplay/InputBridge` 並送出 `sendTouch id=0 guest=(...) pressure=1/0`；`POST sendTouch port 8570 OK http=200 grpc-status=0`。
- guest 端 `adb shell getevent -lt` 同步捕到 `virtio_input_multi_touch_1` 的 `ABS_MT_POSITION_X/Y`、`ABS_MT_PRESSURE`、`TRACKING_ID` down/up＝**觸控確實進 guest kernel**。
- `adb shell input tap` / `am start -a android.settings.SETTINGS` 能改變 guest screencap，但 host `CHIMERA_PERF total` 停在 118/122、shared texture 沒有新 `published sequence`；`Failed to find ColorBuffer ... invalidateGl/invalidateVk` 後 producer 不再 publish。
- 結論：使用者看到的是**舊畫面**，所以點擊看起來完全沒反應；真正壞的是 `-Fast` shared-texture 顯示 producer 停更。S110/CLAUDE 先前「ColorBuffer 是無害噪音」的結論在這個現場被否證：它可能不是唯一因，但它與 producer 停更同窗出現，且停更後 input/guest 都仍活著。

**修法（保命，不宣稱根治 gfxstream）**：`src/host/ui/main.cpp`
1. gRPC unary capture 在 shared texture 模式下**照樣建立**（原本被 `!sharedTextureCapture` 排除）。
2. 新增 **shared-texture stall watchdog**（1s tick）：以 `lastSharedTextureFrameMs` 為準，超過 `kSharedTextureStallMs`（6s）沒有新幀就 `grpcCapture->start()` 保命；producer 恢復發幀即 `stop()` 停回。

**為何不是「兩條 capture 同時常駐」**：`GuestDisplay::setFrame()` 會清掉 `m_sharedD3D11TextureName`，`setSharedD3D11Texture()` 會清掉 `m_frame`——兩個 capture 同時餵會互相清狀態、在兩個不同延遲的來源間跳動。所以同一時間只能有一個 capture 驅動 GuestDisplay，watchdog 負責切換。（第一版修法漏了 retry timer 裡第二條 `grpcCapture->stop()`，是 code review 抓出來的；當時現場沒炸只是因為 gRPC 先拿到第一幀、timer 提早 return＝時序僥倖。）

**驗證**：
| 項目 | 結果 |
|------|------|
| regression test RED→GREEN | contract 測試先 FAIL（wiring 排除 fallback / 無 watchdog），修後 PASS |
| unit | `ctest -LE integration` **24/24 PASS** |
| live：真根因重現 | shared texture 停在 `published sequence=240`；ADB 開 Settings 改變 guest screencap，但修前 host `CHIMERA_PERF total` 完全不動 |
| live：watchdog 生效 | `Shared texture producer stalled for 6664 ms; resuming gRPC capture` → `producer recovered; parking gRPC capture`；host total **126→215** |

**誠實限制**：閒置靜止畫面本來就沒有新幀（guest 正確不重繪），watchdog 無法從「沒幀」本身分辨 idle 與 dead，因此門檻只是取捨——2s 門檻在閒置 Home 每 2–3 秒誤觸一次（實測 5 次啟停），改 6s 後降到 1 次。真正 dead 的 producer 永不發幀，所以任何門檻都會救回來；代價是最壞情況下有最多 6 秒的舊畫面，以及 fallback 期間只有 stock gRPC 的低 FPS。這是可用性補丁，**不是** gfxstream ColorBuffer 生命週期的根治。

**啟動速度方向（尚未完整實作）**：冷 boot 仍約 36s（本輪 log：`Boot completed in 36541 ms`）+ post-boot setup/launcher/home 顯示；Quick Boot 是最大槓桿但預設未開。短期若要加速，應把 Quick Boot snapshot 做成安全的一鍵預設或更可靠的 opt-in，而不是再追 shared-texture 60fps。先修可用性，再談速度。

**README**：已重寫成誠實版：明講 Chimera 目前不是可投入日常/正式使用的模擬器，而是 AI-assisted Android emulator experiment / 整活專案；刪除 BlueStacks parity / production-ready 口吻，保留建置、啟動、架構與已知限制。


使用者要求「審查代碼、精簡清死碼（不要額外產出 BUG）、更新文件、清理專案垃圾檔、持續測試功能與性能」。

**Code review（聚焦近期 churn：S108 輸入 + S109b gfxstream 產生器）**——逐行審 Chimera 自有碼（`src/host`、`scripts/generate-chimera-vk-ext-handlers.py`），確認乾淨：
- 產生器**五路對稱**（size / reserved / marshal / unmarshal / deepcopy 全從同一 `gen` POD universe 取；struct 要嘛五路全有、要嘛全無〔pointer/union/handle 一致排除進 `skipped`〕，即先前 review 抓過的 coverage mismatch 不會再犯）、**確定性**（`sorted(enum_to_struct)` + `norm_to_enum.setdefault` 不依賴 PYTHONHASHSEED）、**idempotent**（`strip_blocks` 先清舊 block、`write()` 內容相同不覆寫＝無假 mtime rebuild）、size_t BE64 在 reserved/marshal/unmarshal 三路對稱。
- `InputBridge.h/.cpp`：console 移除後每個 member 都在用（console 只留 keyboard/text，附 getevent-proven 註解）、`grpcUsable()` gate 使 QMP/ADB fallback 可達，無孤兒碼。

**清死碼＝無可清（誠實結論）**：`src/host` 的 `-prop`/`GLESDynamicVersion` 是「為何不做」的說明註解、`legacy`（ProcessLauncher `_popen` rollback、EmuGL classic runtime）與 `Q_UNUSED`（Qt 介面樁）皆刻意；`--qemu-backend`/`--hcs-backend`/gfxstream proxy 屬 R&D，CLAUDE.md 明訂「保留不刪」。強行移除會違反「不要額外產出 BUG」與既有保留決策，故不動。`src/virtualization/qemu/`（609 命中的絕大多數）是 vendored 上游 QEMU fork，不在審查/精簡範圍。

**垃圾清理**：`tmp/` loose 診斷 throwaway（session `.log/.err/.png/.txt`）**65 檔 / 9.1MB** 刪除，保留 active gfxstream build trees（`aosp*`、`gfxstream-src`，共 2.7GB）；`scripts/__pycache__` 刪除。root/tools 掃描無殘留 build 產物。

**持續測試（功能 + 性能）**：
| 層 | 結果 |
|----|------|
| unit `ctest -LE integration` | **24/24 PASS**（7.84s）＝清理零破壞 |
| 互動 UI（Fast+GuestVulkan+SyntheticScroll、**below_normal 出貨預設**） | path=gpu-direct、sharedTexture=yes、fallback=none、host 視窗 nonblack 100%；sustained-scroll effFps 49.3 / effMin 45.8 / dup 0＝**fast-ui-visible-not-60**。根因 `guest≈stream≈render` 1:1、限制在 guest scroll 期 SurfaceFlinger render cadence（~52 unique fps、push-based）＝**非 host 瓶頸、非回歸**（S102/S104 記錄的 fling-settle idle 變異；上輪同組態量到 56.6 屬同一區間的 run-to-run 抖動） |
| 定義性 1080p60（gl60 continuous、**normal priority**、乾淨隔離重跑） | **result=pass**：min **59.0** / avg **59.9** / dup 0＝對齊 S109c 的 59.1/60.0，60 gate 乾淨過 |

**誠實邊界 / 量測教訓**：① 互動 scroll effective 隨 guest render cadence run-to-run 變（本輪 49.3 / 上輪 56.6），連續渲染的定義性 60 gate 才是穩定性能指標（唯一槓桿＝priority，S104/S109c 定案）。② **1080p60 gate 首跑 min 45.6 FAIL＝與前一個互動 verifier 的 teardown 短暫重疊造成 host 競爭**；機器淨空後單獨隔離重跑即 **min 59.0 pass**——教訓：perf gate 必須單獨隔離跑，不可與其他 emulator/verifier 生命週期重疊，否則 min（最差單一窗）會被瞬時競爭汙染而誤判 regression。③ 3DMark/GravityMark 仍非正式 gate（裝置驗證/反作弊擋模擬 GPU）。收尾：STATUS/CONTEXT/CLAUDE 更新、`configs/instances.json` 被 normal-priority 測試寫過已還原 below_normal、tmp 本輪 perf log 清除、無 Chimera/emulator/qemu 殘留。

## 2026-07-07 — Session 109c — 全功能驗證 + 性能 benchmark sweep（無程式碼變更）

使用者要求「自己跑測試驗證所有功能 + 驗證性能跑 benchmark」。逐層跑 repo 既有 verifier，全綠（integration/Quick Boot 各一次 transient timeout，重跑即過，非 regression）：

| 層 | 命令 | 結果 |
|----|------|------|
| unit | `ctest -LE integration` | **24/24 PASS**（6.7s） |
| integration | `ctest -L integration` | 兩次 ctest harness 內 60s ADB timeout **FAIL**；手動同參數啟 `chimera_dev` 10s 內 `device`+`boot=1`＝**ctest ADB daemon timing，非 boot regression** |
| SelfTest | `start-chimera.ps1 -Fast -SelfTest` | **result=pass**（boot 39s、visible_home 52s、guest+host nonblack 100%、interactivity ok、residual 0） |
| Quick Boot | `verify-quick-boot.ps1` | 首輪 90s timeout FAIL；`-QuickBootTimeoutSec 180` 重跑 **pass**（full 31.9s / quick **9.5s** / threshold 25s） |
| 互動 UI | `verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -SyntheticScroll` | **result=pass-gpu-direct-60**（shared texture、fallback none、sustained-scroll effFps 56.6 / effMin 52.8 / dup 0、below_normal） |
| 嚴格 1080p60 | `verify-true-1080p60.ps1`（預設 below_normal） | min **52.3** < 57 gate **FAIL**＝below_normal priority 假象 |
| 嚴格 1080p60 | 同上 + `CHIMERA_INTERACTIVE_PRIORITY=normal` | **result=pass**：min **59.1** / avg **60.0** / dup 0 |

**重要更正**：CLAUDE.md/STATUS「S101 後 GLES 嚴格 60 gate 不再通過」的敘述只在 **below_normal** 成立；**normal priority 下 gl60 continuous-render 嚴格 gate 乾淨過 60.0**。即 1080p60 唯一槓桿仍是 priority（S104 結論再確認），非架構性不可過。

**Benchmark sweep（normal priority、gl60 HeavyIterations）**：

| iters | eff_min | eff_avg | dup |
|-------|---------|---------|-----|
| 0 | 59.1 | 60.0 | 0% |
| 48 | 11.9 | 13.1 | 0% |
| 128 | 5.0 | 5.8 | 0% |
| 256 | 2.5 | 3.1 | 0% |

全程 `dup=0`、`min≈avg`＝重 GLES fill 只**乾淨單調降穩定幀率**（SwiftShader CPU-fill floor），無 jitter/熔毀；Vulkan-native 遊戲繞過此牆（S104 負載掃描再確認）。**3DMark/GravityMark 未列入正式 gate**（3DMark 裝置驗證/反作弊擋模擬 GPU；SwiftShader GLES 量到的是 CPU fill 非實體 GPU）。收尾：工作樹乾淨、`configs/instances.json` 被 runtime 寫成 normal 已還原回 below_normal 出貨預設、無 Chimera/emulator/qemu 殘留。

## 2026-07-07 — Session 109b — gfxstream Vulkan pNext / mesh shader abort 修復

GravityMark 進入 Vulkan capability enumeration 時帶 `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT` / properties 等 pNext。這棵 gfxstream snapshot 的 `goldfish_vk_extension_structs.cpp` 已知道很多 extension struct size，但 generated cereal switches 不完整：known-size struct 進 `reservedunmarshal_extension_struct()` / marshal / unmarshal / deepcopy 時可能 default abort 或半套處理。

**修法**：新增/擴充 `scripts/generate-chimera-vk-ext-handlers.py`，從 `vulkan_core.h` 建 enum↔struct mapping，對 POD-only struct（scalar/fixed arrays；pointer/union/handle 跳過）自動補：
- `goldfish_vk_extension_struct_size()` + `_with_stream_features()` missing size cases
- `goldfish_vk_reserved_marshaling.cpp` 的 `reservedunmarshal_*` + switch cases
- `goldfish_vk_marshaling.cpp` 的 `marshal_*` / `unmarshal_*` + switch cases
- `goldfish_vk_deepcopy.cpp` 的 `deepcopy_*` + switch cases

mesh shader EXT 特例在 `apply-chimera-gfxstream-patch.ps1` 保留手寫區塊（含 transform/deepcopy），其他 POD struct 由 generator 補齊；build script 每次 custom runtime build 會先套 patch + generator。

**code review 抓到並已修的洞**：第一版 generator 只補 size/reserved/marshal/unmarshal，沒有補 deepcopy。這會讓 538 個 POD struct 進 size table 後，deepcopy 流程從「size=0 跳過」變成「alloc pNext，`deepcopy_extension_struct` default return」，留下未初始化 payload。已補上 `deepcopy` generation，實際生成 autogen deepcopy cases=380。另 `write()` 改為內容相同不覆寫，避免每次 patch 都更新大型 cereal TU mtime 造成無謂 rebuild。

**驗證**：
- generator 重跑：`chimera-vk-ext-autogen: universe 538 POD structs; size+96/+96, reserved+380, marshal+380, deepcopy+380; skipped 170 (pointer/unknown fields)`。
- `goldfish_vk_deepcopy.cpp` autogen deepcopy cases=380，mesh shader manual deepcopy 仍在。
- custom gfxstream runtime rebuild PASS（manifest written to `build/chimera-gfxstream-runtime/lib64/chimera-gfxstream-shared-texture.json`）。
- `cmake --build build --config Release --target chimera-ui` PASS。
- `ctest --test-dir build -C Release --output-on-failure -LE integration` **24/24 PASS**。
- 二次 code review：無重大問題，APPROVE。

**誠實邊界**：這修的是 gfxstream 對 known Vulkan pNext capability structs 的平台 crash/abort，不代表 3DMark 跑分已可完成；3DMark 本身仍以裝置驗證/反作弊拒絕模擬 GPU（詳 S109 前半段）。

使用者回報切 stock 後「性能不穩有夠卡」（stock gRPC ~10-19fps 本質）。重啟 S107 留下的「-Fast ColorBuffer 凍結」修復工作，但先驗證前提——結果**前提是錯的**。

**Phase 1 — code 檢視「凍結」機制**：逐一讀 `Failed to find ColorBuffer` / `bad color buffer handle` 全部 call site（`frame_buffer.cpp` 4382/4394/4412/4605/5168/5180、`vk_common_operations.cpp` 4882）——全是 guest 對已銷毀 handle 叫 invalidate/flush 時 **log + `return false` 跳過該次呼叫**，有 throttle（1/60/每600 才印）。**沒有任何路徑會因此停掉 post/producer。** S107「producer 在 ColorBuffer 失效後停止發佈」在 code 上就站不住。

**Phase 2 — 實測否證（決定性，兩輪）**：
- 60s adb 驅動（Settings 滾動+HOME 切換）：producer `kVk GPU path` frame 1→500 穩定爬升、`postFrameDirectGpu` 每幀 0.2-1.5ms、**零 ColorBuffer 錯誤**、零 device-lost。
- **150s 出貨組態**（-Fast + below_normal + `CHIMERA_SYNTHETIC_SCROLL`=真實 gRPC 輸入路徑 + 定期 HOME/Settings 轉場）：producer 1→**4800**；host `CHIMERA_PERF total` 110→**4593（~1:1 緊跟）**；螢幕 BitBlt hash（CopyFromScreen，DWM 合成＝使用者所見）**30 distinct/58 樣本、nonblack 100%**。此輪 ColorBuffer 錯誤有出現（throttled 到 count 5400）但三指標全程健康＝**確認是無害背景噪音**，與凍結無關。
- 我自己的背離偵測器也誤標了 2 次「DIVERGENCE」——下一 tick host total 就追上、hash 隨即再變＝**fling-settle 靜止幀**。這正是 S107 誤判的機制：guest 內容不變→正確不重繪→單張 byte-identical 被讀成「凍結」。S107 當時的 `total卡140/maxMs 8184` 極可能是量測窗剛好落在 idle 段+單張快照的組合誤讀（原始環境已不可考，受控重現 0 次）。

**Phase 3 — 修法**：`start-chimera.cmd` 一鍵預設**改回 `-Fast`**（順暢共享貼圖顯示），**保留 S107 正確的音訊修法**（不加 `-InteractiveFirst`＝below_normal；150s 實測證明 -Fast 配 below_normal 顯示完全健康，不需 normal priority）。`-Stock`/`-AudioFirst`/`-InteractiveFirst` 仍可手動。

**Phase 4 — 端到端驗證**：`start-chimera.ps1 -Fast -SelfTest` **全過**——boot 35s、visible_home 48s、guest screenshot nonblack 100%/spread 716、**host 視窗 nonblack 100%**、Settings 互動 ok、residual 0。

**狀態整理**：「有畫面但無法點擊」歷經 S106（埠被搶）→S107（顯示凍結）**兩個根因皆否證**，受控測試無法重現原始症狀；輸入路徑（S107 端到端證明）與顯示路徑（本次 150s liveness）都健康。若再發生：`CHIMERA_GRPC_INPUT_DIAG` + 三指標 liveness 法（producer `kVk` frameN vs host `CHIMERA_PERF total` vs 螢幕 BitBlt hash **隨時間對照看背離**）當場拆帳。診斷腳本樣板在 session scratchpad（`diag-fast-freeze.ps1`，核心：MaxFrame/HostTotal/ScreenHash 三函式）。

**教訓**：①「liveness」必須是**時間序列上的三指標背離**，單張 byte-identical 截圖不能證凍結（idle-static 陷阱，S107 栽在這、本次偵測器也誤標 2 次）。② 接手「待修」項先驗證其前提——S107 的凍結敘事 code 檢視 10 分鐘就能否證，比直接動 gfxstream 省一次重建 runtime。**改動檔**：僅 `start-chimera.cmd`（+docs）。

### 第二部分 — 輸入 defense-in-depth：gRPC breaker + ADB fallback；console pointer 通道實證為幻象

S106/S107 共同的結構性缺陷：gRPC 是唯一 pointer 通道、fire-and-forget、`m_grpcInput` 恆非 null＝fallback 死碼——通道死亡＝點擊靜默全丟。本輪補上自癒，過程中連續抓到兩個隱藏缺陷：

**① Breaker 設計**：`EmulatorGrpcInput` 加 3-strike transport breaker——POST reply 的 `netErr!=NoError || http!=200` 算 transport 失敗（grpc-status 拒絕＝endpoint 活著、不算）；連續 3 次→`isHealthy()=false`→2s `QTimer` 打 `getStatus`（google.protobuf.Empty、無輸入副作用）→成功即恢復。`InputBridge` 全部 8 個 gRPC 分支（touch/text/mouse down·up·move/key down·up/wheel）改 gate `grpcUsable()`＝non-null 且 healthy。

**② E2E 抓到缺口一：hang 型劫持使 breaker 全盲**。首輪 E2E（dummy listener 占 8554 只 accept 不回話）：synthetic scroll 的 sendTouch 都發了、但 **msgLog 零個 POST 完成**——input POST 原本沒 transferTimeout，reply 永遠掛著、`finished` 不觸發、失敗永遠數不到。修：每 POST `setTransferTimeout(2000)`（健康 reply 個位數 ms，2s 只在通道死亡時觸發＝OperationCanceledError＝可數失敗）。修後 breaker 3-strike 正確開啟。

**③ E2E 抓到缺口二：console pointer 是幻象通道**。breaker 開了但 fallback 點擊仍不達 guest→逐層拆：console probe `event mouse 0 0 0 0` 回 OK＝`isMouseReady()`＝clicks 被 console 分支接走。裸機直測（headless emulator + telnet console + auth）：`event mouse`/`event send 3:47…`（sendMultiTouch 的 MT Type-B 形式）/加 BTN_TOUCH 版**全回 OK 但 guest `getevent -l` 零 kernel 事件**；縮放 0..32767 座標（virtio MT 裝置 ABS range）也無效；`event keydown`/`event multitouch` 直接 `KO: bad sub-command`。＝**本 build console `event` 子系統整組是裝飾品：語法 OK、送達零**。歷來「Console (5554) 觸控鏈」只驗過指令被接受，從未驗過送達。修：console 從所有 pointer 鏈移除（clipboard/geo/power 服務不動、keyboard 自有 probe gate）；`onTouchPoint` fallback 改 ADB tap（release 觸發、降級但可點）；`onTextInput` console gate 由 `isConnected()`（幻象）改 `isKeyboardReady()`；刪 dead `hasConsoleMouse`/`m_touchPointSlots`/`m_touchSlotIds`。

**④ 驗證三層**：unit **24/24**（新 `tests/unit/test_emulator_grpc_input.cpp`：3-strike/重置/恢復邏輯 + 真網路 dead-port 開閘測試）；**劫持 E2E `result=fallback-pass`**（hang-hijack 8554 + 直開 chimera-ui 固定埠＝start-chimera 挑埠保護不到的路徑：點擊 A+B 犧牲〔4 POST timeout〕→breaker 開→點擊 C 走 ADB tap→guest window token 換新＝送達）；健康路徑回歸 `verify-interactive-ui -Mode Fast -SyntheticScroll`＝**`pass-gpu-direct-60`**（guestFps 60.0/effMin 55.6/dup 0%、breaker 未觸發）＝gating 零影響。E2E 期間另修一次 oracle 誤判（synthetic scroll fallback taps 灌爆 ADB 佇列〔cap 64×~200ms〕害 4s focus oracle 等不到＝測試設計問題非產品 bug）。

**教訓**：① probe「指令回 OK」≠「事件送達」——通道健康檢查必須用行為 oracle（getevent/focus）驗送達，語法 probe 會讓幻象通道擋住真 fallback。② fire-and-forget 通道必須有 per-request timeout，否則 hang 型死亡對任何 error-counting 都隱形。③ 這兩個缺陷都只有 E2E 抓得到（unit tests 全綠）。**改動檔**：`EmulatorGrpcInput.{h,cpp}`、`InputBridge.{h,cpp}`、`tests/unit/test_emulator_grpc_input.cpp`（新）、`tests/unit/CMakeLists.txt`。

## 2026-07-04 — Session 107 — 「有畫面但無法點擊」真根因＝-Fast 顯示凍結（非輸入）；否證 S106；一鍵改 stock＋修音訊〔凍結診斷已被 S108 否證；輸入證明/音訊修法/診斷工具仍有效〕

使用者回報「問題依然沒修好，有畫面但無法點擊」（S106 的挑埠修法沒解決），隨後又回報「開模擬器我的音樂會出現雜音」。系統化除錯（superpowers:systematic-debugging）從頭取證，**否證 S106 根因**，找到真根因＝顯示凍結。

**Phase 1 — 否證 S106「埠被搶」**：現場 `netstat` 乾淨（無 orphan 占 8554）；照舊碼固定 5554→8554 本應可用卻仍不能點。給 `EmulatorGrpcInput::post` 加診斷日誌（`CHIMERA_GRPC_INPUT_DIAG`：記每個 POST 的 `netErr/http/grpc-status`，這本來就是該補的「fire-and-forget 靜默吞錯」缺陷）→ 重編 → synthetic scroll 驅動下 **3138 個 `POST sendTouch port 8560` 全 `OK http200 grpc-status0`，零失敗**。⇒ gRPC 傳輸完好、埠正確，S106 根因不成立。

**Phase 2 — 證明輸入路徑端到端完好**：關鍵盲點＝synthetic scroll 直呼 `InputBridge::onTouchPoint`（繞過 GuestDisplay），從沒驗過真實滑鼠那段。用 Win32 `PostMessage(WM_MOUSEMOVE/LBUTTONDOWN/LBUTTONUP)` 打 chimera-ui HWND（不搶實體滑鼠）：一個點擊→**恰 2 個 sendTouch POST**（press+release）＝`GuestDisplay::mousePressEvent→mapToGuest→onMouseButton→sendTouch` 全通。再對 client(640,380) 點擊→**guest `mCurrentFocus` 由 launcher 變 `SearchActivity`**＝真實點擊確實開啟 guest UI（對照 adb tap 960,500 同效）。另 `WM_NCHITTEST=HTCLIENT`、`WindowFromPoint`=本視窗、主/副螢幕皆 96DPI＝排除 hit-test/DPI/前景/覆蓋。**輸入不是問題。**

**Phase 3 — 真根因＝顯示凍結（決定性）**：權威螢幕 BitBlt liveness（`CopyFromScreen` 抓 DWM 實際合成＝使用者真正看到的）：guest `HOME→Settings`（`mCurrentFocus` 確變），但 **host 視窗螢幕像素兩次 byte-identical（凍結）**。乾淨啟動（完全不碰視窗）重現：`CHIMERA_PERF total` 卡在 ~140、`maxMs=8184ms`、**連續 20 次 adb swipe 也不動**。emulator 端 log：`postFrameDirectGpu`/「kVk GPU path」停在 ~frame100/seq240 後噴 **`ERROR Failed to find ColorBuffer 65/34`、`bad color buffer handle 67`（throttled）**＝**producer 引用的 ColorBuffer handle 失效後停止發佈新幀**。guest 續跑、輸入照進、但凍結畫面永不顯示反應＝「有畫面像死當」。（PrintWindow 對 D3D11 合成抓不到即時幀會誤判，故用螢幕 BitBlt 複驗。）

**Phase 4 — 修法（使用者選「先切 stock＋接著修 -Fast」）**：實測 **stock gRPC 顯示 live**（HOME→Settings host 像素會變、`total` 持續爬、可正常點擊，~10-19fps）。① `start-chimera.cmd` 一鍵預設 **拿掉 `-Fast`→走 stock**（畫面持續更新、點擊立即有反應）。② 順手修使用者回報的**開模擬器音樂雜音**：一鍵 **拿掉 `-InteractiveFirst`**（=normal priority＝最傷 host audio thread；stock 慢路徑用 normal 幾乎沒 FPS 好處）→回 `below_normal`（`CHIMERA_INTERACTIVE_PRIORITY` 預設，`main.cpp:1124` 覆蓋 instances.json）。`-Fast`/`-InteractiveFirst`/`-AudioFirst` 仍可手動 opt-in。③ 還原 instances.json 殘留的 `processPriority: normal`（runtime 持久化的雜訊）回 below_normal。

**待修（-Fast 平滑路徑）**：`frame_buffer.cpp` post 路徑的 ColorBuffer 生命週期——boot 穩定後 SurfaceFlinger 輪替/銷毀的 colorBuffer handle 讓 `postImpl`/`postFrameDirectGpu` 找不到目標而停止發佈。屬 deep gfxstream，需改 source + 重建 runtime（`build-chimera-gfxstream-runtime.ps1`）；這條路歷史上是地雷區（S101 零幀、多次假 60fps）。**教訓：FPS≠輸入送達≠畫面 liveness；任何「可見/可點」宣稱必須用 host 螢幕 BitBlt 對 guest 變化做 liveness 比對 + 行為 oracle，不能只看 FPS gate。**

## 2026-07-06 — Session 106 — 「有畫面但完全無法點擊」＝gRPC 輸入埠被搶（固定埠假設）〔根因已被 S107 否證，挑埠強化保留〕

使用者回報：啟動後**有畫面但完全無法點擊、沒反應**。用系統化除錯逐邊界拆帳，實機重現根因後以最小非破壞改動修復並端到端驗證。

**Phase 1 — guest 邊界（排除 guest 掛掉）**：headless 直開 stock emulator（同一 AVD /data）→ `boot_completed=1`、`bootanim=stopped`；`dumpsys window mCurrentFocus`＝`com.chimera.launcher/.MainActivity`（焦點正常非崩潰 app）、logcat 0 ANR/FATAL、`persist.sys.locale=zh-Hant-TW` 存活、virtio multi-touch 裝置在。**觸控注入測試**：`screencap` md5 baseline→`input swipe`（下拉 NotificationShade）→md5 變、`mCurrentFocus=NotificationShade`→`input keyevent 4`（BACK）→md5 還原成 baseline。**結論：guest 觸控 dispatch 完全正常**，問題在 host→guest 輸入路徑。

**Phase 2 — host 輸入路徑靜態分析**：`GuestDisplay` 三個 mouse handler + touchEvent 全走 `m_mapper.mapToGuest()`（座標 letterbox 數學正確、mapper 幾何由 geometryChange/setGuestSize 餵、item `setAcceptedMouseButtons(AllButtons)`＝可收滑鼠）；tap 用 `kPrimaryTouchId`、wheel 用 `kWheelTouchId=9`（S105 wheel 改動與 tap 無交集）。關鍵：`EmulatorGrpcInput::post` 對 `127.0.0.1:grpcPort` **fire-and-forget HTTP/2 POST**；`main.cpp` 一接上就 `setGrpcInput`＝`m_grpcInput` **恆非 null**→`InputBridge` 觸控/滑鼠分支永遠走 gRPC、**`else if(!m_adbPath.empty())` ADB fallback 是死碼**。顯示走 gfxstream shared texture（獨立於 gRPC）。⇒ **gRPC 埠打錯＝畫面照常、點擊靜默全丟、無回退**。

**Phase 3 — 埠推導 + 啟動流程**：`main.cpp` `consolePort` 預設 **5554**（除非 `CHIMERA_EMULATOR_CONSOLE_PORT`）→`grpcPort=8554+((c-5554)/2)*2`；`VirtualMachine` 以 `-ports console,adb -grpc grpcPort` 啟動；三者一致**前提是 emulator 真的 bind 到 8554**。`start-chimera.cmd`→`start-chimera.ps1 -Fast -InteractiveFirst` 正常路徑用**固定 5554**（只有 `-SelfTest` 才 `Resolve-EmulatorConsolePort 0` 挑空埠）；`Get-FreeEmulatorConsolePort` 只驗 console+adb **沒驗 gRPC 埠**。emulator 是 chimera-ui 子行程掛 kill-on-close Job Object＝正常退出無 orphan；**但 S105 多次直開 emulator（無 Job Object）留下的 orphan 會卡住 8554**。

**Phase 4 — 實機重現（決定性）**：① 占用 `127.0.0.1:8554`（dummy listener）後開 emulator `-grpc 8554`→emulator log 仍印「Started GRPC server at [::]:8554」**回報成功**，netstat 同時有 `0.0.0.0:8554`(emulator)＋`127.0.0.1:8554`(dummy)＝Windows 允許具體位址與 wildcard 並存、**連 127.0.0.1 命中較具體的 dummy**→chimera 觸控 POST 打到 dummy＝點擊全失（＝使用者症狀）。② 兩個真 emulator 都 `-grpc 8554`→第二個 `WSA error 10048 bind [::]:8554`「Failed to start grpc service, even though it was explicitly requested」「Failed to setup emulator in a timely fashion」。**兩種碰撞都讓可見 guest 的 gRPC 輸入失效。**

**根因**：pointer 輸入對「固定 8554 由自己 emulator 服務」的**未驗證硬依賴** + 正常啟動路徑不挑空埠 + ADB fallback 死碼。

**修法（非破壞、免重編 C++）**：
- `scripts/ChimeraVerifyCommon.ps1`：新增 `Get-EmulatorGrpcPort`（與 main.cpp 同式），`Get-FreeEmulatorConsolePort` 挑埠時**多驗衍生 gRPC 埠**空閒（原只驗 console+adb）。
- `scripts/start-chimera.ps1`：正常啟動改成 `-not $explicitConsolePort` 就自動挑空埠（原只 `-SelfTest`）＝每次啟動免碰撞、順帶乾淨支援多開（第二實例自挑另一組埠而非靜默丟輸入）。

**驗證**：parse-check 兩腳本 OK；故意占用 8554→`verify-interactive-ui.ps1 -Mode Fast -SyntheticScroll`（共用改好的挑埠）自挑 console **5580→gRPC 8580**、`CHIMERA_DISPLAY sharedTexture=yes`、`seg=sustained-scroll guestFps=60.0 effFps=58.6 effMin=57.2 dup=0% result=pass-gpu-direct-60`＝synthetic scroll 走**真實 gRPC 路徑**確實驅動 guest 捲動（修前同狀態全丟）。收尾清乾淨無 orphan/監聽。**誠實邊界**：修的是使用者實際路徑（腳本挑埠）；直開 `chimera-ui.exe`（無腳本）仍走固定埠＝app 端 gRPC 健康檢查 / ADB 觸控回退屬**未實作的 defense-in-depth**（需 C++＋重編，使用者不走此路＝YAGNI，記於 lessons 待需要時做）。

## 2026-07-05 — Session 105 — 使用者 8 項體驗清單：滾輪、系統繁中、app 崩潰、資料持久、debloat、清理

使用者一次列 8 項（做完再答 #5/#8）。全部完成並在單次 headless boot（stock emulator + adb，`emulator-5554`）+ 冷重開實測驗證。

1. **滾輪滾動體驗（滾一格飛到底/被判點擊）** — 根因：`InputBridge.cpp` Wheel case 走 `EmulatorGrpcInput::sendTouchSwipe(..., durationMs=0)`；0ms 讓 press→終點→放開背對背送出＝Android VelocityTracker 看到爆速→maxed fling（且無中間 move 可能被判 tap）。ADB fallback 早就用 100ms，只 gRPC 生產路徑漏帶時長。**修**：`kWheelSwipeMs=80`（內插 move + 終點停留＝速度有界＝小幅 scroll 非到底）、throttle `16ms→90ms`（> 手勢長度，避免同 `kWheelTouchId` 手勢重疊）。
2. **系統語言全繁中** — 非 root user image **無 CLI 可改系統 locale**：`-prop persist.sys.locale=zh-TW`（prop 恆空）與 `setprop persist.*`（`Failed to set property`）都被 SELinux 擋，`cmd locale` 只有 per-app。**改用 Settings UI 自動化**（screencap 逐步 + 搜尋框 "Chinese" → 繁體中文 → 台灣 → 移除 English）；Settings 以系統權限寫 `persist.sys.locale=zh-Hant-TW` 到 /data。驗：`am get-config=…b+zh+Hant+TW`、UI 全中文、Chimera launcher 中文（檔案管理/瀏覽器/設定）。曾在 VirtualMachine.cpp 加 `-prop` 但實測無效→**已回退**（dead code；改為註解說明機制）。
3. **App 時常停止運作** — dropbox（`dumpsys dropbox --print`）284 筆、`com.google.android.apps.photos` 反覆崩潰（51 次），trace＝`IllegalStateException: Apps may not schedule more than 150 distinct jobs`＝持久化 JobScheduler job 跨開機累積撞每-app 150 上限→ boot receiver crash-loop。ARM 橋接在（`native.bridge=libndk_translation.so`、abilist 含 arm64）、heap 576m、mem 2.1GB avail＝非全域資源問題。**修＝debloat 停用崩潰源（disable 取消其持久 job＝根因修復）**。冷重開後 crash buffer 0 筆 FATAL。
4. **個人資料持久化（每次全新）** — 靜態證據 hardware-qemu.ini：`userdata-qemu.img`+`useQcow2=true`+`forceColdBoot=false`（config.ini 的 `<temp>`/`<build>` 只是 AVD 模板佔位符，啟動解析成真實路徑）；qcow2 實佔 9GB、dropbox 有 3 天前記錄。**實測**：寫 `/sdcard/chimera_persist_test.txt` marker→`adb reboot`→marker 存活、locale 存活、22 停用套件存活。**結論：/data 本來就持久**，「全新」感＝Google 帳號/登入態沒持久（無 root 登入卡），非資料被清。
5. **新裝 app 上首頁？** — 程式碼確認 YES：`MainActivity.onResume()→loadApps()→queryLaunchableApps` 每次回首頁重查 `queryIntentActivities(LAUNCHER)` + `isUserInstalled` 過濾。視覺佐證：HOME 截圖 `GL60`（user-installed）與四固定入口（Google Play/檔案管理/瀏覽器/設定）並列。條件：有 LAUNCHER activity 的非系統 app。
6. **精簡不必要 app／後台省記憶體** — `pm disable-user --user 0`（可逆）停用 20 個純使用者面向 bloat：Photos/YouTube/YT Music/Gmail/Messages/Maps/Calendar/Contacts/Clock/Docs/Wellbeing/Restore/Android Auto/Google(Assistant)/customization/wallpaper×2/livepicker/tag/Pixel Launcher。**保留**：Play/GMS/GSF/SystemUI/Settings/Gboard/WebView/Chrome/providers/Chimera launcher。**教訓**：`com.google.android.as`/`settings.intelligence` 執行中停用觸發 `Process system isn't responding`（system_server ANR，綁定它們）→ 點 Wait 不點 Close app、把這兩個 re-enable（disable 已清其累積 job）。停 Pixel Launcher 前先 `resolve-activity HOME` 確認 Chimera launcher 是 HOME。實測 MemFree 153MB→697MB（+544MB）。codify 成 `scripts/debloat-guest.ps1`（`-Restore` 可還原）。
7. **持續優化/刪垃圾** — guest debloat（同 #6）；host 清 stale build cache：刪 `tmp/{aosp-build,aosp-build-github,aosp-emu-main-dev,aosp-emu36,aosp-emumain,gfxstream-build,gfxstream-build-system}` **回收 3.21GB**，保留 active pair `aosp-github`/`aosp-github-build`/`aosp`（+ 被 proxy script/AGENTS 引用的 `aosp-sdk-release`、`gfxstream-src`）。修 AGENTS.md stale `tmp\aosp-build` 路徑→`aosp-github-build`。
8. **儲存硬限還是動態？** — 原 `disk.dataPartition.size=6GB`（硬邏輯上限）+ `sdcard.size=512MB`；`userdata.useQcow2=true`＝qcow2 稀疏動態成長，**只佔實際用量、封頂 6GB**。「只增不減」＝qcow2 特性（guest 刪檔釋放的叢集不還給 host 實體檔），與上限無關；真正解只有離線 `qemu-img convert` 壓縮。

   **後續（使用者要求上限改 128GB）**：先 `qemu-img convert` 壓縮（**8.95→6.0GB**，丟棄 stale `chimera_quickboot` 內部 snapshot），資料完整（locale/停用清單存活）。要擴到 128GB 卻卡住：`/data` 是 **中繼資料加密**（fstab `keydirectory=/metadata/vold/metadata_encryption`＝dm-default-key on vdc、磁碟上密文）＋FBE；guest 無 `resize2fs`/root、host 看到密文，**emulator 也只在建立/wipe 時定 userdata 大小、不原地擴既有加密 fs**（先手動 `qemu-img resize` 到 128G 反而讓 emulator 以為 image==config 而不 resize2fs；縮回 6G 讓 image<config 也無效）。**唯一路＝`-wipe-data` 重新格式化**（使用者確認：無 Google 帳號、無個人 app＝無真資料損失）。`emulator -wipe-data`（config=128g）→ `df /data` = **126G 總量、1% 用量**。重設：裝 Chimera launcher APK+`set-home-activity`、跑 `scripts/debloat-guest.ps1`（20 app）、UI 重設繁中。冷重開驗 df 126G/locale zh-Hant-TW/22 停用/HOME=chimera launcher 全留存。刪舊 `.qcow2.bak`（9.6GB）回收；新 qcow2 **4.07GB 實體/128G 虛擬稀疏**。AVD `config.ini`（gitignored）現 `dataPartition.size=137438953472`。答使用者「實際佔用隨用量長、最多 6GB？」＝**guest 內最多（現）128GB、實體檔隨用量長且只增不減（刪檔要離線 convert 才縮）**。

**Build/驗證**：`cmake --build`（chimera-ui + tests）PASS、`ctest -LE integration` 23/23、guest 冷重開全項留存。**改動檔**：`src/host/input/InputBridge.cpp`（wheel）、`src/host/instance/VirtualMachine.cpp`（-prop 回退+註解）、`scripts/debloat-guest.ps1`（新）、`AGENTS.md`（路徑修正）。guest 端 locale/debloat/128G/Chimera launcher 存在**持久 AVD /data**（gitignored；下次 `start-chimera.cmd` 用 custom runtime 啟動即生效，同 AVD/config.ini/userdata，無需重做）。push 待使用者確認。

## 2026-07-04 — Session 104 — 穩定 60fps：全鏈實測定調（使用者配置已穩定）+ 移除生產 post hot-path 診斷 readback

- **使用者 /goal**：「性能已大幅改善，持續優化讓它更穩定在 60fps」。
- **初始假設被自己的量測否證**：先猜 host Qt swapchain vsync-block 在 144Hz 量化 60fps→~57，準備 `setSwapInterval(0)`。但逐幀 `CHIMERA_PERF`（Fast + `CHIMERA_HOST_FRAME_TIMING`，副螢幕）顯示：每 sample **`guest==stream==render`**（59.2/59.2/59.2…）、`dupPct=0`、host consumer `acquire+copy` 恆 **0.1ms**、且 avgMs 出現 **16.2ms(=61.7fps)**——若被 144Hz(6.94ms) vsync 鎖會量化成 6.94 倍數，並沒有。→ **host present 事件驅動 1:1 追上 guest，不是 vsync 量化；swapInterval-0 不套用**（量測擋下一個無效改動）。
- **實測拆帳（誠實）**：
  - Producer 逐幀：`postFrameDirectGpu total=0.5–1.3ms`（`vkWaitForFences` 只 0.1–0.4ms，**非**先前 S102 籠統說的「2 次 submit+wait 瓶頸」）；每幀最大塊是 `glToVkSync（GL→VK readback）=3.5–8.8ms`——SwiftShader(CPU-GL) 合成的 SurfaceFlinger 內容要進 NVIDIA-VK 共享 image，兩者不同 device＝必經 CPU round-trip（read glReadPixels + upload）。guest post 共 ~5–10ms，能撐 60；架構 floor。
  - **priority 是唯一可量測的穩定度槓桿**：below_normal（verifier 預設）avgMs 16.2–17.9/maxMs≤48（抖）；normal avgMs 16.1–17.0/maxMs≤34（穩，`result=pass-gpu-direct-60`、effMin=54）。**使用者一鍵 `start-chimera.cmd = -Fast -InteractiveFirst` 已設 `CHIMERA_INTERACTIVE_PRIORITY=normal`＝實際就在穩定側**——我最初量到的「抖動」是 verifier 預設 below_normal 的假象，非使用者體驗。→ 不改 code 預設（使用者已被覆蓋；改預設投機且犧牲長期 audio 值）。
  - synthetic-scroll 的 ~370ms 週期凍結＝fling→lift→動量遞減→列表 settle idle（guest 正確不重繪），**測試假象**非 stall（S102 已警告）；聚合 streamFps（10.8 vs 57.8 兩 run）被 idle gap 分佈汙染，只看 per-sample active 樣本 + effMin 才誠實。
- **修改（hot-path 衛生，已 build+驗證）**（`src/host/runtime/gfxstream_bridge_templates/vulkan/ChimeraGfxstreamVulkanSharedTextureBridge.cpp`）：`postFrameDirectGpu` 裡 `debugReadbackSharedImage`（S101 零幀診斷）原以 `frameIndex%240==0` **無條件**每 ~4s 跑一次完整 buffer alloc+submit+等 fence+map+free。改用 `CHIMERA_GFXSTREAM_DIAG_READBACK`（預設 off）gate。重建 custom runtime（增量，`vkReadback=0` 確認 gate 生效，仍 `pass-gpu-direct-60`/effMin=54）。**誠實邊界：gate 後 `maxMs` 未明顯降＝它非 maxMs spike 主因**（主因是 GL→VK/SwiftShader 合成變異＝架構 floor）；移除它是正確衛生（生產 post thread 不再每 4s 付一次昂貴 GPU round-trip），不宣稱成「修好卡頓」。
- **定調 + 使用者側最大平滑度來源**：
  - 使用者實際配置（normal priority）**已穩定 ~57–60fps**（render 1:1 追 guest、host consumer 0.1ms）。**S102「host present ceiling ~57」在 normal + 動畫關（S103）下不再觀察到**。
  - 殘留 sub-60 與 maxMs 27–34 單幀 hitch＝GL→VK readback + SwiftShader 合成變異（架構 floor）；rock-solid 60 需 guest VK-native 合成（skiavk 牆擋，blocked）。
  - **144Hz 螢幕上 60fps 本質 2.4× pulldown judder（S103）**；顯示端最大平滑度改善＝若螢幕支援 **120Hz**（2×60＝完美 2:1 pulldown、零 judder）——屬使用者 Windows 顯示設定，非 Chimera code，故建議而非代改。
- **教訓**（詳 `tasks/lessons.md` S104）：優化「使用者感覺不穩」前先確認量測配置＝使用者實際跑的（一鍵腳本帶的 priority/flags）；疑 vsync 量化算 avgMs 是否 refresh 週期整數倍；聚合 fps 被 idle gap 汙染用 effMin/per-sample；診斷用昂貴 GPU readback 別留生產 hot path。

### 負載掃描（遊戲級連續高負載穩定度實測）

- **請求**：使用者要求用「負載更高的場景像遊戲」測穩定度。無現成遊戲 APK（僅 material-files），改用 `verify-true-1080p60.ps1 -HeavyIterations N`＝gl60 全螢幕 plasma quad + 每像素 N 次 sin/cos 迴圈，正是 GLES/SwiftShader 連續填充壓力代理。固定使用者實際配置（`CHIMERA_INTERACTIVE_PRIORITY=normal` + `CHIMERA_HOST_FRAME_TIMING=1` + 副螢幕），gate 放寬成純量測，逐級 clean-start 隔離。
- **曲線**（每級 25s 量測窗，post-warmup per-sample）：

  | heavyIters | effMin | effAvg | avgMs | maxMs | dup/drop | 判讀 |
  |---|---|---|---|---|---|---|
  | 0（輕） | 59.9 | 60.2 | 16.1–16.3 | 19–24 | 0 | 完美穩定 60 |
  | 48（中重） | 12.8 | 13.1 | 73–77 | 90–104 | 0 | 穩定 ~13 |
  | 128（重） | 5.3 | 5.7 | ~175 | — | 0 | 穩定 ~5.5 |
  | 256（極端） | — | 0 | 264（單幀 12.5s） | 12465 | 0 | 崩潰停產（verifier fail on zero-effective；`visible_frame_nonblack_pct=83.3`＝host 仍活著顯示內容） |

- **關鍵結論（穩定度定調）**：① **frame pacing 對負載不變（load-invariant）**——每一級每一 sample 都 `guest==stream==render`（整條管線 lockstep）、`dup=0`、`dropped=0`、`effMin≈effAvg`（緊變異）。**host/顯示側在任何負載都不引入 jitter**。② 重 GLES 片段填充**不造成抖動**，而是**乾淨地降到一個穩定的較低幀率**——因為合成走 SwiftShader（CPU 軟體 GL），每像素 shader 跑在 CPU＝已知架構 floor（[[project-true-1080p60-achieved]] 的同一牆）。③ 只有在**極端不切實際的負載**（256-iter 全螢幕 transcendental fill）CPU rasterizer 才熔毀停產——真實遊戲不會有這種 fill。
- **邊界（誠實）**：gl60-heavy 只壓 **fragment fill**；真實遊戲另有 vertex/CPU 遊戲邏輯/GC/texture 頻寬。且 **Vulkan 遊戲（`CHIMERA_GUEST_VULKAN`，`-Fast`）直達 NVIDIA GPU、繞過 SwiftShader，不吃這道 fill 牆**；典型 2D/UI/輕遊戲維持近 60。→ 對「穩定 60」的意義：**管線本身在任何負載都穩定 paced，60 的天花板由 guest workload 是否落在 SwiftShader CPU-fill 預算內決定；要移除該牆需 guest VK-native 合成（skiavk blocked）**。無新 code 可安全提升重-GLES 天花板。

## 2026-07-03 — Session 103 — 客製化載入畫面（進度條）+ 手勢列閃爍 guest 側排除（定調 host present-timing）

- **使用者回報**（S102 修復後）：「性能改善非常多」，但 (1) 手機最底下滑動回主畫面的手勢橫條不斷閃爍；(2) 載入畫面中間 Pixel 圖標想換成進度條、要能客製化。
- **Fix 2（載入畫面）✅ 完成**（`ChimeraWindow.qml`）：載入 placeholder 拿掉中間漸層「C」圖標，改 CHIMERA 字標 + 客製 indeterminate 進度條（`ProgressBar` 自訂 contentItem，綠色漸層 pip 左右掃動）+「正在啟動 Android…」。placeholder 全程 `visible: !guestReady` 覆蓋顯示區＝使用者看不到 guest 預設 Pixel 開機動畫。驗證：`chimera-ui --no-emulator`（guestReady 恆 false）PrintWindow 截圖確認進度條、中間圖標已移除。
- **Fix 1（閃爍）— 初始假設被自己的取證否證，改定調**：
  - **初始假設（錯）**：`applyGuestPerformanceSettings()` 開機強制 `policy_control immersive.navigation=*`，在手勢導覽上讓 SystemUI 隱藏/顯示互鬥製造 relayout 閃爍。
  - **兩個程式化測試否證（guest 側完全靜態）**：① guest ADB screencap ×7 + host PrintWindow ×14（HOME+Settings）手勢列區域全 byte-identical（spread=0）；② **`dumpsys SurfaceFlinger --latency "NavigationBar0#N"` 三階段（FIXED / 強制 immersive / REFIXED）幀數皆 0/3s**＝NavigationBar layer 從不重繪，且強制 `immersive.navigation` **零效果**（consistent with `policy_control` 在此 gesture-nav/疑 Android 12+ image 已無作用——既沒隱藏 bar 也沒閃爍）。
  - **定調（host present pulldown）**：手勢列在 guest 端與 Qt 算好的 frame 都完全靜態（0 layer 重繪、PrintWindow byte-identical、post-DWM 螢幕擷取也 byte-identical）。查到**使用者螢幕是 144Hz**（RTX 3070 Ti，`Win32_VideoController.CurrentRefreshRate=144`）——guest ~57-60fps 內容在 144Hz 上呈現＝每幀被顯示 2.4 個刷新週期＝不規則 2-3-refresh pulldown＝judder，細高對比橫條（home handle）上眼睛最敏感（就算內容一致也會感知閃爍；其他靜態內容較不明顯，所以使用者只注意那條）。內容截圖工具（讀「合成好一幀」不讀掃描時序）本質抓不到。
  - **使用者回答定觸發點**：閃爍「兩種都有，主要是切換應用程式或回到主畫面的時候」＝**視窗轉場動畫**的時刻。而 `-Fast`（`CHIMERA_GUEST_VULKAN`）分支把 `window/transition/animator` 動畫 scale 從 base 的 0 開回 1（理由是 S99「general-UI 60」——已被 S101/S102 重新定性為 ~57 boundary，理由失效）。60fps 動畫在 144Hz 上 judder。
  - **修法（`main.cpp` `applyGuestPerformanceSettings`）**：移除 GuestVulkan 分支的動畫 re-enable，動畫維持關（scale 0）→ 轉場即時、無動畫運動＝無 pulldown judder；也減少 idle 無謂重繪。實證（新 build 副螢幕開機）：3 個 animation scale 皆讀回 0、HOME→Settings 轉場 distinct-frame=3（即時，動畫開時會有 10+ 動畫幀）、host 視窗 nonblack 100% 無回歸。
  - **另計清理（`main.cpp`）**：移除 dead 的 `put policy_control immersive.navigation=*`（此 image no-op）+ `settings delete global policy_control` 清舊 AVD 殘值。非閃爍修復。
  - **殘留 / 驗證**：轉場 case 由動畫關解決（機制實證）；閃爍本身是 present artifact 無法截圖自證，需使用者目視確認「切換 app/回主畫面」不再閃。若有殘留 idle pulldown＝S102 host-present boundary——桿子＝全螢幕（F11 flip present 繞過 windowed DWM）或 windowed present-pacing（hard open item）。**未來若要保留動畫又不 judder**：需 host present 對齊 144 的整除幀率（AVD 顯示模式多半只 60Hz，改 guest peak/min refresh 到 48 未必被採用，故走動畫關）。
- **教訓**（詳 `tasks/lessons.md` S103）：內容截圖 spread=0 只證「合成內容穩定」，不能證「螢幕沒閃」——present-timing 問題 oracle 是眼睛/desktop-duplication/GPU present trace，不是 PrintWindow/screencap；用「該顯示元件的 SF layer 幀數（`--latency`）」量 guest 端有沒有在重繪，一次分清「guest 重繪」vs「host 呈現時序」。

## 2026-07-02 — Session 102 — 畫面糊根因修復 + 60fps 不穩全鏈拆帳（frame-pacing boundary 定案）

### Part A — 畫面糊根因修復（Nearest 縮小取樣）

- **使用者回報**（S101 修復後）：性能顯著改善但不穩 60fps、畫面糊（「1080p 會這麼糊嗎」）。
- **根因（糊）**：producer 端 texture 實證 1920×1080 無誤（log `size 1920 1080 format 28`）；糊在 host 呈現端——預設視窗 1480×860 扣側欄/工具列後 GuestDisplay item 只有約 0.65×，1080p texture 縮小顯示時 filtering 是 **Nearest**（整行整列丟像素→文字筆畫殘缺）。**陷阱**:`QSGSimpleTextureNode` 的 material filtering 預設 Nearest 且 render 時會覆寫 per-texture `setFiltering()`——原代碼三處 `texture->setFiltering()` 全是 no-op，必須設在 node 上。
- **修復**（`GuestDisplay.cpp`，commit `a80dcee`）：node 建立時 `setFiltering(QSGTexture::Linear)`（1:1 對齊時 Linear 取樣 texel 中心＝Nearest，無損）；三處 `setRect` 改經 `snapRectToDevicePixels()`（letterbox 置中的小數座標對齊 device-pixel 格，消除 1:1 時半像素模糊）。
- **驗證**：build + ctest 23/23 + `-Fast -InteractiveFirst -SelfTest` PASS（1920×1080、`host_window_nonblack_pct=100`、interactivity ok、0 residual）；host 視窗截圖對比修復前——文字/圖示邊緣平滑完整。
- **殘餘限制（誠實）**：縮小顯示本質上會損失細節，Linear 只是把「殘缺」變「柔和」；要完全銳利需視窗 ≥1:1 顯示（全螢幕於 ≥1080p 內容區）或未來做 guest 解析度跟隨視窗。

### Part B — 60fps 不穩：全鏈逐段計時拆帳，定案為 frame-pacing boundary（非單一可修瓶頸）

新增全鏈計時（producer 每段、host consumer paintNode `acquire/copy/sincePaint`、capture worker event gap；後兩者 `CHIMERA_HOST_FRAME_TIMING=1` gate）逐段取證，**推翻多個先前假設**：

- **假設 1（producer post 太慢）→ 否**：`postFrameDirectGpu` total 0.3-2.8ms（max 126ms 單次）、`glToVkSync` 4-10ms（max 14.9ms）。好視窗穩 60.0（avgMs 16.1）。producer 每幀成本遠低於 16.7ms。
- **假設 2（互動 scroll 有秒級 stall）→ 否，是量測假象**：synthetic Settings scroll 量到 43 個 gap（多數 300-750ms、一個 2967ms），但 host timing 顯示每個 gap `acquire=0.1ms copy=0.1ms`——**host consumer 從不卡**；gap 是 frame **production** 空檔（`worker event gap≈paintNode sincePaint`），且緊貼 720ms gesture cycle（injector 45 tick×16ms）。**內容不變＝guest 正確不重繪**（push-based），非管線 stall。Settings 太短無法連續 scroll，所以 synthetic 測量大半是 idle gap，**高估不穩定度**。
- **host consumer 端證實最佳**：`AcquireSync + CopyResource` 恆 **0.1ms + 0.1ms**，零優化空間。
- **guest 非 compute-bound**：`qemuCpuPctAvg=34.5 max=46.3`。
- **gl60 連續負載真相（無 idle gap）**：純 `glClear` 連續渲染振盪 **53.4-60.3、avg ~57、avgMs 16.1-17.8、maxMs 24-33（無秒級 stall）**。根因＝每幀 post 付 glReadPixels(~3-4ms) + **2 次同步 VK submit+wait**（`invalidateForVk` staging→image、`postFrameDirectGpu` blit→shared），偶爾單幀超過 16.7ms vsync deadline→miss→掉到 54。
- **A/B 實驗（CPU-direct post vs VK-blit post）**：因 SF 走 SwiftShader-ES GL 合成（skiavk 停用），post target 永遠 GL-backed，VK path「zero-copy」是假的。加 `CHIMERA_GFXSTREAM_FORCE_CPU_POST=1` 走 CPU 路徑（`readToBytes` GL readback→`postFrameCpu` D3D11 UpdateSubresource，無 VK wait）。lean `readToBytes` 每幀僅 **3.8-4.6ms**（vs `readToBytesScaled` 8.6ms〔resizer blit〕、vs VK 5-12ms），**guest production 升到乾淨 60.0**——但 **effective 仍 avg 57.3/min 45.5/max 60.3，與 VK 相同**：瓶頸只是**位移**到 host present（`guest=60.0 render=55`，windowed DWM vsync miss）。且 CPU-path texture 無 keyed mutex→跨 device tearing race（VK path 的 mutex 正是防此）。
- **定案**：**~57fps 是 vsync 邊緣的 frame-pacing boundary，非單一可修瓶頸**——換 post path 只是位移「哪一側 miss vsync」（guest software readback 側 ↔ host windowed present 側）。要 rock-solid 60 需 guest **Vulkan-backed** 內容（無 readback，消除 producer 側 3-4ms）**且** host present pacing 對齊——前者被 skiavk 牆擋（S100 定案），後者屬 windowed DWM present 固有 jitter。
- **本輪保留改動**（current runtime，SelfTest PASS 無回歸）：① `invalidateForVk` 重用 8MB readback buffer（消每幀 zero-init jitter）；② `chimeraPublishFrameToD3D11Texture` 1:1 用 `readToBytes`（省 resizer blit，改善 GLES fallback）；③ 全鏈計時 diagnostics（host 端 gate `CHIMERA_HOST_FRAME_TIMING`）。`CHIMERA_GFXSTREAM_FORCE_CPU_POST` 為 tree-only 實驗旗標（有 tearing 風險、非淨勝、未進 patch script；預設 off，default VK path 不受影響）。
- **驗證**：default VK path `-Fast -SelfTest` PASS（`boot=104s` 環境噪音、`host_window_nonblack_pct=100`、`luma_spread=716`、interactivity ok、0 residual）。
- **量測紀律新增**：synthetic scroll 於短清單（Settings）大半是 idle gap，不能當「不穩定」證據；連續負載用 gl60（純 clear，隔離 fill）；任何 fps 掉幀先分「production gap（內容不變/guest）」vs「consumer stall（host paintNode）」——用 `CHIMERA_HOST_FRAME_TIMING` 的 `sincePaint` vs `acquire/copy` 判別。

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
