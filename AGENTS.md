# Project Chimera — AGENTS.md

> Agent-focused project guide. Read this before making any changes.

## Project Overview

**Project Chimera** is an open-source Windows Android emulator targeting mobile gamers. Built from scratch using open-source components only — no cloud dependency, no ads, no telemetry.

- **License**: Apache 2.0 (host core) + GPL v2 (QEMU layer, IPC-isolated)
- **Platform**: Windows 10/11 Pro/Enterprise with Hyper-V / WHPX
- **UI**: Qt 6.8+ (QML)
- **Virtualization**: QEMU + WHPX (Windows Hypervisor Platform) / HCS (Hyper-V Compute Service)
- **Graphics**: ANGLE (OpenGL ES → D3D11/Vulkan)
- **Android**: AOSP x86_64 Cuttlefish images with libndk_translation for ARM compat

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
├── src/host/graphics/    # Framebuffer capture (gRPC/ADB/VNC), renderer, ANGLE, perf monitor
├── src/host/audio/       # WASAPI audio bridge
├── src/host/storage/     # Shared folders (9pfs)
├── src/host/instance/    # VM lifecycle, process launcher, device spoofer, memory trimmer,
│                         #   disk compactor, Hyper-V manager (HCS + HvSocket)
├── src/host/integration/ # Windows notifications, clipboard, GPS
├── src/common/utils/     # Logger, thread pool, file utils
├── tests/unit/           # Qt Test executables (separate .exe per module)
├── scripts/              # Python/bash automation (build kernels, VHDXs, initrds, tests)
├── configs/              # Default settings JSON (hcs.json, cuttlefish.json, instances.json)
├── docs/                 # Architecture, process, project, references
└── third_party/          # nlohmann/json, ANGLE headers/DLLs, Android SDK, FFmpeg
```

## Coding Standards

- **Immutable data preferred**: create new objects, never mutate in-place
- **Functions < 50 lines**, files < 800 lines, max 4 nesting levels
- **Handle errors at every layer**, never swallow exceptions silently
- **Validate all input at system boundaries** (user input, API responses, file contents)
- **Use RAII** for resource management
- **Forward-declare** in headers when possible to reduce include cycles

## Qt / MOC Specific Rules

- **Do NOT use `QT_NO_KEYWORDS`**: breaks `signals:` / `slots:` macros with MSVC MOC
- **QObject subclasses must use Q_OBJECT macro**
- **Prefer QQuickPaintedItem over QQuickItem** when you need custom paint()
- **Separate test executables**: each QTest module must be its own `.exe`

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
- Current unit tests: config manager, input mapper, instance manager, graphics framebuffer, ADB raw framebuffer, QMP input, process launcher (15 cases)
- Integration tests in `tests/integration/` labelled `integration`; skip in CI with `-LE integration`; need env vars `CHIMERA_ADB_PATH`, `CHIMERA_EMULATOR_PATH`, `CHIMERA_AVD_NAME`

## Completed Features

### Core (Phase 1 MVP)
- ✅ Android Emulator (`emulator.exe`) boots via WHPX
- ✅ Native emulator window embedding (primary) + gRPC/ADB raw fallback (`--stream-capture`)
- ✅ Qt 6 QML guest display with aspect-ratio preservation
- ✅ Keyboard/mouse input forwarding via QMP (ADB fallback)

### Gaming (Phase 2)
- ✅ XInput gamepad (60Hz polling, 14-button mapping)
- ✅ Instance persistence (JSON save/load, clone with data dir copy)
- ✅ Screenshot (timestamped PNG, toolbar + Ctrl+Shift+S)
- ✅ Input mapper side panel + overlay (JSON scheme load/save)
- ✅ Multi-instance manager (right-side panel + QObject wrapper)
- ✅ Macro engine (background thread, loop support)

### Performance (Phase 3)
- ✅ WASAPI audio bridge (shared-mode, render thread)
- ✅ Screen recorder (native child-window capture + FFmpeg H.264 + PNG fallback)
- ✅ ANGLE headers integration + libEGL.dll + libGLESv2.dll (Chrome 147)
- ✅ Dynamic EGL loader (QLibrary, no .lib needed)

### Virtualization (Phase 4)
- ✅ Device spoofing (5 flagship profiles via build.prop)
- ✅ Raw ADB screencap (20 FPS, no PNG overhead)
- ✅ QMP input (console port IS QMP; Nagle disabled; mouse-move dedup; auto-reconnect 5s)
- ✅ MemoryTrimmer (polls /proc/meminfo, auto-trims on critical pressure)
- ✅ DiskCompactor (removes cache/logs/tmp, optional zero-fill)
- ✅ Framebuffer capture abstraction: Grpc + Adb + Vnc backends
- ✅ VirtIO Audio (`-device virtio-snd-pci`), FFmpeg bundle

### Advanced (Phase 5–7)
- ✅ **HCS VM lifecycle**: computecore.dll dynamic load, createVm/startVm/stopVm/terminateVm async with HcsWaitForOperationResult
- ✅ **Serial console pipe**: HCS is SERVER; host opens as CLIENT with CreateFile + retry
- ✅ **HvSocket**: AF_HYPERV port 16 (input) + port 17 (display) — 26–27 FPS, 0 dropped
- ✅ **Real framebuffer**: VideoMonitor 1280×720 → hyperv_drm.ko → /dev/fb0 → BGRA→RGB24 relay ~30 FPS
- ✅ **uinput relay**: creates `Chimera HvSocket Input` virtual device via /dev/uinput
- ✅ **WSL2 6.6 kernel**: dxgkrnl + hv_sock=m + hyperv_drm=m + CONFIG_DMABUF_HEAPS=y
- ✅ **AOSP Cuttlefish VHDXs**: system/vendor/userdata/metadata; Android init → APEX → servicemanager
- ✅ **Phase 6c**: QEMU virtio-gpu-pci + DMABUF_HEAPS → gralloc.ranchu.so init → SurfaceFlinger stable
- ✅ **Phase 7**: chimera-ui.exe --cuttlefish: VncFramebufferCapture + QmpInput wired; SMPTE bars at 5 FPS, 0 dropped
- ✅ Performance monitor (FPS, frame time, drops; 1s signal throttle; visible latency input→pixel; per-stage capture/decode/render; targetHitRate)
- ✅ Modernized QML shell: D3D11 RHI, high-priority process, micro-animations, Traditional Chinese UI

### BlueStacks Parity Roadmap v3 (P0–P4e)
- ✅ **P0 AndroidConsoleInput**: telnet port 5554 state machine; event mouse/keydown/keyup; exponential-backoff reconnect; command probe
- ✅ **P1b InstanceRuntimeConfig**: per-instance consolePort/adbPort/grpcPort/adbSerial; index-based port allocation
- ✅ **P1a CoordinateMapper + InputBridge pipeline**: Host→Normalized→Guest→Backend; handles rotation/letterbox/DPI/scale
- ✅ **P2 ProcessLauncher**: CreateProcessW + quoteArg (CommandLineToArgvW rules); concurrent stdout/stderr; `CHIMERA_PROCESS_LAUNCHER` flag; 15 unit tests
- ✅ **P3a LocationSimulator**: geo fix via AndroidConsoleInput; throttled 1Hz/1e-6°
- ✅ **P3b ClipboardBridge**: CF_UNICODETEXT; syncHostToGuest via `clipboard set`; CJK+emoji
- ✅ **P3c SharedFolder ADR**: ADB push/pull to /sdcard/Download/ as v1; ADR at docs/adr/ADR-001-shared-folder.md
- ✅ **P4a Stub cleanup**: removed GraphicsBridge, Renderer, WindowsNotifier; Framebuffer race fixed
- ✅ **P4b Integration tests**: tests/integration/ with QSKIP guards; labelled integration
- ✅ **P4c Multi-instance grid UI**: QmlInstanceManager batch ops, grid layout, sort-by-name
- ✅ **P4d PerformanceMonitor visible latency**: input→pixel; per-stage timers; targetHitRate
- ✅ **P4e Docs updated**: CLAUDE.md + AGENTS.md

## Current Status (2026-05-18)

- **BlueStacks Parity Roadmap v3 P0–P4e COMPLETE**
- **Engine**: `emulator.exe` (QEMU+WHPX) is the production engine. `--qemu-backend` (stock QEMU 11 + Cuttlefish) and `--hcs-backend` (Hyper-V HCS) are legacy R&D modes.
- **Input path**: BlueStacks does NOT use `BstkDrv.sys` as a kernel input driver. It routes input via `HD-Bridge-Native.dll` → virtio-input. Chimera uses Android Console `event` protocol on port 5554 for <10ms injection latency.
- **Feature flags**: `CHIMERA_INPUT_BACKEND=console|adb|qmp|auto` / `CHIMERA_PROCESS_LAUNCHER=legacy|native|auto`
- **7/7 unit tests passing**; Release build clean; 3 integration tests (need emulator running)
- **Next: Phase 8** — gfxstream-capable QEMU or SwiftShader APEX fallback → SF stable → boot_completed=1 → ADB TCP
- **Blocked**: ADB TCP requires SurfaceFlinger stable; goldfish-opengl vendor needs gfxstream cap set 3; stock QEMU 11 only provides virgl (cap sets 1, 2)

## Critical Decisions (Do Not Change Without Discussion)

1. **MSVC only**: MSYS2 GCC broken on this machine (`cc1plus.exe` crashes). Do NOT try MinGW.
2. **Engine decision**: `emulator.exe` (QEMU+WHPX, Google's fork) is production. `--qemu-backend` / `--hcs-backend` are legacy R&D — kept but not the development focus.
3. **ANGLE dynamic loading**: `libEGL.dll` + `libGLESv2.dll` via QLibrary. No import .lib required.
4. **AVD `google_apis`**: Avoid `google_apis_playstore` — ADB RSA auth issues in automation.
5. **QMP over ADB**: InputBridge prefers QMP for all input. ADB is fallback only.
6. **Native window over screenshot streaming**: NativeEmulatorView is default; gRPC/ADB are `--stream-capture` debug paths.
7. **Reference files**: Reverse engineering artifacts in `docs/references/`. Do NOT commit BlueStacks binaries.

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
- [ ] Error messages do not leak sensitive data
- [ ] Tests pass locally

## Troubleshooting

### `cc1plus.exe` crash / GCC issues
→ Use MSVC. Do NOT attempt to fix MSYS2 GCC.

### Qt6 not found by CMake
→ Ensure `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`

### `chimera-ui.exe` fails with missing `Qt6*.dll`
→ Rebuild `Release`; `src/host/ui/CMakeLists.txt` runs `windeployqt` post-build automatically.

### Tests fail with `0xC0000135`
→ Add Qt bin to PATH: `$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"`

### MOC compile errors with `signals:`
→ Do NOT define `QT_NO_KEYWORDS`.

### Multiple `main()` conflicts in tests
→ Split into separate `add_executable()` targets.

### Emulator fails to start ("Running multiple emulators with the same AVD")
→ `taskkill /F /IM qemu-system-x86_64.exe; taskkill /F /IM emulator.exe`
→ `Remove-Item ~/.android/avd/*.avd/*.lock`

### QMP connection refused
→ Verify `-ports qmpPort,adbPort` maps correctly (console=5554, ADB=5555)

### ANGLE DLLs not found at runtime
→ CMake post-build auto-copies `libEGL.dll`/`libGLESv2.dll` to `build/Release/`

### VNC stuck in resize loop
→ Only set `m_resizedThisUpdate = true` when dimensions actually change (QEMU sends ExtendedDesktopSize in every FBU)

### ADB screencap returns empty data
→ Check `adb shell getprop sys.boot_completed` == 1; use raw format (no `-p`)

---

*Keep this file updated. Agents depend on it.*
