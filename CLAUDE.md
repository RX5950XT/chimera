# Project Chimera — CLAUDE.md

> AI 工作階段快速參考。每次重大變更後更新。

## 當前狀態

**完成度**: BlueStacks Parity Roadmap v3 P0–P4e + Session 2 補強 COMPLETE (2026-05-18)
**生產引擎**: `emulator.exe` (Google QEMU+WHPX fork) — `--qemu-backend` / `--hcs-backend` 為 legacy R&D，保留不刪
**下一步**: Phase 8 — gfxstream-capable QEMU 或 SwiftShader APEX → SurfaceFlinger stable → boot_completed=1 → ADB TCP
**Tests**: 9/9 unit tests PASS；3 integration tests（需 emulator 運行中）

## 架構

```
UI Layer          src/host/ui/           Qt 6 QML 視窗、Dock、設定面板、Input Overlay
Config            src/host/config/       JSON ConfigManager (nlohmann/json)
Input             src/host/input/        InputBridge → Console/QMP/ADB；CoordinateMapper；Gamepad；Macro
Graphics          src/host/graphics/     FramebufferCapture (Grpc/Adb/Vnc)；AngleBackend；PerformanceMonitor
Audio             src/host/audio/        WASAPI shared-mode
Instance          src/host/instance/     VM lifecycle；ProcessLauncher；DeviceSpoofer；MemoryTrimmer
Integration       src/host/integration/  ClipboardBridge (CF_UNICODETEXT)；LocationSimulator (geo fix)
Utils             src/common/utils/      Logger；ThreadPool；FileUtils
Tests             tests/unit/            9 Qt Test executables
                  tests/integration/     emulator-boot / input-inject / screencap (QSKIP guards)
```

**Input priority chain**: HvSocket → Console (port 5554 telnet) → QMP → ADB
**Display path**: Native Win32 window embed (default) → gRPC/ADB fallback (`--stream-capture`)

## 重要決策（不討論不改）

1. **MSVC only** — MSYS2 GCC 在此機器上 `cc1plus.exe` crash，不嘗試 MinGW
2. **emulator.exe 為生產引擎** — BlueStacks 同等級 (QEMU+WHPX)；`--qemu-backend/--hcs-backend` 是 R&D
3. **ANGLE 動態載入** — `libEGL.dll` + `libGLESv2.dll` via QLibrary；不需要 .lib
4. **AVD 用 `google_apis`** — 避免 `google_apis_playstore` 的 ADB RSA 驗證鎖
5. **Port 5554 = Android Console telnet**，不是 JSON QMP（JSON QMP 在 `--qemu-backend` port 4445）
6. **BlueStacks input**: `HD-Bridge-Native.dll` → virtio-input，不是 kernel driver；`BstkDrv.sys` 是 network/filter driver

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration
```

## Feature Flags

| 變數 | 值 | 預設 | 說明 |
|------|----|------|------|
| `CHIMERA_INPUT_BACKEND` | `console\|adb\|qmp\|auto` | `auto` | auto = 嘗試 Console，不 Ready 則退回 ADB |
| `CHIMERA_PROCESS_LAUNCHER` | `legacy\|native\|auto` | `auto` | legacy = `_popen`；native = `CreateProcessW` |

## 已知問題

| 問題 | 狀態 |
|------|------|
| SurfaceFlinger crash-loop (`--cuttlefish`) | OPEN — Phase 8 (gfxstream) |
| ADB TCP blocked (boot_completed=1 未到達) | OPEN — Phase 8 解鎖 |
| gRPC stream 峰值 ~32 FPS @ 720p | ACCEPTED — Native embed 為預設 |
| Native child window 疊在 QML 上 | ACCEPTED — controls 放在 viewport 外 |

## 路徑

| 資源 | 路徑 |
|------|------|
| 專案根目錄 | `D:\Workspace_cloud\Personal_Project\chimera\` |
| Build 輸出 | `build\Release\` |
| Qt | `C:\Qt\6.8.3\msvc2022_64\` |
| Android SDK | `third_party\android-sdk\` |
| AVD | `third_party\android-avd\chimera_dev.avd\` |
| Instance 設定 | `configs\instances.json` |
| ANGLE headers | `third_party\angle\` |

## 參考文件

| 文件 | 用途 |
|------|------|
| `AGENTS.md` | Build、測試、Git、Coding 標準、疑難排解 |
| `CONTEXT.md` | 開發歷程、相位記錄、bug 修正紀錄 |
| `docs/adr/ADR-001-shared-folder.md` | SharedFolder 技術選型 ADR |
| `docs/references/bluestacks.conf` | BlueStacks 設定格式參考 |

**禁止 commit**: BlueStacks binaries (Binaries/, Client/, Engine/, Dumps/)

---
*Updated: 2026-05-18*
