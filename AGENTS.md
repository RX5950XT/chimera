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

# 5. Build Chimera Android HOME launcher（需要時）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-chimera-launcher.ps1
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
- 需要高頻 guest display/render path 用 `QQuickItem` + scene graph texture node；不要回退到 `QQuickPaintedItem` 每幀 `QPainter`
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
- Runtime Quick Boot smoke:
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-quick-boot.ps1 -MaxQuickBootSec 25`
- Runtime display/input smoke：啟動後確認 `wm size=1920x1080`、`wm density=320`、`pm path com.chimera.launcher` 存在、HOME foreground 是 `com.chimera.launcher`，並驗證 tap/launch 後 foreground package 真的改變。Perf log 要看 Guest/Stream/Render 分離；靜止畫面 Guest FPS 可為 0。
- Runtime shared texture smoke：可用 `shared_d3d11_texture_producer` + `chimera-ui --no-emulator` 驗證 host path；合格數據需 Guest/Stream/Render 同步接近 60、`Dup: 0`，不能只看單一 Stream FPS。
- 測試失敗 `0xC0000135` → Qt DLL 未在 PATH
- **Unit tests** (19/19): config-manager, input-mapper, instance-manager, virtual-machine, graphics-framebuffer,
  adb-framebuffer-capture, grpc-framebuffer-capture, shared-memory-framebuffer-capture, shared-d3d11-texture-capture,
  qmp-input, process-launcher, android-console-input, coordinate-mapper,
  clipboard-bridge, location-simulator, device-spoofer, macro-engine, gamepad-manager, audio-bridge
- **Integration tests** (`tests/integration/`, 3 個): 需要 env vars
  `CHIMERA_ADB_PATH`, `CHIMERA_EMULATOR_PATH`, `CHIMERA_AVD_NAME`；
  CI 用 `-LE integration` 略過

## Git Workflow

- Format: `<type>: <description>`（`feat fix refactor docs test chore perf ci`）
- **沒有使用者明確要求不 commit**
- **沒有確認不 push**
- 多個相關零碎提交才 squash；要保留 bisect 能力就不 squash

## Commit 排除（禁止進版控）

`.gitignore` 已涵蓋下列；commit 前確認 `git status` 不含這些：

- **BlueStacks 逆向 binaries**：`Binaries/`、`Client/`、`Engine/`、`Dumps/`
- **debug/擷取產物**：`*.err`、`*.out`、`*.ppm`、`verify*.png`、`qemu_*.png`、
  `shot_*.png`、`chimera-perf.*`、`*.log`
- **R&D 拋棄式腳本**：`run-qemu-*.ps1`、`test-qemu-*.bat`、`test_grpc_*.py`
- **大型 binary 與下載物**：`*.img/*.vhdx/*.qcow2/*.iso/*.dll/*.exe/*.lib`、
  `third_party/android-sdk|android-avd|android-apps|ffmpeg`
- **執行期資料**：`build/`、`instances/`、`recordings/`、`screenshots/`、`tmp/`

清理時先跑 `git ls-files --others --exclude-standard` 確認未追蹤來源檔，再跑
`git ls-files -oi --exclude-standard` 盤點 ignored 產物。`build/`、`third_party/android-sdk/`、
`third_party/android-avd/`、`third_party/android-apps/`、`third_party/ffmpeg/` 是可重建的本機快取；除非要重建環境，預設保留。

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
| 啟動後 FPS 掉到 0–1、整機極卡 | 先檢查有無 **orphan qemu**：`Get-Process qemu-system*`；有則 kill 全部再重啟。正常情況下 `ProcessLauncher` Job Object 會在 host 被 force-kill 時清掉 emulator tree |
| 打開模擬器後主機音樂卡頓或有雜音 | 檢查 qemu priority 不可高於 `Normal`、預設 vCPU=2、gRPC capture 要等 `sys.boot_completed=1` 才啟動；`enableAudio=false` 時不得加 `virtio-snd-pci`。Eco mode 關閉只可恢復 `Normal`，不可拉到 High |
| 出現 `AssignProcessToJobObject failed` | 代表目前程序可能在 nested job 或權限受限；此次啟動的 emulator tree 不保證跟 host 同生共死，結束前要確認無 orphan qemu |
| Quick Boot snapshot 壞掉、ADB offline 或啟動後空畫面 | 預設 full boot；只有設 `$env:CHIMERA_QUICK_BOOT="1"` 才啟用 snapshot。用 `scripts\verify-quick-boot.ps1` 重建並驗證 `chimera_quickboot` |
| 預設啟動後仍是黑/空畫面 | 先確認不是 native embed：預設應顯示 `Stream · 已連線`；`Native · 已連線` 只應出現在明確加 `--native-embed` 時。再抓 ADB screenshot 判斷 guest state |
| 畫面有但點擊沒反應 | 普通點擊應走 emulator gRPC `sendTouch`；不要只看 Android Console `event mouse` 回 OK。用 `dumpsys activity/window` 驗證 tap 後 foreground package 是否改變 |
| 1080p capture FPS 低於 60 | 不可降低預設解析度；`GrpcFramebufferCapture` 會把低於 1920x1080 的 request clamp 回 1080p。效能要靠 shared memory/shared texture/custom producer 解，不准用 800x450 這類降階當完成證據 |
| Shared texture 啟用後黑畫面 | 確認 `CHIMERA_D3D11_TEXTURE_METADATA` 指向 metadata mapping，producer 已建立 named D3D11 shared texture；Qt 必須走 D3D11 RHI（目前 main 會設 `QSG_RHI_BACKEND=d3d11`）。若沒有第一幀，gRPC fallback 應接手 |
| Shared texture 只有 30fps 左右 | 先確認 producer 真的以 60Hz 產生新 even sequence；consumer 端 `SharedD3D11TextureCapture` 應由 worker 等 frame event，不可退回 UI thread QTimer |
| Shared memory/frame metadata 偶發破圖 | 檢查 producer 是否遵守 odd/even sequence seqlock；odd 表示寫入中，consumer 只接受相同且為 even 的 sequence |
| UI 顯示 60 FPS 但體感卡 | 主側欄 FPS 必須是有效 FPS（`min(Guest, Stream, Render)`），不可顯示單純 Stream delivery；靜止畫面可為 0，HUD/log 才看 `Guest/Stream/Render/Dup` |
| 滑鼠滾輪捲動卡頓 | 確認 `InputBridge::Event::Wheel` 走 `EmulatorGrpcInput::sendTouchSwipe()` 並有約 16ms throttle；不可讓 wheel 主路徑退回 `adb shell input swipe` |
| 靜止首頁 CPU 偏高 | 檢查 gRPC duplicate path：重複 frame 不應 emit `frameReady()` 或觸發 QML repaint；idle duplicate cadence 約 50ms，有輸入才 boost 到 16ms |
| Guest FPS 被低估或高估 | 不可未驗證就改 sampled fingerprint；誠實 FPS 要用 full-frame fingerprint 或可靠 dirty signal，並用通知欄/滑動 flow 驗證 Guest/Render |
| 沒有乾淨 HOME / 回到 Pixel Launcher | 先建 `scripts\build-chimera-launcher.ps1`，再確認 `build\launcher\chimera-launcher.apk` 存在；boot completed 後 host 會 install 並 `cmd package set-home-activity com.chimera.launcher/.MainActivity` |
| HOME 只剩黑底/狀態列 | 不能只看 top activity；同時跑 `uiautomator dump` 檢查 `CHIMERA` 與固定入口，並抓 `exec-out screencap -p`。Launcher 需 explicit `am start -n com.chimera.launcher/.MainActivity` |
| Google Play / 檔案管理 / 瀏覽器缺失 | Play 要使用 `google_apis_playstore` system image；檔案管理由 `third_party/android-apps/material-files.apk` 自動安裝；Chrome 由 Play image / Play 安裝提供 |
| Google Play 新安裝 app 沒出現在 Home | Chimera Launcher 應掃描 `ACTION_MAIN` + `CATEGORY_LAUNCHER`，但只追加 user-installed packages；用 `uiautomator dump` 檢查 app label/content-desc |
| Home 出現 `Settings` 重複或 `TMobile` | 動態 app 掃描過濾失效；不可追加 system / updated-system package，也要排除固定入口 package |
| Home 圖示存在但灰掉或點了沒反應 | 固定入口必須有內建 fallback Activity；用 tile content-desc bounds 點擊，再看 `dumpsys activity` foreground package 與 `logcat -s ChimeraLauncher:I` |
| HOME 上方厚黑邊 / 沒有通知列 | 檢查 Launcher theme 不可設 `android:windowFullscreen=true`；只允許 `policy_control=immersive.navigation=*`，status bar 要常駐 |
| 側欄 FPS 卡太亂 | 主側欄只放一個 FPS 數字；Guest/Stream/Render/Dup 細節放 log 或 HUD，不要塞回主卡 |
| build 後 LNK1104 | 確認 `chimera-ui.exe` / `qemu-system*` 未在執行中，鎖住輸出檔 |

---
*Keep this file updated. Agents depend on it.*
