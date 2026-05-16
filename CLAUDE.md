# Project Chimera — CLAUDE.md

> Living document for Claude / OpenCode sessions. Update after every significant change.

## Current State

**Phase**: Phase 5c — WSL2 6.6 dxgkrnl+HvSocket Kernel COMPLETE 2026-05-17
**Date**: 2026-05-17
**Next**: Phase 5d — Build AOSP cuttlefish x86_64 image → VHDX for GPU-PV guest.

### v2 Phase 1 Verification Results (2026-05-16)

- ✅ `chimera-ui.exe --qemu-backend` starts stock QEMU 11.0.50 via `QemuBackend`
- ✅ QEMU boots Android-x86 9.0 r2 from ISO (`-vga vmware` required, boot=d cdrom)
- ✅ VNC connection (port 5900) established by `VncFramebufferCapture` with auto-reconnect
- ✅ QMP connection (port 4444) established; `input-send-event` returns `{'return': {}}`
- ✅ **QMP mouse click verified**: click at (512,384) woke Android screen from sleep; Android-x86 setup wizard "Hi there" appeared with mouse cursor visible at correct position
- ✅ Before/after VNC screenshots saved: `qmp_before.png` (black/sleep) → `qmp_after.png` (Android OOBE with cursor)
- Note: `chimera-base.qcow2` is still empty (32 GiB virtual, no Android installed); boots Live CD only

### What's Working

- ✅ Android Emulator (`emulator.exe`) boots Android 34 x86_64 via WHPX on this machine
- ✅ AVD `chimera_dev` created and verified (`sys.boot_completed = 1`)
- ✅ Full C++ skeleton compiles under MSVC + Qt 6.8.3
- ✅ 6/6 unit tests passing
- ✅ CMake build system configured for Visual Studio 2022
- ✅ **VirtualMachine launches actual `emulator.exe`** with native visible window by default, `-accel on`, `-gpu host`, `-vsync-rate 60`, and crash-report prompts disabled
- ✅ **ProcessLauncher::runAsync()** implemented with `CreateProcessW`, pipe redirection, reader threads
- ✅ **Native emulator window embedding** is the primary current display path; gRPC/ADB screencap remains `--stream-capture` fallback
- ✅ **GuestDisplay** renders QImage with aspect-ratio preservation in Qt Quick
- ✅ **InputBridge** forwards keyboard/mouse events via ADB `shell input` with worker thread + command queue
- ✅ **Qt→Android keycode mapping** implemented for alphanumeric, arrows, function keys, modifiers
- ✅ **End-to-end verified**: `chimera-ui.exe` → InstanceManager → emulator start with crash-report prompts disabled → native Win32 emulator window attach → Android 1280x720 / 60Hz

### Phase 2/2.x Features Completed

- ✅ **Gamepad support**: XInput polling at 60 Hz, button/axis change detection, 14-button mapping to Android keycodes, analog stick → swipe gestures
- ✅ **Instance persistence**: JSON save/load (`configs/instances.json`), auto-save on create/clone/delete, data directory copy on clone
- ✅ **Screenshot**: `GuestDisplay::saveScreenshot()`, timestamped PNG files, toolbar button + `Ctrl+Shift+S` shortcut
- ✅ **InputMapper integration**: right-side key mapping page plus stream-mode `InputMapperOverlay`, `InputMapper` JSON coordinate conversion
- ✅ **Multi-instance manager**: right-side panel page with instance list, create/start/stop/delete/clone, `QmlInstanceManager` QObject wrapper
- ✅ **Macro playback thread**: Background `std::thread` with `sleep_until` timing, loop support, injects via `InputBridge`
- ✅ **Macro UI**: Right-side panel page for record/play/delete, toolbar status indicators

### Phase 3 Features Completed

- ✅ **Audio Bridge (WASAPI)**: `IAudioClient` shared-mode initialization, background render thread, float32 queue with 2-second cap, silence padding
- ✅ **Screen Recorder**: FFmpeg subprocess pipe (H.264 MP4) with raw RGB24 stdin, auto-detects FFmpeg availability, PNG sequence fallback
- ✅ **ANGLE Headers**: Automated download script (`fetch-angle-headers.py`), CMake auto-detection, `AngleBackend` compiles with real EGL types and functions (`eglGetDisplay`, `eglInitialize`, `eglCreateContext`, etc.)

### Phase 4 Features Completed

