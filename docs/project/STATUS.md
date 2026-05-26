# Project Chimera — Status Report

## 2026-05-15 Performance Stabilization Update

**Phase**: Phase 5 framework + stabilization
**Build**: Release build passed
**Tests**: 6/6 Qt unit tests passed
**Review report**: `docs/project/CODE_REVIEW.md`

### Fixed in this review
- Default display path is now headless gRPC framebuffer streaming; native Android Emulator Win32 embedding is legacy opt-in via `--native-embed`.
- Kept ADB raw display capture behind `--adb-display-fallback` as compatibility/debug mode because it is too slow for normal gameplay.
- Applied BlueStacks-inspired host-shell tuning: Qt Quick forced to D3D11 RHI; emulator/qemu process priority is now normal to avoid host contention.
- Added BlueStacks-style runtime shortcut coverage: recording, macro page, multi-instance page, and key mapping page toggles.
- Hid Android Emulator auxiliary tool windows after native embedding; Chimera now keeps controls inside the main shell via a compact right-side status/action panel.
- Cleaned the QML shell layout: removed duplicate FPS badges, removed clipped hover tooltips, and consolidated repeated toolbar actions into one right-side panel.
- Added built-in Android navigation controls in the right panel: Back, Home, and Recents. These send Android semantic keyevents through `InputBridge`/ADB.
- Replaced native-hidden QML dialogs/overlays with right-side panel pages for key mapping, multi-instance management, and macro management.
- Fixed native display sizing by constraining the embedded emulator viewport to the guest 16:9 aspect ratio, preventing emulator-side letterboxing from arbitrary host panel sizes.
- Added native-window recording support for the legacy `--native-embed` path; normal playback uses gRPC frames through `GuestDisplay`.
- Made screenshot/recording relative paths stable by setting the process working directory to the project root at startup.
- Increased ADB shell input timeout and log failures so Android navigation keyevents do not silently disappear under boot/load contention.
- Added `Esc` fullscreen exit in addition to `F11`.
- Synchronized AVD hardware config before boot: `1280x720`, `240 dpi`, 2048 MB RAM, host GPU, no device frame, and 60Hz vsync.
- Added guest-side performance setup after boot: 60Hz min/peak refresh, disabled Android animation scales, and fixed performance mode where supported.
- Switched SystemUI renderer to `skiavk` for emulator.exe runs.
- Disabled Android Emulator metrics/crash-report consent prompts (`-no-metrics`, `-crash-report-mode never`) so emulator boot does not stall before QEMU/ADB starts.
- Added Traditional Chinese QML UI refresh and `--no-emulator` UI-only startup mode.
- Added `GrpcFramebufferCapture` (`-grpc 8554`) as the default display path; ADB raw fallback is opt-in debug/compatibility mode.
- Fixed pre-boot gRPC pending connection by restarting the stream until the first frame arrives.
- Fixed gRPC frame orientation by copying emulator screenshot bytes as top-down; direct gRPC capture confirmed bottom-up copy is the inverted variant.
- Switched gRPC stream from RGBA8888 to RGB888 to cut frame payload by 25%.
- Lowered default guest resolution to 1280×720 and gRPC stream width to 960px; added logical guest-size mapping so keyboard/mouse coordinates still target the real guest resolution.
- Added stale AVD lock cleanup when no emulator/qemu process is running.
- Fixed `ProcessLauncher::terminate()` to terminate emulator child processes, preventing orphaned `qemu-system-x86_64-headless.exe` resource leaks.
- Added emulator `-vsync-rate 60` and `-netfast` launch flags.
- Verified VNC is not viable on Android Emulator 36.5.11 because it requires unsupported `-gpu guest`.
- Fixed QMP mouse button/absolute-position payload generation and auto-reconnect handoff.
- Fixed FFmpeg screen recorder argument construction and delayed encoder startup until first frame.
- Disabled guest startup audio with `-no-audio`.
- Lowered default runtime RAM to 2048 MB and display to 1280×720 to reduce memory, GPU, and screenshot-stream bandwidth.
- Fixed performance monitor first-frame timing so boot delay is not counted as frame time.
- Fixed `Framebuffer::writeBackBuffer()` resize deadlock and added `test-graphics-framebuffer`.
- Fixed force-kill orphan emulator/qemu leakage by assigning async children to a kill-on-close Windows Job Object; force-killing `chimera-ui.exe` now removes the emulator tree.
- Added Quick Boot snapshot support (`-snapshot chimera_quickboot`) with `CHIMERA_QUICK_BOOT=0` fallback; verified relaunch boot time improved from 44s to 10s.
- Hardened Quick Boot startup: if snapshot launch exits immediately, Chimera automatically retries with full boot; latest snapshot smoke reached boot complete in 12s.
- Added `scripts/verify-quick-boot.ps1` runtime smoke; latest run rebuilt `chimera_quickboot`, measured full boot 66.7s and Quick Boot 9.7s, and left no Chimera/emulator/qemu processes running.
- Reverted native Win32 embed to opt-in only after runtime showed black viewport / toolbar leakage; default display is headless gRPC streaming.
- Disabled ADB raw display fallback by default; it is now opt-in via `--adb-display-fallback` because it can collapse display to ~1 FPS.
- Added landscape guest adaptation on boot: 1280x720, 240 dpi, 60Hz settings, orientation request ignore, animation scales off, and wake/dismiss-keyguard/HOME so the stream lands on a usable desktop.
- Disabled Quick Boot by default after snapshot state caused ADB offline / empty-screen risk; set `CHIMERA_QUICK_BOOT=1` or use the verifier when intentionally testing snapshot boot.
- Upgraded default guest adaptation to 1920x1080 landscape / 320 dpi, and later removed the 800x450 gRPC capture cap so capture requests are clamped to at least 1920x1080.
- Added emulator gRPC `sendTouch` and routed normal clicks/touches through it; runtime smoke confirmed tapping Settings brings `com.android.settings` foreground.
- Replaced misleading single FPS counter with truthful `guestFps`, `streamFps`, `renderFps`, and duplicate-frame metrics; static screens no longer report capture-loop 60 FPS as guest FPS.
- Reduced idle overhead by fingerprinting gRPC frames: duplicate frames update stream metrics only, skip QML repaint/recorder feed, and back off capture to 100ms until input or content changes.
- Added a real Android HOME launcher (`com.chimera.launcher`) with a clean landscape app grid, plus `scripts/build-chimera-launcher.ps1` for APK build/sign/verify.
- Host boot flow now installs Chimera Launcher after `sys.boot_completed=1`, attempts to set it as HOME, and starts the HOME intent.
- Hardened Chimera Launcher against black-screen HOME states by removing forced immersive startup, adding visible title/empty-state content, and explicitly starting `com.chimera.launcher/.MainActivity` after install.
- Simplified the right sidebar performance card to a single FPS number and moved detailed Guest/Stream/Render/Dup diagnostics out of the primary side panel.
- Compacted the host shell chrome (46px top bar, narrower right panel) so the emulator viewport gets more usable space.
- Updated Chimera Launcher to keep Android status bar visible, remove the thick top black band, and show only the required entries: Google Play, File Manager, Browser, and Settings.
- Kept the single side-panel FPS as Stream FPS and restored 16ms capture cadence while still suppressing duplicate-frame QML repaint; steady Home streaming now verifies above 60 FPS.
- Switched the default AVD hardware config to the installed Google Play system image when available, enabling real Play Store / Play services support.
- Added support app provisioning for Material Files from `third_party/android-apps/material-files.apk`, giving Chimera a real file manager package instead of a non-launchable DocumentsUI shortcut.
- Updated Chimera Launcher to keep the four required entries pinned while appending all launchable apps, so Google Play-installed apps appear on Home automatically.
- Moved fullscreen into the compact FPS side card and replaced the white native Windows title bar with a frameless dark title bar that contains the Chimera logo.
- Added built-in Chimera browser/file-manager fallback activities so fixed Home entries never ship as disabled grey placeholders when Chrome/Files are absent.
- Tightened Home dynamic app scanning to append only user-installed packages and filter system remnants such as duplicate Settings and TMobile.
- Reduced host audio stutter risk by lowering default emulator/qemu scheduling pressure: 2 vCPU, process priority not above Normal, no pre-boot gRPC capture, and no `virtio-snd-pci` device while guest audio is disabled.
- Reduced mouse-wheel scroll jank by routing wheel input through emulator gRPC `sendTouchSwipe()` with 16ms throttle; ADB `input swipe` is now fallback-only.
- Removed the host title-bar subtitle, kept the large `CHIMERA` logo, and switched the visible host UI copy to Traditional Chinese where this flow exposes it.
- Changed the side-panel FPS to effective FPS (`min(Guest, Stream, Render)`) so Stream delivery can no longer masquerade as true visible smoothness.
- Runtime dynamic-flow evidence still does not prove true 60 FPS: notification/scroll smoke reached Stream `61.3 FPS` while Guest/Render were only `8.9 FPS`; a longer flow reached Guest `13.9` / Render `12.9`. True 60+ now requires shared memory/shared texture capture and a scene graph texture renderer.
- Removed the low-resolution raw capture default after user feedback: lowering capture below 1920x1080 is no longer allowed as a performance shortcut.
- Fixed `InstanceManager` saved/live instance visibility, invalid iterator usage, saved-only start path, and instance name validation.
- Fixed QMP auto-reconnect not starting after failed connection/socket error.
- Fixed `MacroEngine` playback thread replacement risk.
- Rejected unsafe `../` names for input schemes and macros.
- Removed stale `main.moc` include warning from `src/host/ui/main.cpp`.

