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

### 安裝

```powershell
# 1. 克隆倉庫
git clone https://github.com/chimera-emulator/chimera.git
cd chimera

# 2. 初始化子模組（QEMU + ANGLE + ...）
git submodule update --init --recursive

# 3. 編譯
python scripts/build.py --release

# 4. 啟動
python scripts/run.py --system-image path/to/system.img
```

### 使用預編譯映像（快速體驗）

若不想自行編譯 AOSP，可下載 Android Emulator 官方 x86_64 映像：

```powershell
python scripts/download-sysimage.py --api 34 --abi x86_64
python scripts/run.py
```

---

## 功能

| 功能 | 狀態 | 備註 |
|------|------|------|
| Android 14 (x86_64) | Phase 0 | QEMU + WHPX |
| OpenGL ES → D3D11 | Phase 1 | ANGLE |
| 鍵盤/滑鼠輸入 | Phase 1 | virtio-input |
| 手把支援 (XInput) | Phase 2 | 規劃中 |
| 鍵盤映射編輯器 | Phase 2 | 規劃中 |
| 多開管理器 | Phase 2 | 規劃中 |
| 巨集錄製/播放 | Phase 2 | 規劃中 |
| 螢幕錄影 | Phase 2 | FFmpeg |
| GPU-PV 硬體加速 | Phase 3 | 規劃中 |
| 插件系統 | Phase 4 | 規劃中 |

---

## 架構

```
Chimera (Host Windows)
├── UI Layer          → Qt 6 + QML
├── Input Mapper      → C++ / JSON
├── Graphics Bridge   → ANGLE (ES → D3D11/Vulkan)
├── Audio Bridge      → WASAPI
├── Instance Manager  → SQLite + QEMU CLI
└── QEMU + WHPX       → AOSP Android Emulator QEMU fork
        └── Android Guest (AOSP x86_64)
                ├── ANGLE Guest Driver
                ├── VirtIO GPU/Net/Snd
                └── libndk_translation (ARM → x86)
```

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

歡迎提交 Issue 與 PR！請參閱 [CONTRIBUTING.md](docs/CONTRIBUTING.md)。

---

*Project Chimera — Open Source Windows Android Emulator*
