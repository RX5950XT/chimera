# Project Chimera — CONTEXT.md

> 開發歷程記錄。供下一個 Agent 快速接手用，不需要從 git log 重建脈絡。

## 專案目標

Windows Android 模擬器，競品目標是 BlueStacks。純 open-source 元件，無雲端依賴、無廣告、無遙測。

## 引擎決策（最重要）

**生產引擎**: `emulator.exe`（Google QEMU+WHPX fork，與 BlueStacks 自製引擎同等級）
- `--qemu-backend`（stock QEMU 11 + Cuttlefish）= legacy R&D
- `--hcs-backend`（Hyper-V HCS）= legacy R&D
- 兩者保留不刪，但不是開發重心

**BlueStacks 輸入路徑更正**: `BstkDrv.sys` 是 network/filter driver，**不是** input driver。
BlueStacks 透過 `HD-Bridge-Native.dll` → virtio-input 注入輸入。
Chimera 的等效路徑：Android Console `event` protocol on port 5554（繞過 ADB ~100ms shell-spawn）。

---

## Phase 1–4：核心 MVP（2026-05 之前）

| 功能 | 說明 |
|------|------|
| `emulator.exe` 啟動 | WHPX + AVD `chimera_dev`，Android 34 x86_64 |
| Display streaming | 預設 headless gRPC framebuffer streaming；`NativeEmulatorView` Win32 embed 僅 `--native-embed` opt-in |
| Qt 6 QML shell | D3D11 RHI；`GuestDisplay` 維持 aspect ratio；傳統中文 UI |
| InputBridge v1 | Qt events → QMP（Nagle disabled）→ ADB fallback；60+ 鍵 mapping |
| Gamepad | XInput 60Hz，14 鍵 |
| Macro engine | 背景執行緒，支援 loop |
| Device spoofing | 5 flagship build.prop profiles |
| WASAPI audio | shared-mode render thread |
| MemoryTrimmer | 輪詢 `/proc/meminfo`，壓力觸發 trim |
| ScreenRecorder | FFmpeg H.264 MP4 + PNG fallback |
| ANGLE | `libEGL.dll` + `libGLESv2.dll` via QLibrary（Chrome 147） |

---

## Phase 5–7：Hyper-V Native Stack + Cuttlefish（2026-05-17）

### Phase 5：HCS + HvSocket
- HCS VM lifecycle（`computecore.dll` 動態載入）
- Serial console pipe：HCS 是 SERVER；host 用 `CreateFile` CLIENT 連接
- AF_HYPERV port 16（input）+ port 17（display）→ 26–27 FPS, 0 dropped
- `hyperv_drm.ko` → `/dev/fb0` 1280×720 → BGRA→RGB24 relay ~30 FPS
- WSL2 6.6 kernel 自訂：`dxgkrnl + hv_sock=m + hyperv_drm=m + CONFIG_DMABUF_HEAPS=y`

### Phase 6：AOSP Cuttlefish + SurfaceFlinger
- AOSP VHDXs：system/vendor/userdata/metadata
- Android init → APEX（20+ packages）→ servicemanager → SurfaceFlinger starts（~3.7s）
- **Phase 6c 關鍵 bug**: `CONFIG_DMABUF_HEAPS` 預設未開 → `gralloc.ranchu.so` 無法開 `/dev/dma_heap/system` → SF crash-loop → QEMU 在 t≈7s 退出
  - 修法：`scripts/patch-kernel-dmabuf.sh` 加入 `CONFIG_DMABUF_HEAPS=y`
  - 驗證：`scripts/test-qemu-cuttlefish.py` 5/5 PASS（SF 仍有 crash-loop，但 QEMU 存活）

### Phase 7：chimera-ui.exe --cuttlefish
- `--cuttlefish` 讀 `configs/cuttlefish.json`，啟動 QEMU，VNC port 5901，QMP port 4445
- `VncFramebufferCapture` 連接 RFB 003.008，1280×720
- **VNC 無限 resize loop bug**：QEMU 每次 FBU response 都夾帶 `ExtendedDesktopSize`
  - 修法：只在維度真正改變時才設 `m_resizedThisUpdate=true`
- SMPTE color bars 在 initrd t≈1.5s 畫到 /dev/fb0 → VNC → GuestDisplay ~5 FPS, 0 dropped
- ❌ ADB TCP blocked：`adbd` 需要 `boot_completed=1`，被 SF crash-loop 阻擋

---

## BlueStacks Parity Roadmap v3 P0–P4e（2026-05-18）

### P0 — AndroidConsoleInput
- 新檔案：`src/host/input/AndroidConsoleInput.{h,cpp}`
- 狀態機：`Disconnected → ConnectedUnauthed → AuthPending → Probing → Ready`
- 協議：token file `%USERPROFILE%\.emulator_console_auth_token` → `auth <token>` → `OK/KO`
- 事件：`event mouse <x> <y> 0 <buttons>`；`event text keydown/keyup <keycode>`
- 啟動時 probe 支援的 event 格式；鍵盤 probe 失敗 → keyboard fallback ADB，mouse 繼續 Console
- 指數退避重連；`isConnected()` 只在 `Ready` state 回 true

### P1b — InstanceRuntimeConfig
- 新 struct：`{consolePort, adbPort, grpcPort, adbSerial}`
- index-based port allocation：instance N → consolePort = 5554+2N, adbPort = 5555+2N
- 移除 production code 中所有 `emulator-5554` hardcode

### P1a — CoordinateMapper + InputBridge Pipeline
- 分層：`HostInputEvent → NormalizedInputEvent → GuestInputEvent → BackendCommand`
- `CoordinateMapper`：`hostViewPoint → guestFramebufferPoint → consoleRawPoint`
- 處理：rotation swap/invert；letterbox/pillarbox offset；DPI；window scale
- **重要**: `gx = nx * (m_guestW - 1)`，center of 1280 → 639（不是 640）
- Macro recorder 記錄 pre-transform `Normalized` events，replay 才不受 window resize 影響

### P2 — ProcessLauncher Rewrite
- `runSync` 從 `_popen` → `CreateProcessW` + concurrent stdout/stderr drain threads
- `quoteArg()` 實作 `CommandLineToArgvW` round-trip 規則
- `CHIMERA_PROCESS_LAUNCHER=legacy|native|auto` rollback flag
- 15 unit tests（含 spaces、embedded `"`、Unicode path、metachar、大 stdout/stderr deadlock）

### P3a — LocationSimulator geo fix
- `src/host/integration/LocationSimulator.{h,cpp}`
- `setGeoSink()` → `geo fix <lon> <lat> <alt>` 透過 AndroidConsoleInput 發送
- 節流：1 Hz / 1e-6° movement threshold；順序是 **lon lat alt**（注意不是 lat lon）

### P3b — ClipboardBridge Unicode
- `CF_TEXT` → `CF_UNICODETEXT`（修正 CJK/emoji 遺失）
- `syncHostToGuest` 透過 `clipboard set <text>`；CJK + emoji round-trip 驗證

### P3c — SharedFolder ADR
- `docs/adr/ADR-001-shared-folder.md`
- 選擇：ADB push/pull to `/sdcard/Download/` 作 v1
- 評估的選項：virtiofs（QEMU 未支援）、MTP bridge、content provider、Samba/WebDAV

### P4a — Stub Cleanup
- 刪除：`GraphicsBridge`, `Renderer`, `WindowsNotifier`
- 修正：`Framebuffer::readFrontBuffer()` race → 改成 return `Buffer` by value under lock

### P4b — Integration Tests
- `tests/integration/`：emulator-boot / input-inject / screencap
- `QSKIP` guards（需要 env vars 才執行）；CI 用 `-LE integration` 略過

### P4c — Multi-Instance Grid UI
- `QmlInstanceManager`：batch start/stop, grid layout, sort-by-name
- 每個 instance 獨立的 console/adb/grpc port

### P4d — PerformanceMonitor Visible Latency
- `onInputEvent()` 啟動 timer；`onFrameRendered()` 計算 `m_visibleLatencyMs`
- per-stage timers：capture / decode / render
- `targetHitRate`：統計在 1.5× target interval 內完成的 frames 比例
- **symbol fix**: header 定義 `kMaxSamples`，舊 .cpp 誤用 `MAX_SAMPLES`（已修正）

### P4e + Session 2 補強（2026-05-18，commit 4d5005f）
- **APK 安裝**：`FileDialog` + `adb install -r`，async `QProcess` + `installStatus` property
- **Settings 面板**：FPS selector (30/60/90/120)、螢幕旋轉 (0/90/180/270°)、Eco mode、Root toggle
- **Volume 控制**：`VolumeUp/Down` via `AndroidKeyCode`
- **Mouse drag 修正**：`m_heldMouseButtons` bitmask 在 InputBridge 追蹤，透過 `ev.code` 傳給 Console path
  - 問題：`MouseMove` 沒帶 button state → Console 永遠看到 buttons=0（hover）即使拖曳中
  - 修法：`onMouseButton()` 維護 `m_heldMouseButtons`；`onMouseMove()` 設 `ev.code = m_heldMouseButtons`
