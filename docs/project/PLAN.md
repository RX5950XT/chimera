# Project Chimera — 開源 Windows Android 模擬器實作計畫

> **版本**: 1.0.0  
> **日期**: 2026-05-08  
> **目標**: 建立功能對標 BlueStacks 的完全開源 Windows Android 模擬器，優先服務手遊玩家  
> **開發模式**: AI Agent 自動化為主，人類監督為輔  
> **建議授權**: Apache 2.0（核心）+ GPL v2（QEMU 虛擬化層，透過 IPC 隔離）

---

## 1. 專案概述

### 1.1 願景

建立名為 **Project Chimera** 的開源 Windows Android 模擬器，核心定位為**手遊優先**。參考 BlueStacks 的功能清單與架構模式，但全部使用開源元件從頭實作，不依賴任何商業閉源軟體。

### 1.2 與 BlueStacks 的關係

| 層面 | BlueStacks（參考對象） | Project Chimera（我們的實作） |
|------|----------------------|------------------------------|
| 虛擬化 | 修改版 VirtualBox 6.1.36 + NEM | QEMU + WHPX（起步）→ HCS API（進階） |
| Android | 閉源 Pie64 ROM | AOSP x86_64（開源編譯） |
| 圖形 | 自研 AGA 引擎 + Vulkan | ANGLE（ES → D3D11/Vulkan）+ 自研優化層 |
| ARM 相容 | 閉源轉譯層 | libndk_translation（開源） |
| UI | Qt + QML + 內嵌 Web | Qt 6 + 自研 QML 元件 |
| 輸入映射 | 閉源格式 | 開源格式（參考現有 cfg 結構重設計） |
| 多開 | 閉源 MIM | 自研多開管理器 |
| 雲端 | BlueStacks Cloud / Now.gg | **移除**，開源專案不應有強制雲端依賴 |

### 1.3 核心設計原則

1. **手遊優先**: 圖形效能與輸入延遲是第一優先，其次才是功能完整性
2. **零雲端依賴**: 不強制帳號登入、不收集數據、無廣告系統
3. **模組化**: 每個子系統可獨立開發、測試、替換
4. **AI 原生開發**: 程式碼與架構設計需考慮 AI Agent 的理解與生成能力

---

## 2. 技術架構

### 2.1 總覽

