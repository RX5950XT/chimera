# Windows 上執行 Android 的虛擬化技術可行性分析

## 摘要

在 Windows 上建構開源 Android 模擬器，最可行的虛擬化路徑依優先順序為：

1. **QEMU + WHPX** — 最適合開源專案，但有效能與功能限制
2. **Hyper-V 直接整合 (HCS/WHP API)** — 效能最佳，但僅限 Windows Pro/Enterprise
3. **WSL2 + 自編譯核心** — 容器化輕量，但需要核心模組支援與諸多 workaround
4. **VirtualBox** — 授權複雜且與 Hyper-V 共存有問題，不推薦作為新專案基礎
5. **WSA 架構** — 已終止服務，技術參考價值大於實作價值

---

## 1. QEMU + WHPX（Windows Hypervisor Platform）

### 技術架構

Windows Hypervisor Platform (WHPX) 是 Microsoft 從 Windows 10 April 2018 Update (1803) 開始提供的 user-mode API。它允許第三方虛擬化堆疊（如 QEMU）直接呼叫 Hyper-V hypervisor 來建立與管理 VM partition，而無需將 Hyper-V 的虛擬化堆疊整個取代。

QEMU 啟動參數範例：
```bash
qemu-system-x86_64 -accel whpx -m 4096 -smp 4 ...
```

### WHPX API 能力與限制

| 能力 | 支援狀態 | 說明 |
|------|---------|------|
| VM 建立與記憶體配置 | 支援 | 基礎 partition 管理 |
| vCPU 建立與執行控制 | 支援 | 可建立多個 virtual processor |
| MMIO/Port IO 攔截 | 支援 | 透過 WHvSetPartitionProperty 設定 |
| 中斷注入 | 支援 | APIC、X2APIC 基礎支援 |
| Nested Virtualization | **不支援** | 這是最大限制；無法在 WHPX VM 內再執行 KVM/HAXM |
| GPU Passthrough | 不支援 | WHPX 無 expose DDA (Discrete Device Assignment) API |
| Live Migration | 不支援 | 僅 Hyper-V 原生 WMI 堆疊支援 |
|記憶體氣球/熱插拔 | 有限 | 需要自行管理，無高階抽象 |

### 與 Hyper-V 的關係

WHPX 本質上是 **Hyper-V 的「後門」**。當 QEMU 使用 WHPX 時，它並不是繞過 Hyper-V，而是成為 Hyper-V 的「另一個客戶」。這意味著：

- 可以與 Windows 內建的 Hyper-V VM（如 WSL2、Docker Desktop）**同時執行**
- 底層排程、記憶體管理、中斷處理仍由 Hyper-V 控制
- 效能會略低於直接存取 VMX root mode 的方案（如 HAXM、KVM）

### QEMU 在 Windows 上的效能

- **CPU 效能**：接近原生（90-95%），因為使用硬體虛擬化輔助
- **I/O 效能**：磁碟與網路為主要瓶頸，建議使用 virtio-blk/virtio-net
- **圖形效能**：無 GPU passthrough，僅能使用軟體渲染（llvmpipe）或 VirGL（有限支援）
- **與 Linux KVM 比較**：整體慢 10-20%，主要差距在 I/O 路徑與中斷延遲

### 適用場景評估

- ✅ 開源專案可完全基於 QEMU（GPLv2）+ WHPX（Windows 內建）
- ✅ 與 WSL2、Docker Desktop、BlueStacks Hyper-V 版共存
- ❌ 無法支援 nested virt（不能在模擬器內再開 KVM）
- ❌ 圖形效能受限於軟體渲染或 VirGL

---

## 2. Hyper-V 直接整合

### BlueStacks 的實作參考

從 `bluestacks.conf` 可觀察到關鍵設定：

```ini
bst.status.hypervisor="hyperv"
bst.instance.Pie64.graphics_engine="aga"
bst.instance.Pie64.graphics_renderer="vlcn"
bst.status.raw_mode="0"
bst.force_hyperv_elevation="0"
```

這顯示 BlueStacks 的 Hyper-V 版並非透過 QEMU/WHPX，而是直接使用 **Hyper-V 原生虛擬化堆疊**：