- ✅ **Device Spoofing**: `DeviceSpoofer` modifies AVD `overlay/system/build.prop` to fake flagship phones (Samsung S24 Ultra, OnePlus 12, ROG Phone 8, Xiaomi 14 Pro, Pixel 8 Pro). Unlocks high FPS/quality settings locked behind hardware detection
- ✅ **Raw Display Capture**: Switched `AdbScreenCapture` from PNG (`-p`) to raw format. Eliminates PNG encode overhead, increased cap from 10 FPS → 20 FPS
- ✅ **QMP Input**: `QmpInput` connects to Android Emulator console port (5554) which IS the QMP interface. Runtime verified: greeting, capabilities negotiation, `input-send-event` all return success. Latency target: <5ms vs ADB's ~100ms
- ✅ **Memory Trim**: `MemoryTrimmer` polls `/proc/meminfo` via ADB every 5s, detects pressure levels (Moderate/Low/Critical), auto-trims on critical, exposes QML properties for UI indicators
- ✅ **Disk Compactor**: `DiskCompactor` analyzes instance directory size breakdown, safely removes `cache.img`, `*.log`, `*.tmp`, `*.dmp` files, optional zero-fill free space for host-level sparse file compaction
- ✅ **ANGLE Libraries**: `libEGL.dll` + `libGLESv2.dll` copied from Chrome 147, verified exports, CMake auto-copies to output directory
- ✅ **ANGLE Dynamic Loader**: `EglLoader.h` uses `QLibrary` to dynamically load `libEGL.dll` and resolve all core EGL functions at runtime (`eglGetDisplay`, `eglInitialize`, etc.). No import library (.lib) required
- ✅ **Framebuffer Capture Abstraction**: `FramebufferCapture` base class with `GrpcFramebufferCapture` (primary), `AdbFramebufferCapture` (fallback), and `VncFramebufferCapture` (future custom QEMU).
- ✅ **VirtIO Audio**: `VirtualMachine::start()` passes `-qemu -device virtio-snd-pci` to emulator. Runtime verified: emulator accepts and boots with the device
- ✅ **FFmpeg Bundle**: `scripts/fetch-ffmpeg.py` downloads BtbN Windows build (~200MB), extracts `ffmpeg.exe` to `third_party/ffmpeg/`. CMake post-build auto-copies to output dir. `ScreenRecorder` checks bundled path first

### Phase 5a HCS + HvSocket End-to-End Verification Results (2026-05-17)

- ✅ `chimera-ui.exe --hcs-backend` loads `configs/hcs.json`, detects GPU-PV (1 partition available)
- ✅ `HyperVManager::createVm()` succeeds: `HCS state → Creating → Stopped`
- ✅ `HyperVManager::startVm()` succeeds: `HCS state → Starting → Running`
- ✅ VM GUID obtained from `hcsManager->partitionId()` (RuntimeId) — not vmId() — and passed to HvSocket layer
- ✅ Serial console pipe: HCS is pipe SERVER; host opens as CLIENT with `CreateFile` + retry loop (called after Running state)
- ✅ Kernel serial output confirmed via `\\.\pipe\chimera-serial`: vsock/hv_sock modules loaded, both ports ready
- ✅ AF_HYPERV dual-channel connection: port 16 (input) + port 17 (display) both established
- ✅ `guest_display.c` daemon streams 640×480 RGB888 frames at ~30fps via vsock port 17 → `HvSocketFramebufferCapture` decodes → `GuestDisplay` renders: **FPS 26–27, zero dropped frames**
- ✅ `guest_input.c` daemon receives 16-byte `linux_input_event` structs from host on vsock port 16
- ✅ Fixed: zstd-compressed `.ko.zst` modules decompressed to plain `.ko` for busybox `insmod` (init_module syscall rejects zstd magic)
- ✅ Fixed: `sockaddr_vm.svm_flags` must be zero; old binary set `svm_zero[0]=2` which Azure 6.11 kernel interprets as invalid flag → EINVAL. New `vsock_relay.c`/`guest_*.c` use `memset(&addr,0,sizeof(addr))`
- ✅ HCS JSON builder fixed: removed invalid `Uefi.ApplyDefaultRuntimeFirmware` field; SCSI controller omitted when no disks
- ✅ `HvSocketFramebufferCapture` PIMPL-refactored and compiles clean
- ✅ `InputBridge` wired: HvSocket (highest priority) → QMP → ADB fallback chain
- ✅ 6/6 unit tests passing

### Phase 5b Real Framebuffer + uinput Verification Results (2026-05-17)

