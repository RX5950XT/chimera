# Chimera Task Todo

---

## 2026-05-27 Session 29 — enforce 1080p floor, no hidden downscale

### Plan

- [x] 找出仍偷偷把 gRPC capture 預設降到 800x450 的程式碼與文件。
- [x] 在 `GrpcFramebufferCapture` 層建立 1920x1080 最低解析度防線，env var 設小也會被 clamp 回 1080p。
- [x] 新增 unit test 驗證 800x450 request 會提升到 1920x1080，且 full-resolution request encoding 正確。
- [x] 用 1920x1080 D3D11 shared texture producer 跑 runtime smoke，驗證不是靠降解析度拿 60fps。
- [x] 更新交接文件，明確禁止用低於 1920x1080 的 capture 當預設或驗證捷徑。

### Review

- `GrpcFramebufferCapture::normalizedCaptureSize()` 現在最低只接受 1920x1080；`main.cpp` 預設與 `CHIMERA_CAPTURE_WIDTH/HEIGHT` 都會通過同一個 clamp。
- `test-grpc-framebuffer-capture` 覆蓋解析度 floor 與 gRPC image request 的 1920x1080 encoding。
- Runtime shared texture smoke 使用 `shared_d3d11_texture_producer --width 1920 --height 1080 --fps 60`，結果為 `Guest: 59.9 FPS | Stream: 59.9 FPS | Render: 59.9 FPS | Avg: 16.3ms | Dup: 0`。
- `ctest --test-dir build -C Release --output-on-failure -LE integration` 為 19/19 PASS。
- 注意：這輪修掉「偷降解析度」與證明 host shared texture path 可在 1080p 接近 60；Android/emulator 端 shared texture producer 仍是下一個硬缺口，尚未宣稱真機通知欄/遊戲 flow 完成。

---

## 2026-05-27 Session 28 — shared memory/shared D3D11 texture renderer

### Plan

- [x] 將 `GuestDisplay` 從 `QQuickPaintedItem` paint path 改成 Qt scene graph texture node。
- [x] 新增 CPU-copy shared-memory framebuffer backend，先用 seqlock ABI 驗證 frame delivery。
- [x] 新增 D3D11 shared texture metadata backend，讓 producer 可用 named shared texture 對接。
- [x] 讓 `GuestDisplay` 在 D3D11 RHI 下用 `OpenSharedResourceByName` + `QSGD3D11Texture::fromNative()` 渲染。
- [x] D3D11 RHI 下的 CPU frame fallback 改用 persistent texture + `UpdateSubresource()`，避免每幀重建 GPU texture。
- [x] 將 D3D11 metadata capture 從 UI thread QTimer 改為 worker thread 等待 frame event，避免 UI event loop 卡住 frame delivery。
- [x] 新增 runtime helper producer，使用 named D3D11 shared texture 驗證 host shared texture path 可真實 60Hz。
- [x] 保留 gRPC fallback；shared-memory/shared-texture 沒出第一幀時不可造成永久黑畫面。
- [x] Release build 與 unit tests 驗證。

### Review

