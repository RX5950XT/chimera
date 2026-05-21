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
| Native window embed | `NativeEmulatorView` 嵌入 Win32 視窗；gRPC/ADB 為 `--stream-capture` debug |
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
殘留掉幀尖刺為 emulator 端 getScreenshot 偶發停頓；完全消除需 native-embed 路徑
（emulator 自身 GPU 渲染、免截圖輪詢），屬顯示架構層級變更，未在本 session 動。

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

*Updated: 2026-05-21 — Session 12*