- ✅ `VideoMonitor` added to HCS JSON (1280×720) — Hyper-V synthetic video device appears in guest
- ✅ `hyperv_drm.ko` (Azure 6.11, no deps, DRM_FBDEV_EMULATION=y) loaded via `insmod` in init
- ✅ `/dev/fb0` created: `hyperv_drmdrmfb frame buffer device`, 1280×720 32bpp
- ✅ `chimera-display-relay` reads from `/dev/fb0`, BGRA→RGB24 conversion, streams via vsock port 17
- ✅ `chimera-input-relay` creates uinput device `Chimera HvSocket Input` (`/devices/virtual/input/input0`)
- ✅ Both relay daemons connected to host within 30s after VM boot
- ✅ **FPS: ~30fps sustained, zero dropped frames** (1280×720 RGB888, ~2.6 MB/frame, ~78 MB/s vmbus)
- ✅ `initrd.img` rebuilt with: vsock, hv_sock, vsock_loopback, hyperv_drm modules + production relay daemons
- ✅ `build-initramfs.sh` updated: searches system-installed Azure kernel modules, includes hyperv_drm
- Note: `uname`, `mkdir`, `seq` commands missing from busybox symlinks (cosmetic — all key functionality works)

### Phase 5c WSL2 6.6 dxgkrnl+HvSocket Kernel Verification Results (2026-05-17)

- ✅ `scripts/build-android-kernel.sh` builds WSL2 6.6.123.2 kernel with `CONFIG_DXGKRNL=y`, `CONFIG_VSOCKETS=y`, `CONFIG_INPUT_UINPUT=y`
- ✅ `scripts/patch-kernel-vsock-drm-modules.sh` converts `hv_sock=m` + `hyperv_drm=m`; builds `net/vmw_vsock/hv_sock.ko` + `drivers/gpu/drm/hyperv/hyperv_drm.ko` (565KB + 892KB)
- ✅ `scripts/build-wsl-initrd.sh` packs modules + relay daemons into 1.2MB initrd; init script runs `insmod hv_sock.ko` then `insmod hyperv_drm.ko` after boot
- ✅ `hv_sock`: loads successfully on second VMBus registration attempt (VMBus channel offered post-init)
- ✅ `hyperv_drm`: loads after `sleep 2`, probes Synthvid v3.5 VideoMonitor channel → `/dev/fb0` (1280×720 32bpp)
- ✅ `test-hcs-wsl2-kernel.py`: 3/3 checks pass — `/dev/fb0 ready` + `Input relay connected` + `Display relay connected`
- ✅ Azure 6.11 confirmed: has NO dxgkrnl → WSL2 6.6 is the correct base for GPU-PV
- Note: hv_sock registers/unregisters once at insmod, then re-registers successfully (benign retry behaviour)

### Phase 5 Features (Advanced Virtualization)