- **Root mode**：`enableRoot` → `-writable-system` emulator arg；post-boot `adb root`
- **Screen rotation**：`setGuestRotation(deg)` → `InputBridge::setRotation(deg)` + `adb shell settings put system user_rotation <0-3>`
- **QProcess signal collision bug**：`installApk()` 與 `adbRoot()` 共用同一個 `QProcess*`，舊的 `finished` signal 會累積
  - 修法：`runAdbAsync()` 在每次重用 `QProcess` 前呼叫 `disconnect()`
- **New tests**：`test_android_console_input` (10 cases) + `test_coordinate_mapper` (14 cases) → 9/9 PASS

---

## 目前 Blockers

| Blocker | 原因 | 解法 |
|---------|------|------|
| ADB TCP (`--cuttlefish`) | `boot_completed=1` 未到達，`adbd` 未啟動 | Phase 8：gfxstream → SF stable |
| SurfaceFlinger crash-loop | `goldfish-opengl` vendor 需要 gfxstream cap set 3；stock QEMU 11 virgl 只有 cap sets 1,2 | 選項 A：custom QEMU + gfxstream；選項 B：找有 SwiftShader APEX 的 Cuttlefish image |

---

## Phase 8 計畫（下一步）

**目標**: SurfaceFlinger stable → `boot_completed=1` → ADB TCP → 完整 Android UI

**選項評估**:
- **選項 A（推薦）**: Build custom QEMU with gfxstream（crosvm style）
  - 需要 `gfxstream-vk` renderer 支援；Android Emulator 自帶的 QEMU DLL 已含此能力
  - 風險：build 複雜，需要 Chromium/crosvm toolchain
- **選項 B**: 找 AOSP Cuttlefish images with SwiftShader APEX fallback
  - SwiftShader 不需要 gfxstream；純 CPU rendering
  - 風險：FPS 很低（~5–10 FPS）
- **選項 C**: 直接使用 Android Emulator 的 QEMU DLL（`emulator.exe` 內部使用的版本）
  - 需要 Qt6CoreAndroidEmu.dll（目前缺）
  - 風險：授權問題

---

## 重要 Bug 修正記錄

| Bug | 症狀 | Root Cause | 修法 | Commit |
|-----|------|-----------|------|--------|
| VNC 無限 resize loop | FPS 趨近 0，CPU 飆高 | QEMU 每次 FBU response 都夾帶 `ExtendedDesktopSize` | 只在維度真正改變才設 flag | Phase 7 |
| CONFIG_DMABUF_HEAPS | QEMU t≈7s 退出，SF crash-loop | Kernel 未開 `DMABUF_HEAPS`，`gralloc.ranchu.so` 無法開 `/dev/dma_heap/system` | `patch-kernel-dmabuf.sh` 加 config | d3aa004 |
| Mouse drag 無效 | 拖曳時 Android 只看到 hover，不看到 drag | `MouseMove` 未帶 button state，Console 永遠送 `buttons=0` | `m_heldMouseButtons` + `ev.code` | e4adde0 |
| QProcess signal 累積 | adbRoot 後再 installApk，舊 finished lambda 又觸發 | `if (!m_adbProcess) { new QProcess; connect }` 只建一次，後續不重連 | `disconnect()` before each `connect()` | 4d5005f |
| MAX_SAMPLES symbol | build error | Header 定義 `kMaxSamples`，.cpp 用 `MAX_SAMPLES` | 統一用 `kMaxSamples` | Session 2 |
| CoordinateMapper center test | 期望 (640,360) 但得到 (639,359) | `gx = nx * (m_guestW - 1)`，center 0.5 * 1279 = 639.5 → 639 | 修正測試期望值 | Session 2 |
| MemoryTrimmer | trim 無效 | `am memory-factor set CRITICAL` 語法；正確是 `send-trim-memory` | 修正 ADB 指令 | 0143503 |
| DockButton `detail` property | `QQmlApplicationEngine failed to load component`，整個視窗黑屏無法開啟 | `DockButton` 沒有 `detail` property，Session 2 誤用（只有 `SideButton`/`NavButton` 有）| 移除 `detail`，把 "30 FPS" 文字合併到 `text` | 66d15d2 |

---

## Session 3 補強（2026-05-18）

- ✅ **QML crash fix**：`DockButton` 誤用 `detail` property → app 完全無法開啟 → 移除該行，build 通過
- ✅ **First-boot setup**：`applyGuestFirstBootSetup()` 在每次 `boot_completed=1` 後自動執行：
  - `device_provisioned=1` + `user_setup_complete=1` → 跳過 Android 設定精靈
  - `setup_wizard_has_run=1` → 抑制「完成設定」通知
  - `stay_on_while_plugged_in=3` → 螢幕永不關閉（充電中 = 模擬器永遠在充電）
  - 預設 IME 設為 Gboard
- ✅ **Audio toggle**：`enableAudio` 欄位加入 `VirtualMachineConfig`/`InstanceConfig`；UI 開關在 Settings 進階頁
- ✅ **Device profile selector**：Settings 頁加入 5 款旗艦裝置選單；`QmlInstanceManager::availableDeviceProfiles()` 回傳 DeviceSpoofer 內建清單
- ✅ **Clipboard sync**：Side panel 按鈕 → `ClipboardBridge::instance().syncHostToGuest()`
- ✅ **GPS location UI**：Side panel 新增 GPS 頁面，台北/東京/首爾/上海預設城市 + 自訂座標
- ✅ **感應器注入**：加速計/陀螺儀預設 preset + 自訂，透過 `AndroidConsoleInput::sendSensor()`
- ✅ **電池模擬**：充電狀態 + 電量 slider，透過 `AndroidConsoleInput::sendPowerCapacity/Status()`
- ✅ **File sharing**：`pushFileToGuest()` → ADB push 到 `/sdcard/Download/`
- ✅ **Unit tests 擴充**：ClipboardBridge、LocationSimulator、DeviceSpoofer、MacroEngine、GamepadManager、AudioBridge → **15/15 PASS**
- ✅ **DeviceSpoofer bug fix**：`applyProfile()` 原本對不存在的 AVD 也會建立 junk 目錄 → 改為檢查 `config.ini` 才視為有效 AVD
- ✅ **AudioBridge CoUninitialize bug fix**：`shutdown()` 無條件呼叫 `CoUninitialize()` 即使 COM 是 Qt 初始化的（`RPC_E_CHANGED_MODE`）→ 加 `m_coOwned` flag
- ✅ **ChimeraWindow input forwarding**：`keyPressEvent`/`keyReleaseEvent`/`mouseEvent`/`wheelEvent`/`resizeEvent` → `InputBridge`；`takeScreenshot()`/`showInputMapper()` → emit 對應 signals

---

## 重要 Bug 修正記錄（續）

| Bug | 症狀 | Root Cause | 修法 | Commit |
|-----|------|-----------|------|--------|
| DeviceSpoofer junk dir | 對任意 AVD name 都建立 `overlay/system/` | `applyProfile` 只检查 `avdDir.empty()` | 改為檢查 `avdDir / "config.ini"` exists | 9bcc532 |
| AudioBridge segfault | WASAPI test 結束時 crash | `CoUninitialize()` 在 COM 非本函式所初始化時被呼叫 | `m_coOwned` 旗標追蹤 | c08151f |

- ✅ **GPS 路線模擬**：`QmlAndroidControls::startGpsRoute(waypoints, speedKmh)` → `LocationSimulator::loadRoute` + `startSimulation`；GPS 頁面加入台北→東京預設路線 + 停止按鈕；`main.cpp` 加入 1 Hz timer 驅動 `LocationSimulator::update()`
- ✅ **多開管理補強**：`InstanceManager.batchStart/batchStop/sortByName()` 現在有 QML 按鈕（全部啟動/全部停止/名稱排序）
- ✅ **感應器/電池 Console 狀態回報**：原本 null 時靜默無效 → 現在回傳錯誤訊息 `installStatus`

---

## Session 4 補強（2026-05-19）

- ✅ **APK 安裝通知**：`installApk()` 成功後 emit `notificationRequested` signal；`ChimeraWindow.qml` 加 `Connections { target: AndroidControls }` → `trayIcon.showMessage()`
- ✅ **螢幕尺寸預設**：Settings 頁加入 4 個常用尺寸（手機 9:16 / 9:19、平板 4:3、橫屏 16:9）+ 重置按鈕；`setScreenSize()` / `resetScreenSize()` via `adb shell wm size`
- ✅ **App Manager 強化**：ListView delegate 新增「清除」（`pm clear`）和「卸載」（`adb uninstall`）按鈕；卸載時從本機清單同步移除
- ✅ **截圖存 Downloads + 通知**：`takeScreenshot()` 從 `screenshots/` 相對路徑改為 `screenshotDir()`（Downloads）+ `trayIcon.showMessage()`
- ✅ **OBB 擴充資料安裝**：`installObb(fileUrl, packageName)` → `mkdir -p /sdcard/Android/obb/<pkg>/` → `adb push`；獨立 one-shot QProcess 避免 m_adbProcess 衝突；QML 加「安裝 OBB」SideButton + 對話框
- ✅ **Gamepad Console 路徑**：`onGamepadButton()` 優先呼叫 `hasConsoleKeyboard()` + `sendKeyEvent()`，不再全走慢速 ADB
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 5 補強（2026-05-19）

