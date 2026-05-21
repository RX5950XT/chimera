# Chimera Task Todo

## 2026-05-21 Cleanup / Commit Hygiene

### Plan

- [x] 盤點 `git status`、ignored/untracked 檔案與文件現況。
- [x] 確認 `src/host/input/EmulatorGrpcInput.*` 與既有 modified 檔案屬未提交開發成果，不納入垃圾清理。
- [x] 確認 `.gitignore` 已涵蓋大型 binary、debug logs、R&D scripts、runtime/build outputs、BlueStacks reverse-engineering binaries。
- [x] 刪除可重建且不需提交的 R&D/output 垃圾：`out/`、root ISO/QCOW2/installer、QEMU/debug logs、runtime 空資料夾、錯誤路徑殘留。
- [x] 保留本機開發仍可能需要的 `build/`、`third_party/android-sdk/`、`third_party/android-avd/`、`third_party/ffmpeg/`。
- [x] 精簡同步 `AGENTS.md`、`CLAUDE.md`、`CONTEXT.md` 的版控衛生紀錄。
- [x] 驗證：`git status --short`、`git ls-files --others --exclude-standard`、`git ls-files -oi --exclude-standard` 摘要、CMake build/test。

### Review

- 清理後 ignored 摘要只剩 `build/` 與 `third_party/` 快取；未追蹤檔只剩 `src/host/input/EmulatorGrpcInput.*` 與本任務的 `tasks/todo.md`。
- 未刪除 Android SDK/AVD/FFmpeg/build cache，避免破壞目前開發與驗證環境。
- Release build 通過；`ctest --test-dir build -C Release --output-on-failure -LE integration` 為 15/15 PASS。
