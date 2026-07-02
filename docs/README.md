# Project Chimera 文件索引

根目錄保留協作入口：`README.md`、`AGENTS.md`（工作流/標準/疑難排解）、`CLAUDE.md`（現況/決策/flags）、`CONTEXT.md`（開發歷程）。`docs/` 只放長期參考文件：

| 文件 | 用途 |
|------|------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | 願景、模組分層、通訊流、技術選型、法律/授權邊界（含原 PLAN.md 參考內容） |
| [BUILD.md](BUILD.md) | 環境安裝、建置、測試、執行與貢獻流程（含原 CONTRIBUTING.md） |
| [STATUS.md](STATUS.md) | 目前狀態快照、誠實邊界、驗證入口 |
| [ADR-001-shared-folder.md](ADR-001-shared-folder.md) | SharedFolder 技術選型 ADR（ADB Downloads sync v1） |

## References（外部研究/逆向參考）

| 文件 | 用途 |
|------|------|
| [windows_android_virtualization_analysis.md](references/windows_android_virtualization_analysis.md) | Windows Android 虛擬化分析 |
| [bluestacks.conf](references/bluestacks.conf) | BlueStacks 設定格式參考 |
| [bluestacks_runtime_findings.md](references/bluestacks_runtime_findings.md) | BlueStacks 設定/日誌可借鏡策略摘要 |
| [competitor-emulator-smoothness.md](references/competitor-emulator-smoothness.md) | 競品（BlueStacks/LDPlayer/MuMu）平滑度研究 |

> 歷史文件去向：`PLAN.md` 參考價值內容併入 ARCHITECTURE.md；`CONTRIBUTING.md` 併入 BUILD.md；`HANDOVER.md`/`CODE_REVIEW.md` 已被 `CONTEXT.md`（歷程）與 `tasks/lessons.md`（教訓）取代並刪除（追蹤項全數已解決，見 git history）。