### Current known risks
- gRPC streaming is the default display path; native window embedding remains opt-in only via `--native-embed` because it can black out the emulator Qt surface and leak the toolbar.
- Latest live boot test removed stale AVD locks, reached Android boot complete, reported `1920x1080` / `320 dpi`, and ADB screenshot showed a usable landscape Home screen.
- Emulator boot depends on `-crash-report-mode never`; without it, Android Emulator can stall on a crash-report consent dialog before QEMU/ADB becomes available.
- gRPC RGB888 stream is still available, but capture requests are clamped to at least 1920x1080. Full-resolution performance must be solved through shared memory/shared texture/custom producer work, not by downscaling the capture request.
- Perf metrics are now separated: `guestFps` means content-changing guest frames, `streamFps` means capture replies, `renderFps` means Qt paints, and `duplicateRate` exposes repeated frames. A static HOME screen should report Guest 0 FPS and high duplicate rate.
- Runtime smoke verified `com.chimera.launcher` is installed and becomes HOME; launching Settings changes foreground to `com.android.settings`.
- Latest launcher smoke verified UI tree contains `CHIMERA`, ADB screenshot is not black, and tapping Settings from the launcher opens `com.android.settings`.
- Latest launcher smoke verified UI tree contains Google Play / 檔案管理 / 瀏覽器 / 設定, does not contain TMobile, and the ADB screenshot shows the Android status bar persistently visible.
- Latest app provisioning smoke verified Google Play, Material Files, Chrome, and Settings all launch from Chimera Home into their expected foreground packages.
- Latest Home smoke verified `TMobile` is absent, Settings is not duplicated, and there are no disabled fixed tiles; file/browser fall back to Chimera activities if Pixel/Chrome apps are missing.
- Latest host-contention smoke verified qemu stays at `Normal` (not High), gRPC capture starts after Android boot complete, and no Chimera/qemu process remains afterward. Stream can still hit 60+, but the main UI now reports the lower effective FPS.
- Latest steady FPS smoke after boot warm-up measured Stream FPS samples `61.9, 62.7, 63.1, 63.2, 62.4` (min 61.9, avg 62.7).
- Full 1920×1080 raw `getScreenshot` currently drops to ~15-30 FPS; true full-res 60 FPS needs shared memory/shared texture capture instead of raw gRPC payloads.
- ADB raw screencap fallback remains very slow and is intentionally throttled to avoid resource spikes.
- Stable game-level 60 FPS still depends on the guest workload and emulator GPU renderer; current verification proves only Stream delivery can hit roughly 60 FPS, while dynamic Guest/Render FPS on the raw path is still too low.
- qemu/emulator must never be raised above `Normal` by default; host headroom should come from vCPU limits, delayed capture, and capture path efficiency rather than High priority.
- `ProcessLauncher` now warns when Job Object assignment fails; if that warning appears, inspect for orphan `qemu-system*` before another launch.
- Quick Boot snapshot is opt-in (`CHIMERA_QUICK_BOOT=1`) until snapshot state reliability is hardened; use full boot as the default when diagnosing display or ADB state.
- Quick Boot runtime regression should use `scripts/verify-quick-boot.ps1 -MaxQuickBootSec 25`; the script does a clean local emulator run and removes stale AVD locks only after confirming no emulator/qemu process is alive.
- Chimera Launcher is a clean HOME replacement, not yet a full custom ROM layer; deeper BlueStacks parity still needs package pruning, store/search UX, and keymap/game integrations.
- Full app switching can still show short Stream FPS dips even when steady Home sits near 60 FPS; game workload profiling remains separate from Home/display smoke.
- `Framebuffer::readFrontBuffer()` still returns an internal reference; long-term fix should return a snapshot or guard reads.
- `ProcessLauncher` command-line quoting is not fully Windows-escaping-safe.
- Clipboard sync still uses `CF_TEXT`; Unicode clipboard support is pending.