- 建立 Hyper-V Generation 1/2 VM
- 圖形透過自研 AGA (Advanced Graphics Architecture) 引擎 + Vulkan renderer (`vlcn`) 輸出
- 使用虛擬化 GPU 技術（類似 GPU-PV）將渲染指令轉發至 host GPU

### 開源專案能否直接使用 Hyper-V

可以，有三個層級的 API：

1. **WHP API**（Windows Hypervisor Platform）
   - 最低階，類似 KVM API
   - 需要自己實作完整設備模擬（如 QEMU 所做的）

2. **HCS API**（Host Compute System）
   - 中階 API，用於管理 VM 與容器生命週期
   - Windows 內建 Docker Desktop、WSL2 皆使用此 API
   - 可建立輕量 VM，但彈性不如 WHP

3. **WMI Provider for Hyper-V**
   - 最高階，接近 Hyper-V Manager 的功能
   - 適合管理傳統 VM，但不適合建構模擬器

### 授權與限制

| 項目 | 說明 |
|------|------|
| 作業系統需求 | Windows 10/11 **Pro、Enterprise 或 Education**；Home 版無法啟用 Hyper-V |
| 授權費用 | 無額外費用，包含於 Windows 授權中 |
| 硬體需求 | 64-bit CPU + SLAT (EPT/NPT) + VM Monitor Mode + DEP |
| API 授權 | WHP/HCS 為 Windows SDK 一部分，可自由用於商業/開源軟體 |
| 核心限制 | 無法在 Hyper-V VM 內使用 Intel HAXM / KVM（nested virtualization 支援有限） |

### 適用場景評估

- ✅ 效能最佳，可接近 BlueStacks 等商業方案
- ✅ 可透過 GPU-PV 達成較好的圖形效能
- ❌ 僅限 Windows Pro/Enterprise，Home 版用戶無法使用
- ❌ 需要自行實作大量設備模擬或整合 HCS

---

## 3. VirtualBox / VMware

### VirtualBox 授權分析

VirtualBox 採用 **雙重授權** 模式：

| 元件 | 授權 | 限制 |
|------|------|------|
| Base Package（含 Guest Additions） | GPLv3 | 可自由修改、散布，但修改後也必須開源 |
| Extension Pack | PUEL（Personal Use and Educational License） | 個人/教育免費；**商業使用需購買 Enterprise License** |

**重點**：Extension Pack 包含 USB 2.0/3.0、RDP、NVMe、PCIe 等關鍵功能。若開源 Android 模擬器需要這些功能，則用戶必須自行安裝 Extension Pack，或專案本身無法直接 bundle。

### 與 Hyper-V 的共存問題

- VirtualBox 6.0 以前：與 Hyper-V **互斥**，必須關閉 Hyper-V 才能使用
- VirtualBox 6.1+：可透過 `--nested-hw-virt` 或設定 **Hyper-V 作為後端** 共存，但效能大幅下降
- 在 Hyper-V 已啟用的 Windows 系統上，VirtualBox 會自動嘗試使用 WHPX 作為加速層，結果等同於 QEMU+WHPX，且多了一層抽象

### 效能與硬體加速

- **VT-x/AMD-V**：支援，但受限於上述共存問題
- **Nested Paging (SLAT)**：支援
- **GPU 加速**：不支援 GPU passthrough；僅有基礎 3D 加速（DirectX/OpenGL 轉譯）

### 適用場景評估

- ❌ **不推薦**作為新開源專案的基礎：授權複雜、與 Hyper-V 衝突、Oracle 商業策略不透明
- ✅ 僅適合個人使用或已有 VirtualBox 基礎設施的維護專案

---

## 4. Windows Subsystem for Android (WSA)

### 技術架構

WSA 已於 **2025 年 3 月 5 日** 終止支援，Microsoft 已從 Microsoft Store 下架。其 GitHub 倉庫 (`microsoft/WSA`) 也已封存 (archived)。

WSA 的核心架構：

1. **底層虛擬化**：Hyper-V VM（透過 HCS API 管理）
2. **Guest OS**：精簡化 AOSP（Android 13 為最終版本）
3. **ABI 轉譯**：**Intel Bridge Technology** — 將 ARM64 指令轉譯為 x86-64
4. **圖形**：透過 virtio-gpu 或類似機制與 host GPU 溝通
5. **整合層**：與 Windows 11 視窗管理、輸入法、通知系統深度整合