```
┌─────────────────────────────────────────────────────────────┐
│                     Host Windows 10/11                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │   UI Layer   │  │  InputMapper │  │ Multi-Instance   │  │
│  │  (Qt6/QML)   │  │   (C++/JS)   │  │   Manager        │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────┘  │
│         │                  │                                 │
│  ┌──────▼──────────────────▼──────────────────────────────┐  │
│  │              Host Service Layer                        │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐  │  │
│  │  │ Graphics│ │  Audio  │ │  Input  │ │  SharedFS   │  │  │
│  │  │ Bridge  │ │ Bridge  │ │ Bridge  │ │   Server    │  │  │
│  │  └────┬────┘ └────┬────┘ └────┬────┘ └──────┬──────┘  │  │
│  │       └───────────┴───────────┴─────────────┘          │  │
│  │                        │                               │  │
│  │              ┌─────────▼─────────┐                     │  │
│  │              │  QEMU + WHPX      │                     │  │
│  │              │  (Virtualization) │                     │  │
│  │              └─────────┬─────────┘                     │  │
│  └────────────────────────┼────────────────────────────────┘  │
│                           │                                   │
│  ┌────────────────────────▼────────────────────────────────┐  │
│  │              Android Guest (AOSP x86_64)                 │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐  │  │
│  │  │  ANGLE  │ │ VirtIO  │ │ VirtIO  │ │  libndk_    │  │  │
│  │  │ Driver  │ │  GPU    │ │ Net/Snd │ │ translation │  │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────────┘  │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐  │  │
│  │  │ Surface │ │  ADB    │ │ Shared  │ │  Gamepad    │  │  │
│  │  │ Flinger │ │ Daemon  │ │ Folders │ │   HAL       │  │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────────┘  │  │
│  └─────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 技術棧選擇

| 層級 | 元件 | 選擇 | 理由 |
|------|------|------|------|
| **虛擬化引擎** | QEMU | `platform/external/qemu` (AOSP fork) | 已整合 WHPX、Goldfish 裝置、成熟穩定 |
| **CPU 加速** | WHPX | Windows Hypervisor Platform API | 與 Hyper-V 共存、無需額外驅動 |
| **Android 系統** | AOSP | Android 14/15 (`android-x86` 專案) | 開源、持續維護、已有 x86_64 支援 |
| **圖形渲染** | ANGLE | Google ANGLE (D3D11/Vulkan backend) | OpenGL ES → Host GPU，Chrome 驗證過的穩定性 |
| **備援渲染** | SwiftShader | Google SwiftShader | 純 CPU 後備方案 |
| **ARM 轉譯** | libndk_translation | AOSP NDK translation | ARM64 → x86_64，redroid 已驗證 |
| **主機 UI** | Qt 6 | Qt 6.6+ (LGPL/Commercial) | 跨平台、QML 現代化、BlueStacks 也在用 |
| **輸入映射** | 自研 | C++ + JSON format | 參考公開 cfg 結構，完全重設計 |
| **多開管理** | 自研 | C++ + SQLite | 管理多個 VM 實例的生命週期 |
| **影音處理** | FFmpeg | FFmpeg (LGPL/GPL) | 錄影、截圖、串流 |
| **通訊協定** | gRPC | gRPC over Unix socket / TCP | Host-Guest 通訊（替代 BlueStacks 的閉源橋接） |
| **設定系統** | 自研 | JSON + key-value DB | 參考 bluestacks.conf 結構 |

---

## 3. 分階段實作計畫

### Phase 0: 基礎建設（預估 2-4 週）

**目標**: 建立開發環境、編譯系統、CI/CD、專案骨架

| 任務 | 負責 Agent | 輸出 |
|------|-----------|------|
| 建立 monorepo 結構 | `repo-architect` | Git 倉庫、目錄結構、BUILD 系統 (CMake/Bazel) |
| 設定交叉編譯環境 | `build-engineer` | MSYS2/VS + LLVM 工具鍊、Docker 編譯映像 |
| 編譯 QEMU + WHPX | `virtualization-agent` | 可執行的 `qemu-system-x86_64.exe` with WHPX |
| 編譯最小 AOSP 映像 | `android-system-agent` | `system.img` + `ramdisk.img` + `kernel` (x86_64) |
| 建立 CI/CD (GitHub Actions) | `devops-agent` | 自動編譯、測試、Release |
| 撰寫開發文件 | `doc-agent` | README、docs/project/BUILD.md、docs/architecture/ARCHITECTURE.md |

**驗收標準**: `qemu-system-x86_64 -accel whpx` 能在 Windows 啟動 Android 並顯示桌面

### Phase 1: MVP — 能跑簡單手遊（預估 6-12 週）

**目標**: 2D 手遊能穩定 60 FPS，支援基礎鍵盤/滑鼠輸入

| 任務 | 負責 Agent | 輸出 |
|------|-----------|------|
| 整合 ANGLE D3D11 | `graphics-agent` | Guest OpenGL ES → Host D3D11，穩定 60 FPS |
| 建立 Host UI 視窗 | `ui-agent` | Qt 6 視窗嵌入 QEMU 的 OpenGL 紋理 |
| 基礎輸入橋接 | `input-agent` | 滑鼠/鍵盤事件 → Guest virtio-input |
| ADB 整合 | `android-agent` | Host ADB server 連線 Guest，可安裝 APK |
| 音訊輸出 | `audio-agent` | Guest 音訊 → Host WASAPI |
| 共享資料夾 | `storage-agent` | Host 路徑掛載到 Guest `/sdcard/shared` |
| 效能調校 | `perf-agent` | CPU 90%+、GPU 60 FPS、記憶體 < 4GB |

**驗收標準**: 
- 能安裝並執行一款 2D 手遊（如 `com.supercell.clashroyale` 或同等複雜度）
- 穩定 60 FPS，輸入延遲 < 50ms
- 佔用記憶體 < 4GB

### Phase 2: 手遊核心功能（預估 12-24 週）

**目標**: 3D 遊戲、輸入映射、手把支援、多開

| 任務 | 負責 Agent | 輸出 |
|------|-----------|------|
| 進階圖形 | `graphics-agent` | Vulkan backend (ANGLE → Vulkan)、ASTC 硬體解碼、高 FPS 模式 |
| 輸入映射系統 | `input-agent` | JSON-based key mapping、Tap/Swipe/DPad/MOBA skill、手把 (XInput/DirectInput) |
| 多開管理器 | `instance-agent` | 建立/複製/刪除 VM 實例、獨立設定與資料 |
| 巨集錄製/播放 | `macro-agent` | 錄製輸入序列、循環播放、條件觸發 |
| 螢幕錄影/截圖 | `media-agent` | FFmpeg 整合、H.264 編碼、GIF/MP4 輸出 |
| 快捷鍵系統 | `input-agent` | 全域熱鍵（全螢幕、截圖、輸入映射編輯器等） |
| 設定系統 | `config-agent` | 圖形化設定面板、conf 檔案、導入/匯出 |

**驗收標準**:
- 能執行 3D 手遊（如 `com.miHoYo.GenshinImpact` 或同等）
- 輸入映射編輯器 UI 完整、可匯出/匯入
- 至少同時執行 2 個實例，各自獨立

### Phase 3: 效能與體驗優化（預估 12-24 週）

**目標**: 對標 BlueStacks 的效能與 UX

| 任務 | 負責 Agent | 輸出 |
|------|-----------|------|
| Hyper-V 直接整合 | `virtualization-agent` | HCS API 管理 VM、GPU-PV 圖形加速 |
| 記憶體去重 (KSM) | `virt-agent` | 多開時共享相同記憶體頁面 |
| 啟動加速 | `virt-agent` | 快照/休眠啟動、預載系統服務 |
| 節能模式 | `power-agent` | 限制 FPS、降低 CPU 優先級 |
| 推送通知橋接 | `integration-agent` | Guest 通知 → Host Windows 通知中心 |
| 定位模擬 | `location-agent` | GPS spoofing、路徑模擬 |
| 剪貼簿共用 | `integration-agent` | Host ↔ Guest 文字/圖片剪貼簿 |
| 搖晃/旋轉模擬 | `input-agent` | 快捷鍵觸發感測器事件 |

**驗收標準**:
- 3D 遊戲 60 FPS 穩定，GPU 利用率 > 80%
- 啟動時間 < 10 秒（從冷啟動到 Android 桌面）
- 4 開同時執行，總記憶體 < 12GB（KSM 生效後）

### Phase 4: 生態系統與進階功能（預估 24-48 週）

**目標**: 建立可持續維護的開源生態

| 任務 | 負責 Agent | 輸出 |
|------|-----------|------|
| 插件系統 | `ext-agent` | C++/Python/JS 插件 API、市集 |
| 雲端存檔（可選） | `cloud-agent` | 自建或支援第三方雲端（非強制） |
| 社群翻譯 | `i18n-agent` | i18n 架構、Weblate 整合 |
| 自動化測試 | `qa-agent` | E2E 測試、效能迴歸測試 |
| 文件與教學 | `doc-agent` | 使用手冊、API 文件、貢獻指南 |
| 安全審計 | `security-agent` | 依賴掃描、程式碼審查、CVE 追蹤 |

---

## 4. AI Agent 分工架構

基於「一堆 AI Agent 不需要人類」的開發模式，設計以下專業 Agent：

```
┌─────────────────────────────────────────────────────────┐
│                  Orchestrator Agent                     │
│              (排程、整合、衝突解決、發布)              │
└─────────────┬─────────────────────────────┬─────────────┘
              │                             │
    ┌─────────▼─────────┐       ┌──────────▼──────────┐
    │  Host Layer Team  │       │   Guest Layer Team  │
    ├───────────────────┤       ├─────────────────────┤
    │ • ui-agent        │       │ • android-system-   │
    │ • input-agent     │       │   agent             │
    │ • graphics-agent  │       │ • graphics-driver-  │
    │ • audio-agent     │       │   agent             │
    │ • media-agent     │       │ • arm-translation-  │
    │ • config-agent    │       │   agent             │
    │ • integration-    │       │ • virtio-device-    │
    │   agent           │       │   agent             │
    └─────────┬─────────┘       └──────────┬──────────┘
              │                            │
    ┌─────────▼─────────┐       ┌──────────▼──────────┐
    │ Virtualization    │       │   Platform/Tooling  │
    │      Team         │       │       Team          │
    ├───────────────────┤       ├─────────────────────┤
    │ • virtualization- │       │ • repo-architect    │
    │   agent           │       │ • build-engineer    │
    │ • instance-agent  │       │ • devops-agent      │
    │ • perf-agent      │       │ • doc-agent         │
    │ • power-agent     │       │ • security-agent    │
    │ • virt-agent      │       │ • qa-agent          │
    └───────────────────┘       └─────────────────────┘