- ✅ **VirtIO Input Framework**: `VirtioInput` class created with QEMU arg generation (`virtio-keyboard-pci`, `virtio-mouse-pci`, `virtio-tablet-pci`). Prebuilt Android Emulator rejects these devices; requires custom QEMU build
- ✅ **Hyper-V HCS API (Phase 5a Week 1-3)**: `HyperVManager` fully rewritten with correct `computecore.dll`/`computestorage.dll` function signatures, real async `createVm/startVm/stopVm/terminateVm` with `HcsWaitForOperationResult`, `buildHcsJsonString()` using nlohmann/json (LinuxKernelDirect + GPU-PV `AssignmentMode:Mirror`), proper thread lifecycle (no `detach()`, `QPointer` guard, `aborted` flag), mutex-protected `errorString`. `--hcs-backend` flag wired in `main.cpp`. `configs/hcs.json` config template created. Build verified: `computecore.dll` + `HcsCreateOperation` confirmed present on this machine.
- ✅ **HvSocket End-to-End Display Pipeline (Phase 5a Week 3)**: Custom `initrd_azure.img` with `guest_display` (vsock port 17, 640×480 RGB888 ~30fps) + `guest_input` (vsock port 16, 16-byte `linux_input_event`) daemons. `HvSocketFramebufferCapture` decodes 8-byte header + pixel data, emits `frameReady()`. Verified: FPS 26–27 sustained, zero dropped frames. Serial console pipe changed to CLIENT `CreateFile` (HCS is server). Azure 6.11 kernel modules decompressed from `.ko.zst` → plain `.ko`. `sockaddr_vm.svm_flags=0` fix applied.
- ✅ **Performance Monitor**: `PerformanceMonitor` tracks FPS, frame time (avg/max), dropped frames. Logs every 5s. Exposed to QML as `PerfMonitor` context property.
- ✅ **QMP Latency Measurement**: `QmpInput` measures round-trip time per command via `QElapsedTimer`. `lastLatencyMs()` property available
- ✅ **QMP Input Integration**: `InputBridge` now prefers QMP over ADB for all input events (keyboard, mouse, gamepad). Qt→QEMU keycode map added for 60+ keys. Mouse absolute positioning via QMP. Falls back to ADB if QMP unavailable
- ✅ **QMP Auto-Reconnect**: `QmpInput` retries connection every 5s if disconnected. Enabled by default in `main.cpp`
- ✅ **Native Display Path**: `NativeEmulatorView` embeds the Android Emulator Win32 window in the QML shell. Runtime verified with Android reporting 1280x720, 240 dpi, and SurfaceFlinger 60.00 Hz.
- ✅ **Embedded Control Shell**: Android Emulator auxiliary tool windows are hidden after native attach; Chimera provides a compact right-side status/action panel instead.
- ✅ **Clean QML Shell Layout**: Top bar is product/status only, actions are consolidated in the right panel, FPS appears once, clipped hover tooltips were removed, and native-hidden QML dialogs were replaced by right-side pages.
- ✅ **Android Navigation Controls**: Right-side panel includes Back, Home, and Recents buttons. QML calls `QmlAndroidControls`, which sends Android semantic keyevents through `InputBridge`.
- ✅ **Native Recording / Screenshot Fix**: `NativeEmulatorView` can capture the embedded Win32 child window for screenshots and feed `ScreenRecorder` for native-mode recording.
- ✅ **Native Aspect Fit**: Embedded display shell is constrained to 16:9 for the 1280×720 guest, avoiding emulator-side black bars from arbitrary host panel ratios.
- ✅ **BlueStacks-Inspired Host Tuning**: Qt Quick is forced to D3D11 RHI before `QGuiApplication`; emulator/qemu process tree is boosted to high priority during startup.
- ✅ **BlueStacks-Style Shortcuts**: Added keyboard coverage for screenshot, recording, key mapping page, macro page, multi-instance page, fullscreen, and `Esc` fullscreen exit.
- ✅ **Visible Emulator Boot Fix**: `-crash-report-mode never` and `-no-metrics` prevent Android Emulator from stalling on crash-report consent before QEMU/ADB starts.
- ✅ **gRPC Frame Capture**: `GrpcFramebufferCapture` uses Android Emulator `streamScreenshot` over HTTP/2 gRPC for `--stream-capture` fallback/debug mode. ADB raw starts only as fallback.
- ✅ **gRPC Pre-Boot Retry**: `main.cpp` restarts the screenshot stream until the first frame arrives, preventing early pending connections from falling back permanently to ADB.
- ✅ **gRPC Orientation / Bandwidth Fix**: Screenshot bytes are copied top-down; stream uses RGB888 instead of RGBA8888 to cut payload by 25%.
- ✅ **720p Performance Profile**: Default guest and AVD hardware config are synchronized to 1280×720 / 240 dpi / 60Hz. gRPC fallback stream width is 960px.
- ✅ **Traditional Chinese UI**: Main QML shell, toolbar, dialogs, and input overlay use Traditional Chinese labels.

### 2026-05-14 Review Fixes

- ✅ `Framebuffer::writeBackBuffer()` resize deadlock fixed; added `test-graphics-framebuffer`
- ✅ `InstanceManager` now lists saved configs and live VMs, returns saved config data, and can start saved-only instances
- ✅ `QmpInput` reconnect timer now starts after failed connection, socket error, or unexpected disconnect
- ✅ `MacroEngine` stops existing playback before starting another thread
- ✅ `InputMapper` and `MacroEngine` reject unsafe `../` file names
- ✅ Removed stale `main.moc` include warning
- ✅ `chimera-ui` build now auto-runs `windeployqt`, so `build\Release\chimera-ui.exe` can launch without manually setting Qt PATH
- ✅ Fixed ADB device targeting: screen capture and ADB fallback input now use emulator serial (`-s emulator-<consolePort>`) instead of misusing `adb -P`
- ✅ Fixed Android raw screencap parsing for current emulator output (16-byte header)
- ✅ Disabled guest startup audio by launching emulator with `-no-audio` and skipping unused WASAPI bridge init
- ✅ Removed broken VNC primary path. Android Emulator 36.5.11 reports VNC requires unsupported `-gpu guest`.
- ✅ Default RAM reduced to 2048 MB to avoid Windows commit-limit launch failure on this machine.
- ✅ Performance monitor no longer counts first-frame boot wait as max/average frame time.
- ✅ gRPC capture retries pre-boot pending streams; live test verified boot completion in about 32 seconds and frame delivery after retries.
- ✅ Stale AVD locks are cleaned when no emulator/qemu process is running.
- ✅ Emulator termination now kills child `qemu-system-x86_64-headless.exe` processes to avoid orphaned resource usage.

