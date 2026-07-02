# Project Chimera — Architecture Overview

> **⚠ 部分內容為 2026-05 快照**：量測數字與「native embedding / 1280×720」等敘述已過時（現行：headless 強制、1920×1080 floor、custom gfxstream shared texture 顯示路徑）。現況以根目錄 `CLAUDE.md` 與 `docs/project/STATUS.md` 為準；模組分層與設計目標仍有效。

## 1. Design Goals

- **Open Source**: Apache 2.0 (host) + GPL v2 (QEMU virtualization layer via IPC separation)
- **Windows Native**: Optimized for Windows 10/11 Pro with WHPX / Hyper-V
- **Gaming First**: Low input latency, high FPS, keyboard mapping, gamepad support, macros
- **Zero Cloud Dependency**: No forced accounts, no telemetry, no ads
- **Modular**: Every subsystem can be replaced or mocked for testing

## 2. Layered Architecture

```
┌─────────────────────────────────────────────┐
│  UI Layer (Qt 6 / QML)                      │
│  ├─ ChimeraWindow (OpenGL framebuffer)      │
│  ├─ InputMapperOverlay (on-screen controls) │
│  └─ Multi-Instance Manager                  │
├─────────────────────────────────────────────┤
│  Host Service Layer (C++20)                 │
│  ├─ Input: Keyboard, Mouse, Gamepad, Macro  │
│  ├─ Graphics: ANGLE / OpenGL bridge         │
│  ├─ Audio: WASAPI bridge                    │
│  ├─ Storage: Shared folders (9pfs)          │
│  ├─ Instance: VM lifecycle manager          │
│  ├─ Config: JSON-based settings             │
│  └─ Integration: Notifications, Clipboard   │
├─────────────────────────────────────────────┤
│  Virtualization Layer (QEMU + WHPX)         │
│  ├─ QEMU system emulator (x86_64-softmmu)   │
│  ├─ WHPX acceleration module                │
│  └─ Custom VirtIO devices (GPU, input, snd) │
├─────────────────────────────────────────────┤
│  Android Guest (AOSP x86_64)                │
│  ├─ ANGLE Guest Driver (GLES → Host)        │
│  ├─ VirtIO GPU / Net / Snd HALs             │
│  ├─ libndk_translation (ARM → x86)          │
│  └─ Gamepad / InputMapper JNI layer         │
└─────────────────────────────────────────────┘
```

## 3. Module Responsibilities

### 3.1 Host UI (`src/host/ui/`)
- **ChimeraWindow**: Qt Quick window embedding the guest framebuffer as an OpenGL texture
- **InputMapperOverlay**: Visual editor for key → screen-position mappings
- **Resources**: QML files, icons, themes

### 3.2 Input System (`src/host/input/`)
- **InputBridge**: Normalizes Windows / Qt events into virtio-input protocol
- **InputMapper**: JSON-based per-app control schemes (tap, swipe, dpad, MOBA skill)
- **GamepadManager**: XInput polling, vibration, state callbacks
- **MacroEngine**: Record & playback input sequences with loop support

### 3.3 Graphics (`src/host/graphics/`)
- **GraphicsBridge**: Receives guest frames and forwards to UI
- **Framebuffer**: Double-buffered RGBA8 exchange (lock-free where possible)
- **Renderer**: Host OpenGL 4.1 quad renderer for UI display
- **AngleBackend**: ANGLE EGL context creation (D3D11 / Vulkan backend)
- **NativeEmulatorView**: embeds the Android Emulator Win32 window into the Qt/QML shell; primary current 60Hz display path
- **GrpcFramebufferCapture**: Android Emulator gRPC screenshot stream for `--stream-capture` fallback/debug mode
- **AdbFramebufferCapture**: raw ADB screencap fallback for compatibility

### 3.4 Instance Management (`src/host/instance/`)
- **InstanceManager**: CRUD for VM instances, persistent settings
- **VirtualMachine**: QEMU command-line builder, process lifecycle
- **ProcessLauncher**: Cross-platform async process spawning with pipe redirection

### 3.5 Audio (`src/host/audio/`)
- **AudioBridge**: WASAPI loopback / capture bridging to virtio-snd

### 3.6 Storage (`src/host/storage/`)
- **SharedFolder**: Maps host directories into guest via QEMU `-virtfs`

