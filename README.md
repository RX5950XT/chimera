# Project Chimera

**Chimera** 是一款完全開源的 Windows Android 模擬器，專為手遊玩家設計。

> 參考 BlueStacks / LDPlayer 的功能與體驗，但全部使用開源元件從頭實作，無強制雲端、無廣告、無數據收集。

---

## 快速開始

### 系統需求

- **OS**: Windows 10/11 Pro/Enterprise (需啟用 Hyper-V + Windows Hypervisor Platform)
- **CPU**: 支援 VT-x / AMD-V + SLAT (EPT/NPT)
- **RAM**: 8 GB 以上（建議 16 GB）
- **GPU**: 支援 DirectX 11 或 Vulkan 的獨立顯卡（建議）
- **磁碟**: 20 GB 可用空間
- **開發工具**: Visual Studio 2022 Community、Qt 6.8.3 MSVC2022_64

### 完整安裝流程

```powershell
# 1. 克隆倉庫
git clone https://github.com/RX5950XT/chimera.git
cd chimera

# 2. 初始化子模組（QEMU）
git submodule update --init --recursive

# 3. 安裝 Qt 6（如果尚未安裝）
python -m pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt

# 4. 下載 Android SDK 與系統映像
python scripts/setup-android-sdk.py

# 5. 載入 VS 開發環境並建置
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release

# 6. 執行測試
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure

# 7. 啟動模擬器
# build 完成後會自動部署 Qt DLL 到 build\Release
.\build\Release\chimera-ui.exe

# 只看 UI / 驗證 QML，不啟動 Android
.\build\Release\chimera-ui.exe --no-emulator
```

### 現有 AVD 直接啟動（若已有 Android Emulator）

```powershell
# 確保 ADB 在 PATH 中
$env:PATH = "$env:PATH;C:\Users\$env:USERNAME\AppData\Local\Android\Sdk\platform-tools"

# 啟動（會自動尋找 AVD）
.\build\Release\chimera-ui.exe
```

---

## 功能

| 功能 | 狀態 | 備註 |
|------|------|------|
| Android 14 (x86_64) | ✅ | QEMU + WHPX |
| OpenGL ES → D3D11 | ✅ | ANGLE headers + Chrome libEGL/libGLESv2 |
| 繁體中文介面 | ✅ | 主 UI、工具列、對話框已繁中化 |
| 鍵盤/滑鼠輸入 | ✅ | QMP 優先，ADB fallback |
| 手把支援 (XInput) | ✅ | 14 鍵映射，60 Hz 輪詢 |
| 顯示路徑 | ✅ | 預設 native emulator window embedding，Android 端 60Hz；`--stream-capture` 才啟用 gRPC/ADB fallback |
| 鍵盤映射編輯器 | ✅ | 右側面板內頁；串流模式可用 QML overlay |
| 多開管理器 | ✅ | 右側面板內頁，JSON 持久化，clone 支援 |
| 巨集錄製/播放 | ✅ | 右側面板內頁，背景執行緒，loop 支援 |
| 螢幕錄影 | ✅ | Native child window 擷取 + FFmpeg H.264 / PNG fallback |
| 效能監控 | ✅ | FPS 計數器、幀時間、掉幀 |
| 裝置偽裝 | ✅ | 5 種旗艦機型 build.prop |
| 記憶體修剪 | ✅ | 自動監控 /proc/meminfo |
| 磁碟壓縮 | ✅ | 清理 cache/logs/tmp |
| GPU-PV 硬體加速 | 🔄 | Hyper-V HCS 框架（實驗中） |
| VirtIO 輸入 | ⏸️ | 需自訂 QEMU 編譯 |
| 插件系統 | ⏸️ | Phase 6 規劃 |

**圖例**: ✅ 完成 | 🔄 進行中 | ⏸️ 待前置條件

---

## 架構

```
Chimera (Host Windows)
├── UI Layer          → Qt 6 + QML (ChimeraWindow.qml)
├── Input System      → InputBridge (QMP 優先) / GamepadManager / InputMapper
├── Display           → NativeEmulatorView / GuestDisplay fallback / PerformanceMonitor
├── Graphics Capture  → GrpcFramebufferCapture / AdbFramebufferCapture (`--stream-capture`)
├── Audio Bridge      → WASAPI shared-mode
├── Instance Manager  → JSON persistence + VirtualMachine launcher
├── QEMU + WHPX       → Android Emulator (prebuilt) / custom QEMU (future)
│       └── Android Guest (AOSP x86_64)
│               ├── VirtIO GPU/Net/Snd
│               └── libndk_translation (ARM → x86)
└── ANGLE             → libEGL.dll + libGLESv2.dll (Chrome)
```

