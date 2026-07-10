# Project Chimera — Status Report

> 目前狀態快照。歷程與根因記錄見 `CONTEXT.md`；架構決策與 feature flags 見 `CLAUDE.md`。

**日期**：2026-07-10（Session 112）
**Build**：Release PASS（MSVC + Qt 6.8.3）
**Tests**：`ctest -LE integration` **24/24 PASS**（S111 再驗 8.45s）；3 integration tests 需 emulator 運行中
**定位**：目前不是可投入日常使用的產品級模擬器；主要是 AI-assisted Android emulator experiment / gfxstream 與 host-shell 整合整活專案。
**維護**：S112 根治「有畫面但點不動」——真根因＝gfxstream `m_refCountPipeEnabled` 硬編 false 的 RefCountPipe 失衡（guest 靜止 ≥10s 後 display CB 被 delayed-close sweep 銷毀→producer 永久停更）。已 force true（kill switch `CHIMERA_GFXSTREAM_NO_REFCOUNT_PIPE=1`）；同 build A/B 定案（fix 3/3 GREEN、killswitch 2/2 RED）、SelfTest pass、production idle-recovery gate pass。S111 watchdog 保留為 belt-and-suspenders。啟動鈴聲（guest 充電/解鎖音）已於首次開機設定關閉。

## 現況總覽

- **生產引擎**：`emulator.exe`（Google QEMU+WHPX fork），headless（`-no-window`）強制；`--qemu-backend` / `--hcs-backend` / `--cuttlefish` 為 legacy R&D。
- **BlueStacks parity**：核心功能同等級（boot/input/multi-touch/IME/gamepad/macro/keymap/APK/OBB/GPS/感應器/電池/錄影/截圖/剪貼簿/proxy/網速/device spoofing/multi-instance/audio/快捷鍵等，完整清單見 `CLAUDE.md`）。
- **一鍵啟動**：`start-chimera.cmd` = `start-chimera.ps1 -Fast`（custom gfxstream shared texture + `-feature Vulkan`；priority 預設 below_normal 護 host audio，S108 定案）；`-InteractiveFirst` 換最順（normal priority、音訊代價）、`-Stock` 為保守 fallback（gRPC，~4–17 FPS）。
- **啟動時間**：S112 起 Quick Boot 為一鍵預設（AVD default_boot；首次冷開 ~34s＋關閉時自動存檔，之後 **~7.5s**；`-NoQuick` 可回全冷開）。boot 期間有 placeholder 不裸黑。

## 顯示路徑（-Fast，Session 101 修復後）

`postFrameDirectGpu`：GLES 合成內容 `flushFromGl()+invalidateForVk()` GL→VK 同步 → GPU `recordCopy` blit → D3D11 NT shared texture（Vulkan `D3D11_TEXTURE_BIT`+dedicated import、keyed mutex）→ host `GuestDisplay` `AcquireSync(0)==S_OK` → 私有副本取樣。host 視窗真實可見（PrintWindow 像素驗證）；一般 UI 於使用者一鍵配置（normal priority）實測**穩定 ~57–60 FPS**（Session 104；S101 首量 43 FPS 是 verifier 預設 below_normal 假象，見下方穩定 60 列）。

**Session 101 重大更正**：shared texture 從 Session 85 起發佈的一直是零幀（compose 不標 dirty / OPAQUE import 無 aliasing / consumer 缺 AcquireSync 三層疊加）；歷來 gate 只驗 guest ADB screencap + host counters，所以「1080p/60 嚴格 PASS」（S85/89/99）全是零幀 blit 節奏，**不可引用**。三層已修，SelfTest 新增 host 視窗像素 gate（`host_window_nonblack_pct`）。另修 emulator idle 自殺（`-idle-grpc-timeout 300` 已移除）。

## 已知限制（誠實邊界）

| 限制 | 說明 |
|------|------|
| 穩定 60（Session 104 實測更正） | 使用者一鍵配置（`-InteractiveFirst`=normal priority）**已穩定 ~57–60**（`pass-gpu-direct-60`、effMin 54）。逐幀否證 S102「host present 天花板」：normal 下 host 端 1:1 追 guest、consumer 0.1ms、非 vsync 量化。**負載掃描（gl60 heavyIters 0/48/128/256）證 frame pacing 對負載不變**：每級 `guest==stream==render` lockstep、dup/drop=0、effMin≈effAvg——重 GLES 填充只乾淨降穩定幀率（60→13→5.5，SwiftShader CPU-fill floor）不造成 jitter，極端 256 才熔毀停產；Vulkan 遊戲繞過。移除生產 post hot-path 每 4s 診斷 readback（`CHIMERA_GFXSTREAM_DIAG_READBACK` gate，預設 off） |
| 真 rock-solid 60 待驗 | 瓶頸＝GL→VK readback 架構 floor（SwiftShader-CPU-GL↔NVIDIA-VK CPU round-trip）；需 guest **Vulkan-backed** 內容（消 readback，skiavk 牆擋）。144Hz 上 60fps 本質 2.4× pulldown judder，顯示端最平滑＝改 120Hz |
| 畫面糊已修（Session 102） | `QSGSimpleTextureNode` node-level filtering 預設 Nearest（覆寫 per-texture）→ 縮小顯示文字殘缺；改 node `setFiltering(Linear)` + device-pixel rect snap。縮小本質損失細節，完全銳利需 ≥1:1 |
| skiavk UI 切換不可行 | playstore user image 無 root；三路（root restart / boot-prop / `ctl.restart`）全 probe 實證死路，禁止再試（Session 100 定案） |
| stock gRPC 路徑低 FPS | ~4–17 FPS 為 unary `getScreenshot` 本質；僅作 fallback/診斷 |
| host audio 競爭 | PARTIAL — `CHIMERA_INTERACTIVE_PRIORITY` 可調（idle=audio-first / normal=interactive-first）；startup 前 30s Idle |
| boot 時間量測噪音 | Session 101 量到 87s vs Session 100 的 33s；連續 boot 環境噪音，待空機重測 |
| `--cuttlefish` SF crash-loop / ADB TCP | OPEN — legacy R&D，不影響生產路徑 |

## 驗證入口

```powershell
# 一鍵路徑 self-test（boot → 1080p → screenshot 內容 gate → host 視窗像素 gate → 互動 → 清理）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\start-chimera.ps1 -Fast -InteractiveFirst -SelfTest

# 日常互動可用性（真實輸入路徑）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-interactive-ui.ps1 -Mode Fast -GuestVulkan -SyntheticScroll

# 連續渲染 gate（synthetic；S101 後 GLES 內容嚴格 60 不通過，數字須配 host 視窗像素證據）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-true-1080p60.ps1 -WarmupSeconds 15

# Quick Boot smoke
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25
```

量測紀律：任何「可見」宣稱必須含 host 視窗像素證據（`Get-HostWindowPixelStats`）；guest ADB 截圖與 FPS 計數器在顯示鏈斷裂時仍會全綠。verifier 一律走 `ChimeraVerifyCommon.ps1`（free port pair、cmdline-filtered cleanup）；禁止會搶實體滑鼠的測法。
