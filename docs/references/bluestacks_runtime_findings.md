# BlueStacks Runtime Findings

本文件只記錄可由設定檔、日誌、公開行為推導出的設計策略；不移植 BlueStacks 專有程式碼或二進位。

## 可借鏡設定

| 類別 | BlueStacks 觀察值 | Chimera 對應 |
|------|------------------|--------------|
| 顯示 | 原生 GPU 視窗，不靠截圖流玩遊戲 | `NativeEmulatorView` 作為預設顯示路徑 |
| Qt Shell | 日誌顯示 Qt renderer 使用 `Direct3D11` | 啟動前設定 `QSG_RHI_BACKEND=d3d11` |
| FPS | `max_fps=60`，遊戲實例以 60 FPS 為目標 | `maxFps=60`、`-vsync-rate 60`、Android 60Hz 設定 |
| DPI | `dpi=240` | AVD `hw.lcd.density=240` |
| CPU/RAM | 常見設定為 4 cores / 4096 MB；實際依機器調整 | Chimera 預設 4 cores / 2048 MB，降低記憶體壓力 |
| GPU | `graphics_engine=aga`、renderer 類似 Vulkan 路徑 | 目前用 Android Emulator `-gpu host` + `skiavk` |
| 輸入 | 原生鍵鼠、手把、鍵位覆蓋層與快捷鍵 | QMP 優先、ADB fallback、XInput、QML overlay |
| 啟動體感 | warmup/hidden/snapshot 類策略 | 目前先保持可重現 cold boot；snapshot/warmup 待實測導入 |
| 資源策略 | 記憶體 dedup、eco mode、process/service 分工 | Chimera 已有 trim/compaction；新增 emulator process tree priority |

## 對 Chimera 的結論

- 遊戲顯示不能再依賴 gRPC/ADB screenshot stream；它只能當 debug/fallback。
- Host UI 要固定走 D3D11，避免 Qt Quick 跟 emulator GPU path 搶 OpenGL context 或退回較慢 backend。
- 60 FPS 目標必須分兩層驗證：Host display path 是否 60Hz，以及實際遊戲 workload 是否穩定輸出 60 FPS。
- BlueStacks 的低資源表現來自長期客製 VM/device stack。Chimera 在使用 prebuilt Android Emulator 時，能做的主要是避免額外 frame copy、降低解析度、調整 process priority、限制背景擷取與關閉不必要服務。
- 後續若要再接近 BlueStacks，需要自訂 QEMU/guest image：shared GPU texture、guest input driver、snapshot warmup、memory sharing/ballooning。
