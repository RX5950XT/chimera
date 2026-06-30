# Project Chimera

**Chimera** 是一款 Windows 上的開源 Android 模擬器，面向手遊玩家，參考 BlueStacks / LDPlayer 的功能與體驗。

> 以 fork/patch Android Emulator + gfxstream + QEMU/WHPX 作為 headless 相容核心，外層只保留單一 Chimera 視窗；無強制雲端、無廣告、無數據收集。

---

## 一鍵啟動

```bat
:: 倉庫根目錄，雙擊或執行：
start-chimera.cmd
```

- **預設（雙擊 `start-chimera.cmd`）**：最快可用路徑，等同 `start-chimera.ps1 -Fast -InteractiveFirst`。使用自訂 gfxstream shared-texture runtime + GuestVulkan/skiavk + normal priority；一般 Android UI 可見可互動，真實 host input path 已量到接近 60 的 headroom。
- **`-Stock`（fallback）**：SDK emulator + gRPC 顯示路徑。一般 Android 首頁 / app 正常渲染、輸入完整，但 FPS 較低（push-based 內容約 4–17），只作保守 fallback / 診斷。
- **`-Fast`（custom 60fps runtime）**：自訂 gfxstream shared-texture runtime，連續渲染內容（遊戲）走 `postFrameDirectGpu`（guest VK image → GPU blit → D3D11 shared texture）已通過嚴格可見 **1920×1080 / 60 FPS / 120 秒**驗證；一般 Android UI 會自動啟用 GuestVulkan/skiavk 並重開動畫。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Fast
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -SelfTest   # 開機→驗 1080p→截圖→清理
```

---

## 系統需求

- **OS**：Windows 10/11，啟用 Hyper-V + Windows Hypervisor Platform
- **CPU**：VT-x / AMD-V + SLAT（EPT/NPT）
- **GPU**：支援 D3D11 / Vulkan 的獨立顯卡（custom 60fps 路徑建議 NVIDIA / AMD）
- **RAM**：16 GB 建議；**磁碟**：20 GB
- **建置工具**：Visual Studio 2022 Community（MSVC）、Qt 6.8.3 msvc2022_64

---

## 建置

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration   # 20/20

# custom gfxstream 60fps runtime（選用）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-chimera-gfxstream-runtime.ps1
```

詳見 [docs/project/BUILD.md](docs/project/BUILD.md)。

---

## 顯示架構

```
Chimera (Host Windows, 單一 Qt6/QML 視窗)
  Input    InputBridge → emulator gRPC sendTouch/sendKey（console/QMP/ADB fallback）
  Display  ┌─ stock：headless emulator + gRPC getScreenshot（1920×1080，可用，低 FPS）
           └─ custom gfxstream runtime：
                continuous content → postFrameDirectGpu（guest VK image → GPU blit → D3D11 shared texture）→ 60 FPS
                normal UI（SurfaceFlinger GLES 合成）→ SwiftShader ES compositor（可見 / 可互動）
  Audio    WASAPI shared-mode
  Engine   Android Emulator / gfxstream / QEMU + WHPX（headless，-no-window）
              └─ Android guest（x86_64 + libndk_translation ARM→x86）
```

**重要**：正式路徑強制 headless（`-no-window`），不外露、不多開原生 Android Emulator 視窗。原生視窗嵌入 / window capture 僅作為本機 unsafe 診斷（需多重 opt-in flag）。顯示尺寸固定維持至少 1920×1080，不以降解析度換取假 FPS。

### host GLES / 一般 UI 修正（Session 88）

**根因（log 實證）**：custom runtime headless 下 prebuilt emulator 仍把 GLES mode 報為 `host`；但實際 underlying EGL/GLES 會落到 bundled **SwiftShader ES**。renderer enum 仍為 HOST 時，gfxstream translator 會發出桌面 `#version 330 core` compositor shader，被 SwiftShader ES compiler 拒絕（`'core' : invalid version directive`）→ SurfaceFlinger 合成空畫面 → 一般 UI 黑屏。

**已修正**：`-Fast` 啟動時設定 `CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1`，custom gfxstream backend 在此 gate 下只關閉 headless HOST 的 core-profile shader emission，讓一般 UI 走 SwiftShader ES shader path，同時保留 renderer identity 與 direct-VK shared-texture path。驗證：`start-chimera.ps1 -Fast -SelfTest` PASS，1920×1080 Chimera Launcher 截圖約 76 KB、Settings 可互動、0 residual process。

**ANGLE / D3D11 狀態**：ANGLE headless + D3D11 + NVIDIA 可初始化，也能消除 shader version error；但 SurfaceFlinger 後續 draw 會在 ANGLE `libGLESv2.dll` 內 AV（`glDrawArrays`，program 28/31），新版 ANGLE 亦同。故正式修法採 SwiftShader ES compositor path；direct-VK shared-texture 60fps path 仍保留給連續渲染內容。

