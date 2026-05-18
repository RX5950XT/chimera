# Project Chimera — AGENTS.md

> Agent 工作指南：如何在此 repo 中正確建置、測試、提交。

## Build System

- **Generator**: CMake + Visual Studio 17 2022（不用 MSYS2 / Ninja）
- **Compiler**: MSVC 19.44+（每個新 terminal 都要先執行 `vcvarsall.bat amd64`）
- **Qt**: 6.8.3 at `C:\Qt\6.8.3\msvc2022_64`
- **Standard**: C++20

```powershell
# 1. 載入 VS 環境（必要）
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

# 2. Configure（已存在 build/ 可略）
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64

# 3. Build
cmake --build build --config Release

# 4. Test（Qt DLL 需在 PATH）
ctest --test-dir build -C Release --output-on-failure -LE integration
```

## Coding Standards

- 不可變資料優先；不直接修改既有物件
- 函數 < 50 行，檔案 < 800 行，巢狀 ≤ 4 層
- 例外不靜默吞掉；邊界回傳結構化錯誤
- 所有外部輸入都驗證（user input、API response、檔案內容）
- 用 RAII 管理資源；header 盡量前向宣告

## Qt / MOC 規則

- **不要定義 `QT_NO_KEYWORDS`** — 會破壞 `signals:` / `slots:` macro
- QObject 子類必須有 `Q_OBJECT`
- 需要 custom paint 用 `QQuickPaintedItem`，不用 `QQuickItem`
- 每個 QTest module 必須是獨立的 `.exe`（不共用 main）

## CMake 規則

- 每個 `src/` 下的模組有自己的 `CMakeLists.txt`
- Library 預設 `STATIC`
- `nlohmann_json` 是 `INTERFACE` library，指向 `third_party/`
- `CHIMERA_BUILD_TESTS=ON`（預設）才建置 tests
- `CHIMERA_BUILD_QT_UI=ON`（預設）才建置 UI

## Testing

- Framework: Qt Test (`QTest`)
- 格式: `QTEST_MAIN(TestClassName)` + `#include "file.moc"` 在檔尾
- 執行: `ctest --test-dir build -C Release`
- 測試失敗 `0xC0000135` → Qt DLL 未在 PATH
- **Unit tests** (9/9): config-manager, input-mapper, instance-manager, graphics-framebuffer,
  adb-framebuffer-capture, qmp-input, process-launcher, android-console-input, coordinate-mapper
- **Integration tests** (`tests/integration/`, 3 個): 需要 env vars
  `CHIMERA_ADB_PATH`, `CHIMERA_EMULATOR_PATH`, `CHIMERA_AVD_NAME`；
  CI 用 `-LE integration` 略過

## Git Workflow

- Format: `<type>: <description>`（`feat fix refactor docs test chore perf ci`）
- **沒有使用者明確要求不 commit**
- **沒有確認不 push**
- 多個相關零碎提交才 squash；要保留 bisect 能力就不 squash

## 需維護的文件

| 文件 | 何時更新 |
|------|---------|
| `CLAUDE.md` | 架構、模組邊界、決策變更時 |
| `AGENTS.md` | 工作流程、環境、標準變更時 |
| `CONTEXT.md` | 每個重大 Phase 完成後 |
| `docs/project/STATUS.md` | 重大里程碑或 blocker 解決後 |

## Safety Checklist（commit 前）

- [ ] 無硬編碼 secrets
- [ ] 所有使用者輸入已驗證
- [ ] 錯誤訊息不洩漏敏感資料
- [ ] Tests 本地通過

## Troubleshooting

| 症狀 | 解法 |
|------|------|
| `cc1plus.exe` crash | 用 MSVC；不修 MSYS2 GCC |
| Qt6 not found by CMake | 加 `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64` |
| `chimera-ui.exe` 缺 `Qt6*.dll` | Rebuild Release；`windeployqt` 已在 post-build 自動執行 |
| Tests `0xC0000135` | `$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"` |
| MOC 編譯錯誤 `signals:` | 不要定義 `QT_NO_KEYWORDS` |
| 多個 `main()` 衝突 | 每個 test 獨立 `add_executable()` |
| Emulator "multiple emulators with same AVD" | `taskkill /F /IM emulator.exe`；移除 `*.avd/*.lock` |
| QMP connection refused | 確認 `-ports 5554,5555`（console=5554, ADB=5555） |
| ANGLE DLL 找不到 | CMake post-build 自動 copy 到 `build/Release/` |
| VNC 卡在 resize loop | 只在維度真正改變時才設 `m_resizedThisUpdate=true` |
| ADB screencap 空資料 | 確認 `sys.boot_completed=1`；用 raw format（不加 `-p`） |

---
*Keep this file updated. Agents depend on it.*
