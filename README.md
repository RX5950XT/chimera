# Project Chimera

Chimera 是一個 Windows Android 模擬器實驗專案。**即使近期修掉了幾個嚴重 bug，它目前仍然不能當作真正可投入日常使用的模擬器**——更像是拿 Android Emulator / QEMU / gfxstream / Qt host shell 來測試 AI 輔助開發、圖形管線改造與自動化驗證的「整活」專案。

請不要把它當成 BlueStacks、LDPlayer、MuMu 這類成熟產品的替代品。它可以開機、能看到 Android、點擊有反應、放置一段時間後回來也還能用，比早期版本穩定不少；但它只在一台開發機、一組人工設計的測試場景下驗證過，沒有經過真實日常使用的長期考驗，穩定性、相容性與邊界情況隨時可能出問題。

---

## 目前定位

- **用途**：AI 開發實驗、Windows host / Android guest 整合研究、gfxstream/shared texture 測試、娛樂性整活。
- **不適合**：日常手遊、帳號登入、長時間掛機、正式 benchmark、可靠工作流、任何需要穩定性的用途。
- **現況**：能開機、能點、開機後放著一段時間再回來也還有反應（過往「點不動」的頭號 bug 已根治並驗證），但這只代表「目前已知的測試場景不再壞」，不代表「已經是可靠產品」。沒被測過的使用方式仍可能踩到新問題。

---

## 已知最大問題（含已修復項目的誠實現況）

### 1.（已修復，但只驗證過測試場景）「畫面有、看似點不動」

真根因是 gfxstream 內部 ColorBuffer 生命週期管理與實際執行環境對不上：guest 靜止超過約 10 秒後，顯示用的 buffer 會被錯誤回收，導致畫面停在舊幀，看起來像點擊沒反應（實際上輸入有送達，只是畫面沒更新）。

已修正根因並用「開機→放置一段時間→操作」的重現腳本做過 A/B 對照驗證，另外還抓到並修掉兩個關聯問題：Quick Boot 存檔在特定情境下會存到不相容的狀態、以及某條路徑下「關閉視窗」不會正確存檔。目前也保留一層自動偵測與挽救機制作為保險。**但這些驗證都是在單一開發機、由固定腳本操作**，不代表涵蓋所有使用者的真實使用模式；仍可能有沒測到的情境會重現類似問題。

### 2.（已改善）啟動速度

預設改用 Quick Boot 存檔開機，多數情況下幾秒內就能進入畫面；但第一次開機、或變更執行設定後的下一次開機，仍會是數十秒等級的完整開機。

### 3. `-Fast` 顯示路徑仍是實驗性質

`-Fast` 使用自訂 gfxstream runtime + D3D11 shared texture。這條路徑是本專案最有趣的部分，也是最容易出問題的部分：雖然已知的停更根因修掉了，但這條自訂管線本質上仍在持續修改中，沒有到「穩定不動」的程度。

### 4. 一般介面操作還無法穩定滿 60fps

捲動、切換 App 這類日常操作目前約在 50 上下 fps，會受背景省電/音訊優先權設定影響；要換取更順的畫面可以手動切到較高優先權模式，但那會增加音訊卡頓機率。這是取捨，不是單純的性能瓶頸還沒修。

### 5. benchmark／自動化測試結果不能當真實產品能力保證

歷史上曾多次把「計數器看起來正常」誤判成「可見畫面真的正常」，也曾把單一 bug 誤判成另一個 bug 修好、又反覆復發。現在所有性能數字與「已修復」的說法都附有具體驗證方式，但這些驗證終究是開發者在單機上設計的測試，**不是大規模真實使用的保證**。

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

*Project Chimera — AI-assisted Android emulator experiment. 已修掉多個嚴重穩定性 bug，但仍未經真實日常使用驗證，不是可投入使用的正式產品。*
