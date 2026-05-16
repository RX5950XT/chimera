# Project Chimera — AGENTS.md

> Agent-focused project guide. Read this before making any changes.

## Project Overview

**Project Chimera** is an open-source Windows Android emulator targeting mobile gamers. Built from scratch using open-source components only — no cloud dependency, no ads, no telemetry.

- **License**: Apache 2.0 (host core) + GPL v2 (QEMU layer, IPC-isolated)
- **Platform**: Windows 10/11 Pro/Enterprise with Hyper-V / WHPX
- **UI**: Qt 6.8+ (QML)
- **Virtualization**: QEMU + WHPX (Windows Hypervisor Platform)
- **Graphics**: ANGLE (OpenGL ES → D3D11/Vulkan)
- **Android**: AOSP x86_64 with libndk_translation for ARM compat

## Working Directory

```
D:\Workspace_cloud\Personal_Project\chimera\
```

## Build System

- **Generator**: CMake + Visual Studio 17 2022 (NOT MSYS2/Ninja)
- **Compiler**: MSVC 19.44+ (via `vcvarsall.bat amd64`)
- **Qt**: 6.8.3 at `C:\Qt\6.8.3\msvc2022_64`
- **Standard**: C++20

### Build Commands

```powershell
# Load VS dev environment (REQUIRED for every new terminal)
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

# Configure
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64

# Build
cmake --build build --config Release

# Test ($env:PATH must include Qt bin for DLLs)
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

## Key Architecture

```
chimera/
├── src/host/ui/          # Qt 6 QML window, input overlay, screen recorder
├── src/host/config/      # JSON config manager (nlohmann/json)
├── src/host/input/       # Input bridge, mapper, gamepad, macros, QMP, virtio
├── src/host/graphics/    # Framebuffer capture (gRPC/ADB/VNC), renderer, ANGLE backend, performance monitor
├── src/host/audio/       # WASAPI audio bridge
├── src/host/storage/     # Shared folders (9pfs)
├── src/host/instance/    # VM lifecycle, process launcher, device spoofer, memory trimmer, disk compactor, Hyper-V manager
├── src/host/integration/ # Windows notifications, clipboard, GPS
├── src/common/utils/     # Logger, thread pool, file utils
├── tests/unit/           # Qt Test executables (separate .exe per module)
├── scripts/              # Python automation scripts
├── configs/              # Default settings JSON
├── docs/                 # Architecture docs, reference materials
│   ├── architecture/     # Architecture overview
│   ├── process/          # Contributing and handover docs
│   ├── project/          # Build, status, plan, review reports
│   └── references/       # Reverse engineering artifacts (bluestacks.conf, virtualization analysis)
└── third_party/          # nlohmann/json, ANGLE headers, Android SDK, FFmpeg
```

## Coding Standards

- **Immutable data preferred**: Create new objects, never mutate existing ones in-place
- **Functions < 50 lines**, files < 800 lines
- **Max 4 nesting levels**
- **Handle errors at every layer**, never swallow exceptions silently
- **Validate all input at system boundaries** (user input, API responses, file contents)
- **Use RAII** for resource management
- **Forward-declare** in headers when possible to reduce include cycles

## Qt / MOC Specific Rules

- **Do NOT use `QT_NO_KEYWORDS`**: It breaks `signals:` / `slots:` macros with MSVC MOC
- **QObject subclasses must use Q_OBJECT macro**
- **Prefer QQuickPaintedItem over QQuickItem** when you need custom paint()
- **Separate test executables**: Each QTest module must be its own `.exe` (never link multiple QTEST_MAIN into one binary)

## CMake Rules

- Every module under `src/` has its own `CMakeLists.txt`
- Libraries are `STATIC` unless there's a reason to share them
- `find_package(Qt6)` at top level; subdirectories use component targets
- `nlohmann_json` is an `INTERFACE` library pointing to `${CMAKE_SOURCE_DIR}/third_party`
- Tests only build when `CHIMERA_BUILD_TESTS=ON` (default ON)
- Qt UI only builds when `CHIMERA_BUILD_QT_UI=ON` (default ON)

## Testing

- Use Qt Test (`QTest`) framework
- Each test file: `QTEST_MAIN(TestClassName)` + `#include "file.moc"` at bottom
- Run with: `ctest --test-dir build -C Release`
- Qt DLLs must be in PATH or tests fail with `0xC0000135`
- Current unit tests: config manager, input mapper, instance manager, graphics framebuffer, ADB raw framebuffer, QMP input

