# 貢獻指南

## 開發流程

本專案主要由 AI Agent 自動化開發，人類監督為輔。若您想貢獻：

### 回報問題

1. 使用 GitHub Issues 回報 Bug
2. 提供重現步驟、系統環境、錯誤訊息
3. 附上 `build/CMakeFiles/CMakeOutput.log`（如為建置問題）

### 提交修改

1. Fork 本倉庫
2. 建立功能分支：`git checkout -b feat/your-feature`
3. 遵循現有程式碼風格（見 AGENTS.md）
4. 確保單元測試通過：`ctest --test-dir build -C Release`
5. 提交格式：`<type>: <description>`
   - Types: feat, fix, refactor, docs, test, chore, perf, ci

### 程式碼標準

- **C++**: MSVC 2022, C++17, Qt 6
- **最大行數**: 函式 < 50 行，檔案 < 800 行
- **巢狀層數**: 不超過 4 層
- **錯誤處理**: 每一層都要處理，禁止靜默吞掉例外
- **測試**: 新增功能需搭配單元測試

## 授權

提交 PR 即表示您同意將程式碼以 Apache 2.0 授權釋出。