### 2026-05-15 Performance & UI Pass

- ✅ **QMP low-latency socket**: `QmpInput` disables Nagle's algorithm (`LowDelayOption`) and enables `KeepAliveOption` on connect. Tiny QMP command packets no longer wait up to ~40ms for TCP coalescing.
- ✅ **QMP mouse-move dedup**: `sendMouseMove()` drops duplicate coordinates, so high-rate mouse hardware no longer floods the QMP socket with no-op events.
- ✅ **Adaptive gamepad polling**: `GamepadManager::poll()` re-probes unplugged XInput slots only ~2x/sec (staggered per slot) instead of every frame; connected controllers keep full 60Hz polling.
- ✅ **PerformanceMonitor signal throttling**: `metricsChanged()` is emitted once per 1s window instead of per-frame, cutting 60Hz QML binding churn. `maxFrameTimeMs()` now reports the worst frame over the rolling window instead of an all-time value that never decays.
- ✅ **Batched guest tuning**: `applyGuestPerformanceSettings()` issues all six guest settings in a single `adb shell` invocation instead of six separate process spawns.
- ✅ **Modernized QML shell**: Refined neutral-dark palette, micro-interaction animations (hover/press transitions, press scale), top-bar status pill with pulsing indicator, recording badge, prominent FPS stat card, animated side-panel page transitions, app intro fade-in. All bindings and shortcuts preserved.

### Optimizations Applied / Measured

- ✅ **Frame capture bandwidth**: gRPC capture now requests 960px RGB888 frames; ADB fallback is throttled to 1 Hz
- ✅ **Error recovery**: `AdbFramebufferCapture` skips frame if previous capture still running (prevents queue buildup)
- ✅ **QMP port fix**: `VirtualMachine::start()` correctly maps `-ports qmpPort,adbPort` (console=5554/QMP, ADB=5555)
- ✅ **Dynamic DLL loading**: ANGLE + HCS APIs loaded at runtime via `QLibrary`, no link-time dependency on import libraries
- ✅ **Native display path**: live test attached the emulator window and Android reported active mode 60.00 Hz. gRPC fallback remains capped by screenshot-stream overhead.
- ⚠️ **Game-level locked 60 FPS not yet proven**: Requires workload-specific profiling; the display path is 60Hz-capable, but guest app jank can still occur.

### Build Verification

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

## Architecture

### Layered Design

```
┌─────────────────────────────────────────────┐
│  UI Layer (Qt 6 / QML)                      │
│  ├─ GuestDisplay (QQuickPaintedItem + QImage)│
│  ├─ InputMapperOverlay (on-screen controls) │
│  ├─ ScreenRecorder (FFmpeg pipe / PNG seq)  │
│  └─ ApplicationWindow (toolbar, shortcuts)  │
├─────────────────────────────────────────────┤
│  Host Service Layer (C++20)                 │
│  ├─ Input: QMP first, ADB fallback          │
│  ├─ Display: native window, stream fallback │
│  ├─ Audio: WASAPI bridge stub               │
│  ├─ Storage: Shared folders (9pfs)          │
│  ├─ Instance: VM lifecycle, CreateProcess   │
│  ├─ Config: JSON-based settings             │
│  └─ Integration: Notifications, Clipboard   │
├─────────────────────────────────────────────┤
│  Virtualization Layer (QEMU + WHPX)         │
│  ├─ Android Emulator (prebuilt)             │
│  ├─ WHPX acceleration                       │
│  └─ Custom VirtIO devices (future)          │
├─────────────────────────────────────────────┤
│  Android Guest (AOSP x86_64)                │
│  ├─ ANGLE Guest Driver (future)             │
│  ├─ VirtIO GPU / Net / Snd HALs (future)    │
│  ├─ libndk_translation (future)             │
│  └─ Gamepad / InputMapper JNI (future)      │
└─────────────────────────────────────────────┘
```

### Module Boundaries