- 新增 `SharedMemoryFrameAbi.h`、`SharedMemoryFramebufferCapture`、`SharedD3D11TextureCapture`。
- `GuestDisplay` 現在支援三種 texture path：D3D11 persistent upload、CPU `QImage` fallback、named D3D11 shared texture native render。
- `SharedD3D11TextureCapture` 只在 even sequence 新 frame 時計入 Stream，不再用重複 metadata tick 灌水；capture worker 直接等 Win32 frame event。
- 新增 `shared_d3d11_texture_producer` helper，GPU 端用 `ClearRenderTargetView` 固定節拍更新 named texture。
- `test-shared-d3d11-texture-capture` 會真的建立 named D3D11 shared texture，並用另一個 D3D11 device 透過名稱打開，避免只測 metadata 字串。
- 驗證：`cmake --build build --config Release --target chimera-ui test-shared-memory-framebuffer-capture test-shared-d3d11-texture-capture` 通過。
- 驗證：`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 18/18 PASS。
- Runtime shared texture smoke：`shared_d3d11_texture_producer` + `chimera-ui --no-emulator` 實測 `Guest: 59.6 FPS | Stream: 59.6 FPS | Render: 59.6 FPS | Avg: 16.1ms | Dup: 0`，結束後無殘留程序。
- 注意：host renderer / metadata capture 已就緒；Android/emulator 端真正 producer 尚未接入，因此還不能宣稱遊戲/通知欄實測已穩定 1080p 60 FPS。

---

## 2026-05-26 Session 27 — honest FPS + wheel/notification drag smoothness

### Plan

- [x] 移除 host title bar 左上角灰色副標，保留白色 `CHIMERA` Logo。
- [x] 將側欄 FPS 改成更保守的有效 FPS，不再用 Stream delivery 假裝互動流暢。
- [x] 修正 wheel 與 notification shade drag：避免 gRPC touch 事件洪峰與重疊 swipe。
- [x] 維持 800x450 raw capture 預設，避免用 1024x576/1080p `getScreenshot` 硬推造成更嚴重掉幀。
- [x] Build + unit tests + runtime smoke 驗證，並同步 lessons / handoff 文件。

### Review

- 依 BlueStacks 類方向請子代理研究後，結論一致：短期要先做到 hardware acceleration、frame pacing、低延遲 input 與誠實 telemetry；真 1080p/60+ 不能靠 raw `getScreenshot`，需 shared memory/shared texture/GPU capture。
- 主側欄 FPS 改為有效 FPS：`min(Guest, Stream, Render)`，靜止畫面或 duplicate frame 不再用 Stream 60+ 假裝流暢。
- 左上角灰色副標已移除；Host shell / HUD / sidebar 主要可見字串改為繁體中文。
- 滾輪路徑繼續走 emulator gRPC touch，不回退 ADB shell；wheel throttle 從 8ms 拉到 16ms，instant swipe 從 4 個 touch request 降到 3 個，降低高頻滾輪洪峰。
- gRPC duplicate idle cadence 改為約 50ms；有輸入時才喚回較高 cadence。duplicate frame 不送 QML repaint。
- 1024x576 與 sampled fingerprint 已驗證不可靠：sampled fingerprint 會低估內容變化，1024x576 raw path 仍不夠穩。預設保留 800x450，full fingerprint 用於誠實 Guest FPS。
- 驗證：`scripts\build-chimera-launcher.ps1` 通過；`cmake --build build --config Release --target chimera-ui` 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：Android `wm size=1920x1080`、`wm density=320`；qemu priority 未高於 Normal；結束後無 `chimera-ui` / `qemu-system*` 殘留。
- Runtime Perf 證據：短版通知欄/滑動 flow 顯示 Stream 可到 `61.3 FPS`，但 Guest/Render 只有 `8.9 FPS`；較長 flow 最高也只有 Guest `13.9 FPS` / Render `12.9 FPS`。所以本輪沒有宣稱真 60，已把 UI 改成會顯示這個低值。
- 下一個真正 60 FPS phase：新增 shared memory/shared texture capture + scene graph texture renderer，並用 notification shade / wheel scroll / app switch 三個動態 flow 驗證 Guest/Render/visible latency。

---

## 2026-05-25 Session 26 — wheel/input jank + stream headroom

### Plan

- [x] 重跑 runtime smoke，確認 host audio mitigation 後是否仍有 app 切換掉幀。
- [x] 釐清滑鼠滾輪卡頓路徑，移除高成本 ADB shell wheel fallback。
- [x] 調整 gRPC raw capture 預設，保留 1920x1080 guest/input，同時降低 host 傳輸/CPU 尖峰。
- [x] Build + unit tests + runtime smoke 驗證功能、FPS、priority 與無 orphan process。
- [x] 同步更新 lessons / CONTEXT / CLAUDE / AGENTS / STATUS。

### Review

- 滾輪根因已確認：舊路徑每次 wheel 都跑 `adb shell input swipe`，會造成 shell spawn 與 Android input queue 抖動。現在優先走 emulator gRPC `sendTouchSwipe()`，並以 12ms throttle 合併高頻 wheel 事件；只有沒有 gRPC 時才 fallback ADB。
- 預設 gRPC raw capture 從 1024x576/960x540/896x504 收斂到 800x450；Android guest、座標與 UI viewport 仍維持 1920x1080/320dpi。
- qemu/emulator priority 預設維持 `Normal`，不再用 High；BelowNormal 會降低 host 搶占，但連續 app switch 實測會掉到 41-46 FPS，因此不作預設。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke 通過：Google Play / 檔案管理 / 瀏覽器 / 設定皆能開啟，Home 無 TMobile / duplicate Settings / disabled tiles，Stream samples `62.6, 62.4, 62.6, 63.0, 62.7, 62.9, 62.8, 62.2`，min 62.2、avg 62.6。
- 收尾確認無 `chimera-ui` / `qemu-system*` 殘留。

---

## 2026-05-25 Session 25 — host audio stutter mitigation

### Plan

- [x] 盤點 emulator 啟動期造成 host 音樂卡頓/雜音的資源搶占來源。
- [x] 降低 qemu/emulator 對 host audio thread 的排程干擾。
- [x] 延後 gRPC capture 到 Android `sys.boot_completed=1` 後，避免開機期間 CPU/IO 雙重搶占。
- [x] Build + unit tests + runtime smoke 驗證 app、FPS、process priority 與無 orphan process。
- [x] 更新 `tasks/lessons.md` 與交接文件。

### Review

- 預設 vCPU 由 4 降到 2；process priority 維持 `normal`，不再拉到 High。`below_normal` 可降低 host 搶占，但後續 app switch smoke 掉到 41-46 FPS，已改回 Normal 作預設。
- `enableAudio=false` 時不再額外掛 `virtio-snd-pci`，避免 `-no-audio` 旁邊又建立 guest sound device。
- gRPC screen capture 現在等 Android boot completed 後才啟動；開機前不再用 screenshot stream 跟 emulator boot 搶 CPU/IO。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke 後續以 `normal` priority + 800x450 capture 重驗通過，細節見 Session 26。
- 收尾確認無 `chimera-ui` / `qemu-system*` 殘留。

---

## 2026-05-25 Session 24 — fix Home app false positives

### Plan

- [x] 修正固定入口灰色不可用：檔案管理 / 瀏覽器不可再只顯示假圖示。
- [x] 移除 Home 動態掃描塞回來的系統垃圾與重複 Settings。
- [x] 保留 Google Play 新安裝 App 會出現在 Home 的行為，但只列使用者安裝 app。
- [x] 重建 Launcher APK，跑 runtime smoke 驗證首頁 XML 與四個入口點擊。
- [x] 更新 `tasks/lessons.md` 與交接文件。

### Review

- 新增 `BrowserActivity` / `FileManagerActivity` 作為內建 fallback；Chrome / Material Files 不存在時固定入口仍可點，不再灰掉。
- `queryLaunchableApps()` 只追加非 system / 非 updated-system app，並過濾固定入口與 `com.tmobile*`、Settings、DocumentsUI。
- Runtime smoke：`home_has_tmobile=false`、`home_has_duplicate_settings=false`、`disabled_tiles=[]`。
- Runtime tap：Google Play → `com.android.vending`；檔案管理 → `com.chimera.launcher/.FileManagerActivity`；瀏覽器 → `com.chimera.launcher/.BrowserActivity`；設定 → `com.android.settings`。
- Stream samples：`62.8, 61.2, 60.8, 64.6, 62.2, 62.4, 62.8, 62.4`，min 60.8、avg 62.4。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS；結束後無 `chimera-ui` / `qemu-system*` 殘留。

---

## 2026-05-25 Session 23 — app provisioning + compact FPS/fullscreen + custom title bar

### Plan

- [x] 盤點目前 AVD/system image、已安裝 package、Launcher resolver 狀態，確認 Google Play / 檔案管理 / 瀏覽器缺失根因。
- [x] 讓必要 app 可用：優先用 Play Store system image 或可驗證的系統/開源 APK；避免安裝不可信來源 APK。
- [x] 修正滑動卡頓：量測 Stream/Render/Dup 與 UI repaint，針對實際瓶頸調整。
- [x] 調整側欄：移除 top bar 連線 pill，把全螢幕移到 FPS 卡右側，縮小 FPS 卡。
- [x] 調整視窗標題列：改用自繪深色 title bar，Logo 移入 title bar，釋放原本 header 空間。
- [x] Build + unit tests + runtime smoke 驗證 app 可點、FPS/滑動、UI 排版與無 orphan process。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Play Store system image 已啟用，Material Files 由 `third_party/android-apps/material-files.apk` 自動安裝，Chrome 使用 Play image 內既有安裝。
- Chimera Launcher 改為固定四入口置頂，並追加所有 `ACTION_MAIN` / `CATEGORY_LAUNCHER` app；Google Play 新安裝的可啟動 app 會出現在 Home。
- 固定入口 runtime smoke 通過：Google Play → `com.android.vending`、檔案管理 → `me.zhanghai.android.files`、瀏覽器 → `com.android.chrome`、設定 → `com.android.settings`。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Stream 穩態仍可維持約 60 FPS；app 切換期間可見短暫 50fps 左右尖刺，完整遊戲級鎖 60 仍需 shared texture / GPU capture。

---

## 2026-05-24 Session 22 — more emulator space + required apps + 60 FPS

### Plan

- [x] 盤點 host header、launcher padding、package list 與目前 stream FPS 現況。
- [x] 縮小 host 頂欄與側欄占用，讓模擬器 viewport 取得更多畫面空間。
- [x] 調整 Android Home：縮窄上方黑邊、保留 status bar、只展示必要 app 入口。
- [x] 開機後精簡/停用多餘 launcher app；保留 Play、檔案管理、瀏覽器、設定。
- [x] 調整 capture 目標與指標，驗證 stream 穩定 60+。
- [x] Build + unit tests + runtime smoke 驗證。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Host shell compact pass：外框 margin 16→10、頂欄 62→46px、側欄 204→190px，讓同視窗尺寸下 viewport 得到更多可用空間。
- Android HOME 改為固定四個入口：Google Play、檔案管理、瀏覽器、設定；不再列舉所有 launchable app，因此 TMobile 等多餘圖示不會出現在首頁。不存在的 app 會保留入口但停用，不假裝可開。
- Launcher theme 移除 fullscreen，Android status bar 常駐；ADB screenshot 驗證上方厚黑邊消失，狀態列時間/圖示可見。
- gRPC duplicate frame 仍不送 QML repaint，但 idle capture cadence 維持 16ms，讓主側欄單一 Stream FPS 在靜止首頁也反映穩定 60Hz delivery；Guest/Render/Dup 細節仍在 log/HUD。
- Launcher APK build + sign verify 通過；Release build 通過；unit tests 16/16 PASS。
- Runtime smoke 通過：`wm size=1920x1080`、`wm density=320`、`policy_control=immersive.navigation=*`、HOME top activity 是 `com.chimera.launcher`、UI tree 包含四個必要入口且不含 TMobile、tap `設定` 後 foreground 進入 `com.android.settings`。
- 穩態 FPS smoke 通過：boot 後等待 35 秒，Stream FPS 樣本 `61.9, 62.7, 63.1, 63.2, 62.4`，最低 61.9、平均 62.7。

---

## 2026-05-24 Session 21 — black screen + simplified sidebar

### Plan

- [x] 診斷目前黑屏：確認 top activity、launcher crash/logcat、ADB screenshot 與 host stream 是否一致。
- [x] 修正 Chimera Launcher 黑屏 / HOME 啟動穩定性。
- [x] 將側邊欄效能卡簡化成乾淨單一 FPS 顯示，不塞 stream/latency/duplicate 細節。
- [x] 整理側邊欄按鈕排版，保留可用主操作並避免佔用太多空間。
- [x] Build + unit tests + runtime smoke 驗證黑屏、HOME、按鈕與 FPS UI。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- 修正 launcher 黑屏風險：移除 Activity 啟動時強制 immersive hide system bars，改成正常可見的深色 HOME；無 app 時顯示 `No launchable apps`，不會只剩空黑畫面。
- Host install flow 在設 HOME 後新增 explicit `am start -n com.chimera.launcher/.MainActivity`，不只依賴 HOME resolver。
- 側邊欄效能卡改為 64px 高的單一 FPS 顯示；移除 Guest/Stream/Latency/Dup 小字堆疊。
- 主側欄操作精簡為導航、鎖鼠標、鍵位配置、截圖、錄影、安裝 APK、應用程式、剪貼簿同步、設定；移除主頁上較少用且佔位的 OBB/檔案/GPS/感應器/多開/巨集等入口。
- Launcher APK build + sign verify 通過；Release build 通過；unit tests 16/16 PASS。
- Runtime smoke 通過：`wm size=1920x1080`、`wm density=320`、top activity 是 `com.chimera.launcher`，UI tree 包含 `CHIMERA` / `Apps`，ADB screenshot 非黑屏且顯示 Settings / TMobile app icon。
- 點 launcher 中 Settings icon 後 foreground 進入 `com.android.settings`，確認 HOME app 可點。

---

## 2026-05-24 Session 20 — truthful FPS + lower overhead + clean launcher

### Plan

- [x] 盤點 PerformanceMonitor / gRPC frame metadata，確認 FPS 虛報來源。
- [x] 將前端顯示 FPS 改為真實新 guest frame / rendered frame 指標，避免重複畫面被算成 60 FPS。
- [x] 降低 capture 開銷：減少靜止畫面重複 repaint / log 誤導，保留互動時 60 FPS。
- [x] 建立乾淨 Chimera Android Launcher，取代 Pixel Launcher 首頁與多餘 Google 元件。
- [x] 開機後自動安裝/設為 HOME，並驗證首頁乾淨、可點、效能數據同步。
- [x] Build + unit tests + runtime smoke 驗證。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- FPS 虛報根因：舊 `PerfMonitor.fps` 計算的是 capture/presentation loop，不是 Android guest 新內容。現在 `fps/guestFps` 只計 content-changing frame，另有 `streamFps`、`renderFps`、`duplicateRate`。
- gRPC capture 現在會對 raw frame 做 fingerprint；重複畫面只更新 stream metric，不再送進 `GuestDisplay::setFrame()` 觸發 QML repaint。連續重複後 idle capture 退到 100ms，輸入或內容變更後回到 16ms 互動頻率。
- 新增 `tools/chimera-launcher/` Android HOME launcher 與 `scripts/build-chimera-launcher.ps1`；host 在 boot completed 後自動安裝 `build/launcher/chimera-launcher.apk`、設為 HOME、啟動 HOME。
- Launcher APK build 通過並用 `apksigner verify` 驗證；過程中補裝 Android `build-tools;34.0.0`。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Runtime smoke 通過：`wm size=1920x1080`、`wm density=320`、`pm path com.chimera.launcher` 存在、HOME top activity 為 `com.chimera.launcher`、啟動 Settings 後 foreground 進入 `com.android.settings`、log 有 `[Perf] Guest/Stream/Render` 分離指標。
- 靜止首頁 smoke 顯示 `Guest: 0.0 FPS`、`Stream: ~10 FPS`、`Dup: 100%`，代表靜止畫面不再謊報 60 FPS，也不再每幀重繪。

---

## 2026-05-24 Session 19 — 1080p stream + clickable guest input

### Plan

- [x] 盤點目前 gRPC stream、GuestDisplay 座標映射、InputBridge 點擊注入路徑。
- [x] 將預設 Android guest / capture / input logical size 升到 1920x1080 landscape。
- [x] 修正普通滑鼠點擊卡在首頁無反應：不要讓 emulator console mouse 假成功吃掉 tap。
- [x] Build + unit tests 驗證。
- [x] Runtime smoke 驗證：1080p、FPS、ADB tap 後畫面/guest state 有變化、無 orphan process。
- [x] 同步更新 `tasks/lessons.md`、`CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Android guest / input logical size 已升到 1920x1080 landscape / 320 dpi；settings preset 也新增 1080p。
- 1080p raw gRPC capture 實測只有 15-32 FPS；1280x720 raw 約 35-59 FPS。預設改為 1024x576 raw capture + smooth scaling，runtime 62-67 FPS、0 dropped。
- `EmulatorGrpcInput` 新增 `sendTouch`；普通滑鼠左鍵與 QML touch 優先走 emulator gRPC touchscreen，不再依賴 Console `event mouse`。
- Runtime gRPC touch smoke：點 Settings 後 `dumpsys activity/window` 看到 `com.android.settings`，確認 guest 真有收到點擊。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- 清理後無 Chimera 啟動的 `chimera-ui` / `emulator` / `qemu-system*`；本機仍有一個非本 repo 的 `LINE_playstore_x86_64` qemu 行程，未處理。

