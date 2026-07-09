# Project Chimera

Chimera 是一個 Windows Android 模擬器實驗專案。它目前**不能當作真正可投入日常使用的模擬器**，更像是拿 Android Emulator / QEMU / gfxstream / Qt host shell 來測試 AI 輔助開發、圖形管線改造與自動化驗證的「整活」專案。

請不要把它當成 BlueStacks、LDPlayer、MuMu 這類成熟產品的替代品。它可以開機、能看到 Android，也有不少功能雛形；但穩定性、輸入、顯示、啟動速度與長時間使用都還不可靠。

---

## 目前定位

- **用途**：AI 開發實驗、Windows host / Android guest 整合研究、gfxstream/shared texture 測試、娛樂性整活。
- **不適合**：日常手遊、帳號登入、長時間掛機、正式 benchmark、可靠工作流、任何需要穩定性的用途。
- **現況**：能跑，但經常需要除錯；最近仍出現「畫面有、看似點不動」這類阻斷使用的問題。

---

## 已知最大問題

### 1. 顯示會停在舊畫面

最近一次實測確認：使用者覺得「完全點不動」時，輸入其實已經送進 guest kernel，`getevent` 能看到 multitouch event；真正壞的是 `-Fast` shared-texture 顯示 producer 停止發佈新幀，host 視窗停在舊畫面，所以看起來像點擊沒反應。

目前已加 gRPC unary capture 作為 shared-texture 停更時的保命 fallback，但這是穩定性補丁，不代表顯示管線已成熟。

### 2. 啟動慢

冷開機通常仍是數十秒等級。Quick Boot snapshot 有做過，但不是預設保證路徑；snapshot、ADB、AVD 狀態都可能讓啟動時間波動。

### 3. `-Fast` 不是可靠產品路徑

`-Fast` 使用自訂 gfxstream runtime + D3D11 shared texture。這條路徑是本專案最有趣的部分，也是最不穩的部分：效能、畫面更新、ColorBuffer lifecycle、fallback 都還在修。

### 4. benchmark 結果不能當真實產品能力

歷史上曾多次把「計數器看起來正常」誤判成「可見畫面真的正常」。現在所有性能數字都只能當開發診斷，不代表實際可用體驗。

---

## 一鍵啟動

```bat
start-chimera.cmd
```

等同執行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Fast
```

常用參數：

```powershell
# 預設實驗快路徑：自訂 gfxstream shared texture + fallback
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Fast

# 保守低 FPS 診斷路徑：stock gRPC 顯示
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Stock

# 自測：啟動、截圖、互動、清理
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Fast -SelfTest
```

---

## 建置

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -LE integration
```

自訂 gfxstream runtime：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-chimera-gfxstream-runtime.ps1
```

---

## 架構簡述

```text
Windows Qt/QML host
  ├─ InputBridge：mouse/key/touch → emulator gRPC，必要時退 ADB
  ├─ Display：gfxstream D3D11 shared texture；保命 fallback 為 gRPC screenshot
  ├─ Audio：WASAPI / emulator audio
  └─ Instance：Android Emulator / QEMU / WHPX / AVD lifecycle

Android guest
  └─ Google Play x86_64 image + Chimera Launcher
```

---

## 測試入口

```powershell
ctest --test-dir build -C Release --output-on-failure -LE integration
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -SyntheticScroll
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-true-1080p60.ps1 -RuntimeKind Gfxstream
```

這些測試只能證明特定路徑在當下機器狀態下通過；不能保證一般使用穩定。

---

## 文件索引

| 文件 | 內容 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | AI agent 工作參考、架構決策、已知問題 |
| [CONTEXT.md](CONTEXT.md) | 開發歷程與每次修正紀錄 |
| [AGENTS.md](AGENTS.md) | Build、測試、Git、疑難排解 |
| [docs/STATUS.md](docs/STATUS.md) | 狀態快照 |

---

## 授權

- `src/host/`, `src/common/`：Apache 2.0
- `src/virtualization/qemu/`：GPL v2（QEMU / emulator fork）
- `third_party/`：依各自授權

---

*Project Chimera — AI-assisted Android emulator experiment. Not a production-ready emulator.*