## Completed Features (Phase 1-5)

### Core (Phase 1 MVP)
- ✅ Android Emulator (`emulator.exe`) boots via WHPX
- ✅ Native Android Emulator window embedding primary display path + gRPC/ADB raw fallback via `--stream-capture`
- ✅ Qt 6 QML guest display with aspect-ratio preservation
- ✅ Keyboard/mouse input forwarding via ADB

### Gaming (Phase 2)
- ✅ XInput gamepad support (60 Hz polling, 14-button mapping)
- ✅ Instance persistence (JSON save/load, clone with data dir copy)
- ✅ Screenshot (timestamped PNG, toolbar + Ctrl+Shift+S)
- ✅ Input mapper side panel + stream-mode overlay (JSON scheme load/save)
- ✅ Multi-instance manager (right-side panel + QObject wrapper)
- ✅ Macro engine (background thread, loop support)

### Performance (Phase 3)
- ✅ WASAPI audio bridge (shared-mode, render thread)
- ✅ Screen recorder (native child-window capture + FFmpeg H.264 pipe + PNG sequence fallback)
- ✅ ANGLE headers integration (auto-download script, CMake detection)
- ✅ ANGLE libraries copied from Chrome (libEGL.dll + libGLESv2.dll)
- ✅ Dynamic EGL loader (QLibrary, no .lib import library needed)

### Virtualization (Phase 4)
- ✅ Device spoofing (5 flagship profiles, build.prop modification)
- ✅ Raw ADB screencap (20 FPS, no PNG encoding overhead)
- ✅ QMP input (console port IS QMP, runtime verified)
- ✅ Memory trimmer (polls /proc/meminfo, auto-trims on critical pressure)
- ✅ Disk compactor (removes cache/logs/tmp, optional zero-fill)
- ✅ Framebuffer capture abstraction (GrpcFramebufferCapture + AdbFramebufferCapture + VncFramebufferCapture)
- ✅ VirtIO Audio (`-device virtio-snd-pci`, emulator accepts)
- ✅ FFmpeg bundle (fetch-ffmpeg.py + CMake auto-copy)

### Advanced (Phase 5)
- ✅ VirtIO Input framework (QEMU arg generation, prebuilt rejects — needs custom QEMU)
- ✅ Hyper-V HCS API framework (computecore.dll dynamic loading, GPU-PV detection)
- ✅ Performance monitor (FPS, frame time, drops, QML UI counter)
- ✅ QMP latency measurement (round-trip timer per command)
- ✅ QMP input integration (preferred over ADB for keyboard/mouse/gamepad)
- ✅ QMP auto-reconnect (5-second retry loop; fixed after failed connection/socket error on 2026-05-14)
- ✅ **HvSocket end-to-end (2026-05-17)**: HCS VM lifecycle → serial console pipe (CLIENT, not server) → AF_HYPERV dual-channel → `guest_display.c` 640×480 RGB888 ~30fps → `HvSocketFramebufferCapture` → `GuestDisplay` renders at 26–27 FPS, zero dropped frames. `guest_input.c` receives `linux_input_event` on port 16. Azure 6.11 kernel module fix: `.ko.zst` decompressed; `svm_flags=0` applied.

## Current Review Notes

