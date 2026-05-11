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
├── src/host/graphics/    # Framebuffer capture (ADB/VNC), renderer, ANGLE backend, performance monitor
├── src/host/audio/       # WASAPI audio bridge
├── src/host/storage/     # Shared folders (9pfs)
├── src/host/instance/    # VM lifecycle, process launcher, device spoofer, memory trimmer, disk compactor, Hyper-V manager
├── src/host/integration/ # Windows notifications, clipboard, GPS
├── src/common/utils/     # Logger, thread pool, file utils
├── tests/unit/           # Qt Test executables (separate .exe per module)
├── scripts/              # Python automation scripts
├── configs/              # Default settings JSON
├── docs/                 # Architecture docs, reference materials
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

## Completed Features (Phase 1-5)

### Core (Phase 1 MVP)
- ✅ Android Emulator (`emulator.exe`) boots via WHPX
- ✅ ADB screencap (raw format, ~20 FPS)
- ✅ Qt 6 QML guest display with aspect-ratio preservation
- ✅ Keyboard/mouse input forwarding via ADB

### Gaming (Phase 2)
- ✅ XInput gamepad support (60 Hz polling, 14-button mapping)
- ✅ Instance persistence (JSON save/load, clone with data dir copy)
- ✅ Screenshot (timestamped PNG, toolbar + Ctrl+Shift+S)
- ✅ Input mapper overlay (JSON scheme load/save)
- ✅ Multi-instance manager (QML dialog + QObject wrapper)
- ✅ Macro engine (background thread, loop support)

### Performance (Phase 3)
- ✅ WASAPI audio bridge (shared-mode, render thread)
- ✅ Screen recorder (FFmpeg H.264 pipe + PNG sequence fallback)
- ✅ ANGLE headers integration (auto-download script, CMake detection)
- ✅ ANGLE libraries copied from Chrome (libEGL.dll + libGLESv2.dll)
- ✅ Dynamic EGL loader (QLibrary, no .lib import library needed)

### Virtualization (Phase 4)
- ✅ Device spoofing (5 flagship profiles, build.prop modification)
- ✅ Raw ADB screencap (20 FPS, no PNG encoding overhead)
- ✅ QMP input (console port IS QMP, runtime verified)
- ✅ Memory trimmer (polls /proc/meminfo, auto-trims on critical pressure)
- ✅ Disk compactor (removes cache/logs/tmp, optional zero-fill)
- ✅ Framebuffer capture abstraction (AdbFramebufferCapture + VncFramebufferCapture)
- ✅ VirtIO Audio (`-device virtio-snd-pci`, emulator accepts)
- ✅ FFmpeg bundle (fetch-ffmpeg.py + CMake auto-copy)

### Advanced (Phase 5)
- ✅ VirtIO Input framework (QEMU arg generation, prebuilt rejects — needs custom QEMU)
- ✅ Hyper-V HCS API framework (computecore.dll dynamic loading, GPU-PV detection)
- ✅ Performance monitor (FPS, frame time, drops, QML UI counter)
- ✅ QMP latency measurement (round-trip timer per command)
- ✅ QMP input integration (preferred over ADB for keyboard/mouse/gamepad)
- ✅ QMP auto-reconnect (5-second retry loop)

## Critical Decisions (Do Not Change Without Discussion)

1. **MSVC only**: MSYS2 GCC is broken on this machine (`cc1plus.exe` crashes). Do NOT try to reintroduce MinGW builds.
2. **Android Emulator binary**: We use the prebuilt `emulator.exe` from Android SDK (QEMU+WHPX) rather than compiling QEMU from source for Phase 1-4.
3. **ANGLE dynamic loading**: `libEGL.dll` + `libGLESv2.dll` loaded at runtime via QLibrary. No import library (.lib) required.
4. **AVD `google_apis`**: Avoid `google_apis_playstore` images — they cause ADB RSA authorization issues in automation.
5. **QMP over ADB**: InputBridge prefers QMP (console port 5554) for all input events. Falls back to ADB if QMP unavailable.
6. **Reference files**: Reverse engineering artifacts (bluestacks.conf, virtualization analysis) stored in `docs/references/`. Do NOT commit BlueStacks binaries.

## Git Workflow

- **Format**: `<type>: <description>`
- **Types**: feat, fix, refactor, docs, test, chore, perf, ci
- **Do NOT commit without explicit user request**
- **Never push to remote without explicit confirmation**

## Files That Must Stay Updated

| File | When to Update |
|------|---------------|
| `STATUS.md` | After any major milestone or blocker resolution |
| `BUILD.md` | When build steps, dependencies, or tools change |
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
