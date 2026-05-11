# Project Chimera — CLAUDE.md

> Living document for Claude / OpenCode sessions. Update after every significant change.

## Current State

**Phase**: 4 (Core Virtualization) — IN PROGRESS  
**Date**: 2026-05-09  
**Next**: Memory trim, ANGLE libraries, disk compaction, Hyper-V HCS

### What's Working

- ✅ Android Emulator (`emulator.exe`) boots Android 34 x86_64 via WHPX on this machine
- ✅ AVD `chimera_dev` created and verified (`sys.boot_completed = 1`)
- ✅ Full C++ skeleton compiles under MSVC + Qt 6.8.3
- ✅ 3/3 unit tests passing
- ✅ CMake build system configured for Visual Studio 2022
- ✅ **VirtualMachine launches actual `emulator.exe`** with `-no-window -accel on -gpu swiftshader_indirect`
- ✅ **ProcessLauncher::runAsync()** implemented with `CreateProcessW`, pipe redirection, reader threads
- ✅ **ADB screencap** captures valid PNG frames (1.16MB, `?PNG` header verified)
- ✅ **GuestDisplay** renders QImage with aspect-ratio preservation in Qt Quick
- ✅ **InputBridge** forwards keyboard/mouse events via ADB `shell input` with worker thread + command queue
- ✅ **Qt→Android keycode mapping** implemented for alphanumeric, arrows, function keys, modifiers
- ✅ **End-to-end verified**: `chimera-ui.exe` → InstanceManager → emulator start → ADB boot → screen capture → Qt render

### Phase 2/2.x Features Completed

- ✅ **Gamepad support**: XInput polling at 60 Hz, button/axis change detection, 14-button mapping to Android keycodes, analog stick → swipe gestures
- ✅ **Instance persistence**: JSON save/load (`configs/instances.json`), auto-save on create/clone/delete, data directory copy on clone
- ✅ **Screenshot**: `GuestDisplay::saveScreenshot()`, timestamped PNG files, toolbar button + `Ctrl+Shift+S` shortcut
- ✅ **InputMapper integration**: `InputMapperOverlay` loads/saves schemes via `InputMapper` JSON, coordinate conversion
- ✅ **Multi-instance dialog**: QML `Dialog` with instance list, create/start/stop/delete/clone, `QmlInstanceManager` QObject wrapper
- ✅ **Macro playback thread**: Background `std::thread` with `sleep_until` timing, loop support, injects via `InputBridge`
- ✅ **Clone dialog + Macro UI**: Per-instance clone, macro record/play/delete with loop count, toolbar status indicators

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
- ✅ **Framebuffer Capture Abstraction**: `FramebufferCapture` base class with `AdbFramebufferCapture` (raw/PNG backends) and `VncFramebufferCapture` (RFB protocol client for future custom QEMU). Replaces inline `AdbScreenCapture` in `main.cpp`
- ✅ **VirtIO Audio**: `VirtualMachine::start()` passes `-qemu -device virtio-snd-pci` to emulator. Runtime verified: emulator accepts and boots with the device
- ✅ **FFmpeg Bundle**: `scripts/fetch-ffmpeg.py` downloads BtbN Windows build (~200MB), extracts `ffmpeg.exe` to `third_party/ffmpeg/`. CMake post-build auto-copies to output dir. `ScreenRecorder` checks bundled path first

### Phase 5 Features (Advanced Virtualization)

- ✅ **VirtIO Input Framework**: `VirtioInput` class created with QEMU arg generation (`virtio-keyboard-pci`, `virtio-mouse-pci`, `virtio-tablet-pci`). Prebuilt Android Emulator rejects these devices; requires custom QEMU build
- ✅ **Hyper-V HCS API Framework**: `HyperVManager` class with `computecore.dll` dynamic loading, GPU-PV detection via `EnumDisplayDevicesW`, HCS operation function pointer resolution. VM creation is experimental scaffolding
- ✅ **Performance Monitor**: `PerformanceMonitor` tracks FPS, frame time (avg/max), dropped frames. Logs every 5s. Exposed to QML as `PerfMonitor` context property. FPS counter visible in toolbar
- ✅ **QMP Latency Measurement**: `QmpInput` measures round-trip time per command via `QElapsedTimer`. `lastLatencyMs()` property available
- ✅ **QMP Input Integration**: `InputBridge` now prefers QMP over ADB for all input events (keyboard, mouse, gamepad). Qt→QEMU keycode map added for 60+ keys. Mouse absolute positioning via QMP. Falls back to ADB if QMP unavailable
- ✅ **QMP Auto-Reconnect**: `QmpInput` retries connection every 5s if disconnected. Enabled by default in `main.cpp`

### Optimizations Applied

- ✅ **Frame capture interval**: Reduced from 50ms (20 FPS) → 33ms (~30 FPS target)
- ✅ **Error recovery**: `AdbFramebufferCapture` skips frame if previous capture still running (prevents queue buildup)
- ✅ **QMP port fix**: `VirtualMachine::start()` correctly maps `-ports qmpPort,adbPort` (console=5554/QMP, ADB=5555)
- ✅ **Dynamic DLL loading**: ANGLE + HCS APIs loaded at runtime via `QLibrary`, no link-time dependency on import libraries

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
│  ├─ Input: ADB command queue, key mapping   │
│  ├─ Graphics: Frame callback (ADB screencap)│
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

**Guest → Host Frame Delivery (Phase 1 — ADB screencap)**
```
Guest SurfaceFlinger → ADB daemon → adb exec-out screencap -p → QProcess → QImage → GuestDisplay::setFrame() → paint()
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
| ADB screencap ~20 FPS (raw) | ACCEPTED | Phase 4+: QEMU display protocol or GPU texture sharing for 60+ FPS |
| Audio not wired to emulator | OPEN | WASAPI output ready; need virtio-snd in QEMU args |
| FFmpeg not bundled | OPEN | Auto-detected from PATH; falls back to PNG sequence |
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
- [x] Framebuffer capture abstraction (AdbFramebufferCapture + VncFramebufferCapture)
- [x] VirtIO Audio (emulator accepts `-device virtio-snd-pci`)
- [x] FFmpeg bundle (fetch-ffmpeg.py + CMake auto-copy)
- [ ] Replace ADB screencap with QEMU display protocol (VNC backend ready, needs custom QEMU)
- [ ] Replace ADB input with virtio-input (QMP is interim solution)
- [ ] Hyper-V HCS API (GPU-PV)

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
| `PLAN.md` | 4-phase implementation plan with full task breakdown |
| `ARCHITECTURE.md` | Detailed module responsibilities and tech choices |
| `BUILD.md` | Step-by-step build instructions for MSVC + Qt 6 |
| `STATUS.md` | Current phase status, verification log, resolved issues |
| `AGENTS.md` | Agent workflow, coding standards, safety checklist |
| `docs/references/windows_android_virtualization_analysis.md` | Windows Android virtualization tech analysis (from reverse engineering) |
| `docs/references/bluestacks.conf` | BlueStacks configuration file reference |

## Reverse Engineering References

Original analysis files from `BlueStacks_nxt/` have been copied to:
- `docs/references/windows_android_virtualization_analysis.md` — Virtualization feasibility analysis
- `docs/references/bluestacks.conf` — BlueStacks config format reference

**Do NOT commit** BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/).

---

*Updated: 2026-05-11*
*Phase: 4 complete + Phase 5 framework + optimizations*
*Tests: 3/3 passing*