- Latest review report: `docs/project/CODE_REVIEW.md`
- Last verified: 2026-05-17, Release build passed, 6/6 Qt unit tests passed
- HCS smoke (2026-05-17): `--hcs-backend` boots custom kernel/initrd, serial pipe reads kernel output, HvSocket port 16+17 both connected, display FPS 26–27 sustained with zero dropped frames
- Live smoke: stale AVD locks removed, emulator boot completed, QMP auto-reconnect succeeded, native emulator window attached, Android reports 1280x720 / 240 dpi / 60.00 Hz / `skiavk`
- Toolbar smoke: native embedding hides Android Emulator auxiliary tool windows; Chimera exposes controls in its own compact right-side status/action panel and supports `Esc` to leave fullscreen.
- UI shell pass: top bar is product/status only, repeated toolbar actions were consolidated, duplicate FPS badges were removed, clipped hover tooltips were removed, Android Back/Home/Recents controls were added, and key/multi/macro UI now opens inside the right panel so native child windows cannot cover it.
- Native viewport pass: embedded emulator viewport is constrained to the 1280×720 guest's 16:9 aspect ratio; screenshots/recording now work on the native child-window path.
- Perf & UI pass (2026-05-15): QMP socket disables Nagle (`LowDelayOption`) + enables KeepAlive; `sendMouseMove` drops duplicate coordinates; `GamepadManager` adaptively backs off polling on unplugged XInput slots; `PerformanceMonitor` emits `metricsChanged` once per 1s window and reports rolling-window max frame time; `applyGuestPerformanceSettings` batches all six guest tweaks into one `adb shell` call; QML shell modernized with micro-interaction animations, status pill, FPS stat card — all bindings/shortcuts preserved.
- BlueStacks reference pass: use native display, D3D11 Qt shell, 60 FPS cap, 240 DPI, dense shortcuts, and process priority tuning; findings are in `docs/references/bluestacks_runtime_findings.md`
- Visible/native boot requires `-crash-report-mode never`; otherwise Android Emulator may stall on a crash-report consent dialog before QEMU/ADB starts
- Known follow-ups: game-level 60 FPS profiling under real workloads, framebuffer read-side synchronization, ProcessLauncher quoting, Unicode clipboard
- Runtime note: ADB host commands must target Android Emulator by serial (`emulator-5554` style), not by `adb -P <devicePort>`
- Runtime note: Current emulator raw screencap uses a 16-byte header; image decode must account for it
- Runtime note: Android Emulator 36.5.11 VNC is not viable with host GPU; it requires unsupported `-gpu guest`. Use gRPC first and ADB only as fallback.

## Critical Decisions (Do Not Change Without Discussion)

1. **MSVC only**: MSYS2 GCC is broken on this machine (`cc1plus.exe` crashes). Do NOT try to reintroduce MinGW builds.
2. **Android Emulator binary**: We use the prebuilt `emulator.exe` from Android SDK (QEMU+WHPX) rather than compiling QEMU from source for Phase 1-4.
3. **ANGLE dynamic loading**: `libEGL.dll` + `libGLESv2.dll` loaded at runtime via QLibrary. No import library (.lib) required.
4. **AVD `google_apis`**: Avoid `google_apis_playstore` images — they cause ADB RSA authorization issues in automation.
5. **QMP over ADB**: InputBridge prefers QMP (console port 5554) for all input events. Falls back to ADB if QMP unavailable.
6. **Native window over screenshot streaming**: Default display path is `NativeEmulatorView` embedding the Android Emulator Win32 window. `GrpcFramebufferCapture` and ADB raw capture are fallback/debug paths behind `--stream-capture`.
7. **Reference files**: Reverse engineering artifacts (bluestacks.conf, virtualization analysis) stored in `docs/references/`. Do NOT commit BlueStacks binaries.

## Git Workflow

- **Format**: `<type>: <description>`
- **Types**: feat, fix, refactor, docs, test, chore, perf, ci
- **Do NOT commit without explicit user request**
- **Never push to remote without explicit confirmation**

## Files That Must Stay Updated

| File | When to Update |
|------|---------------|
| `docs/project/STATUS.md` | After any major milestone or blocker resolution |
| `docs/project/BUILD.md` | When build steps, dependencies, or tools change |
| `AGENTS.md` | When agent workflow, conventions, or environment changes |
| `CLAUDE.md` | When architecture, module boundaries, or decisions change |
| `README.md` | When user-facing features or quick-start steps change |