| Module | Responsibility | Key Files |
|--------|---------------|-----------|
| `chimera-config` | Flat JSON key-value config (like bluestacks.conf) | `ConfigManager.h/cpp` |
| `chimera-input` | Qt events → ADB shell input; mapping schemes; XInput gamepad; macro record/playback | `InputBridge.h/cpp`, `InputMapper.h/cpp`, `GamepadManager.h/cpp`, `MacroEngine.h/cpp` |
| `chimera-graphics` | Guest frame callback (ADB screencap); ANGLE EGL backend with auto-detected headers | `GraphicsBridge.h/cpp`, `Framebuffer.h/cpp`, `Renderer.h/cpp`, `AngleBackend.h/cpp` |
| `chimera-audio` | WASAPI shared-mode output with background render thread | `AudioBridge.h/cpp` |
| `chimera-storage` | Host↔Guest folder sharing stub | `SharedFolder.h/cpp` |
| `chimera-instance` | VM CRUD, lifecycle, CreateProcess launch, pipe I/O, device spoofing, memory trim, disk compaction | `InstanceManager.h/cpp`, `VirtualMachine.h/cpp`, `ProcessLauncher.h/cpp`, `DeviceSpoofer.h/cpp`, `MemoryTrimmer.h/cpp`, `DiskCompactor.h/cpp` |
| `chimera-integration` | Windows toast, clipboard, GPS stub | `WindowsNotifier.h/cpp`, `ClipboardBridge.h/cpp`, `LocationSimulator.h/cpp` |
| `chimera-utils` | Logging, threading, file I/O | `Logger.h/cpp`, `ThreadPool.h/cpp`, `FileUtils.h/cpp` |
| `chimera-ui` | Qt Quick executable: Instance launch, ADB capture, input forward | `ChimeraWindow.h/cpp`, `GuestDisplay.h/cpp`, `InputMapperOverlay.h/cpp`, `main.cpp`, `.qml` |

### Communication Flows

**Guest → Host Display (Current — native primary)**
```
Guest SurfaceFlinger → Android Emulator native GPU window → NativeEmulatorView → Qt/QML shell
Fallback/debug: Guest SurfaceFlinger → Android Emulator gRPC streamScreenshot → GrpcFramebufferCapture → QImage → GuestDisplay::setFrame() → paint()
Compatibility fallback: Guest SurfaceFlinger → ADB daemon → adb exec-out screencap raw → AdbFramebufferCapture → QImage → GuestDisplay
```

**Host Input → Guest (Phase 2 — ADB shell input + gamepad)**
```
Qt Event → GuestDisplay → InputBridge::onMouseButton/onKeyEvent → command queue → worker thread → QProcess(adb shell input tap/keyevent) → Android EventHub → App
XInput poll (60 Hz) → GamepadManager::detectChanges() → InputBridge::onGamepadButton/onGamepadAxis → command queue → ADB keyevent / swipe → Android
```

**Instance Lifecycle**
```
User clicks "Start" → InstanceManager → VirtualMachine.buildEmulatorArgs() → ProcessLauncher.runAsync(emulator.exe, args)
                                  → StateCallback(VMState::Running)
```

## Key Decisions

1. **QEMU via Android Emulator binary**: Rather than compiling QEMU from source, we use the prebuilt Android SDK `emulator.exe` which is already QEMU+WHPX. This gets us to MVP faster. Raw QEMU compilation is deferred to Phase 3.

2. **MSVC toolchain**: MSYS2 UCRT64 GCC 16.1.0 has a broken `cc1plus.exe` on this machine. MSVC 19.44 is the only working compiler.

3. **Qt 6.8.3 MSVC build**: Installed via `aqtinstall` at `C:\Qt\6.8.3\msvc2022_64`.

4. **ANGLE stubbed**: Real ANGLE headers/libs are not yet integrated. `AngleBackend` compiles as a stub to avoid blocking the build.

5. **`google_apis` system image**: Avoids ADB RSA authorization lock in headless/automation mode.

6. **nlohmann/json for config**: Header-only, bundled in `third_party/`. Simple flat key-value JSON like bluestacks.conf.

7. **ADB screencap for MVP frame capture**: Not efficient (~10 FPS), but requires zero QEMU modifications. Will be replaced with QEMU display protocol or shared memory in Phase 2.

8. **ADB shell input for MVP input forwarding**: ~50-100ms latency, sufficient for verification. Will be replaced with virtio-input in Phase 2.

## Known Issues

| Issue | Status | Workaround |
|-------|--------|------------|
| MSYS2 GCC broken | WONTFIX | Use MSVC exclusively |
| Android Emulator gRPC stream peaks around 32 FPS under current 720p/RGB888 profile | ACCEPTED | Native window embedding is now default; shared GPU texture/custom QEMU remains future deep-integration path |
| Native child window overlays QML content inside the viewport | ACCEPTED | Keep controls outside the embedded viewport; current right-side panel is outside the native child window |
| High process priority can increase host contention | MONITOR | Use during 60 FPS tuning; profile desktop responsiveness under real games |
| ADB raw screencap fallback is throttled | ACCEPTED | Keep only as compatibility fallback, not primary display path |
| QMP mouse runtime validation | PARTIAL | Payload schema fixed; verify target-game behavior per title |
| Framebuffer read-side synchronization | OPEN | Replace internal reference return with snapshot or guarded read API |
| ProcessLauncher quoting | OPEN | Implement Windows command-line escaping for quotes/backslashes |
| Clipboard Unicode | OPEN | Replace `CF_TEXT` with `CF_UNICODETEXT` |
| No kernel input driver | OPEN | Phase 5: Windows filter driver (BstkDrv.sys equivalent) |