---

## 2026-05-22 Session 18 — 60 FPS display path + landscape system adaptation

### Plan

- [x] 確認 1-2 FPS 根因是否來自 ADB fallback / 錯誤 native embed 路徑。
- [x] 回退 native emulator embed 為 opt-in；預設維持 headless gRPC streaming。
- [x] 關閉預設 ADB display fallback，避免 1 FPS screencap 蓋掉 gRPC。
- [x] 將 AVD hardware config 與 guest boot settings 固定為 1280x720 landscape / 240 dpi / 60Hz。
- [x] 修正 full boot 後停在空鎖定/載入畫面：自動 wake / dismiss keyguard / HOME。
- [x] Build + unit tests 驗證。
- [x] Runtime smoke 驗證 FPS、方向、ADB `wm size` / rotation 狀態與 orphan cleanup。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- native embed 實測仍會黑畫面並讓 Android Emulator 工具列外漏；已回退為 `--native-embed` opt-in。
- 預設顯示路徑為 headless gRPC；ADB raw screencap fallback 只在 `--adb-display-fallback` 時啟用，避免 1 FPS fallback。
- Quick Boot snapshot 在本輪造成 ADB offline / 空畫面風險；預設改為 full boot，`CHIMERA_QUICK_BOOT=1` 才啟用 snapshot。
- Runtime clean full boot 驗證：`sys.boot_completed=1`，`wm size=1280x720`，`wm density=240`，ADB screenshot 為正常橫向 Home。
- gRPC runtime log 穩定 61-65 FPS；未看到 native attach、ADB fallback 或 Qt cache warning spam。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。

