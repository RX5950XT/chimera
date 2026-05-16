# Project Chimera Code Review

**日期**: 2026-05-14  
**範圍**: `src/common/`, `src/host/`, `tests/`, `scripts/`, CMake 與 Markdown 文件。`src/virtualization/qemu/` 視為上游/移植來源，只檢查整合邊界。  
**驗證**: Release build 成功，6/6 Qt unit tests passed。

## 已修正

| 類型 | 檔案 | 問題 | 修正 |
|------|------|------|------|
| Deadlock | `src/host/graphics/Framebuffer.cpp` | `writeBackBuffer()` 持有 `m_mutex` 時呼叫 `resize()`，解析度變更會重入同一把 mutex 卡死 | 改為在同一個 critical section 內直接 resize front/back buffer，新增 `test-graphics-framebuffer` 回歸測試 |
| Persistence | `src/host/instance/InstanceManager.cpp` | `listInstances()` / `getInstanceConfig()` 只看 live VM，重啟後從 `configs/instances.json` 載入的 instance 不可見 | 合併 saved configs 與 live VMs；saved-only instance 回報 `Stopped`，`startInstance()` 可先 materialize VM 再啟動 |
| Undefined behavior | `src/host/instance/InstanceManager.cpp` | `deleteInstance()` erase 後又比較失效 iterator | 改用 `liveRemoved` 布林值保存刪除結果 |
| Reconnect | `src/host/input/QmpInput.cpp` | `setAutoReconnect(true)` 只設定 interval，連線失敗或 socket error 時沒有啟動 timer | 失敗、error、非手動斷線時啟動 reconnect timer，並同步處理初始 QMP greeting |
| Thread lifecycle | `src/host/input/MacroEngine.cpp` | 重複 `startPlayback()` 可能覆寫 joinable thread 造成 `std::terminate` | 播放前先停止既有 playback；destructor 自動停止 |
| Path traversal | `src/host/input/InputMapper.cpp`, `src/host/input/MacroEngine.cpp` | scheme/macro 名稱可用 `../` 寫出設定目錄 | 限制名稱為 `[A-Za-z0-9_.-]`，新增 unsafe scheme 測試 |
| Build hygiene | `src/host/ui/main.cpp` | include `main.moc` 但檔案沒有 MOC macro，產生 AutoMoc warning | 移除多餘 include |

## 仍需追蹤

| 優先級 | 檔案 | 問題 |
|--------|------|------|
| High | `src/host/input/QmpInput.cpp` | Mouse QMP event schema 仍需用實機/模擬器驗證，`sendMouseButton()` 目前忽略 button/x/y，實際 click 行為可能不完整 |
| High | `src/host/graphics/Framebuffer.h` | `readFrontBuffer()` 回傳內部 reference，沒有讀取鎖；若 producer/consumer 跨執行緒同時 swap/read，仍有 data race 風險 |
| Medium | `src/host/instance/ProcessLauncher.cpp` | command-line quoting 只包雙引號，未完整處理引號與反斜線 escape；目前參數來源多為內部設定，風險中等 |
| Medium | `src/host/integration/ClipboardBridge.cpp` | 使用 `CF_TEXT`，非 Unicode；中文剪貼簿會失真，應改 `CF_UNICODETEXT` |
| Medium | `src/host/ui/main.cpp` | 啟動 UI 會自動刪除並重建 `chimera_dev` instance，不適合正式多開管理流程 |

## 文件修正方向

- `docs/project/BUILD.md`: 從 Phase 0 改為目前已驗證的 MSVC + Qt 6.8.3 + 6/6 tests。
- `docs/architecture/ARCHITECTURE.md`: 移除 Ninja/Catch2/docs agents 等與實際不符內容，改為 Visual Studio generator 與 Qt Test。
- `docs/project/STATUS.md` / `CLAUDE.md` / `AGENTS.md`: 同步 2026-05-14 審查結果與目前已知限制。