### Intel Bridge Technology 與開源替代

Intel Bridge Technology 是 **專有閉源軟體**，無開源版本。其開源替代方案包括：

| 方案 | 授權 | 效能 | 相容性 |
|------|------|------|--------|
| **libndk_translation** | Apache 2.0 | 中 | ARM64→x86_64；Google 官方 NDK 翻譯庫 |
| **libhoudini** | 專有 | 高 | Intel 為 Android x86 提供的閉源方案 |
| **QEMU user-mode** | GPLv2 | 低 | 可執行但效能極差 |
| **FEX-Emu** | MIT | 中-高 | Linux 為主，Windows 支援實驗中 |
| **box64** | MIT | 中 | 主要針對 Linux，Android 支援有限 |

> redroid 專案已將 `libndk_translation` 整合進預設映像，是目前開源方案中最實用的 ARM→x86 轉譯層。

### 技術遺產

WSA 證明了以下技術路徑可行：
- 在 Hyper-V 輕量 VM 中執行完整 AOSP
- 透過 virtio 實現可接受的 GPU 加速
- 透過指令轉譯層執行 ARM-only App

但 WSA 本身**無法直接 fork**，因為其原始碼未開源，僅有 AOSP 部分可參考。

---

## 5. 容器方案：WSL2 執行 Android

### redroid (Remote Android) 現況

redroid 是目前最成熟的開源 **Android-in-Container** 方案，支援 Android 8.1 ~ 16，可在 Linux host 透過 Docker/Podman 執行。

核心需求：
```bash
modprobe binder_linux devices="binder,hwbinder,vndbinder"
modprobe ashmem_linux
```

### WSL2 的挑戰

WSL2 本身是一個輕量 Hyper-V VM，執行 Microsoft 客製化 Linux 核心。要在 WSL2 中執行 redroid，必須解決：

#### 5.1 核心模組支援

| 模組 | WSL2 預設核心 | 需求 |
|------|-------------|------|
| `binder_linux` | ❌ 未編入 | 必須自行編譯 WSL2 核心 |
| `ashmem_linux` | ❌ 未編入 | 可改用 `memfd` 替代（redroid 已支援 `androidboot.use_memfd=1`） |
| `virtio-gpu` | ✅ 支援 | 用於 GPU 加速 |
| `kvm` | ❌ 不支援 | WSL2 本身已是 VM，無 nested virt |

**結論**：WSL2 預設核心**無法直接執行 redroid**。必須：
1. 下載 WSL2 核心原始碼（Microsoft 有開源）
2. 啟用 `CONFIG_ANDROID_BINDER_IPC` 等選項
3. 替換 WSL2 核心 (`wsl.conf` 設定 `kernel=`)

#### 5.2 GPU 加速

- WSL2 支援 **GPU 透傳**（透過 DXGI/DirectML）
- redroid 的 `androidboot.redroid_gpu_mode=host` 在 WSL2 中**理論可行**，但實際上：
  - WSL2 的 GPU 驅動是 DirectX 12 轉譯層，非標準 Linux DRM/KMS
  - redroid 的 mesa/virgl 路徑可能無法正確識別 WSL2 的 GPU 節點
  - 實測社群回報：多數情況下只能使用軟體渲染 (`guest` mode)

#### 5.3 binder 在 WSL2 中的限制

即使自行編譯核心啟用 binder，仍可能遇到：
- binder 的 `/dev/binder` 節點權限與 SELinux 衝突
- WSL2 與 Windows host 的網路橋接導致 ADB 連線問題
- 檔案系統效能（9pfs / drvfs）影響 Android 應用載入速度

### 適用場景評估

- ✅ 容器化最輕量，啟動速度遠快於完整 VM
- ❌ WSL2 需要核心編譯，門檻高
- ❌ GPU 加速不穩定
- ⚠️ 適合開發者/CI 場景，不適合遊戲或 GPU 密集應用

---

## 6. 硬體需求總結