---

## 2026-05-22 Session 17 — Quick Boot runtime verifier

### Plan

- [x] 新增可重跑的 Quick Boot runtime smoke script，取代手寫 PowerShell 片段。
- [x] 腳本驗證：full/snapshot 啟動達 `sys.boot_completed=1`、保存 `chimera_quickboot`、結束後無 orphan process。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。
- [x] 最終跑 `git diff --check` 與狀態盤點。

### Review

- 新增 `scripts/verify-quick-boot.ps1`，自動 full boot 建立 `chimera_quickboot` snapshot，再以 Quick Boot 重啟並檢查門檻。
- 最終腳本驗證通過：full boot 66.7s；Quick Boot 9.7s；threshold 25s；結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-05-22 Session 16 — Quick Boot fallback hardening

### Plan

- [x] 檢查 Quick Boot snapshot 失效時目前只能手動 `CHIMERA_QUICK_BOOT=0` 回退。
- [x] 加入 snapshot 啟動早退自動 full-boot retry，降低壞 snapshot 卡住啟動的風險。
- [x] Build + unit tests 驗證。
- [x] Runtime smoke 驗證：既有 `chimera_quickboot` snapshot 啟動仍可達 boot complete。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Runtime smoke 通過：既有 `chimera_quickboot` snapshot 啟動 12s 達 `sys.boot_completed=1`。
- smoke 結束後 snapshot save 成功，且無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-05-22 Session 15 — Quick Boot snapshot path

