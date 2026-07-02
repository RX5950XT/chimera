# Project Chimera — Status Report

> 目前狀態快照。歷程與根因記錄見 `CONTEXT.md`；架構決策與 feature flags 見 `CLAUDE.md`。

**日期**：2026-07-02（Session 101）
**Build**：Release PASS（MSVC + Qt 6.8.3）
**Tests**：`ctest -LE integration` 23/23 PASS；3 integration tests 需 emulator 運行中

## 現況總覽

- **生產引擎**：`emulator.exe`（Google QEMU+WHPX fork），headless（`-no-window`）強制；`--qemu-backend` / `--hcs-backend` / `--cuttlefish` 為 legacy R&D。
- **BlueStacks parity**：核心功能同等級（boot/input/multi-touch/IME/gamepad/macro/keymap/APK/OBB/GPS/感應器/電池/錄影/截圖/剪貼簿/proxy/網速/device spoofing/multi-instance/audio/快捷鍵等，完整清單見 `CLAUDE.md`）。
- **一鍵啟動**：`start-chimera.cmd` = `start-chimera.ps1 -Fast -InteractiveFirst`（custom gfxstream shared texture + `-feature Vulkan` + normal priority）；`-Stock` 為保守 fallback（gRPC，~4–17 FPS）。
- **啟動時間**：`boot≈33s`、`visible_home≈49s`（Session 100 SelfTest）；boot 期間有 placeholder 不裸黑；Quick Boot snapshot（更快）維持 opt-in。

## 顯示路徑（-Fast，Session 101 修復後）

`postFrameDirectGpu`：GLES 合成內容 `flushFromGl()+invalidateForVk()` GL→VK 同步 → GPU `recordCopy` blit → D3D11 NT shared texture（Vulkan `D3D11_TEXTURE_BIT`+dedicated import、keyed mutex）→ host `GuestDisplay` `AcquireSync(0)==S_OK` → 私有副本取樣。互動 UI 實測有效 **~43 FPS**（host 視窗真實可見，PrintWindow 像素驗證）。

**Session 101 重大更正**：shared texture 從 Session 85 起發佈的一直是零幀（compose 不標 dirty / OPAQUE import 無 aliasing / consumer 缺 AcquireSync 三層疊加）；歷來 gate 只驗 guest ADB screencap + host counters，所以「1080p/60 嚴格 PASS」（S85/89/99）全是零幀 blit 節奏，**不可引用**。三層已修，SelfTest 新增 host 視窗像素 gate（`host_window_nonblack_pct`）。另修 emulator idle 自殺（`-idle-grpc-timeout 300` 已移除）。

## 已知限制（誠實邊界）

| 限制 | 說明 |
|------|------|
| GLES 內容連續渲染不到 60 | SurfaceFlinger 由 SwiftShader-ES（軟體）合成，每幀付 GL readback + VK upload 同步成本；gl60 嚴格 gate 目前不通過 |
| 真 60 待驗 | 需 guest **Vulkan-backed** 內容（zero-copy 直通，不付同步成本），尚未單獨基準 |
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
