# Chimera Lessons

## 2026-05-22 — 顯示與效能驗證

- 不可把 Android Emulator Win32 native embed 當作預設修法；它會黑畫面、破壞 Qt emulator 視窗群組，且可能讓工具列外漏。預設維持 headless gRPC streaming，native embed 只能作 opt-in 實驗。
- FPS log 只代表 frame 有 paint，不代表使用者看到可用桌面。遇到「空畫面」必須同時抓 ADB screenshot 與 Chimera/GuestDisplay 畫面，判斷是 guest state 還是 host display path。
- Quick Boot snapshot 可能保存壞的 guest state；在 snapshot 穩定前，預設 full boot，Quick Boot 只能用 `CHIMERA_QUICK_BOOT=1` 或 verifier 明確啟用。
- 開機後要主動 wake、dismiss keyguard、HOME，否則 stream 可能停在近乎空的鎖定/載入畫面。

## 2026-05-24 — 觸控與解析度效能

- Android Console `event mouse` 會回 OK，但不保證被 Android launcher 當成 touchscreen tap；普通點擊要走 emulator gRPC `sendTouch` 或其他真觸控路徑，不可讓 console mouse 假成功吃掉事件。
- 不能把 guest 解析度和 stream 擷取解析度混為一談；Android 可維持 1920x1080/320dpi，但 gRPC raw `getScreenshot` 1080p 會掉到 15-30 FPS，預設 capture 必須貼近實際 viewport 才能守住 60+。
- 使用者抱怨「畫面靜止/點不動」時，驗證要包含「點擊後 foreground package 改變」，只看 screenshot 或 FPS 不夠。
- 不可把 capture loop FPS 當成使用者體感 FPS；靜止畫面重複擷取 60 次只代表 host 在輪詢。UI 必須分開顯示 guest/content FPS、stream FPS、render FPS 與 duplicate rate。
- 使用者明講「FPS 虛報」時，主側欄不可顯示單純 Stream FPS；Stream 只代表畫面傳輸 cadence，Guest 內容 FPS、Render FPS、Dup rate 必須保留在 HUD/log 供查證。
- 降低主機卡頓要避免靜止畫面重複 repaint：重複 frame 應只更新 stream metric，不送入 QML。若產品面要求主 FPS 穩定 60，capture 可維持 16ms，但 duplicate path 仍不可觸發 repaint。
- 想做 BlueStacks 類乾淨首頁，應做真正 Android HOME launcher 並在 boot completed 後 install/set-home，而不是只在 host UI 疊一層假首頁。
- 側邊欄是操作面板，不是除錯儀表板；使用者要乾淨時，主卡只顯示單一 FPS，Guest/Stream/Render/Dup 這類細節留給 log 或可切換 HUD。
- Android HOME launcher 不可用接近純黑的空狀態；至少要有固定標題或固定入口與 empty state，並用 `uiautomator dump` + screencap 驗證畫面真的有內容。
- 設 HOME 成功不等於當前畫面已切到 HOME；host boot flow 要 explicit `am start -n com.chimera.launcher/.MainActivity`，再用 top activity / UI tree 驗證。
- Android HOME 要常駐 status bar 時，theme 不可設 `android:windowFullscreen=true`；只隱藏 navigation bar 即可，否則橫向畫面會出現厚黑邊且狀態列圖示不可見。
- 乾淨首頁不要再列舉所有 `CATEGORY_LAUNCHER` app；固定展示必要入口，缺套件時顯示停用狀態，避免 TMobile/Setup 類系統殘留破壞簡潔感。

## 2026-05-25 — App provisioning 與 Home 啟動