### Plan

- [x] 確認 `VirtualMachine` 目前固定使用 `-no-snapshot`，導致每次 full boot。
- [x] 依 Android Emulator 官方 Quick Boot 行為改用可控 `quickBoot` 設定。
- [x] 抽出 `VirtualMachine::buildEmulatorArgs()` 並新增 snapshot 參數單元測試。
- [x] Build + unit tests 驗證。
- [x] 可行時做 runtime snapshot save / relaunch 啟動時間驗證。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md`。

### Review

- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Runtime quick-boot 驗證通過：第一次啟動達 `sys.boot_completed=1` 為 44s；保存 `chimera_quickboot` snapshot 後第二次啟動為 10s。
- 驗證結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

---

## 2026-05-22 Session 14 — Job Object hardening + gRPC cleanup

### Plan

- [x] 盤點 Session 13 未驗證項，優先處理會造成整機卡死的 orphan qemu 防護。
- [x] 補上 `ProcessLauncher::runAsync()` Job Object 建立、設定、assign、resume 的 warning log。
- [x] 清理 `GrpcFramebufferCapture` temporary diagnostics 與過時註解。
- [x] Build + unit tests 驗證；可行時再做 force-kill orphan runtime 驗證。
- [x] 同步更新 `CONTEXT.md`、`CLAUDE.md`、`AGENTS.md`、`docs/project/STATUS.md` 的交接紀錄。

### Review

- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 16/16 PASS。
- Force-kill 驗證通過：`chimera-ui.exe` 啟動後出現 `emulator` + `qemu-system*`，強制結束 host 後兩者皆消失，未留下 orphan。
- `chimera-debug.log` 未出現 `ProcessLauncher` Job Object warning，表示本機 assign/resume path 正常。

---

## 2026-05-21 Session 13 — gRPC 60fps 穩定 + orphan qemu 根因修復

### 已驗證修復

- [x] **gRPC 擷取解析度下調至 640×360**：`main.cpp` `grpcCaptureWidth/Height` 改為 640/360。
  原 1280×720 每幀 2.76MB；640×360 僅 0.69MB（4× 頻寬降低）。直接 gRPC/grpcurl 驗證 emulator 確實會 server-side downscale。
- [x] **gRPC pipeline stall 不 abort**：`GrpcFramebufferCapture::restartPipeline()` 改為只補 slot、不 abort in-flight。
  先前 abort 全部 + re-prime depth 會造成 thundering herd → capture 永久崩到 ~5fps。
- [x] **幀率穩定 60fps / 0 dropped**：clean launch（先殺所有 stale qemu/emulator）後持續 60–68 fps，
  dropped=0，平均幀時間 ~15ms。長跑 8000+ 幀仍穩定。
- [x] **優先級 Normal**：emulator/qemu 由 High → Normal，避免搶佔主機音訊執行緒。

### 驗證數據

```
[Perf] FPS: 62.1 | Avg: 15.5ms | Max: 42.0ms | Dropped: 0 / 8074
```

### 未驗證（待測）

- [x] **ProcessLauncher Job Object**：`runAsync()` 已改 `CREATE_SUSPENDED` → `AssignProcessToJobObject(job, pi.hProcess)` → `ResumeThread()`。
  目的：chimera-ui crash / force-kill 時 Windows 自動殺掉整個 emulator+qemu tree，避免 orphan。
- [x] **Force-kill orphan 測試**：build 後 launch → force-kill `chimera-ui.exe` → 確認 qemu-system-x86_64-headless.exe 同時消失。
- [x] **Job Object 失敗警告**：`AssignProcessToJobObject()` 若失敗（nested job 等）會發 warning log。

### 待清理

- [x] `[GrpcDiag]` temporary diagnostics（`GrpcFramebufferCapture.cpp` 的 `static int s_diag`）移除或 downgrade。
- [x] `GrpcFramebufferCapture.h` stale comment（restartPipeline 仍說會 abort in-flight）。
- [x] `ProcessLauncher.cpp` Job Object comments 在驗證後精簡。

### Review

- gRPC 效能問題已根治：thundering herd + busy-polling + full-res readback 三項同時修正。
- orphan qemu 是整機卡死的**主因之一**（stale ~2.7GB + fresh launch = 雙 VM 搶 RAM/CPU）。
- Job Object force-kill runtime 驗證已於 Session 14 補齊，orphan qemu 路徑可標 resolved。

---

## 2026-05-21 Cleanup / Commit Hygiene

### Plan

- [x] 盤點 `git status`、ignored/untracked 檔案與文件現況。
- [x] 確認 `src/host/input/EmulatorGrpcInput.*` 與既有 modified 檔案屬未提交開發成果，不納入垃圾清理。
- [x] 確認 `.gitignore` 已涵蓋大型 binary、debug logs、R&D scripts、runtime/build outputs、BlueStacks reverse-engineering binaries。
- [x] 刪除可重建且不需提交的 R&D/output 垃圾：`out/`、root ISO/QCOW2/installer、QEMU/debug logs、runtime 空資料夾、錯誤路徑殘留。
- [x] 保留本機開發仍可能需要的 `build/`、`third_party/android-sdk/`、`third_party/android-avd/`、`third_party/ffmpeg/`。
- [x] 精簡同步 `AGENTS.md`、`CLAUDE.md`、`CONTEXT.md` 的版控衛生紀錄。
- [x] 驗證：`git status --short`、`git ls-files --others --exclude-standard`、`git ls-files -oi --exclude-standard` 摘要、CMake build/test。

### Review

- 清理後 ignored 摘要只剩 `build/` 與 `third_party/` 快取；未追蹤檔只剩 `src/host/input/EmulatorGrpcInput.*` 與本任務的 `tasks/todo.md`。
- 未刪除 Android SDK/AVD/FFmpeg/build cache，避免破壞目前開發與驗證環境。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 15/15 PASS。
