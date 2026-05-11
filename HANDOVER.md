# Project Chimera — Handover Document

> **For**: Next AI Agent
> **Date**: 2026-05-11
> **Project**: D:\Workspace_cloud\Personal_Project\chimera

---

## Quick Start

```powershell
# 1. Load VS dev environment (REQUIRED)
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

# 2. Configure
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64

# 3. Build
cmake --build build --config Release

# 4. Test
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

## What Was Done (Phases 1-5)

### Phase 1: MVP ✅
- Emulator boots via WHPX (`emulator.exe` from Android SDK)
- ADB raw screencap (~20 FPS)
- Qt 6 QML display with aspect-ratio preservation
- Keyboard/mouse input via ADB

### Phase 2: Gaming Core ✅
- XInput gamepad (14 buttons → Android keycodes)
- Instance persistence (JSON + clone)
- Screenshot toolbar button
- Input mapper overlay (JSON schemes)
- Multi-instance QML dialog
- Macro engine (background thread)

### Phase 3: Performance ✅
- WASAPI audio bridge
- Screen recorder (FFmpeg pipe + PNG fallback)
- ANGLE headers + libraries (Chrome 147 DLLs)
- Dynamic EGL loader (QLibrary)

### Phase 4: Core Virtualization ✅
- Device spoofing (5 profiles, build.prop)
- Raw ADB screencap (20 FPS)
- QMP input (console port = QMP)
- Memory trimmer (polls /proc/meminfo)
- Disk compactor (removes cache/logs/tmp)
- Framebuffer capture abstraction (ADB + VNC backends)
- VirtIO Audio (emulator accepts `-device virtio-snd-pci`)
- FFmpeg bundle (auto-download + CMake copy)

### Phase 5: Advanced Framework ✅
- VirtIO Input (QEMU args ready, prebuilt rejects)
- Hyper-V HCS API (computecore.dll loading, GPU-PV detection)
- Performance monitor (FPS, frame time, drops)
- QMP latency measurement
- QMP input integration (preferred over ADB)
- QMP auto-reconnect (5s retry)
- FPS counter in QML toolbar

## Architecture

```
src/
├── host/ui/           # Qt 6 QML, GuestDisplay, ScreenRecorder, PerformanceMonitor
├── host/input/        # InputBridge, InputMapper, GamepadManager, MacroEngine, QmpInput, VirtioInput
├── host/graphics/     # FramebufferCapture, AdbFramebufferCapture, VncFramebufferCapture, AngleBackend, EglLoader
├── host/audio/        # AudioBridge (WASAPI)
├── host/storage/      # SharedFolder stub
├── host/instance/     # InstanceManager, VirtualMachine, ProcessLauncher, DeviceSpoofer, MemoryTrimmer, DiskCompactor, HyperVManager
├── host/integration/  # WindowsNotifier, ClipboardBridge, LocationSimulator
├── host/config/       # ConfigManager (JSON)
└── common/utils/      # Logger, ThreadPool, FileUtils
```

## Key Files for Understanding

| File | What It Does |
|------|-------------|
| `src/host/ui/main.cpp` | Application entry point. Sets up emulator, ADB capture, QMP input, gamepad, audio |
| `src/host/graphics/AdbFramebufferCapture.cpp` | Raw ADB screencap (33ms interval = ~30 FPS) |
| `src/host/input/InputBridge.cpp` | Routes input events. Prefers QMP, falls back to ADB |
| `src/host/instance/VirtualMachine.cpp` | Launches emulator.exe with correct args (-ports, -device virtio-snd-pci) |
| `src/host/instance/DeviceSpoofer.cpp` | Modifies AVD overlay/build.prop to spoof flagship phones |
| `src/host/instance/MemoryTrimmer.cpp` | Polls Android meminfo, auto-trims on critical pressure |
| `src/host/instance/DiskCompactor.cpp` | Cleans cache.img, logs, temp files |
| `src/host/input/QmpInput.cpp` | QMP JSON protocol client with latency measurement + auto-reconnect |

## Current Limitations

| Limitation | Why | Resolution Path |
|------------|-----|----------------|
| ADB screencap ~20 FPS | ADB protocol overhead | Custom QEMU with shared memory / VNC display |
| Prebuilt emulator rejects virtio-input | Android Emulator binary doesn't compile virtio-input | Custom QEMU build from source |
| Prebuilt emulator no VNC | `-display vnc` not supported | Custom QEMU build |
| Hyper-V HCS not functional | Scaffolding only, no VM creation | Implement HCS JSON config + GPU-PV |
| No kernel input driver | BstkDrv.sys equivalent not implemented | Windows filter driver (complex) |

## Next Agent Should Know

1. **Build system is MSVC-only**. Do NOT try MinGW/MSYS2.
2. **Qt 6.8.3 at `C:\Qt\6.8.3\msvc2022_64`**. PATH must include Qt bin for tests.
3. **Android SDK at `third_party/android-sdk/`**. AVDs at `third_party/android-avd/`.
4. **ANGLE DLLs** (`libEGL.dll`, `libGLESv2.dll`) copied from Chrome. Auto-copied to build dir by CMake.
5. **FFmpeg** auto-downloaded to `third_party/ffmpeg/ffmpeg.exe`. Auto-copied to build dir.
6. **Reference files** from reverse engineering in `docs/references/`. Do NOT commit BlueStacks binaries.
7. **QMP is the console port (5554)**, NOT a separate port. `-ports 5554,5555` maps console/QMP + ADB.
8. **All 3 unit tests must pass** before declaring a task complete.

## External Dependencies (Not in Repo)

- Visual Studio 2022 Community (MSVC 19.44+)
- Qt 6.8.3 MSVC2022_64 (via aqtinstall)
- Android SDK (downloaded by `scripts/setup-android-sdk.py`)
- Android AVD images (downloaded by setup script)
- FFmpeg (downloaded by `scripts/fetch-ffmpeg.py`)
- ANGLE libraries (copied from Chrome)

## Git Workflow

- **Format**: `<type>: <description>`
- **Types**: feat, fix, refactor, docs, test, chore, perf, ci
- **Do NOT commit without explicit user request**
- **Never push to remote without explicit confirmation**

## Status

- **Phase**: 4 complete + Phase 5 framework
- **Tests**: 3/3 passing
- **Build**: Release mode, zero errors
- **Date**: 2026-05-11

---

*This document is a snapshot. For latest status, check STATUS.md and CLAUDE.md.*