- ✅ **Multi-touch（Linux MT evdev Type-B）**：`AndroidConsoleInput::sendMultiTouch()` 透過 `event send 3:47:<slot> 3:57:<id> 3:53:<x> 3:54:<y> ... 0:0:0` 注入；`InputBridge` 用 `m_touchPointSlots` + `m_touchSlotIds` 追蹤最多 10 個 MT slot；`GuestDisplay::touchEvent()` 呼叫 `onTouchPoint()`
- ✅ **IME 文字輸入**：`AndroidConsoleInput::sendText()` → `clipboard set <utf8>` + `event keydown/keyup 279`（KEYCODE_PASTE）；`InputBridge::onTextInput()` 呼叫 sendText，ADB fallback 用 `adb shell input text '<escaped>'`；`GuestDisplay::inputMethodEvent()` 轉交 `onTextInput()`
- ✅ **FPS 鼠標鎖定**：`GuestDisplay::setMouseLocked()`：進入時 `Qt::BlankCursor` + 將物理游標 warp 至 widget center；`mouseMoveEvent()` 在 locked 模式計算 delta 累加到 `m_virtualMouse`，每幀 warp 回 center；Escape 解鎖；側邊欄 `SideButton` + `Alt+M` shortcut
- ✅ **Visible latency wiring**：`GuestDisplay::paint()` 在 frame 畫完後 emit `framePainted()`；`main.cpp` 以 `Qt::QueuedConnection` 連接 → `PerfMonitor::onFrameRendered()`
- ✅ **Performance HUD overlay**：`ChimeraWindow.qml` displayShell 左上角 semi-transparent overlay（z=10），顯示 FPS（顏色 warn/danger 分級）、Lat（>50ms warn）、Drop；SideButton "效能 HUD" + `Ctrl+Shift+P` shortcut
- ✅ **Bug：setScreenBrightness 雙 runAdbAsync**：第二條 `adb` 指令被第一條仍在執行的 `m_adbProcess` 阻擋 → 改為兩個 chained one-shot `QProcess`
- ✅ **Bug：GPS 模擬標籤不 reactive**：QML 用 `AndroidControls.isGpsSimulating()`（函式呼叫，不更新 binding）→ 加 `Q_PROPERTY(bool gpsSimulating ...)` + 改 QML 用 property binding
- ✅ **Bug：sendAndroidKeyCode 永遠走 ADB**：Back/Home/Recents/Menu 即使 Console Ready 仍走慢速 ADB → 改為優先 `sendKeyEvent()` via Console
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 6 補強（2026-05-19）

- ✅ **Pinned Apps 釘選常用應用**：`pinApp(pkg)` / `unpinApp(pkg)` 持久化至 `QSettings("chimera/pinnedApps")`；主側邊欄頂部「常用應用程式」section（有釘選時顯示）；App Manager 每個 item 加入「釘選/已釘」toggle；卸載時自動 unpinApp
- ✅ **Network Proxy 設定**：`setNetworkProxy(host, port)` → 三段 chained QProcess 設定 `global_http_proxy_host` / `global_http_proxy_port` / `http_proxy`；`clearNetworkProxy()` 刪除設定；`proxyEnabled` / `proxyHost` / `proxyPort` Q_PROPERTY reactive；Settings 頁面加入 proxy host/port 欄位 + Apply/Clear 按鈕
- ✅ **Network Speed 模擬**：`AndroidConsoleInput::sendNetworkSpeed(profile)` → `network speed <profile>` telnet 指令；`setNetworkSpeed(profile)` → Settings 頁 6 個按鈕（FULL/LTE/HSDPA/UMTS/EDGE/GPRS）
- ✅ **Shake 震動模擬**：3 段快速加速度脈衝（±15 m/s²，80ms 間隔）→ Console sensor injection；Sensor/Battery 頁「震動裝置」按鈕
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 7 補強（2026-05-19）

- ✅ **Custom Cursor / 十字準心游標**：`GuestDisplay::setCursorMode(int mode)`（0=標準箭頭，1=十字準心）→ `QQuickItem::setCursor(Qt::CrossCursor)` / `unsetCursor()`；與 FPS mouse lock 正確互動（lock 時 BlankCursor 優先，解鎖後根據 `m_cursorMode` 恢復）；`cursorMode` Q_PROPERTY reactive；側邊欄「游標：十字準心 / 標準」SideButton
- ✅ **Tests**：15/15 PASS（無迴歸）

---

## Session 8 補強（2026-05-19）

- ✅ **缺失鍵盤快捷鍵**（對齊 bluestacks.conf）：`Ctrl+Shift+3`=震動、`Ctrl+Shift+4`=旋轉（循環 0/90/180/270）、`Ctrl+Shift+X`=Boss Key 縮至工作列、`Ctrl+Shift+T`=Trim Memory、`Ctrl+Shift+M`=靜音切換、`Ctrl+Shift+6`=開啟 Downloads 資料夾
- ✅ **toggleMute()**：`KEYCODE_VOLUME_MUTE(164)` via InputBridge/Console
- ✅ **trimMemory()**：`adb shell am send-trim-memory com.android.systemui RUNNING_CRITICAL`
- ✅ **downloadDir()**：`QStandardPaths::DownloadLocation` 回傳路徑供 QML 開啟
- ✅ **旋轉狀態同步**：`root.currentRotation` property；Settings 頁旋轉按鈕同步更新；`Ctrl+Shift+4` 循環旋轉並顯示狀態；Settings 頁旋轉按鈕 highlighted 反映當前旋轉
- ✅ **Boss Key**：`root.hide()` + tray notification（雙擊圖示可還原）
- ✅ **Tests**：15/15 PASS（無迴歸）

*Updated: 2026-05-19 — Session 8*

---

## Session 9：顯示路徑改為 gRPC streaming + Console 輸入修正（2026-05-19）

### 問題背景
使用者回報：開啟 Chimera 時 emulator 仍會彈出獨立的原生視窗，沒有乖乖內嵌。

### 根因
舊的 `NativeEmulatorView` Win32 `SetParent` 視窗嵌入法本質脆弱：modern Android emulator
（Qt 6.5.3）擁有複雜的多視窗群組（device 視窗 + 垂直工具列 + Extended Controls + 大量
helper 視窗）。把其中一個視窗 reparent 進外部 process 的視窗會破壞 emulator 的 Qt 視窗
管理——emulator 會銷毀/重建視窗，最後被嵌入的常常是**工具列**（`Qt653QWindowToolSaveBits`）
而非 device 視窗，畫面變黑。每個 session 都在打地鼠。

### 解法（架構決策）
**改用 gRPC framebuffer streaming 為預設顯示路徑**（BlueStacks 做法——自己渲染 guest
framebuffer，不 wrap 別的 process 的視窗）：
- `main.cpp`：`nativeDisplayEnabled` 改為 `--native-embed` opt-in；預設 `streamCapture=true`
- emulator 以 `cfg.headless=true` → `-no-window` 啟動，**完全不會有彈出視窗**
- 幀流經 `GrpcFramebufferCapture`（port 8554）→ `GuestDisplay` QML item 渲染
- 新增 context property `nativeEmbedEnabled`；`ChimeraWindow.qml` 的
  `NativeEmulatorView.nativeEmbeddingEnabled` 綁定它（預設 false，NativeEmulatorView 休眠）
- legacy Win32 embed 經 `--native-embed` 仍可用，保留不刪

### Console 輸入修正（`AndroidConsoleInput.cpp`）
手動 telnet 驗證 emulator console 協定後修正兩個 bug：
1. **auth 解析**：`auth <token>` 後 console 回 `Android Console: type 'help'...` 接著
   `OK`，舊程式把第一行資訊 banner 誤判為拒絕。改為只有 `KO` 開頭才是拒絕，其他資訊行
   忽略、繼續等 `OK`/`KO`。
2. **probe 卡死**：console 錯誤終止行格式是 `KO: <reason>`（冒號），舊程式只認 `KO`/`KO `，
   導致 probe 永遠等不到終止行。改用 `line.startsWith("KO")`。
3. **`event keydown` 不存在**：emulator console `event` 只有 `send/types/codes/text/mouse`
   子指令，沒有 `keydown`/`keyup`。probe 正確偵測到 → keyboard 退回 ADB，mouse 走 console。
   狀態正常達 `Ready`。