## Safety Checklist (Before Any Commit)

- [ ] No hardcoded secrets (API keys, passwords, tokens)
- [ ] All user input validated
- [ ] No SQL injection vectors (we use JSON, not SQL, but still)
- [ ] Error messages do not leak sensitive data
- [ ] Tests pass locally

## Troubleshooting

### `cc1plus.exe` crash / GCC issues
→ Use MSVC. Do NOT attempt to fix MSYS2 GCC.

### Qt6 not found by CMake
→ Ensure `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`

### `chimera-ui.exe` fails with missing `Qt6*.dll`
→ Rebuild `Release`; `src/host/ui/CMakeLists.txt` now runs `windeployqt` post-build and deploys Qt runtime into `build/Release/`

### Tests fail with `0xC0000135`
→ Add Qt bin to PATH: `$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"`

### MOC compile errors with `signals:`
→ Do NOT define `QT_NO_KEYWORDS`. Use `Q_SIGNALS` if you must, but prefer standard macros.

### Multiple `main()` conflicts in tests
→ Split into separate `add_executable()` targets (see `tests/unit/CMakeLists.txt`)

### Emulator fails to start ("Running multiple emulators with the same AVD")
→ Kill all emulator processes: `taskkill /F /IM qemu-system-x86_64.exe; taskkill /F /IM emulator.exe`
→ Remove lock files: `Remove-Item ~/.android/avd/*.avd/*.lock`

### QMP connection refused (port 5554)
→ Check that `-ports qmpPort,adbPort` maps correctly (console=5554, ADB=5555)
→ Verify no other emulator is using the port

### ANGLE DLLs not found at runtime
→ `libEGL.dll` and `libGLESv2.dll` must be in the same directory as `chimera-ui.exe`
→ CMake post-build step auto-copies them to `build/Release/`

### ADB screencap returns empty data
→ Ensure emulator has finished booting (`adb shell getprop sys.boot_completed` returns `1`)
→ Try raw format (`screencap` without `-p`) instead of PNG

---

*Keep this file updated. Agents depend on it.*


<claude-mem-context>
# Memory Context

# [chimera] recent context, 2026-05-15 2:36am GMT+8

Legend: 🎯session 🔴bugfix 🟣feature 🔄refactor ✅change 🔵discovery ⚖️decision 🚨security_alert 🔐security_note
Format: ID TIME TYPE TITLE
Fetch details: get_observations([IDs]) | Search: mem-search skill

Stats: 50 obs (21,183t read) | 414,116t work | 95% savings