---

## 使用方式

### 目前效能現況

- 預設顯示路徑已改為 `NativeEmulatorView`，直接嵌入 Android Emulator Win32 視窗，避開 screenshot/gRPC 每幀複製瓶頸。
- Android Emulator 原生直式工具列會在嵌入後自動隱藏；Chimera 改用主視窗內建右側狀態/操作面板，包含返回、首頁、最近使用等 Android 導航鍵。
- 介面只保留一個 FPS 狀態區，移除重複 FPS 顯示與會被裁切的 hover tooltip；鍵位、多開、巨集改成右側面板內頁，避免 Win32 native child window 蓋住 QML 彈窗。
- 顯示框固定 16:9 適配目前 1280×720 guest，避免非 16:9 容器造成 Android Emulator 內部黑邊。
- Qt shell 啟動時強制使用 D3D11 RHI；emulator/qemu process tree 預設套用 high priority，降低遊戲幀時間尖峰。
- Live smoke 已驗證 Android 實際 `1280x720`、`240 dpi`、SurfaceFlinger `60.00 Hz`、native window attach 成功。
- `--stream-capture` 才會啟用 gRPC/ADB 畫面擷取；gRPC/ADB 現在是 fallback/除錯路徑，不是主要遊玩路徑。

### 基本操作

| 快捷鍵 | 功能 |
|--------|------|
| `Ctrl + Shift + S` | 截圖 |
| `Ctrl + Shift + R` | 錄影 |
| `Ctrl + Shift + A` | 鍵位配置 |
| `Ctrl + Shift + 7` | 巨集 |
| `Ctrl + Shift + 8` | 多開管理 |
| `Shift + Tab` | 顯示/隱藏鍵位 |
| `Alt + Left` | Android 返回 |
| `Ctrl + Shift + H` | Android 首頁 |
| `Ctrl + Shift + Tab` | Android 最近使用 |
| `F11` | 全螢幕 |
| `Esc` | 離開全螢幕 |
| `Ctrl + 1~9` | 切換實例 |

### 遊戲手把

連接 XInput 相容手把後自動識別，預設映射：
- A/B/X/Y → Android DPAD_CENTER / BACK 等
- 左搖桿 → 觸控滑動
- RT/LT → 音量調整

### 鍵盤映射

1. 開啟 Input Mapper Overlay（工具列按鈕）
2. 拖曳按鍵到畫面對應位置
3. 儲存為 JSON scheme
4. 載入時自動套用

### 多開

1. 點選「+」按鈕新增實例
2. 每個實例獨立 data 目錄
3. 使用 Clone 複製現有實例設定

---

## 授權

本專案採用多重授權，以隔離 GPL 污染：

- **核心程式碼** (`src/host/`, `src/common/`): **Apache 2.0**
- **虛擬化層** (`src/virtualization/qemu/`): **GPL v2**（QEMU 子模組）
- **Android 系統** (`src/guest/`): **Apache 2.0**（AOSP）
- **第三方函式庫** (`third_party/`): 依各自授權

---

## 貢獻

本專案主要由 AI Agent 自動化開發，人類監督為輔。

歡迎提交 Issue 與 PR！請參閱 [CONTRIBUTING.md](docs/process/CONTRIBUTING.md)。

---

## 文件索引

| 文件 | 內容 |
|------|------|
| [docs/README.md](docs/README.md) | 文件總索引 |
| [docs/project/BUILD.md](docs/project/BUILD.md) | MSVC + Qt 6 詳細建置說明 |
| [AGENTS.md](AGENTS.md) | AI Agent 工作流程與編碼標準 |
| [CLAUDE.md](CLAUDE.md) | 架構決策與技術細節 |
| [docs/project/STATUS.md](docs/project/STATUS.md) | 當前階段狀態與驗證記錄 |
| [docs/project/CODE_REVIEW.md](docs/project/CODE_REVIEW.md) | 最新程式碼審查、已修正問題與追蹤項目 |
| [docs/project/PLAN.md](docs/project/PLAN.md) | 完整實作計畫 |
| [docs/process/HANDOVER.md](docs/process/HANDOVER.md) | 給下一個開發者的交接文件 |

---

*Project Chimera — Open Source Windows Android Emulator*