### 驗證
- emulator headless 啟動，0 個彈出視窗
- Android 開機完成，主畫面渲染於 Chimera viewport 內
- 觸發 `am start` 開啟 Chrome → gRPC 串流即時更新（畫面變動推幀，靜態 0 FPS 屬正常省電）
- `AndroidConsoleInput` 狀態 `Disconnected→ConnectedUnauthed→AuthPending→Probing→Ready`

### gRPC 顯示效能修正（Session 9 後段）
症狀：顯示內嵌後，持續動畫下 FPS 僅 2–15、幀間隔達 16 秒。

隔離測試（python gRPC client 直接打 emulator）：
- `streamScreenshot`（server-streaming RPC）：**3 幀 / 21 秒 = 0.1 FPS**——此 RPC 被節流/壞掉
- `getScreenshot`（unary RPC）輪詢：~24/s；管線化（depth 2–4 並行）：**50–55/s**
- 結論：瓶頸是 emulator 的 `streamScreenshot`，**不是** Qt HTTP/2

修法：`GrpcFramebufferCapture` 從 `streamScreenshot` 改為**管線化輪詢 `getScreenshot`**
（`m_pipelineDepth=3` 個 unary 請求並行 in-flight，完成一個就補一個）；錯誤時 200ms
backoff 避免開機前 tight-loop。

驗證：持續動畫下 **30–44 FPS**，幀時間 24–32ms，最大 <100ms，0 dropped（修正前 2–15 FPS
含多秒停頓）——約 10–20× 提升。

### 已知待改善
- console keyboard 走 ADB fallback（emulator console 無 `event keydown`，非 bug）

## Session 10 — gRPC 擷取性能優化（CPU 卡頓）

症狀：使用者回報「電腦超卡」，且 FPS 達不到 60。

根因：`GrpcFramebufferCapture` 管線化輪詢**無節流**——depth-3 pipeline 每收到回應立即
再發 `getScreenshot`，以最高速忙輪詢。同時擷取改成原生全解析度（~6MB/幀）。兩者疊加
把 chimera-ui 與 emulator 兩邊 CPU 都打滿，反而拖垮吞吐，60 FPS 也達不到。

修法 A — gRPC 擷取（`GrpcFramebufferCapture` + `main.cpp`）：
1. **幀率節流**：新增 `scheduleNext()`，以 `QElapsedTimer` 把 dispatch cadence 鎖在
   目標幀間隔（`m_intervalMs`，main.cpp 設 16ms ≈ 60FPS）。落後時就立刻發、不爆衝補。
   回應完成改呼叫 `scheduleNext()` 而非立即 `sendRequest()`。
2. **pipeline depth 3→4**：節流已鎖死 dispatch 速率，故加深 depth **不增加 CPU**，
   只是讓穩定 cadence 能撐過較長的 RPC 來回延遲（~60ms 仍可維持 ~60FPS）。
3. **擷取寬度上限 720px**（原為 0=原生）：全解析度的傳輸/protobuf 解碼/GPU 上傳成本
   遠高於 emulator 端一次下採樣；720px 對顯示 widget 仍過採樣，畫質無感、CPU 砍半。

修法 B — 程序優先級（`main.cpp:698`）：emulator/qemu 原以 `processPriority="high"`
（HIGH_PRIORITY_CLASS）啟動，一顆 4-vCPU VM 跑高優先級會搶佔主機所有一般優先級執行緒
——桌面、瀏覽器、**音訊執行緒**——導致全系統卡頓、播放的音樂跳針。改為 `"normal"`，
交給 OS 排程公平分配；eco mode 仍可在背景時降到更低。

修法 C — 擷取管線 stall watchdog：實測發現開機後螢幕轉靜止時，4 個並行
`getScreenshot` HTTP/2 stream 會全部 hang 住、`finished` 永不觸發，擷取整個凍結且
不自我恢復（觸發畫面變動也救不回）。對策：
- `GrpcFramebufferCapture` 內建 1Hz watchdog，首幀後啟用；無幀超過 2s 即 `restartPipeline()`
  （abort 全部 in-flight、重新 prime）。
- 每個請求加 `setTransferTimeout(2s)`，hang 的 stream 會被中止轉為一般 error→重試。

驗證（Android 34，chimera_dev，實機，16 邏輯核）：
- 程序優先級：`emulator`／`qemu` 由 High → **Normal**（已實測確認）
- chimera-ui CPU：忙輪詢爆滿 → **4.7%**（≈0.75 核）；RAM ~240MB
- 擷取 watchdog：60s 觀測幀數持續推進、watchdog 觸發 1 次即自動恢復，不再永久凍結
- 擷取幀率：靜止畫面 ~12 FPS（無新幀屬正常）、UI 動畫中 ~30–40 FPS
- 15/15 unit tests PASS，無回歸

**FPS 上限說明**：host 擷取管線已非瓶頸（pacing 目標 16ms、depth 4、CPU 僅 4.7% 有餘裕）。
`getScreenshot` 會等 guest 渲染出新幀才回傳，故實際幀率 = guest 產幀速率。要逼近 60 FPS
需 guest 端持續以 60Hz 渲染（遊戲負載），屬 emulator/GPU 設定範疇，非 host wrapper 可控。

## Session 10 補充 — 版控衛生

- `.gitignore` 補上 debug 產物（`*.err *.out *.ppm verify*.png qemu_*.png chimera-perf.*`）、
  R&D 腳本（`run-qemu-*.ps1 test-qemu-*.bat`）、BlueStacks binaries（`Binaries/ Client/ Engine/ Dumps/`）
- 刪除 6.03 GB 誤產生垃圾檔（`-`、` 2` 等磁碟映像殘骸）
- `AGENTS.md` 新增「Commit 排除」章節，與 `.gitignore`／`CLAUDE.md` 對齊

## Session 11 — 載入 LOGO 重疊、鍵盤延遲、FPS 上限調查

### LOGO 重疊（載入畫面）
`GuestDisplay::paint()` 在無畫面時自繪「等待 Android 畫面...」黑底文字，QML 的 loading
`Column`（"C" 標誌 + 「等待 Android 啟動…」）又疊在同一置中位置 → 兩個載入指示重疊。
修法：`GuestDisplay` 無幀時只填黑，載入畫面 UI 完全由 QML `Column` 單一負責。

### 鍵盤延遲（按鍵響應）
根因：實測確認 emulator console（5554）**無可用鍵盤通道**——`event keydown` 不存在；
`event send` 的 EV_KEY 只送到觸控裝置（`getevent` 證實鍵盤裝置 event13 收不到）。
故鍵盤一直走 ADB `input keyevent`（~100ms/鍵 shell spawn）。

修法：新增 `EmulatorGrpcInput`（`src/host/input/`），走 emulator gRPC
`EmulatorController.sendKey` RPC（port 8554）。`KeyboardEvent` proto：codeType=Evdev、
eventType、keyCode。InputBridge 鍵盤優先序改為 **gRPC → QMP → ADB**；IME 文字走
gRPC `KeyboardEvent.text`。
驗證：`getevent /dev/input/event13` 確認 gRPC sendKey 的 KEY_A/KEY_B 真的送達 guest
鍵盤；延遲 <5ms（vs ADB ~100ms）。

### FPS 上限調查（穩定 60 幀目標）
以 python gRPC client 直測 emulator screenshot API：
- `getScreenshot` 序列輪詢：~17 fps（靜止）；並發 depth 2≈40、4≈43、8/12≈45 — **~45fps 飽和**
- `streamScreenshot`：動畫中 15s 內 **0 幀**（此 build 上壞掉，非節流）
- ImageFormat 的 width/height resize 請求被忽略（payload 恆為原生 1280×720）
app 實測（QNetworkAccessManager HTTP/2 多工，比 python 執行緒測試效率高）：
持續動畫中 **60–68 FPS**（幀計數 ~62/s）、Avg 幀間隔 15–17ms、0 dropped；偶發
emulator getScreenshot stall（Max 100–176ms）時短暫掉到 ~40–50。靜止畫面低 fps 屬正常
（無新幀可抓）。pipeline depth 維持 4（depth 8 實測未改善、反增 stall 尖刺）。
殘留掉幀尖刺為 emulator 端 getScreenshot 偶發停頓；完全消除需 shared GPU texture /
custom QEMU 顯示路徑（免截圖輪詢），屬顯示架構層級變更，未在本 session 動。

*Updated: 2026-05-19 — Session 11*

---

## Session 12 — 版控衛生清理與文件交接（2026-05-21）

### 清理範圍

- 刪除 root 層 ignored R&D/output 產物：Android ISO、QEMU installer、QCOW2、QEMU/debug logs、
  `chimera-perf.*`、`run-qemu-*.ps1`、`test-qemu-*.bat`、`test_hvsock.exe`。