### May 14, 2026
946 10:48a 🔵 Build Passes With One AutoMoc Warning; All 3 Tests Green
947 " 🔵 README Feature Table Overstates Completeness of Several Features
948 " ⚖️ Code Review Work Plan Established: Fix Then Document
949 " 🟣 Framebuffer Deadlock Regression Test Added
950 10:49a 🔵 Framebuffer Deadlock Test Confirms Issue Reproducible — Exit Code 1
951 11:00a ⚖️ Root Directory File Organization Policy Established
952 11:01a 🔵 Chimera Project Root Directory Inventory
953 11:02a ✅ Chimera Docs Folder Reorganized — Root Cleaned to 3 Markdown Files
954 " ✅ All Stale Doc Links Updated and docs/README.md Index Created
955 11:03a ✅ Chimera Docs Reorganization Complete — Final State Verified
956 " 🔵 Markdown Link Validation Script Times Out on Windows
957 " 🔵 Markdown Link Validation Passed and All 4 Tests Green Post-Reorganization
967 7:14p 🔵 All 4 Unit Tests Passing in Chimera Project
968 " 🔵 Chimera Release Build Artifacts Confirmed
969 " 🔵 Chimera CMake Project Structure Mapped
981 9:25p 🔵 Android Emulator gRPC Probe Fails Due to Zombie Process Holding AVD Lock
982 9:26p 🔴 Zombie Emulator Processes Killed and chimera_dev AVD Locks Cleared
983 " 🔵 gRPC Connection Now Succeeds But Android Guest Not Yet Booted After 45s
984 9:29p 🔵 gRPC getScreenshot Fails: 720x1560 RGBA8888 Frame Exceeds Default 4MB Message Limit
985 9:30p 🔵 Chimera Graphics Subsystem Architecture: FramebufferCapture Abstraction with ADB and VNC Backends
986 " 🔵 Chimera main.cpp: VNC is Primary Capture Path, ADB is Fallback; No gRPC Integration Yet
987 9:31p 🔵 Chimera Instance Management Architecture: InstanceManager Singleton with JSON Persistence and Hyper-V Support
988 " 🟣 New GrpcFramebufferCapture C++ Backend Added to chimera-graphics
989 9:32p 🟣 GrpcFramebufferCapture.cpp Implementation: Hand-Rolled Protobuf Over Qt HTTP/2 gRPC Streaming
990 9:33p ✅ GrpcFramebufferCapture Integrated into chimera-graphics Build; VNC Disabled in VirtualMachineConfig
991 " ✅ VirtualMachine.cpp: gRPC Args Added, VNC/virtio-snd Gated, Early-Exit Check and Logging Wired
992 9:34p 🟣 main.cpp: VNC Replaced by GrpcFramebufferCapture as Primary Display Backend; RAM Lowered to 2048MB
993 " ✅ Full Release Build Succeeds After gRPC Capture Integration
994 9:35p ✅ All 6 CTest Unit Tests Pass After gRPC Integration
995 " ✅ chimera-ui.exe Smoke Test Passes: Stays Alive 6s in --no-emulator Mode
996 9:38p 🟣 End-to-End gRPC Display Capture Working in Production: 227 Frames Received at 7-13 FPS Post-Boot
997 9:41p 🟣 gRPC Capture Sustains 15-21 FPS Under Active Touch Input via ADB Swipe Stress Test
998 9:42p 🔵 gRPC MMAP Transport Confirmed Working: Pixel Data Written to Shared File, Not gRPC Message
999 " 🔴 PerformanceMonitor First-Frame Boot Spike Fixed with m_hasLastFrame Guard
1000 9:43p 🔵 README.md Architecture Section Lists VncFramebufferCapture — Now Stale After gRPC Migration
### May 15, 2026
1001 2:03a 🔵 Android Emulator / Screen Mirroring App — Three Critical Issues Identified
1002 2:06a 🔵 Chimera gRPC Screenshot Capture Fails: Emulator Exits During Capture
1003 " 🔵 InstanceManager.createInstance Does Not Pass -read-only Flag to Emulator
1004 " 🔵 Stale Lock File Found in chimera_pie64.avd Causing False Multi-Instance FATAL
1005 2:07a 🔴 Stale AVD Lock Cleanup Added to VirtualMachine Startup Sequence
1006 " ✅ Stale AVD Lock Fix Builds Successfully with MSVC 2022
1007 " ✅ All 6 CTest Suites Pass After AVD Lock Fix
1008 2:10a 🔵 Second gRPC Screenshot Capture Run Produces No Stdout Output
1009 " 🟣 GrpcFramebufferCapture switched from RGBA8888 to RGB888 format
1010 " ✅ Full Release build and all 6 unit tests pass after RGB888 change
1011 2:12a 🔵 Live smoke test: gRPC RGB888 stream starts but frame rate degrades to near-zero
1012 " 🔵 Stale AVD lock files and a background qemu headless process explain the duplicate-AVD FATAL warning
1013 2:13a 🔴 ProcessLauncher::terminate() now kills the full child-process tree before killing the root
1014 2:15a 🔵 Second smoke test confirms end-to-end system health: stale locks cleared, cold boot in 38.5s, gRPC reached 32 FPS
1015 2:16a ✅ Large pending commit: docs reorganized into subdirectories, GrpcFramebufferCapture added, many source files modified

Access 414k tokens of past work via get_observations([IDs]) or mem-search skill.
</claude-mem-context>
