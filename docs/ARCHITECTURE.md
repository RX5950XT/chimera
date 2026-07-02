# Project Chimera — Architecture Overview

> 模組分層、技術選型與設計原則（含原 PLAN.md 的長期參考內容）。現況數字與已知限制見 [STATUS.md](STATUS.md)；決策與 feature flags 見根目錄 `CLAUDE.md`。

## 1. 願景與設計目標

建立開源 Windows Android 模擬器，功能對標 BlueStacks，**手遊優先**：

- **Open Source**：Apache 2.0（host）+ GPL v2（QEMU 虛擬化層，獨立 process 經 IPC 隔離避免授權污染）
- **Windows Native**：Windows 10/11 + WHPX / Hyper-V
- **Gaming First**：低輸入延遲、高 FPS、鍵位映射、手把、巨集
- **零雲端依賴**：不強制帳號、無遙測、無廣告
- **模組化**：每個子系統可獨立替換與測試

### 與 BlueStacks 的對照

| 層面 | BlueStacks | Chimera |
|------|-----------|---------|
| 虛擬化 | 修改版 VirtualBox + NEM → Hyper-V | Android Emulator fork（QEMU + WHPX） |
| Android | 閉源 Pie64 ROM | `google_apis_playstore` x86_64 image |
| 圖形 | 自研 AGA 引擎 + Vulkan | fork/patch gfxstream + D3D11 shared texture |
| ARM 相容 | 閉源轉譯層 | libndk_translation（AOSP） |
| 輸入注入 | `HD-Bridge-Native.dll` → virtio-input | emulator gRPC sendTouch/sendKey（Console/QMP/ADB fallback） |
| UI | Qt + QML | Qt 6 + QML |
| 雲端 | BlueStacks Cloud | 無 |

**產品邊界**（不重新討論）：不從零重寫完整 Android VM（WHPX/QEMU/ranchu/virtio/gfxstream/Play image/ADB/snapshot 全套重做不合理）；保留 Android Emulator/QEMU/gfxstream 相容核心，改寫 host shell、headless display producer、input 與 resource policy。使用者面只允許 Chimera 單一視窗，正式路徑強制 headless、不外露原生 Emulator 視窗。

## 2. 分層架構

```
┌─────────────────────────────────────────────┐
│  UI Layer (Qt 6 / QML)                      │
│  ├─ ChimeraWindow / GuestDisplay (QSG D3D11)│
│  ├─ InputMapperOverlay / Dock / 設定面板    │
│  └─ Multi-Instance Manager                  │
├─────────────────────────────────────────────┤
│  Host Service Layer (C++20)                 │
│  ├─ Input: InputBridge / Mapper / Gamepad / │
│  │          Macro / CoordinateMapper        │
│  ├─ Graphics: FramebufferCapture 家族 /     │
│  │          SharedD3D11TexturePublisher     │
│  ├─ Audio: WASAPI bridge                    │
│  ├─ Instance: VM lifecycle / ProcessLauncher│
│  ├─ Config: JSON settings                   │
│  └─ Integration: Clipboard / Location       │
├─────────────────────────────────────────────┤
│  Virtualization (emulator.exe = QEMU+WHPX)  │
│  └─ source-patched gfxstream backend        │
│     (Chimera shared-texture bridge)         │
├─────────────────────────────────────────────┤
│  Android Guest (google_apis_playstore x86_64)│
│  ├─ SurfaceFlinger（headless 下 SwiftShader-│
│  │   ES 合成；Vulkan app 直達實體 GPU）     │
│  ├─ libndk_translation (ARM → x86)          │
│  └─ com.chimera.launcher HOME               │
└─────────────────────────────────────────────┘
```

## 3. 模組職責（`src/`）

| 模組 | 內容 |
|------|------|
| `host/ui/` | Qt6 QML shell、`GuestDisplay`（QSG D3D11 native texture render；keyed-mutex acquire + 私有副本）、ScreenRecorder、PerformanceMonitor；`NativeEmulatorView` 僅 unsafe diagnostics |
| `host/input/` | `InputBridge`（gRPC sendTouch/sendKey 優先 → Console/QMP/ADB fallback）、`CoordinateMapper`（rotation/letterbox/DPI）、`InputMapper`（JSON 鍵位方案）、`GamepadManager`（XInput，focus-gated）、`MacroEngine` |
| `host/graphics/` | `FramebufferCapture` 家族（SharedD3D11 / SharedMemory / Grpc / Adb / Vnc）、`SharedD3D11TexturePublisher`、`AngleBackend`（動態載入） |
| `host/audio/` | `AudioBridge`（WASAPI shared-mode + AUTOCONVERTPCM） |
| `host/instance/` | `InstanceManager`（persistence、runtime probe、port derivation）、`VirtualMachine`（emulator args、headless 強制、visible-HWND watchdog）、`ProcessLauncher`（CreateProcessW、Job Object kill-on-close、priority/EcoQoS policy）、`DeviceSpoofer`、`MemoryTrimmer` |
| `host/integration/` | `ClipboardBridge`（CF_UNICODETEXT）、`LocationSimulator`（geo fix，順序 lon lat alt） |
| `common/utils/` | Logger、ThreadPool、FileUtils、`LowInterferenceProcess`（所有 adb/ffmpeg helper 必經） |