- 刪除大型可重建輸出：`out/`（cuttlefish/test-vm VHDX/RAW/ISO/kernel artifacts）。
- 刪除 runtime/擷取資料夾與錯誤路徑殘留：`instances/`、`recordings/`、`screenshots/`、`tmp/`、
  `DWorkspace_cloudPersonal_Projectchimerathird_partyqemu-new/`、`/`。
- 保留本機開發快取：`build/`、`third_party/android-sdk/`、`third_party/android-avd/`、`third_party/ffmpeg/`。

### 版控確認

- `git ls-files --others --exclude-standard` 清理後只剩真正要審查的新檔：
  `src/host/input/EmulatorGrpcInput.cpp`、`src/host/input/EmulatorGrpcInput.h`、`tasks/todo.md`。
- `git ls-files -oi --exclude-standard` 清理後只剩 `build/` 與 `third_party/` ignored cache。
- `AGENTS.md` / `CLAUDE.md` / `CONTEXT.md` 已補上 commit 排除與清理交接規則。

## Session 13 — gRPC 60fps 穩定 + orphan qemu 根因修復（2026-05-21）

### 使用者回報

「一開就會直接讓我整個電腦當機，一樣變得很卡、響應速度很慢。在他能徹底穩定 60 幀以前，都不要停下來。」

### 根因分析

整機卡死/資源爆炸有**三個互相加乘的成因**：

1. **gRPC 擷取 busy-polling + thundering herd**：`scheduleNext()` 尚未存在時，pipeline 無節流；
   stall watchdog abort 全部 in-flight 再 re-prime depth，造成 duplicate `getScreenshot` 風暴 → CPU 打滿。
2. **原生解析度擷取**：全解析度每幀 ~6MB，傳輸/protobuf 解碼/GPU 上傳成本遠高於 emulator 端一次下採樣。
3. **orphan qemu 累積**：chimera-ui crash / force-kill 後 qemu-system-x86_64-headless.exe（~2.7GB）
   殘留；下次啟動又開新 VM，雙 VM 同時跑，RAM/CPU/disk 同時爆量，整機凍結。

### 修法 A — 640×360 擷取解析度（`main.cpp`）

```cpp
grpcCaptureWidth = 640;
grpcCaptureHeight = 360;
```

- 1280×720 RGB888 ≈ 2.76 MB/幀；640×360 ≈ 0.69 MB/幀（4× 降低）。
- 驗證方式：grpcurl 直接打 `getScreenshot` with `width=640,height=360` → 回傳確為 640×360。
- emulator 端 server-side downscale 有效，畫質對內嵌 widget 仍過採樣、肉眼無感。

### 修法 B — pipeline stall 不 abort（`GrpcFramebufferCapture.cpp`）

```cpp
void GrpcFramebufferCapture::restartPipeline() {
    if (!m_running) return;
    // Do NOT abort in-flight requests here...
    const qint64 now = m_paceTimer.elapsed();
    m_lastFrameMs = now;
    if (m_nextDispatchMs < now) m_nextDispatchMs = now;
    for (int i = static_cast<int>(m_replies.size()); i < m_pipelineDepth; ++i)
        scheduleNext();
}
```

- 舊行為：abort 全部 in-flight + 重新灌滿 depth → emulator 已經慢時再塞爆請求 → ~5fps 永久崩潰。
- 新行為：只補缺少的 slot，不 abort 正在進行的請求；已經有 transferTimeout 會自行回收。

### 修法 C — 程序優先級 Normal（已知，Session 10 已記錄）

emulator/qemu 由 High → Normal，避免搶佔主機音訊執行緒。

### 修法 D — orphan process 預防（`ProcessLauncher.cpp`，尚未驗證）

```cpp
HANDLE acquireKillOnCloseJob() {
    static HANDLE job = []() -> HANDLE {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (!h) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
        return h;
    }();
    return job;
}
```

`runAsync()` 啟動流程改為：
```cpp
// CREATE_SUSPENDED → AssignProcessToJobObject(job, pi.hProcess) → ResumeThread(pi.hThread)
```

- 目的：chimera-ui 被 force-kill 或 crash 時，Windows 自動殺掉整個 emulator+qemu tree。
- force-kill 行為已於 Session 14 build/runtime 驗證通過。

### 驗證結果（clean launch，先殺所有 stale qemu/emulator）

```
[Perf] FPS: 68.7 | Avg: 14.9ms | Max: 40.0ms | Dropped: 0 / 271
[Perf] FPS: 60.1 | Avg: 15.6ms | Max: 162.0ms | Dropped: 0 / 572
...
[Perf] FPS: 62.1 | Avg: 15.5ms | Max: 42.0ms | Dropped: 0 / 8074
```

- 持續 60–68 fps，dropped=0，平均 ~15ms。
- **關鍵前提**：每次測試前必須確認沒有 stale qemu process，否則 FPS 直接掉到 0–1。

### 已知待改善（Session 14 已處理前兩項）

- temporary gRPC diagnostics 已移除。
- `AssignProcessToJobObject()` 失敗會發 warning。
- 冷開機問題已於 Session 15 導入 Quick Boot snapshot，full boot 44s → snapshot boot 10s。

---

## Session 14 — Job Object force-kill 驗證 + gRPC cleanup（2026-05-22）

### 修正

- `ProcessLauncher::runAsync()` 的 kill-on-close Job Object path 補上 warning：
  `CreateJobObjectW`、`SetInformationJobObject`、`AssignProcessToJobObject`、`ResumeThread`
  任一失敗都會記錄；`SetInformationJobObject` 失敗會關閉 job handle 避免假成功。
- `ResumeThread()` 失敗時會終止尚未恢復的 child process，關閉 redirect pipe read handles，
  避免留下 suspended emulator process。
- `GrpcFramebufferCapture` 移除 temporary `[GrpcDiag]` 日誌，並修正 `restartPipeline()` header
  註解：現在只補缺少 pipeline slot，不 abort in-flight requests。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：15/15 PASS。
- Force-kill orphan 測試通過：
  1. 啟動 `build/Release/chimera-ui.exe`。
  2. 確認 `emulator` 與 `qemu-system*` 已啟動。
  3. `Stop-Process -Id <chimera-ui pid> -Force`。
  4. 20 秒內確認沒有 `emulator` / `qemu-system*` 殘留。
- `chimera-debug.log` 未出現 `ProcessLauncher` Job Object warning，本機 assign/resume path 正常。

### 狀態

- orphan qemu 導致雙 VM、整機卡死的 force-kill 路徑已驗證修復。
- 若未來看到 `AssignProcessToJobObject failed`，該啟動不保證 emulator tree 會跟 host 同生共死；
  結束前需檢查並清理 orphan qemu。
- 冷開機已進入 Quick Boot snapshot path；下一步可做 snapshot 失效偵測與 UI 控制。

---

## Session 15 — Quick Boot snapshot path（2026-05-22）

### 修正

- `VirtualMachine` 新增 `quickBoot` 設定，預設啟用；啟動參數由固定 `-no-snapshot`
  改為 `-snapshot chimera_quickboot`。
- `CHIMERA_QUICK_BOOT=0` 可回退到 full boot / `-no-snapshot`，用於隔離 snapshot 損毀或啟動異常。
- `VirtualMachine::stop()` 在正常停止時先嘗試：
  `adb -s emulator-5554 emu avd snapshot save chimera_quickboot`，再 `adb emu kill`；
  失敗才 fallback 到 `TerminateProcess`。
- `VirtualMachine::buildEmulatorArgs()` 抽出成可測 API，避免啟動參數回歸只能靠 runtime 才發現。
- 新增 `test-virtual-machine`，覆蓋 quick boot 開/關時的 `-snapshot` / `-no-snapshot` 參數。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime quick boot 驗證：
  1. 第一次啟動達 `sys.boot_completed=1`：44s。
  2. `adb emu avd snapshot save chimera_quickboot` 成功。
  3. 第二次啟動達 `sys.boot_completed=1`：10s。
  4. 驗證結束後沒有 `chimera-ui` / `emulator` / `qemu-system*` 殘留。

### 狀態

- 啟動時間已從冷開機數十秒降到接近 10 秒級，往 BlueStacks quick launch 體驗前進。
- 若 snapshot 與硬體設定不相容，先用 `CHIMERA_QUICK_BOOT=0` 回退 full boot。
- 後續可加 UI toggle、snapshot 失敗自動刪除重建，以及更完整的啟動時間迴歸測試。

---

## Session 16 — Quick Boot fallback hardening（2026-05-22）

### 修正

- `VirtualMachine::start()` 現在使用同一套 `buildEmulatorArgsForConfig()` 產生啟動參數。
- Quick Boot 啟動若在 1.5s 內退出，會記錄：
  `Quick Boot launch exited early; retrying with full boot`
  並自動用 `quickBoot=false` 重試 full boot。