### 3.7 Integration (`src/host/integration/`)
- **WindowsNotifier**: Toast notifications from guest apps
- **ClipboardBridge**: Bidirectional text clipboard sync
- **LocationSimulator**: GPS spoofing with route playback

## 4. Communication Flows

### 4.1 Guest → Host Frame Delivery
```
Guest SurfaceFlinger → Android Emulator native GPU window → NativeEmulatorView → Qt/QML shell
Fallback/debug: Guest SurfaceFlinger → Android Emulator gRPC streamScreenshot → GrpcFramebufferCapture → GuestDisplay → Qt/QML
Compatibility fallback: Guest SurfaceFlinger → ADB exec-out screencap → AdbFramebufferCapture → GuestDisplay → Qt/QML
```

VNC is not a viable current primary path on Android Emulator 36.5.11: QEMU reports that VNC requires `-gpu guest`, while the emulator CLI does not support that GPU mode. Native window embedding is now the default practical 60Hz path; shared GPU texture or a custom QEMU display path is still needed for deep compositor integration.

### 4.2 Host Input → Guest
```
Qt Event → InputBridge → QmpInput (preferred) → Android Emulator console/QMP → Android input stack
Qt Event → InputBridge → ADB shell input (fallback) → Android input stack
```

### 4.3 Instance Lifecycle
```
User clicks "Start" → InstanceManager → VirtualMachine::start()
                                 → ProcessLauncher::runAsync(emulator.exe, args)
                                 → StateCallback(VMState::Running)
```

## 5. Technology Choices

| Layer | Technology | Reason |
|-------|-----------|--------|
| UI | Qt 6.8.3 (QML) | Verified local build target |
| Build | CMake + Visual Studio 17 2022 | Matches the verified Windows/MSVC environment; do not use Ninja/MSYS2 on this machine |
| JSON | nlohmann/json | Header-only, modern C++ API |
| Graphics Host | OpenGL 4.1 Core | Stable on Windows; ANGLE outputs to D3D11/Vulkan |
| Graphics Guest | ANGLE (EGL/GLES3) | Google's battle-tested translation layer |
| Virtualization | QEMU + WHPX | Same stack as Android Emulator; open source |
| ARM Translation | libndk_translation | Google's official ARM→x86 library (AOSP) |
| Threading | std::thread + ThreadPool | C++20 native; no external deps |
| Testing | Qt Test | One QTest executable per module to avoid multiple `QTEST_MAIN` conflicts |

## 6. Security Boundaries

- **GPL Isolation**: QEMU runs as a separate process; Host layer communicates via TCP/Unix sockets, avoiding license contamination.
- **No Elevated Privileges**: Chimera does not require Administrator rights (Hyper-V/WHPX is a Windows feature, not a driver installation).
- **Sandboxed Guest**: Android apps run inside the VM; host filesystem access is explicitly mounted via SharedFolder.

## 7. Performance Targets

| Metric | Target | Phase |
|--------|--------|-------|
| Cold boot to Android desktop | < 15s | Phase 3 |
| 2D game FPS | 60+ FPS target, not yet verified | Phase 1+ |
| 3D game FPS (ANGLE D3D11) | 60+ FPS target, not yet verified | Phase 2+ |
| Input latency | < 16ms (1 frame) | Phase 2 |
| Multi-instance (4x) RAM | < 12 GB with KSM | Phase 3 |

Current 2026-05-15 measurement: native embedding boots Android at 1280×720 / 240 dpi with SurfaceFlinger active mode 60.00 Hz and attaches the emulator Win32 window into Chimera. gRPC RGB888 screenshot stream remains functional at 1280×720 guest / 960px stream width and peaks around 32 FPS, but it is now fallback-only. The 60 FPS rows mean the display path is 60Hz-capable; game-level locked 60 still depends on the Android workload and emulator GPU renderer.

## 8. Development Workflow

1. **MSVC first**: Configure with `Visual Studio 17 2022` and `-A x64`.
2. **Qt Test per executable**: Each test file owns exactly one `QTEST_MAIN`.
3. **Boundary validation**: Validate file names, JSON config, CLI paths, and guest/host IPC responses before use.
4. **Verification loop**: After code changes, run Release build and `ctest --test-dir build -C Release --output-on-failure`.

---

*Architecture version 1.1 — 2026-05-14*
