# Project Chimera — CLAUDE.md

> Living document for Claude / OpenCode sessions. Update after every significant change.

## Current State

**Phase**: BlueStacks Parity Roadmap v3 P0–P4e COMPLETE (2026-05-18)  
**Engine decision**: `emulator.exe` (QEMU+WHPX, Google's own fork) is the production engine.
`--qemu-backend` (stock QEMU 11 + Cuttlefish) and `--hcs-backend` (Hyper-V HCS) are **legacy R&D** — kept, not deleted, not the focus.  
**Next**: Phase 8 — gfxstream-capable QEMU or virgl+angle → SurfaceFlinger stable → boot_completed=1 → ADB TCP.

## What's Working

- ✅ Android Emulator (`emulator.exe`) boots Android 34 x86_64 via WHPX; AVD `chimera_dev` verified
- ✅ Full C++ skeleton compiles under MSVC + Qt 6.8.3; 7/7 unit tests passing
- ✅ Native emulator window embedding (primary display); gRPC/ADB screencap fallback (`--stream-capture`)
- ✅ GuestDisplay renders QImage with aspect-ratio preservation in Qt Quick
- ✅ InputBridge: QMP first → ADB fallback; Qt→Android keycode mapping (60+ keys)
- ✅ End-to-end verified: chimera-ui.exe → emulator → native Win32 window → Android 1280×720/60Hz

### Phases 2–4

- ✅ Gamepad (XInput 60Hz), Instance persistence (JSON), Screenshot (timestamped PNG + Ctrl+Shift+S)
- ✅ InputMapper (right-side panel + overlay), Multi-instance manager, Macro engine (background thread)
- ✅ Audio bridge (WASAPI), Screen recorder (FFmpeg H.264 + PNG fallback), ANGLE headers/DLLs/EglLoader
- ✅ Device spoofing (5 flagship profiles via build.prop), Raw ADB screencap (20 FPS), QMP input (<5ms)
- ✅ MemoryTrimmer (polls /proc/meminfo), DiskCompactor, VirtIO Audio, FFmpeg bundle
- ✅ Framebuffer capture abstraction: GrpcFramebufferCapture + AdbFramebufferCapture + VncFramebufferCapture
- ✅ QMP: Nagle disabled (LowDelayOption), KeepAlive, mouse-move dedup, auto-reconnect (5s retry)
- ✅ Modernized QML shell: D3D11 RHI, high-priority process, micro-animations, Traditional Chinese UI

### BlueStacks Parity Roadmap v3 (P0–P4e) ✅ Complete

- ✅ **P0 — AndroidConsoleInput**: telnet Android Console (port 5554) state machine; mouse + keyboard injection via `event` commands; exponential-backoff reconnect; console command probe for keyboard fallback
- ✅ **P1b — InstanceRuntimeConfig**: per-instance `{consolePort, adbPort, grpcPort, adbSerial}` model; index-based port assignment (instance N → 5554+2N); no hardcoded `emulator-5554` in production paths
- ✅ **P1a — CoordinateMapper + InputBridge pipeline**: Host→Normalized→Guest→Backend layering; handles rotation, letterbox, DPI, window scale; `InputBridge` priority chain HvSocket → Console → QMP → ADB
- ✅ **P2 — ProcessLauncher rewrite**: `CreateProcessW` + `quoteArg` (CommandLineToArgvW rules); concurrent stdout/stderr drain threads; `CHIMERA_PROCESS_LAUNCHER=legacy|native|auto` rollback flag; 15 unit tests
- ✅ **P3a — LocationSimulator `geo fix`**: throttled 1Hz/1e-6° sink; `lon lat alt` order; wired to AndroidConsoleInput
- ✅ **P3b — ClipboardBridge Unicode**: `CF_UNICODETEXT` for host↔guest; `syncHostToGuest` via `clipboard set`; CJK + emoji round-trip
- ✅ **P3c — SharedFolder ADR**: chose ADB push/pull to `/sdcard/Download/` as v1; virtiofs/MTP/content-provider options documented in `docs/adr/ADR-001-shared-folder.md`
- ✅ **P4a — Stub cleanup**: removed `GraphicsBridge`, `Renderer`, `WindowsNotifier`; fixed `Framebuffer::readFrontBuffer()` race (now returns `Buffer` by value under lock)
- ✅ **P4b — Integration test suite**: `tests/integration/` with emulator-boot / input-inject / screencap tests; QSKIP guards on missing env vars; labelled `-LE integration` to skip in CI
- ✅ **P4c — Multi-instance grid UI**: `QmlInstanceManager` batch start/stop, grid layout, sort-by-name, per-instance port model
- ✅ **P4d — PerformanceMonitor visible latency**: `onInputEvent/onFrameRendered` measure input→pixel latency; per-stage capture/decode/render timers; `targetHitRate` (on-time frames within 1.5× interval)
- ✅ **P4e — Documentation**: this update

### Phases 5–7

- ✅ **HCS + HvSocket**: VM lifecycle → serial pipe (CLIENT CreateFile) → AF_HYPERV port 16+17 → 26–27 FPS, 0 dropped
- ✅ **Real framebuffer**: VideoMonitor 1280×720 → hyperv_drm.ko → /dev/fb0 → BGRA→RGB24 relay at 30 FPS
- ✅ **uinput**: chimera-input-relay creates `Chimera HvSocket Input` via /dev/uinput
- ✅ **WSL2 6.6 kernel**: dxgkrnl + hv_sock=m + hyperv_drm=m; CONFIG_DMABUF_HEAPS=y patched in
- ✅ **AOSP Cuttlefish VHDXs**: system/vendor/userdata/metadata; Android init → APEX → servicemanager
- ✅ **Phase 6c**: virtio-gpu-pci + DMABUF_HEAPS → gralloc.ranchu.so init → SurfaceFlinger stable 5/5 PASS
- ✅ **Phase 7**: --cuttlefish reads configs/cuttlefish.json; VncFramebufferCapture + QmpInput wired; SMPTE bars at 5 FPS, 0 dropped
- ❌ ADB TCP: blocked — needs gfxstream cap set 3 for SurfaceFlinger stable (goldfish-opengl vendor + virgl only cap sets 1,2)

### Key Runtime Notes

- **VNC resize loop**: only set `m_resizedThisUpdate=true` when dimensions actually change — QEMU includes ExtendedDesktopSize in every FBU response
- **dxgkrnl EBADF**: HCS LinuxKernelDirect VMs do NOT receive VMBus GPU channel offer (only WSL2 via LxssManager and Gen2 UEFI VMs)
- **CONFIG_DMABUF_HEAPS=y**: gralloc.ranchu.so needs /dev/dma_heap/system; build with `make bzImage` (skips rbd.ko BTF error)
- **ADB targeting**: use emulator serial (`-s emulator-<consolePort>`), NOT `adb -P`
- **Azure 6.11 modules**: decompress .ko.zst → plain .ko before insmod; `sockaddr_vm.svm_flags` must be zero
- **emulator.exe**: requires `-crash-report-mode never -no-metrics` to avoid consent dialog stall at boot

## Build Verification

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
│  ├─ Audio: WASAPI bridge                    │
│  ├─ Instance: VM lifecycle, CreateProcess   │
│  └─ Config: JSON-based settings             │
├─────────────────────────────────────────────┤
│  Virtualization Layer (QEMU + WHPX / HCS)   │
│  ├─ Android Emulator (prebuilt)             │
│  ├─ HyperVManager (HCS API)                 │
│  └─ QemuBackend (stock QEMU 11)             │
├─────────────────────────────────────────────┤
│  Android Guest (AOSP x86_64)                │
│  ├─ virtio-gpu → DRM KMS → SurfaceFlinger   │
│  ├─ ANGLE Guest Driver (Phase 8)            │
│  └─ libndk_translation (future)             │
└─────────────────────────────────────────────┘
```

### Module Boundaries

| Module | Responsibility | Key Files |
|--------|---------------|-----------|
| `chimera-config` | Flat JSON key-value config | `ConfigManager.h/cpp` |
| `chimera-input` | Qt events → QMP/ADB; XInput gamepad; macro engine | `InputBridge.h/cpp`, `InputMapper.h/cpp`, `GamepadManager.h/cpp`, `MacroEngine.h/cpp` |
| `chimera-graphics` | Frame capture abstraction; ANGLE EGL; perf monitor | `Framebuffer.h/cpp`, `GrpcFramebufferCapture`, `VncFramebufferCapture`, `AngleBackend.h/cpp` |
| `chimera-audio` | WASAPI shared-mode output | `AudioBridge.h/cpp` |
| `chimera-instance` | VM CRUD, lifecycle, CreateProcess, device spoofer, memory trim, disk compactor, Hyper-V manager | `InstanceManager.h/cpp`, `VirtualMachine.h/cpp`, `HyperVManager.h/cpp` |
| `chimera-integration` | Clipboard (CF_UNICODETEXT), GPS/geo fix, (WindowsNotifier removed P4a) | `ClipboardBridge.h/cpp`, `LocationSimulator.h/cpp` |
| `chimera-utils` | Logging, threading, file I/O | `Logger.h/cpp`, `ThreadPool.h/cpp`, `FileUtils.h/cpp` |
| `chimera-ui` | Qt Quick executable; instance launch; display wiring | `main.cpp`, `GuestDisplay.h/cpp`, `ChimeraWindow.h/cpp` |

## Key Decisions

1. **MSVC only**: MSYS2 GCC broken on this machine (`cc1plus.exe` crashes). Do NOT try MinGW.
2. **Android Emulator binary**: Prebuilt `emulator.exe` (QEMU+WHPX) for Phase 1-4; stock QEMU 11 for Cuttlefish path.
3. **ANGLE dynamic loading**: `libEGL.dll` + `libGLESv2.dll` via QLibrary at runtime; no import .lib needed.
4. **AVD `google_apis`**: Avoids ADB RSA auth lock in automation mode.
5. **QMP over ADB**: InputBridge prefers QMP for all input; ADB is fallback only.
6. **Native window over screenshot streaming**: NativeEmulatorView embeds emulator Win32 window; gRPC/ADB are debug paths behind `--stream-capture`.
7. **Qt 6.8.3 MSVC**: Installed via aqtinstall at `C:\Qt\6.8.3\msvc2022_64`.

## Known Issues

| Issue | Status | Workaround |
|-------|--------|------------|
| MSYS2 GCC broken | WONTFIX | MSVC exclusively |
| gRPC stream peaks ~32 FPS at 720p/RGB888 | ACCEPTED | Native window embedding is default |
| Native child window overlays QML inside viewport | ACCEPTED | Keep controls outside native child window |
| SurfaceFlinger crash-loop (--cuttlefish) | OPEN | Phase 8: gfxstream or SwiftShader APEX fallback |
| ADB TCP blocked (boot_completed=1 not reached) | OPEN | Unblocked by Phase 8 |
| Framebuffer read-side synchronization | FIXED (P4a) | readFrontBuffer() now returns Buffer by value under lock |
| ProcessLauncher quoting | FIXED (P2) | CreateProcessW + quoteArg; CHIMERA_PROCESS_LAUNCHER=legacy rollback |
| Clipboard Unicode | FIXED (P3b) | CF_UNICODETEXT; CJK + emoji round-trip via console clipboard set |

## File Locations

- **Project root**: `D:\Workspace_cloud\Personal_Project\chimera\`
- **Build output**: `D:\Workspace_cloud\Personal_Project\chimera\build\Release\`
- **Qt install**: `C:\Qt\6.8.3\msvc2022_64\`
- **Android SDK**: `D:\Workspace_cloud\Personal_Project\chimera\third_party\android-sdk\`
- **AVD data**: `D:\Workspace_cloud\Personal_Project\chimera\third_party\android-avd\chimera_dev.avd\`
- **Instance configs**: `D:\Workspace_cloud\Personal_Project\chimera\configs\instances.json`
- **ANGLE headers**: `D:\Workspace_cloud\Personal_Project\chimera\third_party\angle\`

## Phase Checklists

### Phase 1–4 ✅ Complete

All items verified — see git log for details.

### Phase 5 Checklist (Hyper-V Native Stack) ✅ Complete

- [x] HCS VM lifecycle + serial console pipe (CLIENT CreateFile)
- [x] AF_HYPERV dual-channel: port 16 input + port 17 display
- [x] Guest daemons: display (640×480 RGB888 test) + input (linux_input_event)
- [x] HvSocketFramebufferCapture + real /dev/fb0 via hyperv_drm + uinput relay
- [x] WSL2 6.6 kernel: dxgkrnl + hv_sock=m + hyperv_drm=m + DMABUF_HEAPS
- [x] AOSP Cuttlefish VHDXs + Android initrd with switch_root
- [x] HCS Cuttlefish boot: 6/6 checks (fb0, dxg, input, display, system, switch_root)
- [x] Phase 6a: fb-render SMPTE bars → fb0 → vsock → host renders (7/7 checks)
- [x] Phase 6b: Android init → APEX → servicemanager → SurfaceFlinger starts
- [x] Phase 6c: virtio-gpu + DMABUF_HEAPS → SurfaceFlinger stable 5/5 PASS
- [x] Phase 7: chimera-ui.exe --cuttlefish VNC+QMP end-to-end verified

### Phase 7 Checklist (chimera-ui Cuttlefish Integration) ✅ Complete

- [x] `--cuttlefish` mode: reads configs/cuttlefish.json, starts QEMU, wires VncFramebufferCapture + QmpInput
- [x] VncFramebufferCapture infinite-resize-loop bug fixed
- [x] Serial console log: QemuInstanceConfig.serialLog → qemu-cuttlefish-serial.log
- [x] fb-render at t≈1.5s → VNC framebuffer → GuestDisplay renders; ~5 FPS, 0 dropped
- [x] QMP port 4445 connected, input-send-event wired
- [ ] ADB TCP — BLOCKED: needs SurfaceFlinger stable (gfxstream)
- [ ] Phase 8: gfxstream-capable QEMU OR SwiftShader APEX → SF stable → ADB → full Android UI

## Feature Flags

| Variable | Values | Default | Purpose |
|----------|--------|---------|---------|
| `CHIMERA_INPUT_BACKEND` | `console`, `adb`, `qmp`, `auto` | `auto` | `auto` = try Console, fall back to ADB if not Ready |
| `CHIMERA_PROCESS_LAUNCHER` | `legacy`, `native`, `auto` | `auto` | `auto`/`native` = CreateProcessW; `legacy` = _popen (keep selectable until ≥1 stable release on native) |

## Reference: BlueStacks Architecture

- **ANGLE**: OpenGL ES → D3D11 HLSL; dual mode (GPU direct 240 FPS vs software fallback)
- **Input path**: BlueStacks does NOT use a kernel-mode driver. Input is routed via `HD-Bridge-Native.dll` → virtio-input to the guest. `BstkDrv.sys` is a network/filter driver, not an input driver. Chimera's equivalent: Android Console `event` protocol on port 5554 (bypasses ADB's ~100ms shell-spawn overhead).
- **Device Spoofing**: build.prop → unlock high FPS/quality settings locked behind hardware detection
- **Memory**: trim on pressure, low-memory mode, VDI/VHDX compaction (zero-fill + Optimize-VHD)

## Reference Documents

| Document | Purpose |
|----------|---------|
| `docs/project/PLAN.md` | 4-phase implementation plan |
| `docs/architecture/ARCHITECTURE.md` | Module responsibilities and tech choices |
| `docs/project/BUILD.md` | Step-by-step build instructions |
| `docs/project/STATUS.md` | Current phase status and verification log |
| `AGENTS.md` | Agent workflow, coding standards, safety checklist |
| `docs/references/windows_android_virtualization_analysis.md` | Virtualization feasibility analysis |
| `docs/references/bluestacks.conf` | BlueStacks config format reference |

**Do NOT commit** BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/).

---

*Updated: 2026-05-18 — BlueStacks Parity Roadmap v3 P0–P4e COMPLETE*