## File Locations

- **Project root**: `D:\Workspace_cloud\Personal_Project\chimera\`
- **Build output**: `D:\Workspace_cloud\Personal_Project\chimera\build\Release\`
- **Qt install**: `C:\Qt\6.8.3\msvc2022_64\`
- **Android SDK**: `D:\Workspace_cloud\Personal_Project\chimera\third_party\android-sdk\`
- **AVD data**: `D:\Workspace_cloud\Personal_Project\chimera\third_party\android-avd\chimera_dev.avd\`
- **Instance configs**: `D:\Workspace_cloud\Personal_Project\chimera\configs\instances.json`
- **Screenshots**: `D:\Workspace_cloud\Personal_Project\chimera\screenshots\`
- **Recordings**: `D:\Workspace_cloud\Personal_Project\chimera\recordings\`
- **ANGLE headers**: `D:\Workspace_cloud\Personal_Project\chimera\third_party\angle\`

## Phase 1 MVP Checklist

- [x] VirtualMachine launches actual `emulator.exe` with correct args
- [x] ProcessLauncher implements `runAsync()` with proper HANDLE/pipe handling
- [x] GraphicsBridge receives display frames from emulator (via ADB screencap)
- [x] GuestDisplay renders guest framebuffer as QImage in Qt Quick
- [x] InputBridge forwards keyboard/mouse events to guest (via ADB `input` command)

## Phase 2 Checklist (Gaming Core)

- [x] Gamepad support (XInput → ADB keyevent/swipe)
- [x] Instance persistence (JSON save/load, clone with data dir copy)
- [x] Screenshot (GuestDisplay::saveScreenshot, toolbar + shortcut)
- [x] Keyboard mapping editor (InputMapperOverlay ↔ InputMapper JSON)
- [x] Multi-instance manager (QML Dialog + QmlInstanceManager wrapper)
- [x] Macro playback thread (background std::thread with sleep_until timing)

## Phase 3 Checklist (Performance & Experience)

- [x] Audio bridge (WASAPI shared-mode output with render thread)
- [x] Screen recording (FFmpeg subprocess pipe + PNG fallback)
- [x] ANGLE headers integration (auto-download + CMake detection + real EGL types)
- [x] Bundle FFmpeg for seamless recording (auto-detect + PNG fallback implemented)

## Phase 4 Checklist (Core Virtualization)

- [x] Device spoofing (build.prop modification with 5 flagship profiles)
- [x] Raw display capture (20 FPS vs 10 FPS PNG)
- [x] QMP input (console port IS QMP, runtime verified)
- [x] Memory trim (MemoryTrimmer polls /proc/meminfo, auto-trims on critical pressure)
- [x] Disk compaction (DiskCompactor removes cache.img/logs/tmp, optional zero-fill)
- [x] ANGLE libraries (libEGL.dll + libGLESv2.dll from Chrome, CMake auto-copy)
- [x] ANGLE dynamic loader (EglLoader.h with QLibrary, no .lib required)
- [x] Framebuffer capture abstraction (GrpcFramebufferCapture + AdbFramebufferCapture + VncFramebufferCapture)
- [x] VirtIO Audio (emulator accepts `-device virtio-snd-pci`)
- [x] FFmpeg bundle (fetch-ffmpeg.py + CMake auto-copy)
- [ ] Replace ADB screencap with QEMU display protocol (VNC backend ready, needs custom QEMU)
- [ ] Replace ADB input with virtio-input (QMP is interim solution)
- [x] Hyper-V HCS API (GPU-PV) — HCS VM lifecycle + HvSocket end-to-end COMPLETE 2026-05-17

## Phase 5 Checklist (Hyper-V Native Stack)

- [x] HCS VM create/start/stop/terminate (`HyperVManager`)
- [x] Serial console via named pipe (CLIENT `CreateFile` after Running state)
- [x] AF_HYPERV dual-channel: port 16 input + port 17 display
- [x] Guest display daemon (`guest_display.c`) — 640×480 RGB888 test pattern ~30fps, zero dropped frames
- [x] Guest input daemon (`guest_input.c`) — 16-byte `linux_input_event` receive + console print
- [x] `HvSocketFramebufferCapture` — 8-byte header decode + QImage emit
- [x] Azure 6.11 kernel module fix (decompress `.ko.zst` → `.ko`, `svm_flags=0`)
- [x] `chimera-input-relay` → uinput injection: creates `Chimera HvSocket Input` virtual device, injects `linux_input_event` structs via `/dev/uinput` (CONFIG_INPUT_UINPUT=y, built-in)
- [x] `chimera-display-relay` → `/dev/fb0` real capture: VideoMonitor in HCS JSON + hyperv_drm.ko → 1280×720 32bpp framebuffer; BGRA→RGB24 at ~30fps, zero dropped frames
- [x] Build WSL2 6.6 kernel with dxgkrnl + hv_sock=m + hyperv_drm=m (`scripts/build-android-kernel.sh` + `patch-kernel-vsock-drm-modules.sh`)
- [x] Standalone HCS boot test 3/3 (`test-hcs-wsl2-kernel.py`): /dev/fb0 ✅ + input relay ✅ + display relay ✅
- [ ] Build AOSP cuttlefish x86_64 image → VHDX

## Reference: BlueStacks Architecture (Gemini DeepResearch)

> Technical findings from comprehensive BlueStacks reverse-engineering report.

### Virtualization Evolution
- **Type-2 → Type-1**: BlueStacks migrated from VirtualBox to Hyper-V WHPX
- **WHPX Architecture**: Root Partition (Host Windows) + Child Partition (Android) via VMBus/Hypercalls
- **Validation**: Our use of Android Emulator (QEMU+WHPX) matches this modern architecture

### Graphics Pipeline Insights
- **ANGLE**: OpenGL ES → DirectX 11 with HLSL shader translation (we have EGL headers)
- **Dual Mode Strategy**: Performance Mode (direct GPU, 240 FPS) vs Compatibility Mode (software fallback)
- **Vulkan**: Modern low-overhead API for Android Pie+, multi-threaded scalability
- **ASTC**: Hardware decode preferred, CPU fallback prevents black texture blocks

### Input Latency Critical Finding
- **BstkDrv.sys**: Kernel-mode driver (Ring 0) achieves sub-millisecond latency
- Bypasses all Windows user-mode API stacks and message queues
- **Implication**: Our ADB shell input (~100ms) is 100× too slow for competitive gaming
- **Solution Path**: virtio-input (open-source equivalent) or Windows filter driver

### Memory & Storage
- **Trim Memory**: Monitor Android GC + memory pressure, actively return pages to host
- **Low Memory Mode**: Kill background processes, reduce framebuffer, extend CPU wake intervals
- **VDI vs VHDX**: Dynamic allocation + compaction (zero-fill + `Optimize-VHD`)

### Network & Audio
- **NAT Mode**: Isolated 10.0.2.15, host acts as router, transparent VPN sharing
- **Audio**: AudioFlinger → WASAPI/DirectSound with bidirectional virtual devices

### Competitive Features
- **Device Spoofing**: Modify `build.prop` to fake flagship phones, unlock quality settings
- **ABI Translation**: LayerCake patent (ARM→x86 JIT); our equivalent: `libndk_translation`

## Reference Documents

| Document | Purpose |
|----------|---------|
| `docs/project/PLAN.md` | 4-phase implementation plan with full task breakdown |
| `docs/architecture/ARCHITECTURE.md` | Detailed module responsibilities and tech choices |
| `docs/project/BUILD.md` | Step-by-step build instructions for MSVC + Qt 6 |
| `docs/project/STATUS.md` | Current phase status, verification log, resolved issues |
| `AGENTS.md` | Agent workflow, coding standards, safety checklist |
| `docs/references/windows_android_virtualization_analysis.md` | Windows Android virtualization tech analysis (from reverse engineering) |
| `docs/references/bluestacks.conf` | BlueStacks configuration file reference |

## Reverse Engineering References

Original analysis files from `BlueStacks_nxt/` have been copied to:
- `docs/references/windows_android_virtualization_analysis.md` — Virtualization feasibility analysis
- `docs/references/bluestacks.conf` — BlueStacks config format reference

**Do NOT commit** BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/).

---

*Updated: 2026-05-17*
*Phase: Phase 5c COMPLETE — WSL2 6.6 kernel (dxgkrnl=y, hv_sock=m, hyperv_drm=m) boots via HCS, 3/3 checks pass: /dev/fb0 + input relay + display relay*
*Tests: 6/6 passing*