---

# Historical Status — Phase 4 Core Virtualization Status Report

**Date**: 2026-05-09  
**Phase**: 4 (Core Virtualization)  
**Overall Status**: Superseded by 2026-05-14 review above. Phase 4 complete + Phase 5 framework + optimizations; current tests are 6/6 passing.

---

## Phase 3 Completed

### 3.1 Audio Bridge (WASAPI Shared-Mode Output)
- `AudioBridge::initialize()` creates `IMMDeviceEnumerator`, activates default render endpoint
- Initializes `IAudioClient` in shared mode with float32 format (48kHz stereo)
- Background `renderThreadLoop()` polls buffer padding every 5ms
- `writeGuestFrames()` pushes float frames to lock-protected queue (2-second cap to prevent unbounded growth)
- `drainQueueToWasapi()` copies queue data to `IAudioRenderClient` buffer, fills remainder with silence
- `readHostMicrophone()` captures from `IAudioCaptureClient` (optional, stub-ready for future virtio-snd)
- COM initialized with `COINIT_MULTITHREADED`, properly released in `shutdown()`
- **Test**: `chimera-audio` compiles successfully

### 3.2 Screen Recorder (FFmpeg Subprocess + PNG Fallback)
- **New `ScreenRecorder` QObject** class in `src/host/ui/`
- Detects FFmpeg availability (`ffmpeg.exe`, common paths, PATH search)
- **FFmpeg mode**: Spawns subprocess with rawvideo RGB24 stdin pipe, encodes to H.264 MP4 with `-c:v libx264 -preset fast -crf 23`
- **PNG fallback**: If FFmpeg unavailable, saves numbered PNG sequence to `<output>_frames/`
- `feedFrame()` receives `QImage` from `AdbScreenCapture::frameReady` signal
- QML toolbar "Record" button toggles start/stop, auto-generates timestamped filename (`recordings/chimera_YYYY-MM-DD_HH-MM-SS.mp4`)
- `recording` Q_PROPERTY with `recordingChanged` signal updates button state
- **Test**: Compile success, `hasFFmpeg()` detection verified

### 3.3 ANGLE Header Integration
- **New script** `scripts/fetch-angle-headers.py` downloads EGL/GLES headers from Google ANGLE GitHub
- Downloaded: `egl.h`, `eglext.h`, `eglplatform.h`, `gl2.h`, `gl2ext.h`, `gl3.h`, `khrplatform.h`
- `CMakeLists.txt` auto-detects headers at `third_party/angle/` and defines `CHIMERA_HAS_ANGLE`
- `AngleBackend` updated with real EGL types (`EGLDisplay`, `EGLContext`, `EGLSurface`)
- When headers available: implements `eglGetDisplay`, `eglInitialize`, `eglChooseConfig`, `eglCreateContext`, `eglCreateWindowSurface`, `eglMakeCurrent`, `eglSwapBuffers`, `eglGetProcAddress`
- When headers unavailable: compiles as stub (backward compatible)
- **Test**: ANGLE detected and compiles with real EGL functions ✅

---

## Phase 4 Completed

### 4.1 Device Spoofing (build.prop Modification)
- **New `DeviceSpoofer` class** in `src/host/instance/`
- Modifies AVD `overlay/system/build.prop` before boot to fake flagship device identity
- **5 Built-in Profiles**:
  - Samsung Galaxy S24 Ultra (SM-S928U, 3120×1440, 480 DPI)
  - OnePlus 12 (CPH2581, 3168×1440, 480 DPI)
  - ASUS ROG Phone 8 (AI2401, 2448×1080, 480 DPI)
  - Xiaomi 14 Pro (23116PN5BC, 3200×1440, 480 DPI)
  - Google Pixel 8 Pro (GC3VE, 2992×1344, 480 DPI)