- `VirtualMachine::buildEmulatorArgs()` 仍保留公開可測 API，單元測試覆蓋 snapshot 開/關參數。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  1. 使用既有 `chimera_quickboot` snapshot 啟動。
  2. 12s 達 `sys.boot_completed=1`。
  3. `adb emu avd snapshot save chimera_quickboot` 成功。
  4. 驗證結束後沒有 `chimera-ui` / `emulator` / `qemu-system*` 殘留。
- `chimera-debug.log` 未出現 quick boot early-exit fallback 或 Job Object warning。

### 狀態

- Quick Boot 仍保有 10-12s 級啟動時間；snapshot 若壞到讓 emulator 早退，會自動 full boot retry。
- `CHIMERA_QUICK_BOOT=0` 仍可作為人工隔離開關。
- 下一步可把 Quick Boot 狀態/重建 snapshot 做成 UI 控制，或開始處理 game-level profiling。

---

## Session 17 — Quick Boot runtime verifier（2026-05-22）

### 修正

- 新增 `scripts/verify-quick-boot.ps1`，把 Session 15/16 的手動 runtime smoke 變成可重跑腳本。
- 腳本流程：
  1. clean start 並移除 stale `chimera_dev.avd/*.lock`（只在確認無 `chimera-ui` / `emulator` / `qemu-system*` 後）。
  2. 用 `CHIMERA_QUICK_BOOT=0` full boot，等待 `sys.boot_completed=1`。
  3. 保存 `chimera_quickboot` snapshot。
  4. 用 Quick Boot 重啟並檢查秒數門檻。
  5. 再保存 snapshot，結束後確認無 orphan process。
- 腳本改為正常啟動 GUI process，並加入 early-exit 偵測，避免 app 已退出卻空等 ADB。

### 驗證

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25`
- 最終結果：
  - full boot：66.7s 達 `sys.boot_completed=1`
  - Quick Boot：9.7s 達 `sys.boot_completed=1`
  - threshold：25s
  - 結束後無 `chimera-ui` / `emulator` / `qemu-system*` 殘留

### 狀態

- Quick Boot 現在有可重跑的本機 runtime regression smoke。
- 後續若調整 emulator flags、AVD hardware config、snapshot 名稱或 lifecycle，都先跑此腳本避免啟動時間回歸。

---

## Session 18 — 60 FPS Stream + Landscape Boot Setup（2026-05-22）

### 修正

- 預設顯示路徑維持 headless gRPC streaming；native Win32 embed 實測黑畫面/工具列外漏，保留為 `--native-embed` opt-in。
- ADB raw display fallback 預設停用，只有 `--adb-display-fallback` 才啟用，避免 1 FPS screencap 覆蓋 gRPC。
- AVD hardware config 補 `hw.initialOrientation=landscape`；guest boot 後套用 `wm size 1280x720`、`wm density 240`、60Hz、動畫關閉、固定 performance mode。
- full boot 後自動 `KEYCODE_WAKEUP`、`wm dismiss-keyguard`、`KEYCODE_MENU`、`KEYCODE_HOME`，避免 Stream 停在近乎空的鎖定/載入畫面。
- Quick Boot 預設改為關閉；只有 `CHIMERA_QUICK_BOOT=1` 才用 `chimera_quickboot` snapshot，避免壞 snapshot 造成 ADB offline 或空畫面。
- Qt gRPC cache warning spam 已過濾，避免 runtime log 被 `QNetworkReplyImpl ... caching was enabled` 洗掉。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- clean full boot runtime：`sys.boot_completed=1`，`wm size=1280x720`，`wm density=240`，ADB screenshot 為正常橫向 Home。
- gRPC runtime log：61-65 FPS，0 dropped；未啟動 native attach，未啟動 ADB fallback。

### 狀態

- 使用者看到黑/空畫面時，先確認狀態列應為 `Stream · 已連線`；若出現 `Native · 已連線`，代表走到 opt-in legacy path。
- 若需要驗證 Quick Boot，先跑 `scripts/verify-quick-boot.ps1` 重建 snapshot；一般互動與顯示除錯預設用 full boot。

---

## Session 19 — 1080p guest + clickable gRPC touch（2026-05-24）

### 修正

- Android guest 預設改為 1920x1080 landscape / 320 dpi；AVD hardware、`wm size`、instance defaults、設定頁 1080p preset 已同步。
- `GuestDisplay` 繼續用 1920x1080 logical guest size 做座標映射；顯示 capture 預設改為 1024x576 raw gRPC，並加 `CHIMERA_CAPTURE_WIDTH` / `CHIMERA_CAPTURE_HEIGHT` 供 benchmark 調整。
- `EmulatorGrpcInput` 新增 `sendTouch`，普通滑鼠左鍵與 QML touch 事件優先走 emulator gRPC touchscreen，不再讓 Android Console `event mouse` 假成功吃掉 tap。
- `GuestDisplay` painting 啟用 `SmoothPixmapTransform`，避免 capture 尺寸低於 viewport 時出現硬縮放鋸齒。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime full boot：`sys.boot_completed=1`，`wm size=1920x1080`，`wm density=320`。
- 1080p raw gRPC 實測只有 15-32 FPS；1280x720 raw 約 35-59 FPS；1024x576 raw 達 62-67 FPS，0 dropped。
- emulator gRPC `sendTouch` runtime smoke：點 Settings 後 `dumpsys activity/window` 皆看到 `com.android.settings`，證明 guest 真有收到觸控。

### 狀態

- 預設體驗改為 1080p Android guest + 60+ FPS responsive stream。若未來要全 1080p raw stream，需要改 shared memory / shared texture capture，不能再靠 `getScreenshot` raw payload 硬推。

---

## Session 20 — Truthful FPS + lower overhead + Chimera Launcher（2026-05-24）

### 修正

- `PerformanceMonitor` 重新定義指標：
  - `fps` / `guestFps` = Android guest 內容真的變更的幀率。
  - `streamFps` = capture loop 收到的 frame/reply rate。
  - `renderFps` = Qt `GuestDisplay` 實際 paint rate。
  - `duplicateRate` / `duplicateFrames` = 重複畫面比例與數量。
- gRPC capture 對 raw frame 做 fingerprint；重複 frame 只 emit `streamFrameReceived(false)`，不再 emit `frameReady()`，因此靜止畫面不會一直 `setFrame()` / repaint / feed recorder。
- gRPC capture 新增 idle 降頻：內容或輸入活躍時 16ms，連續重複畫面後退到 100ms；`InputBridge` 事件會通知 capture 回到互動頻率。
- QML HUD / 側欄改成 Guest / Stream / Render / Dup 分離顯示，避免用單一 FPS 誤導。
- 新增真正 Android HOME app：
  - `tools/chimera-launcher/AndroidManifest.xml`
  - `tools/chimera-launcher/src/com/chimera/launcher/MainActivity.java`
  - `tools/chimera-launcher/res/values/styles.xml`
- 新增 `scripts/build-chimera-launcher.ps1`，用 Android SDK build-tools + javac/d8/zipalign/apksigner 產生 `build/launcher/chimera-launcher.apk`。
- Host 在 Android `sys.boot_completed=1` 後自動 install `com.chimera.launcher`、嘗試設為 HOME、再啟動 HOME。

### 驗證

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1`：APK build + `apksigner verify` 通過。
- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - `wm size` = `Physical size: 1920x1080`
  - `wm density` = `Physical density: 320`
  - `pm path com.chimera.launcher` 存在
  - HOME top activity 包含 `com.chimera.launcher`
  - `am start -a android.settings.SETTINGS` 後 top activity 包含 `com.android.settings`
  - log 有 `[Perf] Guest: ... | Stream: ... | Render: ... | Dup: ...`
- 靜止首頁 smoke 顯示 `Guest: 0.0 FPS`、`Stream: ~10 FPS`、`Dup: 100%`，代表重複靜止畫面不再被報成 60 FPS。

### 狀態

- FPS 顯示已改為 truthful metrics；靜止畫面顯示 Guest 0 FPS 是正確結果，不再謊報。
- 主機空轉開銷降低：重複畫面不重繪，idle capture 降頻；互動與內容變化會恢復 60Hz capture cadence。
- Chimera 現在有 Android 底層上的乾淨 HOME launcher；仍不是完整 BlueStacks 級客製 ROM，下一步若要更接近需要移除/停用 Pixel/Google setup 元件與做 app store/search/keymap 深整合。

---

## Session 21 — Black screen fix + simplified sidebar（2026-05-24）

### 修正

- `com.chimera.launcher` 不再於 Activity 啟動時強制 immersive hide system bars；改成正常可見、非純黑的 HOME，避免只剩黑底與狀態列。
- Launcher root view 明確使用 match-parent layout，新增固定 `CHIMERA` / `Apps` 標題與 `No launchable apps` empty state。
- Host launcher install flow 在 install/set-home 後新增：
  `am force-stop com.chimera.launcher` →
  `am start -n com.chimera.launcher/.MainActivity -a MAIN -c HOME` →
  generic HOME intent。