```

### Agent 協作模式

1. **介面先行**: 每個 Agent 在實作前先定義 API 介面（header/CIDL/protobuf），由 Orchestrator 審查
2. **契約測試**: Agent 之間透過 gRPC/IPC 通訊，先寫 stub 與 mock，再並行開發
3. **每日整合**: Orchestrator 每日執行建置、單元測試、整合測試，失敗時自動派工修復
4. **文件即程式碼**: 每個 Agent 的設計文件與程式碼同步更新，存放在 `docs/agents/<agent-name>/`

---

## 5. 風險評估與緩解

### 5.1 技術風險

| 風險 | 影響 | 機率 | 緩解措施 |
|------|------|------|----------|
| QEMU + WHPX 圖形效能不足 | 高 | 中 | 優先實作 ANGLE D3D11；長期遷移至 HCS + GPU-PV |
| AOSP 編譯失敗或過慢 | 高 | 中 | 使用預編譯映像起步；Docker 化編譯環境；ccache |
| ARM App 相容性差 | 高 | 高 | 整合 libndk_translation；建立相容性資料庫 |
| Qt 授權衝突 (LGPL) | 中 | 低 | 動態連結 Qt；提供 object files 供重新連結 |
| 輸入延遲過高 | 高 | 中 | 繞過 QEMU input 層，直接使用 virtio-input；減少緩衝區 |
| 多開記憶體爆炸 | 中 | 高 | KSM / 記憶體氣球；快照共用；限制每實例 RAM |

### 5.2 法律風險

| 風險 | 影響 | 緩解 |
|------|------|------|
| BlueStacks EULA 禁止逆向 | 法律訴訟 | 本計畫**僅參考功能清單與公開設定檔格式**，不直接使用反編譯程式碼 |
| Oracle VirtualBox 授權 | 授權污染 | **不使用 VirtualBox 任何程式碼**，改用 QEMU |
| GPL 授權污染 | 強制開源 | QEMU 層以獨立程序執行，透過 IPC 與 Apache 2.0 核心通訊 |
| 專利風險（Intel HAXM、GPU-PV） | 專利侵權 | 僅使用 Microsoft 公開 API（WHPX/HCS），不繞過授權 |

### 5.3 專案風險

| 風險 | 緩解 |
|------|------|
| AI Agent 產出品質不穩定 | 強制 TDD；每個 PR 需通過單元測試 + 整合測試 |
| 範圍蔓延（想做太多功能） | 嚴格遵守 Phase 計畫，每 Phase 結束才評估下一階段 |
| 開發者流失（AI 無法持續） | 極致文件化；每個模組有獨立設計文件；降低上下文依賴 |

---

## 6. 時間預估（AI Agent 全自動開發）

> 假設：每個 Agent 24/7 執行，人類僅做最終審查與關鍵決策。

| 階段 | 預估時間 | 關鍵里程碑 |
|------|---------|-----------|
| Phase 0 | 2-4 週 | QEMU+WHPX 能啟動 Android 桌面 |
| Phase 1 | 6-12 週 | 2D 手遊 60 FPS |
| Phase 2 | 12-24 週 | 3D 手遊 + 輸入映射 + 多開 |
| Phase 3 | 12-24 週 | GPU-PV + KSM + <10s 啟動 |
| Phase 4 | 24-48 週 | 插件系統 + 生態完整 |
| **總計** | **~1-2 年** | **對標 BlueStacks 核心功能** |

### 6.1 與 BlueStacks 開發歷史比較

- BlueStacks 1.0: 2011 年發布，約 30 人團隊，開發 2 年
- BlueStacks 5: 2021 年發布，約 200 人團隊，累積 10 年
- **Project Chimera**: 1-2 年達到 BlueStacks 3~4 代水準（AI + 開源元件 + 已有知識）

---

## 7. 目錄結構規劃

```
chimera/
├── .github/              # CI/CD, Issue templates
├── docs/                 # 設計文件、API 文件、架構圖
│   ├── architecture/
│   ├── agents/           # 每個 AI Agent 的設計文件
│   └── api/
├── src/
│   ├── host/             # Windows Host 層
│   │   ├── ui/           # Qt 6 UI (QML + C++)
│   │   ├── input/        # 輸入橋接與映射
│   │   ├── graphics/     # Host 圖形渲染與轉譯
│   │   ├── audio/        # 音訊橋接
│   │   ├── storage/      # 共享資料夾
│   │   ├── instance/     # 多開管理器
│   │   ├── config/       # 設定系統
│   │   └── integration/  # Windows 整合（通知、剪貼簿等）
│   ├── virtualization/   # QEMU + WHPX 整合
│   │   ├── qemu/         # QEMU 子模組 (fork from AOSP)
│   │   ├── whpx/         # WHPX 加速模組封裝
│   │   └── devices/      # 自訂 virtio 裝置
│   ├── guest/            # Android Guest 層
│   │   ├── aosp/         # AOSP 編譯腳本與 patch
│   │   ├── kernel/       # 核心配置與模組
│   │   ├── hal/          # 自訂 HAL（顯示、輸入、音訊）
│   │   └── arm-compat/   # libndk_translation 整合
│   └── common/           # 共用函式庫
│       ├── protocol/     # gRPC / IPC 協定定義
│       └── utils/        # 工具函式
├── tests/                # 測試程式碼
│   ├── unit/
│   ├── integration/
│   └── e2e/
├── tools/                # 開發工具
│   ├── build/
│   ├── packager/
│   └── installer/
├── third_party/          # 第三方開源元件
│   ├── angle/
│   ├── ffmpeg/
│   └── ...
├── configs/              # 預設設定檔
└── scripts/              # 自動化腳本
```

---

## 8. 下一步行動

若核准此計畫，Phase 0 立即開始：

1. **建立 GitHub Organization**: `chimera-emulator`
2. **初始化倉庫**: 上述目錄結構 + LICENSE (Apache 2.0) + README
3. **並行啟動 4 個 Agent**:
   - `repo-architect`: 建立 monorepo 與 BUILD 系統
   - `virtualization-agent`: 編譯 QEMU + WHPX
   - `android-system-agent`: 編譯最小 AOSP x86_64 映像
   - `build-engineer`: 建立 CI/CD 與 Docker 編譯環境
4. **每週審查會議**: 檢視進度、調整優先級、解決阻塞

---

## 9. 附錄：參考資料

### 9.1 BlueStacks 架構分析
- 來源: `D:\Workspace_cloud\Personal_Project\Reverse_engineering\BlueStacks_nxt`
- 虛擬化: VirtualBox 6.1.36 + NEM (WinHvPlatform)
- 圖形: AGA engine + Vulkan (`vlcn`)
- 網路: virtio-net NAT, ADB port 5555
- 儲存: VDI/VHD/VHDX (IDE/SATA AHCI)

### 9.2 開源基礎專案
- [AOSP Emulator QEMU](https://android.googlesource.com/platform/external/qemu/)
- [Android-x86 Project](https://www.android-x86.org/)
- [ANGLE](https://github.com/google/angle)
- [SwiftShader](https://github.com/google/swiftshader)
- [redroid](https://github.com/remote-android/redroid)
- [libndk_translation](https://android.googlesource.com/device/generic/vulkan-cereal/)

### 9.3 技術文件
- [WHPX API Reference](https://docs.microsoft.com/en-us/virtualization/api/)
- [QEMU WHPX 實作](https://qemu.readthedocs.io/en/latest/system/accelerators.html)
- [AOSP Build System](https://source.android.com/docs/setup/build)

---

*本計畫由 AI Agent 自動分析與撰寫，基於 BlueStacks 靜態設定檔與公開開源專案資訊。*