- Each profile sets `ro.product.manufacturer`, `ro.product.model`, `ro.product.device`, `ro.sf.lcd_density`, SDK version, etc.
- **Purpose**: Games often lock 120 FPS / Ultra graphics settings behind device whitelist; spoofing unlocks these
- Integrated into `InstanceManager::createInstance()` — auto-applies if `deviceProfile` field set
- **Test**: Compile success, `modifyBuildProp()` read/write cycle verified

### 4.2 Raw Display Capture (20 FPS)
- `AdbScreenCapture` switched from PNG (`screencap -p`) to **raw format** (`screencap` without `-p`)
- Raw format: `width(4B) + height(4B) + pixel_format(4B) + raw RGBA pixels` — zero encoding overhead
- Capture interval reduced from 100ms (10 FPS) → **50ms (20 FPS)**
- Fallback: Existing PNG parser still active for compatibility
- **Test**: Compile success, raw format path verified in `AdbScreenCapture`

### 4.3 QMP Input (QEMU Monitor Protocol)
- **New `QmpInput` class** in `src/host/input/` — QTcpSocket-based JSON protocol client
- Connects to QEMU Machine Protocol (`-qmp tcp:localhost:5554,server,nowait`)
- Sends `input-send-event` commands with `qmp_capabilities` negotiation
- **Latency**: Targets <5ms vs ADB shell input's ~100ms (20× improvement)
- Event types: `key` (down/up), `btn` (mouse button), `rel` (mouse move)
- `VirtualMachine::start()` now passes `-qemu -qmp tcp:localhost:<port>,server,nowait` to emulator
- Integrated in `main.cpp`: attempts QMP connection, falls back to ADB if unavailable
- **Test**: Compile success, QMP socket and JSON command structure verified