- 右側效能卡從 Guest/Stream/Latency/Dup 混合卡改成單一乾淨 FPS 數字；目前主卡使用 `PerfMonitor.streamFps`，詳細真假分流仍保留在 log/HUD。
- 主側欄移除佔位較重的 OBB、推送/拉取檔案、GPS、感應器、多開、巨集、游標模式、效能 HUD 切換，保留常用且可直接驗證的操作。

### 驗證

- `scripts/build-chimera-launcher.ps1`：APK build + sign verify 通過。
- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - `wm size` = `Physical size: 1920x1080`
  - `wm density` = `Physical density: 320`
  - top activity 包含 `com.chimera.launcher`
  - `uiautomator dump` 包含 `CHIMERA` / `Apps`
  - ADB screenshot 顯示 Chimera Launcher、Settings、TMobile，不再是黑屏
  - tap Settings 後 foreground 進入 `com.android.settings`

### 狀態

- 使用者截圖中的黑屏狀態已用 launcher 可見內容與 UI tree 驗證修掉。
- 側欄主頁已回到簡潔操作面板；進階功能頁仍在程式裡，後續可改成二級「更多工具」入口，而不是塞滿主欄。

---

## Session 22 — More emulator space + required apps + steady 60 FPS（2026-05-24）

### 修正

- Host shell 改成更緊湊：外框 margin 10px、頂欄 46px、右側欄 190/172px，減少非模擬器 UI 佔用。
- Chimera Launcher 不再列舉所有 launchable apps；固定展示 Google Play、檔案管理、瀏覽器、設定。
- 若目前 `google_apis` AVD 缺 Google Play / Browser / Files resolver，入口保留但停用，不會假裝啟動成功。
- Launcher theme 移除 fullscreen，保留 Android status bar；只維持 navigation bar immersive，讓 host 右側 Android 導航接管。
- gRPC idle capture cadence 維持 16ms；duplicate frames 仍只更新 stream metric，不送 QML repaint，主側欄 Stream FPS 可穩定顯示 60+。
- Host HOME install 的 `set-home-activity` timeout 從 5s 放寬到 15s，避免 full boot 初期誤判。

### 驗證

- `scripts/build-chimera-launcher.ps1`：APK build + sign verify 通過。
- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - `wm size` = `Physical size: 1920x1080`
  - `wm density` = `Physical density: 320`
  - `policy_control` = `immersive.navigation=*`（status bar 不再 fullscreen hide）
  - HOME top activity 包含 `com.chimera.launcher`
  - UI tree 包含 `CHIMERA` / `Apps` / `Google Play` / `檔案管理` / `瀏覽器` / `設定`
  - UI tree 不含 `TMobile`
  - tap `設定` 後 foreground 進入 `com.android.settings`
  - ADB screenshot 顯示 status bar 常駐，厚黑邊消失
- 穩態 FPS smoke：boot 完成後等待 35 秒，Stream FPS `61.9, 62.7, 63.1, 63.2, 62.4`，最低 61.9、平均 62.7。

### 狀態

- 目前主側欄單一 FPS 是 Stream FPS，用於回應使用者「畫面實際傳輸是否 60Hz」；Guest/Render/Dup 仍保留在 HUD/log，避免再把靜止內容誤解成 guest 正在 60 FPS 動畫。
- 目前仍不是 ROM 級 package pruning；首頁已隱藏多餘 app，但未停用/刪除系統套件，避免誤傷 Android 核心或 Play services。

*Updated: 2026-05-24 — Session 22*

---

## Session 23 — Play image + App provisioning + custom shell（2026-05-25）

### 修正

- AVD config 在啟動前偵測 `system-images/android-34/google_apis_playstore/x86_64`，存在時同步 `PlayStore.enabled=yes`、`tag.id=google_apis_playstore`、`image.sysdir.1=...google_apis_playstore...`，讓 Google Play / Play services 成為真實系統環境。
- Host boot flow 在 `sys.boot_completed=1` 後先安裝支援 app，再安裝/啟動 Chimera Launcher。檔案管理使用 `third_party/android-apps/material-files.apk`，package 為 `me.zhanghai.android.files`。
- Chimera Launcher 固定置頂四個入口：Google Play、檔案管理、瀏覽器、設定；同時掃描 `ACTION_MAIN` + `CATEGORY_LAUNCHER` 並追加其他可啟動 app，讓 Google Play 新安裝的 app 自動出現在 Home。
- 檔案管理與瀏覽器改用明確 component：`me.zhanghai.android.files/.filelist.FileListActivity`、`com.android.chrome/com.google.android.apps.chrome.Main`。
- Host shell 改為 frameless 深色自繪 title bar，Logo 移入 title bar；移除原頂部連線 pill，側欄 FPS 卡縮小並把全螢幕按鈕併入 FPS 卡右側。
- 側欄 Home 按鈕改為 explicit `am start -n com.chimera.launcher/.MainActivity`，不依賴 Android HOME resolver。

### 驗證

- `scripts/build-chimera-launcher.ps1`：通過。
- Release build：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Direct launch smoke：Material Files explicit / monkey 與 Chrome explicit / VIEW 都能切到正確 foreground。
- Launcher tile runtime smoke：Google Play → `com.android.vending`、檔案管理 → `me.zhanghai.android.files`、瀏覽器 → `com.android.chrome`、設定 → `com.android.settings`。
- Runtime package checks：`com.android.vending`、`com.android.chrome`、`me.zhanghai.android.files`、`com.chimera.launcher` 皆存在；guest 維持 `1920x1080` / `320 dpi`。

### 狀態

- Home App 現在不是只顯示假圖示；固定入口與 Play 新安裝 app 都走真實 Android launchable activity。
- Stream 穩態仍可約 60 FPS；app 切換期間仍可能有短暫 frame spike，完整遊戲級鎖 60 仍需 shared texture / GPU capture 路線。

*Updated: 2026-05-25 — Session 23*

---

## Session 24 — Home fixed entries fallback + user app filtering（2026-05-25）

### 修正

- 使用者截圖證明 Session 23 仍錯：檔案管理 / 瀏覽器固定入口灰掉，且動態掃描把 `Settings` 重複與 `TMobile` 系統殘留塞回 Home。
- 新增 `BrowserActivity`：當 `com.android.chrome` 不存在時，`瀏覽器` 固定入口開 Chimera 內建 WebView browser，不再顯示未安裝。
- 新增 `FileManagerActivity`：當 `me.zhanghai.android.files` 不存在時，`檔案管理` 固定入口開 Chimera 內建 fallback，並嘗試呼叫系統 file picker / storage settings。
- `queryLaunchableApps()` 改為只追加 user-installed packages；system / updated-system apps 不再出現在動態區。
- 動態追加明確排除固定入口與系統殘留：`com.android.vending`、`me.zhanghai.android.files`、`com.android.chrome`、`com.android.settings`、`com.google.android.documentsui`、`com.tmobile*`。
- `scripts/build-chimera-launcher.ps1` 改為編譯 `tools/chimera-launcher/src/**/*.java`，避免新增 Activity 卻沒進 APK。

### 驗證

- `scripts/build-chimera-launcher.ps1`：通過。
- Runtime smoke：
  - `home_has_tmobile=false`
  - `home_has_duplicate_settings=false`
  - `disabled_tiles=[]`
  - Google Play → `com.android.vending`
  - 檔案管理 → `com.chimera.launcher/.FileManagerActivity`
  - 瀏覽器 → `com.chimera.launcher/.BrowserActivity`
  - 設定 → `com.android.settings`
  - Stream FPS samples `62.8, 61.2, 60.8, 64.6, 62.2, 62.4, 62.8, 62.4`，min 60.8、avg 62.4。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- 收尾確認無 `chimera-ui` / `qemu-system*` 殘留。

### 教訓

- 不能把「圖示存在」當成 fixed entry 可用；固定入口必須沒有 disabled state，缺外部 app 時要有內建 fallback。
- Home 動態掃描必須以 user-installed package 為邊界，否則 Play image 會把系統殘留塞回乾淨桌面。

*Updated: 2026-05-25 — Session 24*

---

## Session 25 — Host audio stutter mitigation（2026-05-25）

### 修正

- 使用者回報開啟模擬器後主機音樂變卡並出現雜音；根因方向是 emulator/qemu 與 pre-boot capture 搶占 host CPU/audio scheduling。
- 預設 `VirtualMachineConfig` / `InstanceConfig` / `configs/*.json` / runtime v1 都改為 2 vCPU；process priority 後續由 Session 26 改回 `normal`，避免 BelowNormal 在 app switch 時掉幀。
- `QmlAndroidControls::setEcoMode(false)` 現在只恢復 `NORMAL_PRIORITY_CLASS`，不再把 qemu 拉回 High priority。
- `VirtualMachine` 只有在 `enableAudio=true` 時才加 `virtio-snd-pci`；預設 `enableAudio=false` 時維持 `-no-audio` 且不建立 guest sound device。
- qemu process tree priority 在啟動後 60 秒內每秒重套一次，避免子行程在 boot 期間回到較高 priority。
- gRPC screen capture 等 Android `sys.boot_completed=1` 後才啟動，避免開機期間 screenshot stream 和 Android boot 搶 CPU/IO。

