# Project Chimera — Architecture Overview

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
Guest SurfaceFlinger → VirtIO-GPU → QEMU → GraphicsBridge → Framebuffer → Renderer → Qt OpenGL
```

### 4.2 Host Input → Guest
```
Qt Event → InputBridge → virtio-input device → Android EventHub → App
```

### 4.3 Instance Lifecycle
```
User clicks "Start" → InstanceManager → VirtualMachine.buildQemuArgs()
                                 → ProcessLauncher.runAsync(qemu-system-x86_64, args)
                                 → StateCallback(VMState::Running)
```

## 5. Technology Choices

| Layer | Technology | Reason |
|-------|-----------|--------|
| UI | Qt 6.6+ (QML) | BlueStacks also uses Qt; modern declarative UI |
| Build | CMake + Ninja | Cross-platform, fast, IDE-friendly |
| JSON | nlohmann/json | Header-only, modern C++ API |
| Graphics Host | OpenGL 4.1 Core | Stable on Windows; ANGLE outputs to D3D11/Vulkan |
| Graphics Guest | ANGLE (EGL/GLES3) | Google's battle-tested translation layer |
| Virtualization | QEMU + WHPX | Same stack as Android Emulator; open source |
| ARM Translation | libndk_translation | Google's official ARM→x86 library (AOSP) |
| Threading | std::thread + ThreadPool | C++20 native; no external deps |
| Testing | Qt Test + Catch2 | Qt Test for GUI; Catch2 for pure logic |

## 6. Security Boundaries

- **GPL Isolation**: QEMU runs as a separate process; Host layer communicates via TCP/Unix sockets, avoiding license contamination.
- **No Elevated Privileges**: Chimera does not require Administrator rights (Hyper-V/WHPX is a Windows feature, not a driver installation).
- **Sandboxed Guest**: Android apps run inside the VM; host filesystem access is explicitly mounted via SharedFolder.

## 7. Performance Targets

| Metric | Target | Phase |
|--------|--------|-------|
| Cold boot to Android desktop | < 15s | Phase 3 |
| 2D game FPS | 60 FPS stable | Phase 1 |
| 3D game FPS (ANGLE D3D11) | 60 FPS stable | Phase 2 |
| Input latency | < 16ms (1 frame) | Phase 2 |
| Multi-instance (4x) RAM | < 12 GB with KSM | Phase 3 |

## 8. Development Workflow

1. **Agent-driven**: Each subsystem is owned by an AI Agent with its own design doc in `docs/agents/`
2. **Interface-first**: Agents agree on C++ headers / protobuf schemas before implementation
3. **Contract testing**: Stubs and mocks are written before real implementations
4. **Daily integration**: Orchestrator Agent runs CMake build + unit tests every cycle

---

*Architecture version 1.0 — 2026-05-08*