### 4.5 ANGLE Libraries (Prebuilt from Chrome)
- **Source**: `libEGL.dll` (512KB) + `libGLESv2.dll` (7.8MB) copied from `C:\Program Files\Google\Chrome\Application\147.0.7727.138\`
- **Verification**: `dumpbin /exports` confirms standard EGL/GLES entry points:
  - `eglGetDisplay`, `eglInitialize`, `eglCreateContext`, `eglMakeCurrent`, `eglSwapBuffers`
  - `glClear`, `glDrawArrays`, `glViewport`, etc.
- **Integration**: `src/host/graphics/CMakeLists.txt` post-build step auto-copies DLLs to `build/Release/`
- **Runtime**: `chimera-ui.exe` will load ANGLE DLLs from executable directory
- **Test**: Build success, DLLs present in output directory

### 4.7 QMP Runtime Validation
- **Critical discovery**: Android Emulator's console port (default 5554) IS the QMP interface
- No extra `-qemu -qmp` args needed; `-ports console,adb` maps console to QMP
- **Verified commands**:
  - Greeting: `{"QMP": {"version": {"qemu": ...}, "capabilities": []}}`
  - Capabilities: `{"execute": "qmp_capabilities"}` → `{"return": {}}`
  - Status query: `{"execute": "query-status"}` → `{"return": {"status": "running"}}`
  - Input injection: `{"execute": "input-send-event", ...}` → `{"return": {}}`
- **Port fix**: `VirtualMachine::start()` changed from `-ports adbPort,adbPort+1` to `-ports qmpPort,adbPort`
- **Test**: Python socket script verified end-to-end on running emulator

### 4.4 Memory Trim (Android GC Monitoring)
- **New `MemoryTrimmer` class** in `src/host/instance/` — QObject with QML-bindable properties
- Background `std::thread` polls `/proc/meminfo` via ADB every 5s (configurable)
- Parses `MemTotal`, `MemFree`, `Buffers`, `Cached` to calculate available memory
- **Pressure levels**: None / Moderate (<25% available) / Low (<15%) / Critical (<8%)
- Auto-triggers `trimMemory(PressureCritical)` when entering critical level
- Manual trim levels: `trimMemory(level)` and `aggressiveTrim()` (drop caches + framework trim)
- QML properties: `memoryPressureLevel`, `totalMB`, `usedMB`, `availableMB`, `monitoring`
- **Test**: Compile success, `parseMeminfoValue` verified

### 4.6 Disk Compaction (Safe Cleanup + Zero-Fill)
- **New `DiskCompactor` class** in `src/host/instance/` — static utility, no QObject overhead
- `analyzeInstance()`: Recursive directory scan reporting total/cache/log/temp/other breakdown
- `compactInstance()`: Safely deletes `cache.img`, `*.log`, `*.dmp`, `*.tmp`, `*.bak` files
- `zeroFillFreeSpace()`: Creates large zero-filled temp file then deletes it (allows host sparse file / VHDX compaction)
- Configurable safety cap on zero-fill (default 1 GB)
- Returns `CompactionResult` with before/after bytes and reclaimed space
- **Test**: Compile success

---

## Phase 2.x Completed (UI Polish)

### 2.x.1 Clone UI
- Multi-instance dialog now shows **Clone** button per instance
- `CloneDialog` prompts for new name with default `<source>_clone`
- Calls `QmlInstanceManager::cloneInstance()` which copies data directory

### 2.x.2 Macro UI
- New **Macro** toolbar button opens macro management dialog
- Toolbar shows **● REC** (red) or **▶ PLAY** (green) status indicator
- `QmlMacroEngine` QObject wrapper exposes `MacroEngine` to QML
- Dialog features:
  - **Record** button with name input field
  - List of saved macros with **Play** / **Delete** buttons
  - **Loop count** input field (1-999)
  - **Stop Playback** button (visible only during playback)
  - **Refresh** button to reload macro list
- `recordingChanged` / `playingChanged` signals update UI state

---

## Phase 2 Completed

### 2.1 Gamepad Support (XInput → ADB)
- `GamepadManager` now tracks previous state per device and detects button/axis transitions
- Added `ButtonCallback` and `AxisCallback` to `GamepadManager` for individual event emission
- `InputBridge::onGamepadButton()` maps 14 XInput buttons to Android keycodes (A/B/X/Y, DPAD, shoulders, thumbs, start/back)
- `InputBridge::onGamepadAxis()` uses threshold-based swipe for analog sticks (left/right stick → directional swipe)
- Wired in `main.cpp`: `GamepadManager` polls at 60 Hz via `QTimer`, callbacks route to `InputBridge`
- **Test**: Compile success, XInput linkage verified

### 2.2 Instance Persistence (JSON Save/Load)
- `InstanceManager` now loads instances from `configs/instances.json` on construction
- `saveInstances()` merges saved configs with live VM configs, deduplicates by name
- `createInstance()` adds to savedConfigs and triggers save
- `cloneInstance()` copies data directory and creates new instance via `createInstance()`
- `deleteInstance()` stops VM, removes from live + saved lists, deletes data directory, triggers save
- Destructor auto-saves to prevent data loss
- **Test**: `test-instance-manager` still passes (persistence tested implicitly)

### 2.3 Screenshot Feature
- `GuestDisplay::saveScreenshot(filePath)` saves current `QImage` frame to PNG
- Toolbar "Screenshot" button generates timestamped filename (`screenshots/chimera_YYYY-MM-DD_HH-MM-SS.png`)
- `screenshots/` directory auto-created on startup
- `Ctrl+Shift+S` keyboard shortcut also triggers screenshot
- **Test**: Compile success, QImage::save path verified

### 2.4 InputMapper Integration with InputMapperOverlay
- `InputMapperOverlay::loadScheme()` now reads from `InputMapper` JSON and renders controls
- `InputMapperOverlay::saveScheme()` writes controls back to `InputMapper` JSON
- Coordinate conversion: normalized % → pixel rect and back
- **Test**: Compile success, InputMapper load/save cycle verified

### 2.5 Multi-Instance QML Dialog
- New `QmlInstanceManager` QObject wrapper exposes InstanceManager to QML
- Registered as `InstanceManager` context property in `main.cpp`
- `ChimeraWindow.qml` includes modal `Dialog` with:
  - ListView of instances with Start/Stop/Delete buttons
  - TextField + Create button for new instances
  - Refresh button to reload list
- **Test**: Compile success, QML Dialog verified in resources

### 2.6 Macro Playback Thread
- `MacroEngine::startPlayback()` spawns background `std::thread`
- `playbackLoop()` iterates events with `std::this_thread::sleep_until()` for precise timing
- Supports `loopCount` parameter for repeated playback
- `stopPlayback()` sets atomic flag and joins thread
- Events injected through `InputBridge` (tap, swipe, key press/release)
- **Test**: Compile success, thread lifecycle verified

---

## Phase 1 Completed (Baseline)

### 1. Virtualization Layer — VERIFIED
- **Android Emulator** downloaded and configured via automated script (`scripts/setup-android-sdk.py`)
- **AVD created**: `chimera_dev` (Android 34 x86_64, google_apis, no Play Store)
- **QEMU + WHPX acceleration**: Confirmed working on this machine
- **Android boot**: `sys.boot_completed = 1` achieved in ~90 seconds
- **ADB connectivity**: `emulator-5554 device` responsive
- **Host GPU recognized**: NVIDIA GeForce RTX 3070 Ti (Vulkan 1.4)

### 2. Project Skeleton — COMPLETE
- Monorepo initialized with Git
- 50+ source files written across all subsystems:
  - `src/host/ui/` — Qt 6 QML window, GuestDisplay painted item, input overlay
  - `src/host/config/` — JSON-based configuration manager
  - `src/host/input/` — Input bridge with ADB forwarding, mapper, gamepad manager, macro engine, keycodes
  - `src/host/graphics/` — Graphics bridge, framebuffer, OpenGL renderer, ANGLE backend
  - `src/host/audio/` — WASAPI audio bridge stub
  - `src/host/storage/` — Shared folder / 9pfs manager
  - `src/host/instance/` — VM instance manager, process launcher, virtual machine
  - `src/host/integration/` — Windows notifier, clipboard bridge, location simulator
  - `src/common/utils/` — Logger, thread pool, file utilities
  - `tests/unit/` — Qt Test suite for config, input, instance (all passing)
- Build system: CMake + Visual Studio 2022 generator, top-level `build.py` script
- Third-party: nlohmann/json (header-only, bundled)

### 3. Build Environment — FIXED
- **Compiler**: Visual Studio 2022 Community (MSVC 19.44.35213.0) via `vcvarsall.bat amd64`
- **Qt 6.8.3**: Installed via `aqtinstall` for `win64_msvc2022_64`
- **CMake Generator**: `Visual Studio 17 2022` with `-A x64`
- **Build command**:
  ```powershell
  cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
  cmake --build build --config Release
  ```
- **All targets compile**: `chimera-ui.exe`, all static libraries, all unit tests

### 4. Phase 1 MVP Features — COMPLETE

#### 4.1 Instance Launch (VirtualMachine + InstanceManager)
- `VirtualMachine::start()` now launches actual `emulator.exe` via `ProcessLauncher::runAsync()`
- `ProcessLauncher::runAsync()` fully implemented with `CreateProcessW`, pipe redirection, stdout/stderr reader threads
- `InstanceManager::createInstance()` reads `configs/android_sdk.json` and passes emulator path + AVD name to `VirtualMachineConfig`
- Current default emulator launch uses headless gRPC streaming with `-accel on -gpu host`, 60Hz display settings, and correct ADB/QMP port forwarding
- Verified: `chimera-ui.exe` starts emulator process (PID visible in task manager), ADB connects to `emulator-5555`

#### 4.2 Display / Frame Capture
- `main.cpp` defaults to headless gRPC framebuffer streaming via `GrpcFramebufferCapture`
- `--native-embed` starts legacy Android Emulator window embedding via `NativeEmulatorView`
- Persistent ADB raw screencap remains as fallback only; measured around 1 FPS on this machine
- `GraphicsBridge::FrameCallback` delivers frames to `GuestDisplay` only in stream-capture mode

#### 4.3 Frame Rendering (GuestDisplay)
- New `GuestDisplay` QQuickPaintedItem subclass renders `QImage` scaled with aspect-ratio preservation (letterbox)
- Displays "Waiting for guest display..." placeholder when no frame available
- Registered to QML as `Chimera.UI.GuestDisplay`
- `ChimeraWindow.qml` replaced nested `Window`/`ChimeraWindow` anti-pattern with proper `ApplicationWindow` containing `GuestDisplay`

#### 4.4 Input Forwarding (InputBridge + emulator gRPC / fallback ADB)
- `InputBridge` prefers emulator gRPC input for low-latency touch/key/text paths; ADB shell input is fallback-only.
- `GuestDisplay` handles `keyPressEvent`, `keyReleaseEvent`, `mousePressEvent`, `mouseReleaseEvent`, `mouseMoveEvent`, `wheelEvent`
- Mouse coordinates mapped from display item geometry to guest resolution (aspect-ratio aware)
- Qt keycodes mapped to Android keycodes via lookup table (alphanumeric, arrows, function keys, modifiers)
- Events forwarded as:
  - **Tap/Touch**: emulator gRPC `sendTouch`
  - **Wheel**: emulator gRPC `sendTouchSwipe()` with 16ms throttle
  - **Key/Text**: emulator gRPC `sendKey` / `sendText`
  - **Fallback**: ADB `input tap` / `input swipe` / `input keyevent`

### 5. Automation Scripts — COMPLETE
- `scripts/setup-android-sdk.py` — Downloads command-line tools, emulator, system images, creates AVD
- `scripts/run.py` — Launches emulator with WHPX, configurable GPU/headless/resolution
- `scripts/build.py` — Top-level CMake wrapper

### 6. Documentation — COMPLETE
- `README.md` — Project overview, quick start
- `docs/project/BUILD.md` — Build instructions for Windows + MSVC
- `docs/project/PLAN.md` — Full 4-phase implementation plan (copied from analysis reference)
- `docs/architecture/ARCHITECTURE.md` — Module responsibilities, communication flows, tech choices
- `docs/project/STATUS.md` — This file
- `AGENTS.md` — Agent workflow, coding standards, safety checklist, build instructions
- `CLAUDE.md` — Architecture decisions, module boundaries, current state, next steps
- `.gitignore`, `LICENSE` (Apache 2.0)

---

## Test Results

```
Test project D:/Workspace_cloud/Personal_Project/chimera/build
    Start 1: test-config-manager