- 需要 Google Play 時必須切到 `google_apis_playstore` system image；單純側載 Play Store APK 不等於有 Play services/授權環境。
- Android `DocumentsUI` 不是可靠的檔案管理首頁；要有可點的「檔案管理」入口，應安裝並驗證實際 file manager package，例如 `me.zhanghai.android.files`。
- Home App 不能只手刻四個 intent；固定入口要置頂，但也要掃描 `ACTION_MAIN` + `CATEGORY_LAUNCHER`，讓 Google Play 新安裝的 app 自動出現在首頁。
- 驗證 Launcher 點擊時要用 `uiautomator` 的 tile bounds / content-desc 點擊，再用 foreground package 判斷；只看圖示存在或用固定座標容易誤判。
- 固定入口不能以灰色 disabled tile 交付；Chrome / Files 缺失時要有內建 fallback Activity，否則使用者看到的是假入口。
- 動態掃描不可無差別追加 system launchers；只追加 user-installed packages，否則 Play image 會把 `Settings`、`TMobile` 這類系統殘留塞回乾淨首頁。
- 使用者回報「開模擬器後音樂卡、雜音」時，要先看 host 資源搶占：qemu 不可 High priority、vCPU 不可預設吃滿 4 核、開機前不可啟動 gRPC screenshot loop；`enableAudio=false` 時也不可掛 `virtio-snd-pci`。
- 不可用 ADB shell 處理滾輪/滑動主路徑；`adb shell input swipe` 每次都 spawn shell，會造成 host 與 Android input queue 抖動。wheel 要走 emulator gRPC touch swipe 並 throttle，高成本 ADB 只能當 fallback。
- BelowNormal priority 不一定比較好；它能保護 host audio，但本機 app switch smoke 曾掉到 41-46 FPS。預設要用 2 vCPU + Normal priority + 不高於 Normal 的上限，再靠降低 raw capture 尖峰取得 host headroom。
- gRPC raw capture 的預設值必須用 runtime smoke 證明；960x540 可能掉到 49.5 FPS，896x504 也曾回歸到 31 FPS，800x450 才在本機通過 min 62.2 / avg 62.6。若要更高畫質，下一步應走 shared memory/shared texture，而不是硬推 `getScreenshot` 1080p。

## 2026-05-26 — 誠實 FPS 與互動流暢度

- 主側欄 FPS 不可再顯示單純 Stream delivery；使用者體感要看 `min(Guest, Stream, Render)`，靜止畫面或 duplicate frame 顯示 0 是正確且誠實的。
- sampled fingerprint 會低估 Android 內容變化，不能拿來當誠實 Guest FPS；除非有逐 flow 驗證，否則維持 full-frame fingerprint 或更可靠的 frame-dirty signal。
- raw `getScreenshot` + `QImage`/`QQuickPaintedItem` 不是可宣稱真 1080p/60 的路徑；通知欄/滑動 flow 實測 Stream 60+ 時 Guest/Render 仍可能只有 9-14 FPS。真 60 phase 要改 shared memory/shared texture + scene graph texture renderer。
- 滾輪/拖曳的優化目標不是把事件全部送進 guest，而是照 60Hz frame pacing 合併；wheel burst 應節流到約 16ms，且單次 instant swipe request 數要最小化。
- BlueStacks 類效能方向應拆成硬體加速 boot gate、renderer profile、frame pacing、resource profile、Eco mode 與低延遲 input；不要只調高 qemu priority 或盲目提高 capture 解析度。

## 2026-05-27 — Shared texture 不是 CPU-copy

- CPU shared memory 只能降低 IPC/程序啟動成本，仍會有 CPU copy 與 Qt texture upload；不可把它宣稱為真正 shared texture。
- D3D11 shared texture 必須用 producer 建立 named shared handle，consumer 用 Qt scene graph 的 D3D11 device 在 render thread `OpenSharedResourceByName`，再交給 `QSGD3D11Texture::fromNative()`。
- 即使還在 CPU frame fallback，也不要每幀 `createTextureFromImage()` 重建 GPU texture；D3D11 RHI 下應維持 persistent texture，逐幀 `UpdateSubresource()`。
- Shared capture backend 必須是 opportunistic：沒有第一幀時要讓 gRPC fallback 接手，不能因為 env var 設錯就永久黑畫面。
- Shared frame metadata 必須用 odd/even sequence 做 seqlock；只看 header 或只測字串會漏掉 producer 寫入中的 torn frame。
- 測試 shared texture 時至少要建立真 D3D11 shared resource，並用第二個 D3D11 device 打開；只測 metadata signal 不足以證明 named handle 可用。
- 不可讓 shared texture metadata capture 依賴 UI thread QTimer；frame event 應由 worker 等待，且只有新 even sequence 才能計入 Stream/Guest，否則又會變成「看起來 60、實際沒新 frame」。
- Runtime helper producer 也不能用高成本 CPU 全圖填色來測 60fps；要用 GPU render/clear 與固定 frame pacing，否則會把 producer 自己的 30fps 誤判成 renderer 瓶頸。