---

## 功能（BlueStacks parity，production 路徑）

| 類別 | 功能 |
|------|------|
| 核心 | Android boot (QEMU+WHPX)、headless 顯示內嵌、多開（批次啟停） |
| 輸入 | 鍵盤 / 滑鼠 / 觸控 / 手把(XInput)、多點觸控(MT Type-B)、IME、鍵位映射匯入匯出、巨集錄製播放 |
| 顯示 | FPS lock(30/60/90/120)、Screen resize/DPI/rotation、效能 HUD(FPS/Lat/Drop)、十字準心游標 |
| App | APK/OBB 安裝、launch/stop/uninstall/clear、釘選常用應用、Chimera Launcher 首頁 |
| 系統 | Root mode、裝置偽裝(5 旗艦)、剪貼簿同步、檔案分享(push/pull)、網路 Proxy / 網速模擬 |
| 模擬 | GPS(geo fix+route)、感應器(acc/gyro/mag)、震動、電池、Shake、Rotate |
| 媒體 | 螢幕錄影、截圖、Audio(WASAPI) |
| 體驗 | Eco mode、Boss Key、Trim Memory、Mute、快捷鍵 |

完整對照見 [CLAUDE.md](CLAUDE.md)。

### 常用快捷鍵

| 快捷鍵 | 功能 | 快捷鍵 | 功能 |
|--------|------|--------|------|
| `Ctrl+Shift+S` | 截圖 | `Ctrl+Shift+R` | 錄影 |
| `Ctrl+Shift+A` | 鍵位配置 | `Ctrl+Shift+7` | 巨集 |
| `Ctrl+Shift+8` | 多開管理 | `Ctrl+Shift+X` | Boss Key |
| `Ctrl+Shift+T` | Trim Memory | `Ctrl+Shift+M` | Mute |
| `F11` / `Esc` | 全螢幕 / 離開 | `Ctrl+1~9` | 切換實例 |

---

## 現況與邊界（誠實版）

- **日常可用**：`start-chimera.cmd` 現在預設最快可用路徑（custom gfxstream + GuestVulkan/skiavk + normal priority），不是舊 stock gRPC 慢路徑。`-Stock` 只作保守 fallback / 診斷。
- **Fast custom runtime**：`-Fast` 會 boot 到可見一般 UI，host 會套 `debug.renderengine.backend=skiavk` / `debug.hwui.renderer=skiavk` 並 framework restart，讓 HWUI + SurfaceFlinger 走硬體 Vulkan；GuestVulkan 時會重開 Android animations。
- **1080p/60（synthetic）**：custom runtime 對**連續渲染內容**已驗證 `min 59.8 / avg 60.0 / dup 0`（`scripts\verify-true-1080p60.ps1`）。push-based 的開機動畫 / idle Home 本來就是低 FPS，屬正常。
- **日常互動（真實量測）**：`scripts\verify-interactive-ui.ps1` 的 adb swipe 只代表測試注入路徑，不能外推成實際滑鼠手感。一次性 host mouse-drag probe 量到 production input path `guestMax=116.7 / render=57.4 / dup=0`，證明 GPU-direct + host input 有接近 60 headroom；但該測法會搶實體滑鼠，已禁止再用。後續若要量 host input，需新增不移動實體游標的 internal synthetic touch hook。
- **背景音樂干擾**：雙擊 `start-chimera.cmd` 會用 `-InteractiveFirst`（normal priority，最順但可能較影響 host audio）。需要保護背景音樂時改用 `start-chimera.ps1 -Fast -AudioFirst`。
- **一般 UI 合成**：只設 app HWUI skiavk 不夠；SurfaceFlinger 也必須 skiavk 並 framework restart，否則會只剩背景/空 UI。ANGLE/D3D11 硬體 compositor 已確認會在 SurfaceFlinger draw 觸發 ANGLE `libGLESv2.dll` AV，暫不作正式路徑。
- 真實遊戲 flow 受益於 direct-VK 路徑，但尚未逐一重測。

---

## 授權

- 核心程式（`src/host/`, `src/common/`）：**Apache 2.0**
- 虛擬化層（`src/virtualization/qemu/`）：**GPL v2**（QEMU 子模組）
- 第三方（`third_party/`）：依各自授權

---

## 文件索引

| 文件 | 內容 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | 架構決策、功能對照、feature flags、已知問題 |
| [CONTEXT.md](CONTEXT.md) | 開發歷程與 session 紀錄 |
| [AGENTS.md](AGENTS.md) | Build / 測試 / Git / Coding 標準 |
| [tasks/todo.md](tasks/todo.md) | 當前任務規劃與回顧 |

---

*Project Chimera — Open Source Windows Android Emulator（主要由 AI Agent 自動化開發）*