## 4. 通訊流

### 4.1 顯示（Guest → Host）
```
正式（-Fast）：guest 渲染 → gfxstream postFrameDirectGpu
   （GLES 合成內容 flushFromGl+invalidateForVk → GPU recordCopy blit）
   → D3D11 NT shared texture（keyed mutex）
   → GuestDisplay AcquireSync==S_OK → CopyResource 私有副本 → QSG 取樣
Fallback（-Stock）：emulator gRPC getScreenshot 管線輪詢（~4–17 FPS）
診斷（CLI opt-in）：raw MMAP / screenrecord / ADB screencap
```

### 4.2 輸入（Host → Guest）
```
滑鼠/觸控：GuestDisplay 座標轉換 → InputBridge → emulator gRPC sendTouch
鍵盤：gRPC sendKey；wheel：gRPC sendTouchSwipe（~16ms throttle）
Fallback 鏈：Console(5554 telnet) → QMP → ADB
（輸入只由 GuestDisplay 轉發；window 層直送會雙送/座標錯）
```

### 4.3 Port derivation（不可硬寫）
```
console = 5554 + 2N；adb = console + 1；gRPC = 8554 + console offset
所有對 emulator 的連線（capture/input/keyboard/console）取自同一公式。
```

## 5. 技術選型

| 層級 | 選擇 | 理由 |
|------|------|------|
| 虛擬化 | `emulator.exe`（AOSP QEMU fork）+ WHPX | 與 BlueStacks 同級；WHPX 與 Hyper-V/WSL2 共存、免驅動 |
| 顯示 producer | source-patched gfxstream（build id 對齊 SDK） | stock ABI 不相容實測 `0xC0000005`；proxy hook 定案死路 |
| Host render | Qt 6.8.3 QML + D3D11 RHI（QSG native texture） | `QQuickPaintedItem` 每幀 QPainter 不可用於高頻路徑 |
| ARM 轉譯 | libndk_translation | AOSP 官方，redroid 驗證 |
| Build | CMake + Visual Studio 17 2022（MSVC only） | 本機 MSYS2 GCC `cc1plus` crash，不用 MinGW/Ninja |
| JSON | nlohmann/json | header-only |
| 測試 | Qt Test（每 module 一個 exe） | 避免多重 `QTEST_MAIN` 衝突 |
| 影音 | FFmpeg | 錄影 H.264 |

## 6. 安全與授權邊界

- **GPL 隔離**：QEMU/emulator 為獨立 process，host 經 gRPC/socket 通訊，避免授權污染。
- **免特權**：不需 Administrator（WHPX 是 Windows 功能非驅動）。
- **法律邊界**：只參考 BlueStacks 功能清單與公開設定檔格式（`docs/references/`），不使用反編譯程式碼；不使用 VirtualBox 任何程式碼；BlueStacks binaries 禁止 commit。
- **Guest 隔離**：host 檔案存取只經明確的 push/pull（見 [ADR-001](ADR-001-shared-folder.md)）。

## 7. 效能現況

量測數字、誠實邊界與驗證入口一律見 [STATUS.md](STATUS.md)——歷史上多次「60 FPS」宣稱被後續 session 更正，本文件不保留數字快照。量測紀律：可見宣稱必須含 host 視窗像素證據；FPS 分清 guest/stream/render/dup 與 workload（push-based idle 低 FPS 正常）。

## 8. 開發工作流

1. **MSVC first**：`Visual Studio 17 2022` + `-A x64`；每個新終端先 `vcvarsall.bat amd64`。
2. **每個 test 檔一個 `QTEST_MAIN`**；邊界輸入（檔名、JSON、CLI、IPC 回應）先驗證再用。
3. **改動後跑 Release build + `ctest -LE integration`**；runtime 行為用 `scripts/` verifier 驗證。
4. 建置/測試/貢獻細節見 [BUILD.md](BUILD.md) 與根目錄 `AGENTS.md`。

## 9. 參考資料

- [AOSP Emulator QEMU](https://android.googlesource.com/platform/external/qemu/)、[ANGLE](https://github.com/google/angle)、[SwiftShader](https://github.com/google/swiftshader)、[redroid](https://github.com/remote-android/redroid)
- [WHPX API Reference](https://docs.microsoft.com/en-us/virtualization/api/)、[QEMU accelerators](https://qemu.readthedocs.io/en/latest/system/accelerators.html)
- 本地參考：`docs/references/`（BlueStacks 逆向分析、競品平滑度研究）

---
*Architecture version 2.0 — 2026-07-02（整併原 PLAN.md 參考內容；數字快照移除，改指向 STATUS.md）*