| 需求 | QEMU+WHPX | Hyper-V 直接 | VirtualBox | WSL2+redroid |
|------|-----------|--------------|------------|--------------|
| **CPU VT-x / AMD-V** | 必要 | 必要 | 必要 | 必要 |
| **SLAT (EPT/NPT)** | 必要 | 必要 | 建議 | 建議 |
| **VM Monitor Mode** | 必要 | 必要 | 必要 | 不必要 |
| **DEP (XD/NX bit)** | 必要 | 必要 | 必要 | 必要 |
| **記憶體最低** | 4 GB | 4 GB | 4 GB | 4 GB |
| **記憶體建議** | 8 GB+ | 8 GB+ | 8 GB+ | 8 GB+ |
| **GPU Passthrough** | 不支援 | 支援 (GPU-PV) | 不支援 | 有限支援 |
| **Nested Virtualization** | 不支援 | 有限 | 有限 | 不支援 |
| **Windows 版本** | Pro/Edu+ | Pro/Edu+ | Home/Pro+ | Pro/Edu+ (建議) |

### GPU Passthrough 細節

若目標是遊戲或高圖形效能，Hyper-V 的 **GPU Partitioning (GPU-PV)** 是目前 Windows 上唯一可行的開源方案基礎：

- 需要 **Windows 11** 與支援的 GPU（Intel Xe、AMD RDNA2+、NVIDIA Ampere+）
- 透過 HCS API 或 PowerShell `Set-VMPartitionableGpu` 設定
- 開源專案可透過 HCS API 建立帶 GPU-PV 的 VM，再於其中執行 AOSP + mesa/turnip

---

## 7. 推薦技術路徑

### 場景 A：開源 Android 模擬器（類似 BlueStacks 替代品）

**推薦架構**：
```
Windows Host
  └── Hyper-V (WHP API / HCS API)
        └── 輕量 Android VM (AOSP x86_64)
              ├── GPU-PV (若可用) 或 VirtIO-GPU
              ├── libndk_translation (ARM→x86 轉譯)
              └── virtio-input / virtio-snd (輸入/音訊)
```

**理由**：
- 與 WSL2、Docker Desktop、BlueStacks 共存
- 可達接近商業方案的 CPU 效能
- GPU-PV 提供最佳圖形效能
- 完全基於開源 AOSP + 標準 API

### 場景 B：輕量開發/自動化測試

**推薦架構**：
```
Windows Host
  └── WSL2 (自編譯核心含 binder)
        └── Docker
              └── redroid (x86_64 + libndk_translation)
```

**理由**：
- 啟動快速，資源佔用低
- 適合 CI/CD 與自動化測試
- 透過 ADB 可無縫整合現有 Android 開發流程

### 場景 C：x86_64 Home 版用戶

**唯一選項**：
- 關閉 Hyper-V → 使用 QEMU + TCG（純軟體模擬，效能極差）
- 或升級至 Windows Pro
- 或使用 Android Studio Emulator（底層也是 QEMU + WHPX/HAXM，但 HAXM 已淘汰）

> 注意：Intel HAXM 已於 2023 年停止維護，不支援 Windows 11 與新 CPU。QEMU + WHPX 是 HAXM 的事實繼任者。

---

## 8. 結論

| 方案 | 開源可行性 | 效能 | 圖形 | 易用性 | 推薦度 |
|------|-----------|------|------|--------|--------|
| QEMU + WHPX | 高 | 中-高 | 低-中 | 中 | ⭐⭐⭐⭐ |
| Hyper-V 直接 (HCS/WHP) | 高 | 高 | 高 | 低 | ⭐⭐⭐⭐⭐ |
| VirtualBox | 中（授權複雜） | 中 | 低 | 中 | ⭐⭐ |
| WSL2 + redroid | 高 | 中 | 低 | 低 | ⭐⭐⭐ |
| WSA (已終止) | N/A | 中 | 中 | 高 | 已終止 |

**最終建議**：

若目標是建立**開源 Windows Android 模擬器**與 BlueStacks/LDPlayer 競爭：

1. **基於 AOSP + QEMU + WHPX** 是最快起步的路徑，利用現有 Android Emulator 的大部分基礎設施
2. 長期應該遷移至 **HCS API + GPU-PV**，以達到商業級圖形效能
3. **避免依賴 VirtualBox**，授權與共存問題會持續困擾用戶
4. 對於 ARM App 相容性，整合 **libndk_translation** 是目前最穩健的開源方案