### 驗證

- Release build 通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime smoke：
  - qemu process priority = `BelowNormal`（後續 Session 26 改回 `Normal` 並重驗）
  - `wm size` = `1920x1080`，`wm density` = `320`
  - Google Play → `com.android.vending`
  - 檔案管理 → `me.zhanghai.android.files`
  - 瀏覽器 → `com.android.chrome`
  - 設定 → `com.android.settings`
  - Home 無 `TMobile`、無 duplicate Settings、無 disabled tiles
  - Stream FPS samples `61.7, 59.5, 62.7, 58.0, 62.2, 61.5, 62.7, 60.1`，min 58.0、avg 61.0
- 驗證結束後無 `chimera-ui` / `qemu-system*` 殘留。

### 狀態

- 已修掉最直接造成 host 音樂卡頓/雜音的資源搶占路徑。若未來要啟用 guest audio，需單獨做 virtio audio end-to-end latency/buffer 測試，不可直接打開 `virtio-snd-pci` 當預設。

*Updated: 2026-05-25 — Session 25*

---

## Session 26 — Wheel/input jank + stream headroom（2026-05-25）

### 修正

- 使用者回報開 Chimera 後 host 瀏覽器音樂卡頓、滑鼠滾輪捲動也很卡；實際根因分成 host resource/scheduling contention 與 wheel input 主路徑成本過高。
- 滾輪原本每次事件都走 `adb shell input swipe`，等於高頻 spawn shell；現在優先使用 emulator gRPC `sendTouchSwipe()`，並以 12ms throttle 降低 wheel burst 抖動。ADB swipe 只保留為沒有 gRPC input 時的 fallback。
- 960x540 raw capture 在連續 app switch smoke 仍掉到 min 49.5 FPS；896x504 也曾回歸到 min 31 FPS。預設 capture 改為 800x450。Android guest / input coordinate space 仍是 1920x1080 / 320dpi。
- qemu/emulator priority 預設維持 `Normal` 且不得高於 Normal；BelowNormal 在本機曾讓 app switch 掉到 41-46 FPS，不再當預設。

### 驗證

- `cmake --build build --config Release --target chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- `git diff --check`：通過（只有 LF/CRLF warning）。
- Runtime smoke：
  - process priority：`chimera-ui` / `qemu-system-x86_64-headless` 都是 Normal。
  - `wm size` = `1920x1080`，`wm density` = `320`。
  - Google Play → `com.android.vending`
  - 檔案管理 → `me.zhanghai.android.files`
  - 瀏覽器 → `com.android.chrome`
  - 設定 → `com.android.settings`
  - Home 無 `TMobile`、無 duplicate Settings、無 disabled tiles。
  - Stream FPS samples `62.6, 62.4, 62.6, 63.0, 62.7, 62.9, 62.8, 62.2`，min 62.2、avg 62.6。
- 驗證結束後無 `chimera-ui` / `qemu-system*` 殘留。

### 狀態

- 已移除最明確的 wheel jank 路徑，並把 raw capture 預設降到本機 smoke 可穩定通過的值。
- 目前仍不是「真正 1080p raw 60fps」；全 1080p `getScreenshot` 會吃滿 CPU/頻寬。要同時要 1080p 清晰度與穩定 60+，下一步需改 shared memory/shared texture/GPU capture。

*Updated: 2026-05-25 — Session 26*

---

## Session 27 — Honest FPS + Traditional Chinese UI + wheel pacing（2026-05-26）

### 修正

- 使用者指出側欄 FPS 仍像虛報；主側欄 FPS 已改為有效 FPS：`min(guestFps, streamFps, renderFps)`。靜止畫面或 duplicate frame 會顯示 0，不再用 Stream 60+ 假裝流暢。
- Host title bar 左上角灰色副標已移除，只保留大的白色 `CHIMERA` logo；Host shell / HUD / sidebar 主要可見文字改為繁體中文。
- Chimera Launcher 移除 `Apps` 副標，首頁更簡潔；固定入口仍維持 Google Play、檔案管理、瀏覽器、設定。
- 滾輪仍走 emulator gRPC touch path，但 wheel throttle 改為約 16ms，instant swipe 從 4 個 touch request 降為 3 個，降低高頻滾輪事件洪峰。
- gRPC duplicate frame idle cadence 改為約 50ms，有輸入時才 boost 到 16ms；duplicate frame 不觸發 QML repaint。
- 1024x576 與 sampled fingerprint 實測不可靠，已收斂回 800x450 raw capture + full-frame fingerprint，確保 FPS 指標先誠實。

### 驗證

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1`：通過。
- `cmake --build build --config Release --target chimera-ui`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：16/16 PASS。
- Runtime short smoke：Android `wm size=1920x1080` / `wm density=320`，操作通知欄與滑動後，Perf log 顯示 Stream 可到 `61.3 FPS`，但 Guest/Render 只有 `8.9 FPS`；較長 runtime log 最高也只有 Guest `13.9 FPS` / Render `12.9 FPS`。
- 驗證結束後無 `chimera-ui` / `qemu-system*` 殘留。

### 狀態

- 本輪修掉「前端 FPS 虛報」與部分 wheel event 洪峰，不宣稱已達成真 60 FPS。
- BlueStacks 類優化方向已由子代理研究確認：短期要守 hardware acceleration、renderer profile、frame pacing、低延遲 input、resource profile；要真的讓動態畫面 60+，下一 phase 必須做 shared memory/shared texture capture + scene graph texture renderer，不能繼續靠 raw `getScreenshot`。

*Updated: 2026-05-26 — Session 27*

---

## Session 28 — Shared memory / D3D11 shared texture display path（2026-05-27）

### 修正

- `GuestDisplay` 改為 `QQuickItem` + Qt scene graph texture node，移除 `QQuickPaintedItem` / `QPainter` 每幀 paint 路徑。
- 新增 CPU-copy shared-memory framebuffer backend：`SharedMemoryFrameAbi.h` + `SharedMemoryFramebufferCapture`，使用 odd/even sequence seqlock，避免讀到 producer 寫入中的 torn frame。
- 新增 D3D11 shared texture metadata backend：`SharedD3D11TextureCapture` 讀取 metadata mapping，發出 named shared texture frame。
- `SharedD3D11TextureCapture` 改成 worker thread 等待 Win32 frame event，不再靠 UI thread QTimer 輪詢；只有新 even sequence 會發出 frame signal 與 Stream metric。
- `GuestDisplay` 在 D3D11 RHI 下會用 Qt scene graph device `OpenSharedResourceByName`，再以 `QNativeInterface::QSGD3D11Texture::fromNative()` 建立 native texture wrapper。
- 對仍走 CPU frame 的 fallback，D3D11 RHI 下改為 persistent texture + `UpdateSubresource()`，避免每幀 `createTextureFromImage()` 重建 GPU resource。
- `main.cpp` 新增 `CHIMERA_SHMEM_FRAME_NAME` / `CHIMERA_SHMEM_FRAME_EVENT` 與 `CHIMERA_D3D11_TEXTURE_METADATA` / `CHIMERA_D3D11_TEXTURE_EVENT` 接線；兩者都維持 opportunistic，不會阻斷 gRPC fallback。
- 新增 `shared_d3d11_texture_producer` helper，使用 named shared D3D11 texture + GPU `ClearRenderTargetView` 固定節拍產生 runtime smoke source。

### 驗證

- `cmake --build build --config Release --target chimera-ui test-shared-memory-framebuffer-capture test-shared-d3d11-texture-capture`：通過。
- `ctest --test-dir build -C Release --output-on-failure -LE integration`：18/18 PASS。
- `test-shared-d3d11-texture-capture` 會建立真 named D3D11 shared texture，並用第二個 D3D11 device 透過名稱打開，確認 named handle path 可用。
- `shared_d3d11_texture_producer` + `chimera-ui --no-emulator` runtime smoke：`Guest: 59.6 FPS | Stream: 59.6 FPS | Render: 59.6 FPS | Avg: 16.1ms | Dup: 0`，結束後無 `chimera-ui` / producer 殘留。

### 狀態

- Host 端 renderer / metadata capture 已就緒；這比 CPU shared memory 更接近 BlueStacks 類 GPU sharing。
- Android/emulator 端還沒有真正把 guest framebuffer 生產成 named D3D11 shared texture；因此目前不能宣稱通知欄、滑動或遊戲 flow 已穩定 1080p 60 FPS。
- 下一步是把 producer 接到 emulator/custom display path，然後用 notification shade、wheel scroll、app switch 三個動態 flow 重測 Guest/Stream/Render 與 visible latency。

*Updated: 2026-05-27 — Session 28*