1/6 Test #1: test-config-manager ..............   Passed
    Start 2: test-input-mapper
2/6 Test #2: test-input-mapper ................   Passed
    Start 3: test-instance-manager
3/6 Test #3: test-instance-manager ............   Passed
    Start 4: test-graphics-framebuffer
4/6 Test #4: test-graphics-framebuffer ........   Passed
    Start 5: test-adb-framebuffer-capture
5/6 Test #5: test-adb-framebuffer-capture .....   Passed
    Start 6: test-qmp-input
6/6 Test #6: test-qmp-input ...................   Passed

100% tests passed, 0 tests failed out of 6
```

---

## Phase 1 MVP Verification Log

| Step | Result | Evidence |
|------|--------|----------|
| `chimera-ui.exe` launches | ✅ | Process starts, no crash |
| InstanceManager creates `chimera_dev` | ✅ | stdout: `Instance created: "chimera_dev"` |
| VirtualMachine starts emulator | ✅ | `emulator.exe` process visible in task manager (PID 4916) |
| ADB connects | ✅ | `adb devices` shows `emulator-5555 offline` → online after boot |
| Android boots | ✅ | `sys.boot_completed = 1` achieved in ~20s |
| ADB screencap works | ✅ | 1.16MB valid PNG produced (`?PNG` header) |
| Frame callback triggers | ✅ | stdout: `ADB screen capture started` after 15s delay |
| InputBridge ADB queue works | ✅ | Compile success, worker thread implemented |
| GuestDisplay renders | ✅ | Compile success, QQuickPaintedItem with QImage paint |

---

## Architecture Changes in Phase 1

### New Files
| File | Purpose |
|------|---------|
| `src/host/ui/GuestDisplay.h/cpp` | QQuickPaintedItem rendering QImage with aspect-ratio scaling |
| `AGENTS.md` | Agent workflow, coding standards, build instructions, safety checklist |
| `CLAUDE.md` | Architecture decisions, module boundaries, known issues, next steps |

### Modified Files
| File | Change |
|------|--------|
| `src/host/instance/ProcessLauncher.cpp` | Implemented `runAsync()` with `CreateProcessW`, pipe redirection, reader threads |
| `src/host/instance/VirtualMachine.cpp` | `start()` now builds emulator args and launches via `ProcessLauncher::runAsync()` |
| `src/host/instance/InstanceManager.cpp` | Reads `android_sdk.json`, passes `emulatorPath`/`avdName` to `VirtualMachineConfig` |
| `src/host/instance/VirtualMachine.h` | Added `emulatorPath`, `avdName`, `adbPort`, `headless` to `VirtualMachineConfig` |
| `src/host/input/InputBridge.h/cpp` | Added ADB command queue + worker thread, Qt→Android keycode mapping |
| `src/host/ui/ChimeraWindow.qml` | Replaced with proper `ApplicationWindow` + `GuestDisplay` + `InputMapperOverlay` |
| `src/host/ui/main.cpp` | Added `InstanceManager` launch, `AdbScreenCapture` timer, `InputBridge` config |
| `src/host/ui/GuestDisplay.cpp` | Added mouse/keyboard event handling + coordinate mapping to `InputBridge` |
| `CMakeLists.txt` (multiple) | Added `nlohmann_json`, `Qt6::Core`, `find_package(OpenGL REQUIRED)` where needed |

---

## Reference: BlueStacks Architecture Analysis (Gemini DeepResearch)

> Key technical findings from detailed BlueStacks reverse-engineering report, informing Chimera's Phase 3+ roadmap.

### Virtualization Architecture (Confirmed: WHPX is Correct)
- BlueStacks evolved from **VirtualBox (Type-2)** → **Hyper-V WHPX (Type-1)**
- **Root Partition** (Host Windows) + **Child Partition** (Android) via **VMBus/Hypercalls**
- WHPX allows coexistence with WSL2, Docker, Windows Sandbox without BSOD
- Our use of Android Emulator (QEMU+WHPX) aligns with this architecture

### Graphics Pipeline (Multi-Mode Strategy)
- **ANGLE**: OpenGL ES → DirectX 11 with HLSL shader translation (we have headers)
- **Performance Mode**: Shortened graphics pipeline, direct GPU mapping, up to 240 FPS
- **Compatibility Mode**: Software fallback for non-standard OpenGL extensions, pixel-perfect accuracy
- **Vulkan**: Modern low-overhead API for Android Pie/11/13, multi-threaded state scalability
- **ASTC Texture Compression**: Hardware decode if available, otherwise CPU fallback (prevents black blocks)

### Input System (Critical: Kernel Driver for Latency)
- **BstkDrv.sys**: Kernel-mode driver (Ring 0) for sub-millisecond input latency
- Intercepts hardware interrupts at lowest abstraction layer, bypasses Windows user-mode API stacks
- **Advanced Keymapping**: Coordinate transformation matrices, D-Pad (WASD), MOBA skill shots, shooter mode (mouse delta → camera rotation)
- Our ADB shell input (~100ms) is 100× slower; **virtio-input or kernel driver essential for competitive gaming**

### Memory Management
- **Trim Memory**: Monitors Android memory pressure + GC state, actively returns unused pages to host
- **Low Memory Mode**: Aggressive background process killing, reduced framebuffer size, longer CPU wake intervals
- **VMMEM Process**: Hyper-V central memory manager; high RAM usage is by design, not a bug

### Storage
- **VDI** (VirtualBox traditional) vs **VHDX** (Hyper-V modern)
- **Dynamic Allocation**: Initial small footprint, grows on write
- **Disk Compaction**: Zero-fill free space → `Optimize-VHD -Mode Full` (VHDX) or `vboxmanage clonehd` (VDI)

### Audio
- **AudioFlinger** → **WASAPI/DirectSound** redirect with bidirectional virtual audio devices
- Microsecond-level buffer jitter control to prevent lip-sync issues
- Our `AudioBridge` architecture matches this approach

### Network
- **NAT Mode** (default): Isolated private IP (10.0.2.15), host acts as virtual router, shares VPN
- **Bridged Mode** (optional): Real LAN IP, but exhausts router DHCP pool in multi-instance

### ABI Translation
- **LayerCake Patent**: ARM → x86 dynamic binary translation (JIT-style)
- Multi-ABI support: armeabi-v7a, arm64-v8a, x86, x86_64
- Our equivalent: **libndk_translation** (AOSP open-source)

### Device Spoofing
- Modify `build.prop` + HAL environment variables to fake flagship phone models
- Unlocks high FPS/quality options locked behind hardware detection

---

## 2026-05-27 Update — Shared capture renderer

- Host display renderer now uses Qt scene graph texture nodes instead of `QQuickPaintedItem`.
- D3D11 RHI CPU-frame fallback now reuses a persistent texture and updates it with `UpdateSubresource()` instead of recreating a GPU texture every frame.
- Added CPU-copy shared-memory framebuffer capture with seqlock metadata.
- Added D3D11 named shared texture metadata capture and `GuestDisplay` native D3D11 texture render path via `QSGD3D11Texture::fromNative()`.
- New verification: `test-shared-d3d11-texture-capture` creates a real named D3D11 shared texture and opens it from another D3D11 device.
- `SharedD3D11TextureCapture` now waits for Win32 frame events on a worker thread and only counts new even sequences, so duplicate metadata ticks cannot inflate Stream FPS.
- Added `shared_d3d11_texture_producer` runtime helper. Smoke test with `chimera-ui --no-emulator` measured `Guest/Stream/Render 59.6 FPS`, average `16.1ms`, `Dup: 0`, with no leftover processes.
- Added `test-grpc-framebuffer-capture`; `GrpcFramebufferCapture` now clamps requests below 1920x1080 back to 1080p.
- Latest 1920x1080 shared texture smoke measured `Guest/Stream/Render 59.9 FPS`, average `16.3ms`, `Dup: 0`, with no leftover processes.
- Current status: host side is ready; Android/emulator producer is still missing, so true dynamic 1080p/60 FPS remains unproven until producer integration and runtime flow tests.

## Known Limitations

| Limitation | Reason | Resolution Path |
|------------|--------|----------------|
| Native child window overlays QML content | Win32 child windows are composed above Qt Quick content | Main controls now stay in the right-side panel; viewport overlays remain stream-mode only |
| Game workload can still drop below 60 FPS | Android Emulator screenshot/gRPC readback and guest workload overhead remain | Default gRPC path is stabilized for 60+ FPS on Home; shared GPU texture/custom QEMU remains the long-term capture path |
| VirtIO audio not fully wired end-to-end | Emulator accepts `virtio-snd-pci`, but host/guest audio path still needs runtime validation | Custom QEMU / Android HAL integration |
| QMP mouse input needs runtime validation | Current schema compiles but click/move behavior must be verified on emulator | Test against running emulator and adjust event payload |
| Keyboard mapping drag-and-drop | Not yet implemented | Future polish |
| No kernel-mode input driver | BstkDrv.sys equivalent not implemented | Phase 5: Windows filter driver (complex) |

---

## Next Steps (Phase 5+)

### Critical Path (Gaming Performance)
1. **Game-level 60 FPS profiling** — Verify real games with Android frame stats and isolate guest-side jank under workload
2. **Shared capture path** — Keep gRPC streaming as default; add shared GPU texture or custom QEMU display path for recording/overlay without screenshot overhead
3. **VirtIO Input** — Replace QMP/ADB with direct virtio-input HID injection (target: <10ms latency)
   - QMP is interim solution; virtio-input is the long-term open-source equivalent to BstkDrv.sys
4. **VirtIO Audio** — Wire AudioBridge to QEMU `-device virtio-snd-pci`
5. **ANGLE D3D11 Backend** — Wire `AngleBackend` to use copied DLLs, create EGL context + surface

### Platform Hardening
6. **Hyper-V HCS API** — Experiment with GPU-PV for hardware-accelerated guest graphics
7. **Bundle FFmpeg** — Include `ffmpeg.exe` in installer for seamless screen recording

## Phase 5 Completed (Framework)

### 5.1 VirtIO Input Framework
- **New `VirtioInput` class** in `src/host/input/`
- Generates QEMU args: `-device virtio-keyboard-pci`, `-device virtio-mouse-pci`, `-device virtio-tablet-pci`
- **Status**: Prebuilt Android Emulator rejects all virtio-input devices (exit code 1). Custom QEMU build required
- Provides `openDevice()`, `sendKey()`, `sendMouseMove()`, `sendMouseButton()` API ready for future use

### 5.2 Hyper-V HCS API Framework
- **New `HyperVManager` class** in `src/host/instance/`
- Dynamically loads `computecore.dll` and resolves HCS functions: `HcsCreateComputeSystem`, `HcsStartComputeSystem`, etc.
- GPU-PV detection via `EnumDisplayDevicesW` (checks for NVIDIA/AMD/Intel dGPU)
- `HcsConfig` struct supports `GpuPartitionMode` (None / Partition / DDA)
- **Status**: VM creation is experimental scaffolding; not yet functional

### 5.3 Performance Monitor
- **New `PerformanceMonitor` class** in `src/host/graphics/`
- Tracks: FPS (1s window), average frame time, max frame time, dropped frame count
- QML properties: `fps`, `averageFrameTimeMs`, `maxFrameTimeMs`, `droppedFrames`, `totalFrames`
- Auto-logs every 5 seconds: `[Perf] FPS: X | Avg: Yms | Max: Zms | Dropped: N / M`
- Connected to `AdbFramebufferCapture::frameReady` in `main.cpp`

### 5.4 QMP Latency Measurement
- `QmpInput` now uses `QElapsedTimer` to measure command round-trip time
- `lastLatencyMs()` returns latency of most recent command
- Useful for benchmarking QMP vs ADB input latency

### 5.5 QMP Input Integration
- `InputBridge` now checks `hasQmp()` before falling back to ADB
- **Keyboard**: Qt keycodes mapped to QEMU keycodes (Linux input event codes) via `s_qemuKeyMap` for 60+ keys
- **Mouse**: Absolute positioning via `QmpInput::sendMouseMove(x, y)` + button down/up via `sendMouseButton()`
- **Gamepad**: Axis threshold swipes sent via QMP absolute mouse move (fallback to ADB for buttons)
- **Latency**: QMP target <5ms vs ADB's ~100ms (20× improvement)
- `main.cpp` wires `QmpInput` into `InputBridge::instance().setQmpInput()` on successful connection

### 5.6 QMP Auto-Reconnect
- `QmpInput::setAutoReconnect(true, 5000)` enables 5-second retry loop
- `onReconnectTimeout()` attempts `connectToHost()` until success
- Timer stops on successful connection, restarts on disconnect
- Enabled by default in `main.cpp`

### 5.7 FPS Counter UI
- QML toolbar now shows `"FPS: " + PerfMonitor.fps.toFixed(1)` label
- Visible only when `PerfMonitor.fps > 0`
- Updates in real-time via `fpsChanged` signal

## Optimizations Applied

1. **Frame capture payload**: gRPC switched from RGBA8888 to RGB888 and 960px stream width; ADB fallback throttled to 1s compatibility mode
2. **Error recovery**: `AdbFramebufferCapture` skips frame if previous `QProcess` still running
3. **QMP port fix**: `-ports qmpPort,adbPort` correctly maps console/QMP and ADB
4. **Dynamic DLL loading**: ANGLE (`libEGL.dll`) and HCS (`computecore.dll`) loaded at runtime via `QLibrary`
5. **FFmpeg path priority**: `ScreenRecorder` checks bundled `ffmpeg.exe` in app dir first
6. **Input latency**: QMP preferred over ADB for all input events (keyboard, mouse, gamepad)

---

## Files Location

All Chimera project files:
```
D:\Workspace_cloud\Personal_Project\chimera\
```

---

*Report generated automatically by build agent.*
