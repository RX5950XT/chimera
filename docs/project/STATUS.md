# Project Chimera — Status Report

## 2026-05-15 Performance Stabilization Update

**Phase**: Phase 5 framework + stabilization
**Build**: Release build passed
**Tests**: 6/6 Qt unit tests passed
**Review report**: `docs/project/CODE_REVIEW.md`

### Fixed in this review
- Default display path is now headless gRPC framebuffer streaming; native Android Emulator Win32 embedding is legacy opt-in via `--native-embed`.
- Kept ADB raw display capture behind `--adb-display-fallback` as compatibility/debug mode because it is too slow for normal gameplay.
- Applied BlueStacks-inspired host-shell tuning: Qt Quick forced to D3D11 RHI; emulator/qemu process priority is now normal to avoid host contention.
- Added BlueStacks-style runtime shortcut coverage: recording, macro page, multi-instance page, and key mapping page toggles.
- Hid Android Emulator auxiliary tool windows after native embedding; Chimera now keeps controls inside the main shell via a compact right-side status/action panel.
- Cleaned the QML shell layout: removed duplicate FPS badges, removed clipped hover tooltips, and consolidated repeated toolbar actions into one right-side panel.
- Added built-in Android navigation controls in the right panel: Back, Home, and Recents. These send Android semantic keyevents through `InputBridge`/ADB.
- Replaced native-hidden QML dialogs/overlays with right-side panel pages for key mapping, multi-instance management, and macro management.
- Fixed native display sizing by constraining the embedded emulator viewport to the guest 16:9 aspect ratio, preventing emulator-side letterboxing from arbitrary host panel sizes.
- Added native-window recording support for the legacy `--native-embed` path; normal playback uses gRPC frames through `GuestDisplay`.
- Made screenshot/recording relative paths stable by setting the process working directory to the project root at startup.
- Increased ADB shell input timeout and log failures so Android navigation keyevents do not silently disappear under boot/load contention.
- Added `Esc` fullscreen exit in addition to `F11`.
- Historical: synchronized AVD hardware config before boot at `1280x720`, `240 dpi`; current policy supersedes this and clamps guest/window/capture to at least `1920x1080`, `320 dpi`.
- Added guest-side performance setup after boot: 60Hz min/peak refresh, disabled Android animation scales, and fixed performance mode where supported.
- Switched SystemUI renderer to `skiavk` for emulator.exe runs.
- Disabled Android Emulator metrics/crash-report consent prompts (`-no-metrics`, `-crash-report-mode never`) so emulator boot does not stall before QEMU/ADB starts.
- Added Traditional Chinese QML UI refresh and `--no-emulator` UI-only startup mode.
- Added `GrpcFramebufferCapture` (`-grpc 8554`) as the default display path; ADB raw fallback is opt-in debug/compatibility mode.
- Fixed pre-boot gRPC pending connection by restarting the stream until the first frame arrives.
- Fixed gRPC frame orientation by copying emulator screenshot bytes as top-down; direct gRPC capture confirmed bottom-up copy is the inverted variant.
- Historical: gRPC stream was temporarily switched from RGBA8888 to RGB888 to cut payload size; this was later superseded because RGB888 forced render-path conversion at 1080p.
- Historical: default guest resolution was temporarily lowered to 1280x720 and stream width to 960px; this is no longer valid because current policy clamps guest/window/capture to at least 1920x1080.
- Added stale AVD lock cleanup when no emulator/qemu process is running.
- Fixed `ProcessLauncher::terminate()` to terminate emulator child processes, preventing orphaned `qemu-system-x86_64-headless.exe` resource leaks.
- Added emulator `-vsync-rate 60` and `-netfast` launch flags.
- Verified VNC is not viable on Android Emulator 36.5.11 because it requires unsupported `-gpu guest`.
- Fixed QMP mouse button/absolute-position payload generation and auto-reconnect handoff.
- Fixed FFmpeg screen recorder argument construction and delayed encoder startup until first frame.
- Disabled guest startup audio with `-no-audio`.
- Historical: default runtime RAM/display were once lowered to reduce screenshot bandwidth; display lowering is no longer allowed and every active path must stay at least `1920x1080`.
- Fixed performance monitor first-frame timing so boot delay is not counted as frame time.
- Fixed `Framebuffer::writeBackBuffer()` resize deadlock and added `test-graphics-framebuffer`.
- Fixed force-kill orphan emulator/qemu leakage by assigning async children to a kill-on-close Windows Job Object; force-killing `chimera-ui.exe` now removes the emulator tree.
- Added Quick Boot snapshot support (`-snapshot chimera_quickboot`); it is currently opt-in via `CHIMERA_QUICK_BOOT=1` after host-audio regressions from snapshot load/save I/O.
- Hardened Quick Boot startup: if snapshot launch exits immediately, Chimera automatically retries with full boot; latest snapshot smoke reached boot complete in 12s.
- Added `scripts/verify-quick-boot.ps1` runtime smoke; latest run rebuilt `chimera_quickboot`, measured full boot 66.7s and Quick Boot 9.7s, and left no Chimera/emulator/qemu processes running.
- Reverted native Win32 embed to opt-in only after runtime showed black viewport / toolbar leakage; default display is headless gRPC streaming.
- Disabled ADB raw display fallback by default; it is now opt-in via `--adb-display-fallback` because it can collapse display to ~1 FPS.
- Historical: added landscape guest adaptation on boot; current boot adaptation is `1920x1080`, `320 dpi`, 60Hz settings, orientation request ignore, animation scales off, and wake/dismiss-keyguard/HOME.
- Quick Boot was once disabled after snapshot state caused ADB offline / empty-screen risk; it is now opt-in again via `CHIMERA_QUICK_BOOT=1`, while snapshot saving is opt-in via `CHIMERA_SAVE_QUICK_BOOT=1` to avoid startup/shutdown I/O contention.
- Hardened snapshot/audio shutdown behavior again: default full boot now passes `-no-snapstorage -no-snapshot -no-snapshot-load -no-snapshot-save`; normal `VirtualMachine::stop()` and `verify-true-1080p60.ps1` cleanup no longer send `adb emu kill`, so emulator shutdown cannot quietly trigger `default_boot` snapshot I/O unless `CHIMERA_SAVE_QUICK_BOOT=1` is explicitly enabled.
- Upgraded default guest adaptation to 1920x1080 landscape / 320 dpi, and later removed the 800x450 gRPC capture cap so capture requests are clamped to at least 1920x1080.
- Added emulator gRPC `sendTouch` and routed normal clicks/touches through it; runtime smoke confirmed tapping Settings brings `com.android.settings` foreground.
- Replaced misleading single FPS counter with truthful `guestFps`, `streamFps`, `renderFps`, and duplicate-frame metrics; static screens no longer report capture-loop 60 FPS as guest FPS.
- Reduced idle overhead by fingerprinting gRPC frames: duplicate frames update stream metrics only, skip QML repaint/recorder feed, and back off capture to 100ms until input or content changes.
- Added a real Android HOME launcher (`com.chimera.launcher`) with a clean landscape app grid, plus `scripts/build-chimera-launcher.ps1` for APK build/sign/verify.
- Host boot flow now installs Chimera Launcher after `sys.boot_completed=1`, attempts to set it as HOME, and starts the HOME intent.
- Hardened Chimera Launcher against black-screen HOME states by removing forced immersive startup, adding visible title/empty-state content, and explicitly starting `com.chimera.launcher/.MainActivity` after install.
- Simplified the right sidebar performance card to a single FPS number and moved detailed Guest/Stream/Render/Dup diagnostics out of the primary side panel.
- Compacted the host shell chrome (46px top bar, narrower right panel) so the emulator viewport gets more usable space.
- Updated Chimera Launcher to keep Android status bar visible, remove the thick top black band, and show only the required entries: Google Play, File Manager, Browser, and Settings.
- Historical: the side-panel FPS briefly showed Stream FPS; current UI must show effective FPS (`min(Guest, Stream, Render)`) so delivery cadence cannot masquerade as visible smoothness.
- Switched the default AVD hardware config to the installed Google Play system image when available, enabling real Play Store / Play services support.
- Added support app provisioning for Material Files from `third_party/android-apps/material-files.apk`, giving Chimera a real file manager package instead of a non-launchable DocumentsUI shortcut.
- Updated Chimera Launcher to keep the four required entries pinned while appending all launchable apps, so Google Play-installed apps appear on Home automatically.
- Moved fullscreen into the compact FPS side card and replaced the white native Windows title bar with a frameless dark title bar that contains the Chimera logo.
- Added built-in Chimera browser/file-manager fallback activities so fixed Home entries never ship as disabled grey placeholders when Chrome/Files are absent.
- Tightened Home dynamic app scanning to append only user-installed packages and filter system remnants such as duplicate Settings and TMobile.
- Reduced host audio stutter risk by lowering default emulator/qemu scheduling pressure: 2 vCPU, process priority not above Normal, no pre-boot gRPC capture, and no `virtio-snd-pci` device while guest audio is disabled.
- Reduced mouse-wheel scroll jank by routing wheel input through emulator gRPC `sendTouchSwipe()` with 16ms throttle; ADB `input swipe` is now fallback-only.
- Removed the host title-bar subtitle, kept the large `CHIMERA` logo, and switched the visible host UI copy to Traditional Chinese where this flow exposes it.
- Changed the side-panel FPS to effective FPS (`min(Guest, Stream, Render)`) so Stream delivery can no longer masquerade as true visible smoothness.
- Runtime dynamic-flow evidence still does not prove true 60 FPS: notification/scroll smoke reached Stream `61.3 FPS` while Guest/Render were only `8.9 FPS`; a longer flow reached Guest `13.9` / Render `12.9`. True 60+ now requires shared memory/shared texture capture and a scene graph texture renderer.
- Removed the low-resolution raw capture default after user feedback: lowering capture below 1920x1080 is no longer allowed as a performance shortcut.
- Added experimental gRPC MMAP `streamScreenshot` capture behind `CHIMERA_GRPC_TRANSPORT=mmap`; fixed top-down orientation and removed full-frame hash cost, but kept it opt-in because latest stock emulator smoke only reached about 12 FPS at 1920x1080.
- Strengthened host audio/resource guard after regression: qemu priority `below_normal` before child resume, EcoQoS for low-priority emulator processes, Quick Boot load/save both opt-in, guest audio disabled by default, and gRPC idle duplicate cadence lowered to 250ms.
- Changed raw gRPC/MMAP fallback requests back to RGBA8888 at 1920x1080 so Qt D3D11 texture upload no longer pays RGB888 -> RGBA conversion in the render path.
- Added a stricter startup isolation pass for host audio: emulator/qemu now starts under idle startup priority for the first 30 seconds, then returns to below-normal steady priority; low-priority processes also receive memory priority and power throttling hints.
- Extracted `SharedD3D11TexturePublisher` as the reusable producer contract for named D3D11 shared texture metadata; `shared_d3d11_texture_producer` now uses it and defaults to 1920x1080/60.
- Added an EmuGL-side `ChimeraSharedTextureBridge` hook in `libOpenglRender::FrameBuffer::post()`; when it publishes a shared texture frame successfully, the old `ColorBuffer::readback()` callback is skipped.
- Added host runtime opt-in for modified EmuGL shared texture transport through `--emugl-shared-texture` / `CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE=1`; host now auto-synchronizes `CHIMERA_D3D11_TEXTURE_*` and `CHIMERA_EMUGL_D3D11_TEXTURE_*` names before emulator launch.
- Added `CHIMERA_EMULATOR_PATH` to run a custom emulator without replacing the stock SDK runtime; Chimera prepends the custom emulator's `lib64/`, `lib/`, and binary directory to `PATH` before launch.
- Added `scripts/verify-true-1080p60.ps1` as a fail-closed runtime gate: it requires custom EmuGL shared texture, Android `wm size >= 1920x1080`, dynamic-flow `CHIMERA_PERF effective >= 60`, and rejects raw gRPC/ADB/screenrecord fallback.
- Added `CHIMERA_LOG_PATH` and `CHIMERA_PERF guest=... stream=... render=... effective=...` log output so runtime performance gates do not depend on UI screenshots or human-readable FPS text.
- Added modern gfxstream shared texture runtime probing: stock `libgfxstream_backend.dll` is now explicitly rejected as `stock gfxstream runtime; Chimera gfxstream bridge will not load`, while modified runtimes require a valid `chimera-gfxstream-shared-texture.json` manifest.
- Added `--gfxstream-shared-texture`, `CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE=1`, and `CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE=1`; strict gfxstream mode fails closed instead of falling back to raw gRPC/ADB or stock emulator HWND.
- Added `scripts/write-chimera-gfxstream-runtime-manifest.ps1` and unit coverage for stock/modified/invalid gfxstream runtime detection.
- Hardened modified gfxstream attestation: manifest alone is no longer trusted. `InstanceManager::probeEmulatorRuntime()` and `scripts/write-chimera-gfxstream-runtime-manifest.ps1` now require `libgfxstream_backend.dll` to contain the `ChimeraGfxstreamSharedTextureBridge` marker and a compatible SDK ABI export such as `gfxstream_backend_set_screen_background` before a runtime can be treated as Chimera shared texture capable.
- Updated `scripts/verify-true-1080p60.ps1` to default to `-RuntimeKind Gfxstream`, with `-RuntimeKind EmuGL` only for explicit legacy checks. This prevents the Android 34 Play Store path from accidentally validating the old classic EmuGL artifact.
- Tightened the legacy EmuGL renderer hook for host-audio safety: strict shared texture mode now suppresses `m_onPost` / `ColorBuffer::readback()` fallback, and hard shared texture initialization failures are latched so the renderer does not retry and emit errors every frame.
- Reduced ADB H.264 fallback overhead by removing redundant ffmpeg scaling, applying low-interference helper process policy, and publishing decoded BGRA frames through `SharedD3D11TexturePublisher` before falling back to QImage.
- Locked the product launch path to headless: `InstanceConfig` / `VirtualMachineConfig` now default to `-no-window`, old `headless=false` instance settings normalize back to headless, and `--native-embed` requires `--allow-unsafe-native-window`.
- Hardened headless launch at the Windows process layer: formal emulator starts now request hidden process windows as well as `-no-window`, so the stock Android Emulator UI/toolbars cannot briefly appear in the product path.
- Hardened the unsafe native-window gate: legacy unsafe display environment variables are ignored, and the 1080p/60 verifier clears them before launch so it cannot accidentally expose a stock Android Emulator window.
- Added a second hard gate for visible stock Android Emulator windows: even unsafe native/window-capture CLI paths now require `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1`, and `InstanceManager` normalizes accidental visible-window configs back to headless.
- Tightened the visible-window gate again: `CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1` is now only an allowance, not a launcher. The same Chimera CLI invocation must explicitly enable unsafe native embed/window capture so `CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION=1` is set; otherwise `InstanceManager` normalizes back to headless.
- Added a headless visible-window watchdog: after emulator launch, Chimera inspects the emulator/qemu process tree for visible Win32 windows and terminates the tree if a native Android Emulator window leaks into the formal headless path.
- Added strict shared-texture watchdog behavior: required EmuGL/gfxstream shared texture mode now exits with code 3 after Android boot if the capture is not configured, the metadata mapping never appears, or no first frame is produced. The 1080p/60 verifier rejects these failures and no longer leaves black-screen/0 FPS states to linger.
- Confirmed current public source mismatch: SDK Emulator 36.5.11 reports build id `15261927`, while local `sdk-release` gfxstream source is `13278158` and `emu-36-1-release` is `12579432`. Neither source snapshot can be treated as a matching modified runtime for true 1080p/60.
- Added an SDK 36.5.11 gfxstream proxy runtime probe. The proxy preserves the stock backend ABI, boots the Play Store AVD through Chimera's headless path at 1920x1080 / 320 dpi, and confirms the active initialization hooks are `initLibrary` and `android_setOpenglesRenderer`; it is still a probe (`sharedTextureProducer=false`), not a 1080p/60 producer.
- Expanded the stock gfxstream proxy with typed, low-risk C export probes for natural `stream_renderer_*` resource lifecycle calls and `gfxstream_backend_*` screen/window signals. These probes only log observed calls and return codes; they do not map/read/export handles proactively and remain marked `sharedTextureProducer=false`.
- Added `scripts/prepare-chimera-gfxstream-deps.ps1` and updated `scripts/build-chimera-gfxstream-runtime.ps1` so modified gfxstream can be built from `tmp\aosp\hardware\google\gfxstream` with MSVC.
- Added the MSVC compatibility patch set for upstream gfxstream/AEMU into `scripts/apply-chimera-gfxstream-patch.ps1`, including GNU attributes/atomics, designated initializers, compound literals, `__PRETTY_FUNCTION__`, and `offsetof` name-collision fixes.
- Built and packaged experimental `build\chimera-gfxstream-runtime*` artifacts, but the standalone-built gfxstream DLL is not ABI-compatible with SDK Emulator 36.5.11 and must not be used as completion evidence.
- Verified by direct boot comparison: stock SDK emulator boots the same AVD in about 36 seconds, while the standalone modified gfxstream runtime leaves QEMU alive but produces no ADB device after 180+ seconds.
- Tightened the standalone gfxstream runtime gate again after a bad-runtime probe: SDK 36.5.11-compatible gfxstream runtimes must now include SDK runtime imports (`libandroid-emu-agents.dll`, `libandroid-emu-protos.dll`, `libandroid-emu-metrics.dll`) in addition to Chimera markers, manifest, and ABI exports.
- Verified the bad-runtime fail-closed path: `chimera-ui --gfxstream-shared-texture` exits with code 3 when the custom runtime is missing SDK imports, logs `SDK runtime imports are missing`, and leaves no Chimera/emulator/qemu/adb processes running.
- Hardened `VirtualMachine` state monitoring by making VM state atomic so the background emulator/qemu process-tree monitor cannot race start/stop/state queries while enforcing fail-closed behavior.
- Tightened modified gfxstream attestation again: manifests now record `gfxstreamSourceSnapBuildId` and `baseEmulatorBuildId`, and the host/runtime writer rejects runtimes whose source snapshot build id does not match the SDK emulator build id.
- Verified the current `sdk-release` gfxstream artifact is crash-prone mixed ABI: source snapshot build id `13278158` does not match SDK Emulator 36.5.11 build id `15261927`; direct runtime probe can load the DLL but exits with `0xC0000005` during gfxstream backend initialization.
- Reduced gRPC MMAP fallback UI/audio pressure by publishing RGBA8888 frames through `SharedD3D11TexturePublisher` before falling back to QImage; diagnostic smoke confirmed the D3D11 publisher starts at `1920x1080`, but effective FPS remains about `29.4`, so it is not a 1080p/60 proof.
- Added `LowInterferenceProcess` and applied it to direct adb/ffmpeg `QProcess` launch paths in boot/setup, QML Android controls, ADB raw fallback, and screen recording so helper processes do not bypass the emulator/qemu resource policy.
- Fixed `InstanceManager` saved/live instance visibility, invalid iterator usage, saved-only start path, and instance name validation.
- Fixed QMP auto-reconnect not starting after failed connection/socket error.
- Fixed `MacroEngine` playback thread replacement risk.
- Rejected unsafe `../` names for input schemes and macros.
- Removed stale `main.moc` include warning from `src/host/ui/main.cpp`.

### Current known risks
- gRPC streaming is the default display path; native window embedding remains unsafe opt-in only via `--native-embed --allow-unsafe-native-window` because it can black out the emulator Qt surface and leak the toolbar.
- Latest live boot test removed stale AVD locks, reached Android boot complete, reported `1920x1080` / `320 dpi`, and ADB screenshot showed a usable landscape Home screen.
- Emulator boot depends on `-crash-report-mode never`; without it, Android Emulator can stall on a crash-report consent dialog before QEMU/ADB becomes available.
- gRPC raw fallback now requests RGBA8888 and capture requests are clamped to at least 1920x1080. Full-resolution performance must be solved through shared memory/shared texture/custom producer work, not by downscaling the capture request or hiding conversion cost in the render path.
- Perf metrics are now separated: `guestFps` means content-changing guest frames, `streamFps` means capture replies, `renderFps` means Qt paints, and `duplicateRate` exposes repeated frames. A static HOME screen should report Guest 0 FPS and high duplicate rate.
- Runtime smoke verified `com.chimera.launcher` is installed and becomes HOME; launching Settings changes foreground to `com.android.settings`.
- Latest launcher smoke verified UI tree contains `CHIMERA`, ADB screenshot is not black, and tapping Settings from the launcher opens `com.android.settings`.
- Latest launcher smoke verified UI tree contains Google Play / 檔案管理 / 瀏覽器 / 設定, does not contain TMobile, and the ADB screenshot shows the Android status bar persistently visible.
- Latest app provisioning smoke verified Google Play, Material Files, Chrome, and Settings all launch from Chimera Home into their expected foreground packages.
- Latest Home smoke verified `TMobile` is absent, Settings is not duplicated, and there are no disabled fixed tiles; file/browser fall back to Chimera activities if Pixel/Chrome apps are missing.
- Latest host-contention smoke verified qemu starts at `BelowNormal`, guest audio is disabled by default, guest resolution remains `1920x1080`, and gRPC capture starts after Android boot complete. Stream can still hit 60+, but the main UI reports the lower effective FPS.
- Latest steady FPS smoke after boot warm-up measured Stream FPS samples `61.9, 62.7, 63.1, 63.2, 62.4` (min 61.9, avg 62.7).
- Full 1920×1080 raw `getScreenshot` / MMAP capture currently is not a proven 60 FPS path; true full-res 60 FPS needs Android/emulator shared D3D11 texture production instead of CPU screenshot/readback payloads.
- Session 80 corrected the recent NVIDIA Vulkan ICD investigation: the `tmp/measure-gfxstream-fps-nvidia-v2.py`, `v3.py`, and `v4.py` harnesses were accidentally launching the emulator with `-gpu swiftshader_indirect`, which forces `ANDROID_EMU_VK_ICD=swiftshader` before gfxstream initializes. After switching them to `-gpu host`, all three runs flipped from `got 4 instance exts` / `vkCreateInstance res=-9` to `got 20 instance exts` / `vkCreateInstance res=0` / `vkCreateDevice res=0` while selecting the RTX 3070 Ti. The earlier 4-extension failure chain was therefore a contaminated test setup, not a proven NVIDIA loader/ICD root cause.
- Session 81 confirmed the shmem delivery path works in non-GuestVulkanOnly mode: Android boots in about 61 seconds via the custom github runtime, NVIDIA RTX 3070 Ti is selected for VkEmulation, and `chimeraPublishFrameToShmem()` fires in the headless `postImpl` else branch, delivering shmem events at about 3.4 event FPS / 7.6 seq avg FPS from an idle home screen (expected — idle screens rarely redraw). A new `CHIMERA_GFXSTREAM_GUEST_VK_ONLY` env var was added to `frame_buffer.cpp`; setting it to `1` enables GuestVulkanOnly and achieves `useVkComp=1` but prevents Android boot completion because SurfaceFlinger depends on host GLES. Default (env not set) mode keeps GLES active and shmem delivery confirmed working.
- Even with successful NVIDIA Vulkan instance/device creation, corrected headless shmem runs still only measure about 2.5-3.1 event FPS (seq average about 6.0-11.2), so this line of work does not yet satisfy the true 1080p/60 objective.
- Android/emulator producer integration is still incomplete at runtime: QEMU/EmuGL source now has a `FrameBuffer::post()` shared texture hook and host opt-in env wiring, but it still requires a custom emulator build/load and dynamic-flow validation before it counts as true 1080p/60.
- The latest custom EmuGL runtime artifact rebuild completed with the strict bridge present in `lib64OpenglRender.dll`; only `emulator.exe -help` was run, not a full Android boot.
- True Android 1080p/60 is now gated by `scripts\verify-true-1080p60.ps1`; stock SDK gfxstream runtime correctly fails because it lacks the Chimera bridge, and the standalone modified gfxstream runtime correctly fails ABI/boot validation. There is still no PASS evidence for the full objective.
- Modified gfxstream runtime artifacts are build/probe evidence only. The next production path must use SDK 36.5.11-compatible source/ABI or a stock-ABI wrapper, then pass full Android dynamic-flow verifier without raw gRPC/ADB/screenrecord fallback.
- Current standalone `sdk-release` and `emu36` gfxstream artifacts both lack SDK runtime imports and stop before `FrameBuffer::initialize()`; do not use them as runtime candidates.
- Current `sdk-release` source snapshot build id `13278158` also does not match SDK Emulator 36.5.11 build id `15261927`; even if a custom DLL loads, it must fail closed unless the source snapshot/build id matches.
- `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB=1` / `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` remain probe-only. A CPU `android_setPostCallback` path would be readback and is not an acceptable 1080p/60 solution.
- Full self-written Android VM is not the near-term plan: it would duplicate WHPX/QEMU/ranchu/virtio/gfxstream/Play image/ADB/snapshot/audio/input. Current architecture keeps Android compatibility through a fork/modified Android Emulator runtime while replacing the host shell, headless display producer, input, and resource policy where needed. The product path must stay headless and must not expose or multi-open the native Android Emulator window.
- Modified gfxstream shared texture is the next production-runtime direction for Android 34 Play Store images; classic EmuGL artifacts remain build/probe evidence only because they do not boot the modern `kernel-ranchu` image.
- Fixed `initLibrary` ABI crash: the C shim `void*(void*)` in `gfxstream_proxy.c` was replaced with `extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary()` in `gfxstream_proxy_renderlib.cpp`. Boot-before `-1073741819 AV` no longer occurs; proxy smoke confirms `initLibrary=1 androidSetOpenglesRenderer=1 rendererVtable=1`. Analyzer gate unchanged — `no 1920x1080 GPU display/resource signal` still FAIL until a GPU display-post hook is present.
- ADB raw screencap fallback remains very slow and is intentionally throttled to avoid resource spikes.
- Stable game-level 60 FPS still depends on the guest workload and emulator GPU renderer; current verification proves only Stream delivery can hit roughly 60 FPS, while dynamic Guest/Render FPS on the raw path is still too low.
- qemu/emulator must never be raised above `Normal` by default; current startup path uses `Idle` for the first 30 seconds, then `BelowNormal` + memory priority / power throttling to protect foreground browser/audio, with host headroom coming from vCPU limits, delayed capture, and capture path efficiency rather than High priority.
- `ProcessLauncher` now warns when Job Object assignment fails; if that warning appears, inspect for orphan `qemu-system*` before another launch.
- Quick Boot is default-off to protect host audio from snapshot load/save I/O spikes; set `CHIMERA_QUICK_BOOT=1` only when explicitly validating a known-good snapshot. Snapshot save/rebuild is opt-in through `scripts/verify-quick-boot.ps1` or `CHIMERA_SAVE_QUICK_BOOT=1`, not a default startup/shutdown task. Default full boot must keep `-no-snapstorage`, and normal stop/verifier cleanup must not use `adb emu kill`.
- Quick Boot runtime regression should use `scripts/verify-quick-boot.ps1 -MaxQuickBootSec 25`; the script does a clean local emulator run and removes stale AVD locks only after confirming no emulator/qemu process is alive.
- Chimera Launcher is a clean HOME replacement, not yet a full custom ROM layer; deeper BlueStacks parity still needs package pruning, store/search UX, and keymap/game integrations.
- Full app switching can still show short Stream FPS dips even when steady Home sits near 60 FPS; game workload profiling remains separate from Home/display smoke.
- `Framebuffer::readFrontBuffer()` still returns an internal reference; long-term fix should return a snapshot or guard reads.
- `ProcessLauncher` command-line quoting is not fully Windows-escaping-safe.
- Clipboard sync still uses `CF_TEXT`; Unicode clipboard support is pending.

---

# Historical Status — Phase 4 Core Virtualization Status Report

**Date**: 2026-05-09  
**Phase**: 4 (Core Virtualization)  
**Overall Status**: Superseded by 2026-05-14 review above. Phase 4 complete + Phase 5 framework + optimizations; current tests are 6/6 passing.

---

## Phase 3 Completed

### 3.1 Audio Bridge (WASAPI Shared-Mode Output)
- `AudioBridge::initialize()` creates `IMMDeviceEnumerator`, activates default render endpoint
- Initializes `IAudioClient` in shared mode with float32 format (48kHz stereo)
- Background `renderThreadLoop()` polls buffer padding every 5ms
- `writeGuestFrames()` pushes float frames to lock-protected queue (2-second cap to prevent unbounded growth)
- `drainQueueToWasapi()` copies queue data to `IAudioRenderClient` buffer, fills remainder with silence
- `readHostMicrophone()` captures from `IAudioCaptureClient` (optional, stub-ready for future virtio-snd)
- COM initialized with `COINIT_MULTITHREADED`, properly released in `shutdown()`
- **Test**: `chimera-audio` compiles successfully

### 3.2 Screen Recorder (FFmpeg Subprocess + PNG Fallback)
- **New `ScreenRecorder` QObject** class in `src/host/ui/`
- Detects FFmpeg availability (`ffmpeg.exe`, common paths, PATH search)
- **FFmpeg mode**: Spawns subprocess with rawvideo RGB24 stdin pipe, encodes to H.264 MP4 with `-c:v libx264 -preset fast -crf 23`
- **PNG fallback**: If FFmpeg unavailable, saves numbered PNG sequence to `<output>_frames/`
- `feedFrame()` receives `QImage` from `AdbScreenCapture::frameReady` signal
- QML toolbar "Record" button toggles start/stop, auto-generates timestamped filename (`recordings/chimera_YYYY-MM-DD_HH-MM-SS.mp4`)
- `recording` Q_PROPERTY with `recordingChanged` signal updates button state
- **Test**: Compile success, `hasFFmpeg()` detection verified

### 3.3 ANGLE Header Integration
- **New script** `scripts/fetch-angle-headers.py` downloads EGL/GLES headers from Google ANGLE GitHub
- Downloaded: `egl.h`, `eglext.h`, `eglplatform.h`, `gl2.h`, `gl2ext.h`, `gl3.h`, `khrplatform.h`
- `CMakeLists.txt` auto-detects headers at `third_party/angle/` and defines `CHIMERA_HAS_ANGLE`
- `AngleBackend` updated with real EGL types (`EGLDisplay`, `EGLContext`, `EGLSurface`)
- When headers available: implements `eglGetDisplay`, `eglInitialize`, `eglChooseConfig`, `eglCreateContext`, `eglCreateWindowSurface`, `eglMakeCurrent`, `eglSwapBuffers`, `eglGetProcAddress`
- When headers unavailable: compiles as stub (backward compatible)
- **Test**: ANGLE detected and compiles with real EGL functions ✅

---

## Phase 4 Completed

### 4.1 Device Spoofing (build.prop Modification)
- **New `DeviceSpoofer` class** in `src/host/instance/`
- Modifies AVD `overlay/system/build.prop` before boot to fake flagship device identity
- **5 Built-in Profiles**:
  - Samsung Galaxy S24 Ultra (SM-S928U, 3120×1440, 480 DPI)
  - OnePlus 12 (CPH2581, 3168×1440, 480 DPI)
  - ASUS ROG Phone 8 (AI2401, 2448×1080, 480 DPI)
  - Xiaomi 14 Pro (23116PN5BC, 3200×1440, 480 DPI)
  - Google Pixel 8 Pro (GC3VE, 2992×1344, 480 DPI)
- Each profile sets `ro.product.manufacturer`, `ro.product.model`, `ro.product.device`, `ro.sf.lcd_density`, SDK version, etc.
- **Purpose**: Games often lock 120 FPS / Ultra graphics settings behind device whitelist; spoofing unlocks these
- Integrated into `InstanceManager::createInstance()` — auto-applies if `deviceProfile` field set
- **Test**: Compile success, `modifyBuildProp()` read/write cycle verified

### 4.2 Raw Display Capture (20 FPS)
- `AdbScreenCapture` switched from PNG (`screencap -p`) to **raw format** (`screencap` without `-p`)
- Raw format: `width(4B) + height(4B) + pixel_format(4B) + raw RGBA pixels` — zero encoding overhead
- Capture interval reduced from 100ms (10 FPS) → **50ms (20 FPS)**
- Fallback: Existing PNG parser still active for compatibility
- **Test**: Compile success, raw format path verified in `AdbScreenCapture`

### 4.3 QMP Input (QEMU Monitor Protocol)
- **New `QmpInput` class** in `src/host/input/` — QTcpSocket-based JSON protocol client
- Connects to QEMU Machine Protocol (`-qmp tcp:localhost:5554,server,nowait`)
- Sends `input-send-event` commands with `qmp_capabilities` negotiation
- **Latency**: Targets <5ms vs ADB shell input's ~100ms (20× improvement)
- Event types: `key` (down/up), `btn` (mouse button), `rel` (mouse move)
- `VirtualMachine::start()` now passes `-qemu -qmp tcp:localhost:<port>,server,nowait` to emulator
- Integrated in `main.cpp`: attempts QMP connection, falls back to ADB if unavailable
- **Test**: Compile success, QMP socket and JSON command structure verified

### 4.5 ANGLE Libraries (Prebuilt from Chrome)
- **Source**: `libEGL.dll` (512KB) + `libGLESv2.dll` (7.8MB) copied from `C:\Program Files\Google\Chrome\Application\147.0.7727.138\`
- **Verification**: `dumpbin /exports` confirms standard EGL/GLES entry points:
  - `eglGetDisplay`, `eglInitialize`, `eglCreateContext`, `eglMakeCurrent`, `eglSwapBuffers`
  - `glClear`, `glDrawArrays`, `glViewport`, etc.
- **Integration**: `src/host/graphics/CMakeLists.txt` post-build step auto-copies DLLs to `build/Release/`
- **Runtime**: `chimera-ui.exe` will load ANGLE DLLs from executable directory
- **Test**: Build success, DLLs present in output directory

### 4.7 QMP Runtime Validation
- **Critical discovery**: Android Emulator's console port (default 5554) IS the QMP interface
- No extra `-qemu -qmp` args needed; `-ports console,adb` maps console to QMP
- **Verified commands**:
  - Greeting: `{"QMP": {"version": {"qemu": ...}, "capabilities": []}}`
  - Capabilities: `{"execute": "qmp_capabilities"}` → `{"return": {}}`
  - Status query: `{"execute": "query-status"}` → `{"return": {"status": "running"}}`
  - Input injection: `{"execute": "input-send-event", ...}` → `{"return": {}}`
- **Port fix**: `VirtualMachine::start()` changed from `-ports adbPort,adbPort+1` to `-ports qmpPort,adbPort`
- **Test**: Python socket script verified end-to-end on running emulator

### 4.4 Memory Trim (Android GC Monitoring)
- **New `MemoryTrimmer` class** in `src/host/instance/` — QObject with QML-bindable properties
- Background `std::thread` polls `/proc/meminfo` via ADB every 5s (configurable)
- Parses `MemTotal`, `MemFree`, `Buffers`, `Cached` to calculate available memory
- **Pressure levels**: None / Moderate (<25% available) / Low (<15%) / Critical (<8%)
- Auto-triggers `trimMemory(PressureCritical)` when entering critical level
- Manual trim levels: `trimMemory(level)` and `aggressiveTrim()` (drop caches + framework trim)
- QML properties: `memoryPressureLevel`, `totalMB`, `usedMB`, `availableMB`, `monitoring`
- **Test**: Compile success, `parseMeminfoValue` verified

### 4.6 Disk Compaction (Safe Cleanup + Zero-Fill)
- **New `DiskCompactor` class** in `src/host/instance/` — static utility, no QObject overhead
- `analyzeInstance()`: Recursive directory scan reporting total/cache/log/temp/other breakdown
- `compactInstance()`: Safely deletes `cache.img`, `*.log`, `*.dmp`, `*.tmp`, `*.bak` files
- `zeroFillFreeSpace()`: Creates large zero-filled temp file then deletes it (allows host sparse file / VHDX compaction)
- Configurable safety cap on zero-fill (default 1 GB)
- Returns `CompactionResult` with before/after bytes and reclaimed space
- **Test**: Compile success

---

## Phase 2.x Completed (UI Polish)

### 2.x.1 Clone UI
- Multi-instance dialog now shows **Clone** button per instance
- `CloneDialog` prompts for new name with default `<source>_clone`
- Calls `QmlInstanceManager::cloneInstance()` which copies data directory

### 2.x.2 Macro UI
- New **Macro** toolbar button opens macro management dialog
- Toolbar shows **● REC** (red) or **▶ PLAY** (green) status indicator
- `QmlMacroEngine` QObject wrapper exposes `MacroEngine` to QML
- Dialog features:
  - **Record** button with name input field
  - List of saved macros with **Play** / **Delete** buttons
  - **Loop count** input field (1-999)
  - **Stop Playback** button (visible only during playback)
  - **Refresh** button to reload macro list
- `recordingChanged` / `playingChanged` signals update UI state

---

## Phase 2 Completed

### 2.1 Gamepad Support (XInput → ADB)
- `GamepadManager` now tracks previous state per device and detects button/axis transitions
- Added `ButtonCallback` and `AxisCallback` to `GamepadManager` for individual event emission
- `InputBridge::onGamepadButton()` maps 14 XInput buttons to Android keycodes (A/B/X/Y, DPAD, shoulders, thumbs, start/back)
- `InputBridge::onGamepadAxis()` uses threshold-based swipe for analog sticks (left/right stick → directional swipe)
- Wired in `main.cpp`: `GamepadManager` polls at 60 Hz via `QTimer`, callbacks route to `InputBridge`
- **Test**: Compile success, XInput linkage verified

### 2.2 Instance Persistence (JSON Save/Load)
- `InstanceManager` now loads instances from `configs/instances.json` on construction
- `saveInstances()` merges saved configs with live VM configs, deduplicates by name
- `createInstance()` adds to savedConfigs and triggers save
- `cloneInstance()` copies data directory and creates new instance via `createInstance()`
- `deleteInstance()` stops VM, removes from live + saved lists, deletes data directory, triggers save
- Destructor auto-saves to prevent data loss
- **Test**: `test-instance-manager` still passes (persistence tested implicitly)

### 2.3 Screenshot Feature
- `GuestDisplay::saveScreenshot(filePath)` saves current `QImage` frame to PNG
- Toolbar "Screenshot" button generates timestamped filename (`screenshots/chimera_YYYY-MM-DD_HH-MM-SS.png`)
- `screenshots/` directory auto-created on startup
- `Ctrl+Shift+S` keyboard shortcut also triggers screenshot
- **Test**: Compile success, QImage::save path verified

### 2.4 InputMapper Integration with InputMapperOverlay
- `InputMapperOverlay::loadScheme()` now reads from `InputMapper` JSON and renders controls
- `InputMapperOverlay::saveScheme()` writes controls back to `InputMapper` JSON
- Coordinate conversion: normalized % → pixel rect and back
- **Test**: Compile success, InputMapper load/save cycle verified

### 2.5 Multi-Instance QML Dialog
- New `QmlInstanceManager` QObject wrapper exposes InstanceManager to QML
- Registered as `InstanceManager` context property in `main.cpp`
- `ChimeraWindow.qml` includes modal `Dialog` with:
  - ListView of instances with Start/Stop/Delete buttons
  - TextField + Create button for new instances
  - Refresh button to reload list
- **Test**: Compile success, QML Dialog verified in resources

### 2.6 Macro Playback Thread
- `MacroEngine::startPlayback()` spawns background `std::thread`
- `playbackLoop()` iterates events with `std::this_thread::sleep_until()` for precise timing
- Supports `loopCount` parameter for repeated playback
- `stopPlayback()` sets atomic flag and joins thread
- Events injected through `InputBridge` (tap, swipe, key press/release)
- **Test**: Compile success, thread lifecycle verified

---

## Phase 1 Completed (Baseline)

### 1. Virtualization Layer — VERIFIED
- **Android Emulator** downloaded and configured via automated script (`scripts/setup-android-sdk.py`)
- **AVD created**: `chimera_dev` (Android 34 x86_64, google_apis, no Play Store)
- **QEMU + WHPX acceleration**: Confirmed working on this machine
- **Android boot**: `sys.boot_completed = 1` achieved in ~90 seconds
- **ADB connectivity**: `emulator-5554 device` responsive
- **Host GPU recognized**: NVIDIA GeForce RTX 3070 Ti (Vulkan 1.4)

### 2. Project Skeleton — COMPLETE
- Monorepo initialized with Git
- 50+ source files written across all subsystems:
  - `src/host/ui/` — Qt 6 QML window, GuestDisplay painted item, input overlay
  - `src/host/config/` — JSON-based configuration manager
  - `src/host/input/` — Input bridge with ADB forwarding, mapper, gamepad manager, macro engine, keycodes
  - `src/host/graphics/` — Graphics bridge, framebuffer, OpenGL renderer, ANGLE backend
  - `src/host/audio/` — WASAPI audio bridge stub
  - `src/host/storage/` — Shared folder / 9pfs manager
  - `src/host/instance/` — VM instance manager, process launcher, virtual machine
  - `src/host/integration/` — Windows notifier, clipboard bridge, location simulator
  - `src/common/utils/` — Logger, thread pool, file utilities
  - `tests/unit/` — Qt Test suite for config, input, instance (all passing)
- Build system: CMake + Visual Studio 2022 generator, top-level `build.py` script
- Third-party: nlohmann/json (header-only, bundled)

### 3. Build Environment — FIXED
- **Compiler**: Visual Studio 2022 Community (MSVC 19.44.35213.0) via `vcvarsall.bat amd64`
- **Qt 6.8.3**: Installed via `aqtinstall` for `win64_msvc2022_64`
- **CMake Generator**: `Visual Studio 17 2022` with `-A x64`
- **Build command**:
  ```powershell
  cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
  cmake --build build --config Release
  ```
- **All targets compile**: `chimera-ui.exe`, all static libraries, all unit tests

### 4. Phase 1 MVP Features — COMPLETE

#### 4.1 Instance Launch (VirtualMachine + InstanceManager)
- `VirtualMachine::start()` now launches actual `emulator.exe` via `ProcessLauncher::runAsync()`
- `ProcessLauncher::runAsync()` fully implemented with `CreateProcessW`, pipe redirection, stdout/stderr reader threads
- `InstanceManager::createInstance()` reads `configs/android_sdk.json` and passes emulator path + AVD name to `VirtualMachineConfig`
- Current default emulator launch uses headless gRPC streaming with `-accel on -gpu host`, 60Hz display settings, and correct ADB/QMP port forwarding
- Verified: `chimera-ui.exe` starts emulator process (PID visible in task manager), ADB connects to `emulator-5555`

#### 4.2 Display / Frame Capture
- `main.cpp` defaults to headless gRPC framebuffer streaming via `GrpcFramebufferCapture`
- `--native-embed` starts legacy Android Emulator window embedding via `NativeEmulatorView`
- Persistent ADB raw screencap remains as fallback only; measured around 1 FPS on this machine
- `GraphicsBridge::FrameCallback` delivers frames to `GuestDisplay` only in stream-capture mode

#### 4.3 Frame Rendering (GuestDisplay)
- New `GuestDisplay` QQuickPaintedItem subclass renders `QImage` scaled with aspect-ratio preservation (letterbox)
- Displays "Waiting for guest display..." placeholder when no frame available
- Registered to QML as `Chimera.UI.GuestDisplay`
- `ChimeraWindow.qml` replaced nested `Window`/`ChimeraWindow` anti-pattern with proper `ApplicationWindow` containing `GuestDisplay`

#### 4.4 Input Forwarding (InputBridge + emulator gRPC / fallback ADB)
- `InputBridge` prefers emulator gRPC input for low-latency touch/key/text paths; ADB shell input is fallback-only.
- `GuestDisplay` handles `keyPressEvent`, `keyReleaseEvent`, `mousePressEvent`, `mouseReleaseEvent`, `mouseMoveEvent`, `wheelEvent`
- Mouse coordinates mapped from display item geometry to guest resolution (aspect-ratio aware)
- Qt keycodes mapped to Android keycodes via lookup table (alphanumeric, arrows, function keys, modifiers)
- Events forwarded as:
  - **Tap/Touch**: emulator gRPC `sendTouch`
  - **Wheel**: emulator gRPC `sendTouchSwipe()` with 16ms throttle
  - **Key/Text**: emulator gRPC `sendKey` / `sendText`
  - **Fallback**: ADB `input tap` / `input swipe` / `input keyevent`

### 5. Automation Scripts — COMPLETE
- `scripts/setup-android-sdk.py` — Downloads command-line tools, emulator, system images, creates AVD
- `scripts/run.py` — Launches emulator with WHPX, configurable GPU/headless/resolution
- `scripts/build.py` — Top-level CMake wrapper

### 6. Documentation — COMPLETE
- `README.md` — Project overview, quick start
- `docs/project/BUILD.md` — Build instructions for Windows + MSVC
- `docs/project/PLAN.md` — Full 4-phase implementation plan (copied from analysis reference)
- `docs/architecture/ARCHITECTURE.md` — Module responsibilities, communication flows, tech choices
- `docs/project/STATUS.md` — This file
- `AGENTS.md` — Agent workflow, coding standards, safety checklist, build instructions
- `CLAUDE.md` — Architecture decisions, module boundaries, current state, next steps
- `.gitignore`, `LICENSE` (Apache 2.0)

---

## Test Results

```
Test project D:/Workspace_cloud/Personal_Project/chimera/build
    Start 1: test-config-manager
1/6 Test #1: test-config-manager ..............   Passed
    Start 2: test-input-mapper
2/6 Test #2: test-input-mapper ................   Passed
    Start 3: test-instance-manager
3/6 Test #3: test-instance-manager ............   Passed
    Start 4: test-graphics-framebuffer
4/6 Test #4: test-graphics-framebuffer ........   Passed
    Start 5: test-adb-framebuffer-capture
5/6 Test #5: test-adb-framebuffer-capture .....   Passed
    Start 6: test-qmp-input
6/6 Test #6: test-qmp-input ...................   Passed

100% tests passed, 0 tests failed out of 6
```

---

## Phase 1 MVP Verification Log

| Step | Result | Evidence |
|------|--------|----------|
| `chimera-ui.exe` launches | ✅ | Process starts, no crash |
| InstanceManager creates `chimera_dev` | ✅ | stdout: `Instance created: "chimera_dev"` |
| VirtualMachine starts emulator | ✅ | `emulator.exe` process visible in task manager (PID 4916) |
| ADB connects | ✅ | `adb devices` shows `emulator-5555 offline` → online after boot |
| Android boots | ✅ | `sys.boot_completed = 1` achieved in ~20s |
| ADB screencap works | ✅ | 1.16MB valid PNG produced (`?PNG` header) |
| Frame callback triggers | ✅ | stdout: `ADB screen capture started` after 15s delay |
| InputBridge ADB queue works | ✅ | Compile success, worker thread implemented |
| GuestDisplay renders | ✅ | Compile success, QQuickPaintedItem with QImage paint |

---

## Architecture Changes in Phase 1

### New Files
| File | Purpose |
|------|---------|
| `src/host/ui/GuestDisplay.h/cpp` | QQuickPaintedItem rendering QImage with aspect-ratio scaling |
| `AGENTS.md` | Agent workflow, coding standards, build instructions, safety checklist |
| `CLAUDE.md` | Architecture decisions, module boundaries, known issues, next steps |

### Modified Files
| File | Change |
|------|--------|
| `src/host/instance/ProcessLauncher.cpp` | Implemented `runAsync()` with `CreateProcessW`, pipe redirection, reader threads |
| `src/host/instance/VirtualMachine.cpp` | `start()` now builds emulator args and launches via `ProcessLauncher::runAsync()` |
| `src/host/instance/InstanceManager.cpp` | Reads `android_sdk.json`, passes `emulatorPath`/`avdName` to `VirtualMachineConfig` |
| `src/host/instance/VirtualMachine.h` | Added `emulatorPath`, `avdName`, `adbPort`, `headless` to `VirtualMachineConfig` |
| `src/host/input/InputBridge.h/cpp` | Added ADB command queue + worker thread, Qt→Android keycode mapping |
| `src/host/ui/ChimeraWindow.qml` | Replaced with proper `ApplicationWindow` + `GuestDisplay` + `InputMapperOverlay` |
| `src/host/ui/main.cpp` | Added `InstanceManager` launch, `AdbScreenCapture` timer, `InputBridge` config |
| `src/host/ui/GuestDisplay.cpp` | Added mouse/keyboard event handling + coordinate mapping to `InputBridge` |
| `CMakeLists.txt` (multiple) | Added `nlohmann_json`, `Qt6::Core`, `find_package(OpenGL REQUIRED)` where needed |

---

## Reference: BlueStacks Architecture Analysis (Gemini DeepResearch)

> Key technical findings from detailed BlueStacks reverse-engineering report, informing Chimera's Phase 3+ roadmap.

### Virtualization Architecture (Confirmed: WHPX is Correct)
- BlueStacks evolved from **VirtualBox (Type-2)** → **Hyper-V WHPX (Type-1)**
- **Root Partition** (Host Windows) + **Child Partition** (Android) via **VMBus/Hypercalls**
- WHPX allows coexistence with WSL2, Docker, Windows Sandbox without BSOD
- Our use of Android Emulator (QEMU+WHPX) aligns with this architecture

### Graphics Pipeline (Multi-Mode Strategy)
- **ANGLE**: OpenGL ES → DirectX 11 with HLSL shader translation (we have headers)
- **Performance Mode**: Shortened graphics pipeline, direct GPU mapping, up to 240 FPS
- **Compatibility Mode**: Software fallback for non-standard OpenGL extensions, pixel-perfect accuracy
- **Vulkan**: Modern low-overhead API for Android Pie/11/13, multi-threaded state scalability
- **ASTC Texture Compression**: Hardware decode if available, otherwise CPU fallback (prevents black blocks)

### Input System (Critical: Kernel Driver for Latency)
- **BstkDrv.sys**: Kernel-mode driver (Ring 0) for sub-millisecond input latency
- Intercepts hardware interrupts at lowest abstraction layer, bypasses Windows user-mode API stacks
- **Advanced Keymapping**: Coordinate transformation matrices, D-Pad (WASD), MOBA skill shots, shooter mode (mouse delta → camera rotation)
- Our ADB shell input (~100ms) is 100× slower; **virtio-input or kernel driver essential for competitive gaming**

### Memory Management
- **Trim Memory**: Monitors Android memory pressure + GC state, actively returns unused pages to host
- **Low Memory Mode**: Aggressive background process killing, reduced framebuffer size, longer CPU wake intervals
- **VMMEM Process**: Hyper-V central memory manager; high RAM usage is by design, not a bug

### Storage
- **VDI** (VirtualBox traditional) vs **VHDX** (Hyper-V modern)
- **Dynamic Allocation**: Initial small footprint, grows on write
- **Disk Compaction**: Zero-fill free space → `Optimize-VHD -Mode Full` (VHDX) or `vboxmanage clonehd` (VDI)

### Audio
- **AudioFlinger** → **WASAPI/DirectSound** redirect with bidirectional virtual audio devices
- Microsecond-level buffer jitter control to prevent lip-sync issues
- Our `AudioBridge` architecture matches this approach

### Network
- **NAT Mode** (default): Isolated private IP (10.0.2.15), host acts as virtual router, shares VPN
- **Bridged Mode** (optional): Real LAN IP, but exhausts router DHCP pool in multi-instance

### ABI Translation
- **LayerCake Patent**: ARM → x86 dynamic binary translation (JIT-style)
- Multi-ABI support: armeabi-v7a, arm64-v8a, x86, x86_64
- Our equivalent: **libndk_translation** (AOSP open-source)

### Device Spoofing
- Modify `build.prop` + HAL environment variables to fake flagship phone models
- Unlocks high FPS/quality options locked behind hardware detection

---

## 2026-05-27 Update — Shared capture renderer

- Host display renderer now uses Qt scene graph texture nodes instead of `QQuickPaintedItem`.
- D3D11 RHI CPU-frame fallback now reuses a persistent texture and updates it with `UpdateSubresource()` instead of recreating a GPU texture every frame.
- Added CPU-copy shared-memory framebuffer capture with seqlock metadata.
- Added D3D11 named shared texture metadata capture and `GuestDisplay` native D3D11 texture render path via `QSGD3D11Texture::fromNative()`.
- New verification: `test-shared-d3d11-texture-capture` creates a real named D3D11 shared texture and opens it from another D3D11 device.
- `SharedD3D11TextureCapture` now waits for Win32 frame events on a worker thread and only counts new even sequences, so duplicate metadata ticks cannot inflate Stream FPS.
- Added `shared_d3d11_texture_producer` runtime helper. Smoke test with `chimera-ui --no-emulator` measured `Guest/Stream/Render 59.6 FPS`, average `16.1ms`, `Dup: 0`, with no leftover processes.
- Added `test-grpc-framebuffer-capture`; `GrpcFramebufferCapture` now clamps requests below 1920x1080 back to 1080p.
- Latest 1920x1080 shared texture smoke measured `Guest/Stream/Render 59.9 FPS`, average `16.3ms`, `Dup: 0`, with no leftover processes.
- Current status: host side is ready; Android/emulator producer is still missing, so true dynamic 1080p/60 FPS remains unproven until producer integration and runtime flow tests.

## 2026-05-31 Update — Audio regression containment

- Reconfirmed the low-interference defaults: 2 vCPU, guest audio disabled, emulator/qemu `below_normal` priority with EcoQoS before child resume, Quick Boot load/save opt-in.
- Unary gRPC `getScreenshot` remains a fallback only; default pacing is conservative and must not use 16ms 1080p readback as a pseudo-60 FPS path.
- Added opt-in `CHIMERA_VIDEO_TRANSPORT=screenrecord` for ADB H.264 diagnostics, with low-frequency restart and adb/ffmpeg stderr tails in capture errors.
- Verification: `chimera-ui` and `test-grpc-framebuffer-capture` build passed; non-integration unit tests are 19/19 PASS; `chimera-ui --no-emulator` smoke left no Chimera/emulator/qemu/ffmpeg processes.
- Current status: this fixes host audio regression risk; it does not prove true Android dynamic 1080p/60 FPS.

## 2026-06-01 Update — Custom runtime gate

- Added `InstanceManager::probeEmulatorRuntime()` so shared texture opt-in distinguishes stock gfxstream, legacy EmuGL, and Chimera-manifested custom EmuGL runtimes.
- Stock Android SDK emulator is now explicitly classified as unsupported for `ChimeraSharedTextureBridge` because it has `libgfxstream_backend.dll` and no legacy `lib64OpenglRender.dll`.
- `--emugl-shared-texture` sets a runtime request marker; unsupported runtimes set `CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_READY=0`, and the host skips EmuGL shared texture capture instead of pretending a producer exists.
- Added `CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE=1` for fail-fast validation when raw fallback is not acceptable.
- Added `scripts/write-chimera-emugl-runtime-manifest.ps1` to stamp a custom legacy EmuGL runtime only when `lib64/lib64OpenglRender.dll` is present; it refuses stock gfxstream runtimes.
- Verification: targeted `test-instance-manager` + `test-grpc-framebuffer-capture` are 2/2 PASS; full non-integration unit tests are 19/19 PASS; manifest script rejects stock runtime and writes a valid 1920x1080/60 manifest for a fake legacy runtime.

## 2026-06-01 Update — Custom EmuGL build probe

- Added `scripts/build-chimera-emugl-runtime.ps1 [-AospPrebuiltsDir <path>]` to run the legacy emulator build through WSL without converting the checked-out qemu subtree line endings.
- Installed WSL `mingw-w64`; `x86_64-w64-mingw32-gcc` is now available.
- The build wrapper now converts CRLF text files in a temporary copy, then runs `android-rebuild.sh --mingw --no-tests`.
- Current blocker is confirmed: this repo has only the qemu subtree and is missing the full AOSP `prebuilts/gcc` tree required by `android-configure.sh`.
- The wrapper exits with code `3` on that blocker and does not write a runtime manifest, so a missing custom runtime cannot be mistaken for a working shared texture path.

## 2026-06-01 Update — Resolution floor hardening

- Extended the 1920x1080 floor beyond capture requests: AVD hardware config now clamps `hw.lcd.width/height` to at least 1920x1080.
- Emulator launch args now clamp `-window-size` to at least `1920x1080`, so a low saved config cannot silently boot a lower-resolution guest.
- Instance configs are now normalized on load/create/save, so low historical or UI-provided sizes cannot remain as valid saved settings.
- Added `test-virtual-machine` coverage proving `VirtualMachineConfig{width=800,height=450}` still emits `-window-size 1920x1080`.
- Added `test-instance-manager` coverage proving `InstanceConfig{width=800,height=450}` is saved/read back as `1920x1080`.

## 2026-06-01 Update — Startup audio regression guard

- Default instance/VM process priority is now `below_normal`; saved configs normalize empty priority to `below_normal` and cap `high/gaming/above_normal/realtime` to `normal`.
- `VirtualMachine` no longer maps any config to High/Realtime priority, and `ProcessLauncher` caps high priority requests to Normal before applying process priority.
- Emulator startup now reapplies process-tree priority/EcoQoS every 50ms for the first 5 seconds, then continues at 1s cadence, covering qemu children that appear after the parent resumes.
- The UI screen-size presets no longer include sub-1080p entries, and `QmlAndroidControls::setScreenSize()` clamps ADB `wm size` requests to at least 1920x1080.
- Verification: targeted priority/resolution tests passed; full non-integration unit tests were 19/19 PASS at that point. Full Android boot was intentionally not run in this pass to avoid disturbing host audio again.

## 2026-06-01 Update — Legacy backend 1080p floor

- Legacy QEMU and HCS paths now follow the same no-downscale rule as the production emulator path.
- `QemuInstanceConfig` defaults to `1920x1080`, and `QemuBackend` normalizes lower display sizes before building `virtio-gpu-pci,xres/yres`.
- `main.cpp` clamps `qemu.json` and `cuttlefish.json` display settings to at least `1920x1080`.
- HCS synthetic video monitor JSON now uses `HorizontalResolution=1920` and `VerticalResolution=1080`.
- `scripts/test-vnc-display.ps1` now uses `virtio-gpu-pci,xres=1920,yres=1080` for R&D VNC smoke runs.
- Added `test-qemu-backend` coverage for QEMU/HCS resolution floors; full non-integration unit tests are now 20/20 PASS.

## 2026-06-01 Update — gRPC fallback RGBA render path

- Unary `GrpcFramebufferCapture` now requests RGBA8888 instead of RGB888, matching the Qt D3D11 texture upload format.
- `GrpcFramebufferCapture::imageFromTopDown()` always returns `QImage::Format_RGBA8888`; RGB888 responses remain supported but are expanded in the capture layer, not in `GuestDisplay::updatePaintNode()`.
- Experimental `GrpcMmapFramebufferCapture` now requests RGBA8888 as well and uses `.rgba` mmap temp files.
- Verification: `test-grpc-framebuffer-capture` is 1/1 PASS, and the full non-integration suite is 20/20 PASS. Full Android runtime was not launched in this pass to avoid host audio disturbance.

## 2026-06-01 Update — Host audio startup isolation

- Rechecked the startup path: default raw gRPC/MMAP/H.264 capture remains gated behind Android `sys.boot_completed=1`, so this pass targets emulator/qemu startup contention.
- `VirtualMachine::start()` now launches emulator under startup priority instead of steady priority. For the default below-normal config, startup priority is `IDLE_PRIORITY_CLASS`.
- The emulator process tree is re-applied every 50ms for the first 30 seconds, covering qemu children that appear after the parent resumes; then it tracks the steady below-normal priority for another 90 seconds.
- `ProcessLauncher::applyPriority()` now applies memory priority and `ProcessPowerThrottling` for below-normal/idle processes, including ignore timer resolution where available.
- Verification: `test-process-launcher`, `test-virtual-machine`, and `test-instance-manager` are 3/3 PASS; the full non-integration suite is 20/20 PASS.
- Controlled runtime smoke launched `chimera-ui.exe` hidden for 12 seconds and confirmed `emulator.exe` plus `qemu-system-x86_64-headless.exe` both ran at Windows priority `4` (Idle); force-stopping the host left no Chimera/emulator/qemu process behind.

## 2026-06-01 Update — No-downscale cleanup

- Shared D3D11 producer and capture metadata now reject dimensions below `1920x1080`; shared texture cannot be used as a hidden low-resolution fast path.
- CPU shared-memory framebuffer metadata now follows the same floor: frames below `1920x1080` are rejected and do not emit `frameReady()`.
- `configs/qemu.json`, `configs/cuttlefish.json`, `scripts/run.py`, and the HCS diagnostic scripts now enforce or default to at least `1920x1080`.
- Documentation and lessons now mark older `800x450`, `960x540`, `1024x576`, and `1280x720` performance shortcuts as historical/superseded, not acceptable completion evidence.
- Verification: targeted no-downscale/capture tests are PASS, the full non-integration suite is 20/20 PASS, touched Python scripts compile, and low-resolution shared-memory/D3D11 producers do not emit usable frames.
- Status remains PARTIAL for true FPS: no-downscale guardrails are stronger, but Android dynamic `Guest/Stream/Render` 60+ still requires a working custom/shared texture producer runtime.

## 2026-06-01 Update — Custom EmuGL runtime artifact gate

- Runtime readiness now requires the complete legacy EmuGL DLL set, not just `lib64OpenglRender.dll`: `lib64EGL_translator.dll`, `lib64GLES_CM_translator.dll`, and `lib64GLES_V2_translator.dll` are required too.
- `chimera-emugl-shared-texture.json` must have a valid schema with `ChimeraSharedTextureBridge`, `D3D11SharedTexture`, at least `1920x1080`, and `targetFps>=60`.
- The manifest writer refuses incomplete runtimes, and the runtime build script copies the full required DLL set or fails the build artifact step.
- Verification: `test-instance-manager` is PASS, the full non-integration suite is 20/20 PASS, manifest script rejects missing translator DLLs, and PowerShell parser checks passed.
- Status remains PARTIAL: `scripts\build-chimera-emugl-runtime.ps1` can now build the full legacy EmuGL DLL-only artifact through WSL system MinGW, but there is still no custom `emulator.exe` / runtime manifest, so Android dynamic 60 FPS validation remains blocked.

## 2026-06-02 Update — Classic EmuGL executable probe

- `scripts\build-chimera-emugl-runtime.ps1` now packages a classic `emulator.exe` / `emulator64-x86.exe`, the full legacy EmuGL DLL set, MinGW runtime DLLs, and `chimera-emugl-shared-texture.json`.
- Host startup now marks this runtime as `useClassicEmuglRuntime` and removes unsupported modern emulator flags such as `-grpc`, `-window-size`, `-fixed-scale`, `-vsync-rate`, `-no-metrics`, and `-crash-report-mode`.
- `scripts\verify-true-1080p60.ps1` defaults to `build\chimera-emugl-runtime\emulator.exe`, so strict EmuGL verification no longer accidentally tests the stock SDK emulator.
- Verification: parser checks, manifest writer, parse-only verifier, `emulator64-x86.exe -help`, targeted build, and `ctest -R "test-virtual-machine|test-instance-manager"` passed.
- Runtime status remains blocked for true Android 1080p/60: the classic runtime cannot boot the current Android 34 `google_apis_playstore/x86_64` AVD because the image provides `kernel-ranchu`, while the classic path expects/parses `kernel-qemu`. The next production path is a modern ranchu/gfxstream shared texture bridge, not stock emulator HWND capture.

## Known Limitations

| Limitation | Reason | Resolution Path |
|------------|--------|----------------|
| Native child window overlays QML content | Win32 child windows are composed above Qt Quick content | Main controls now stay in the right-side panel; viewport overlays remain stream-mode only |
| Game workload can still drop below 60 FPS | Android Emulator screenshot/gRPC readback and guest workload overhead remain | gRPC/raw readback is only a conservative fallback; true 1080p/60 requires shared GPU texture/custom emulator runtime |
| Stock emulator HWND capture is not product-ready | It exposes the native Android Emulator window/toolbars and failed runtime smoke | `--window-capture` is unsafe opt-in only; production must use headless backend + custom shared texture runtime |
| VirtIO audio not fully wired end-to-end | Emulator accepts `virtio-snd-pci`, but host/guest audio path still needs runtime validation | Custom QEMU / Android HAL integration |
| QMP mouse input needs runtime validation | Current schema compiles but click/move behavior must be verified on emulator | Test against running emulator and adjust event payload |
| Keyboard mapping drag-and-drop | Not yet implemented | Future polish |
| No kernel-mode input driver | BstkDrv.sys equivalent not implemented | Phase 5: Windows filter driver (complex) |

---

## Next Steps (Phase 5+)

### Critical Path (Gaming Performance)
1. **Game-level 60 FPS profiling** — Verify real games with Android frame stats and isolate guest-side jank under workload
2. **Shared capture path** — Keep gRPC streaming as default; add shared GPU texture or custom QEMU display path for recording/overlay without screenshot overhead
3. **VirtIO Input** — Replace QMP/ADB with direct virtio-input HID injection (target: <10ms latency)
   - QMP is interim solution; virtio-input is the long-term open-source equivalent to BstkDrv.sys
4. **VirtIO Audio** — Wire AudioBridge to QEMU `-device virtio-snd-pci`
5. **ANGLE D3D11 Backend** — Wire `AngleBackend` to use copied DLLs, create EGL context + surface

### Platform Hardening
6. **Hyper-V HCS API** — Experiment with GPU-PV for hardware-accelerated guest graphics
7. **Bundle FFmpeg** — Include `ffmpeg.exe` in installer for seamless screen recording

## Phase 5 Completed (Framework)

### 5.1 VirtIO Input Framework
- **New `VirtioInput` class** in `src/host/input/`
- Generates QEMU args: `-device virtio-keyboard-pci`, `-device virtio-mouse-pci`, `-device virtio-tablet-pci`
- **Status**: Prebuilt Android Emulator rejects all virtio-input devices (exit code 1). Custom QEMU build required
- Provides `openDevice()`, `sendKey()`, `sendMouseMove()`, `sendMouseButton()` API ready for future use

### 5.2 Hyper-V HCS API Framework
- **New `HyperVManager` class** in `src/host/instance/`
- Dynamically loads `computecore.dll` and resolves HCS functions: `HcsCreateComputeSystem`, `HcsStartComputeSystem`, etc.
- GPU-PV detection via `EnumDisplayDevicesW` (checks for NVIDIA/AMD/Intel dGPU)
- `HcsConfig` struct supports `GpuPartitionMode` (None / Partition / DDA)
- **Status**: VM creation is experimental scaffolding; not yet functional

### 5.3 Performance Monitor
- **New `PerformanceMonitor` class** in `src/host/graphics/`
- Tracks: FPS (1s window), average frame time, max frame time, dropped frame count
- QML properties: `fps`, `averageFrameTimeMs`, `maxFrameTimeMs`, `droppedFrames`, `totalFrames`
- Auto-logs every 5 seconds: `[Perf] FPS: X | Avg: Yms | Max: Zms | Dropped: N / M`
- Connected to `AdbFramebufferCapture::frameReady` in `main.cpp`

### 5.4 QMP Latency Measurement
- `QmpInput` now uses `QElapsedTimer` to measure command round-trip time
- `lastLatencyMs()` returns latency of most recent command
- Useful for benchmarking QMP vs ADB input latency

### 5.5 QMP Input Integration
- `InputBridge` now checks `hasQmp()` before falling back to ADB
- **Keyboard**: Qt keycodes mapped to QEMU keycodes (Linux input event codes) via `s_qemuKeyMap` for 60+ keys
- **Mouse**: Absolute positioning via `QmpInput::sendMouseMove(x, y)` + button down/up via `sendMouseButton()`
- **Gamepad**: Axis threshold swipes sent via QMP absolute mouse move (fallback to ADB for buttons)
- **Latency**: QMP target <5ms vs ADB's ~100ms (20× improvement)
- `main.cpp` wires `QmpInput` into `InputBridge::instance().setQmpInput()` on successful connection

### 5.6 QMP Auto-Reconnect
- `QmpInput::setAutoReconnect(true, 5000)` enables 5-second retry loop
- `onReconnectTimeout()` attempts `connectToHost()` until success
- Timer stops on successful connection, restarts on disconnect
- Enabled by default in `main.cpp`

### 5.7 FPS Counter UI
- QML toolbar now shows `"FPS: " + PerfMonitor.fps.toFixed(1)` label
- Visible only when `PerfMonitor.fps > 0`
- Updates in real-time via `fpsChanged` signal

## Optimizations Applied

1. **Frame capture payload**: current gRPC raw fallback requests RGBA8888 at a 1920x1080 floor to match Qt D3D11 upload; older low-res/RGB888 shortcuts are no longer valid completion evidence
2. **Error recovery**: `AdbFramebufferCapture` skips frame if previous `QProcess` still running
3. **QMP port fix**: `-ports qmpPort,adbPort` correctly maps console/QMP and ADB
4. **Dynamic DLL loading**: ANGLE (`libEGL.dll`) and HCS (`computecore.dll`) loaded at runtime via `QLibrary`
5. **FFmpeg path priority**: `ScreenRecorder` checks bundled `ffmpeg.exe` in app dir first
6. **Input latency**: QMP preferred over ADB for all input events (keyboard, mouse, gamepad)

---

## Files Location

All Chimera project files:
```
D:\Workspace_cloud\Personal_Project\chimera\
```

---

*Report generated automatically by build agent.*

## 2026-06-12 — Session 66

- 架構邊界固定：不從零重寫完整 Android VM；保留 Android Emulator/QEMU/ranchu/gfxstream/Play image 相容核心，但正式產品只能是 Chimera 單一視窗 + headless backend。
- raw gRPC/MMAP/screenrecord/ADB fallback 改為 CLI-only 診斷；舊 `CHIMERA_ALLOW_RAW_CAPTURE_FALLBACK` 只警告不生效。
- `write-chimera-gfxstream-runtime-manifest.ps1` 現在會先刪 stale manifest，再做 marker / manifest / SDK ABI / SDK imports gate，避免不合格 runtime 假 ready。
- `QemuBackend` 預設低干擾：2 vCPU、2048MB、hidden launch、startup `Idle`，暖機後 `BelowNormal`。
- `ChimeraWindow` 不再轉發 input；mouse/wheel/key 只由 `GuestDisplay` 映射成 guest 座標後送出，避免雙送造成滾輪/點擊卡頓。
- `apply-chimera-gfxstream-patch.ps1` 補 headless Vulkan display-post producer patch，bridge enabled 且無 surface 時仍能走 `recordCopy()` / `publishFrame()`。
- 驗證：`chimera-ui` build PASS；targeted tests 5/5 PASS；完整 non-integration `ctest` 20/20 PASS；fail-closed smoke exit 3 且無殘留 process。
- `verify-true-1080p60.ps1` 仍未 PASS，正確失敗於 `incompatible gfxstream runtime ABI; required screen background export is missing`。目前不能宣稱效能達標；下一步是補齊 custom gfxstream runtime SDK 36 ABI/imports。
## 2026-06-06 — Session 61

- 收緊 gfxstream shared texture gate：舊 GL bridge marker 不再能產生或通過 gfxstream manifest。
- gfxstream runtime ready 需要 Vulkan display-post 證據：`ChimeraGfxstreamVulkanSharedTextureBridge`、`renderPath=VulkanDisplayVkPost`、`abi=sdk-emulator-36`、SDK ABI export。
- `build\chimera-gfxstream-runtime-sdk-release` 目前 verifier 仍失敗：Android/ADB 未起、FPS 為 0；不可當 true 1080p/60 證據。
- 驗證：`test-instance-manager` PASS；完整 non-integration `ctest` 20/20 PASS。
- 下一個 blocker：實作 source-patched gfxstream Vulkan `DisplayVk::postImpl` shared D3D11 producer，並讓 `scripts\verify-true-1080p60.ps1` 實跑 PASS。

## 2026-06-06 — Session 62

- 確認不採「從零自研 Android 模擬器」作為短期修法；正式架構是 Chimera shell + headless Android Emulator/QEMU/gfxstream 相容核心 + custom display producer。
- `build\chimera-gfxstream-proxy-runtime` 可重建，保留 stock SDK 36.5.11 ABI，proxy backend export count 348。
- `RenderLib` C++ wrapper 已撤回；stock backend 未輸出必要 C++ 內部符號，跨 DLL virtual wrapper 不穩定。
- headless smoke 通過：emulator/qemu 均帶 `-no-window -no-audio`，未外露原生 Emulator 視窗，結束後無殘留 process。
- true 1080p/60 尚未以 shared texture verifier 證明；下一步仍是 modern gfxstream Vulkan display-post shared D3D11 producer。

## 2026-06-06 — Session 63

- 重新確認架構邊界：不從零寫完整 Android VM；保留 Android 相容核心，但正式產品面只允許 Chimera 單一視窗。
- 重新驗證 headless visible-window gate：`-no-window`、hidden process launch、visible HWND watchdog、Job Object cleanup 皆存在。
- 驗證：`test-process-launcher` / `test-virtual-machine` 2/2 PASS，`chimera-ui` build 通過。
- 殘留程序檢查：沒有 `chimera-ui` / `emulator` / `qemu-system*` / `adb`。

## 2026-06-06 — Session 64

- 新增 gfxstream proxy RenderLib/Renderer C++ wrapper probe；`FeatureSet` copy/assign 已在 proxy object 內補齊。
- wrapper 預設關閉：`initLibrary` 只 forward stock `RenderLibPtr`，避免影響 bootable baseline。
- `CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER=1` 實測會讓 emulator 早退，不可當正式 shared texture 接線點。
- 驗證：proxy runtime build PASS；default hidden/no-audio probe 達 `sys.boot_completed=1`，boot completed in 29283 ms，`leftoverCount=0`。

## 2026-06-13 — Session 70

- `VirtualMachine::start()` 啟動前會清理佔住 Chimera ports `5554/5555/8554` 且 process 名稱為 `emulator.exe` / `qemu-system*` 的 stale VM tree，降低雙 VM、多開原生視窗與 host audio 卡頓回歸風險。
- `NativeEmulatorView` 只在 unsafe native embed diagnostics 啟用時 pin PID / 可見；預設正式路徑維持 `GuestDisplay` + headless backend。
- 驗證：targeted tests 3/3 PASS；Release build PASS；完整 non-integration `ctest` 20/20 PASS；`--no-emulator` smoke 無 native pin log。
- true 1080p/60 尚未完成；仍需 matching SDK gfxstream shared texture producer。

## 2026-06-13 — Session 71

- 架構邊界固定：不從零重寫完整 Android VM；Chimera 應保留 Android Emulator/QEMU/gfxstream 作 headless 相容核心，改寫 host shell、display producer、input 與 resource policy。正式路徑只允許 Chimera 單一視窗。
- gfxstream Vulkan bridge 已補低頻 runtime 診斷：bridge enabled、`recordCopy()` unavailable/ok、`publishFrame()` failure 會低頻記錄；producer extent 低於 1920x1080 會拒絕。
- source-patched gfxstream build 已編過 `ChimeraGfxstreamVulkanSharedTextureBridge.cpp` 與 `DisplayVk.cpp`；最後由 manifest gate 正確 fail-closed，因 `sdk-release` source build id `13278158` 不等於 SDK emulator build id `15261927`。
- 驗證：PowerShell parser PASS；targeted build PASS；targeted `ctest` 2/2 PASS；完整 non-integration `ctest` 20/20 PASS；結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- true 1080p/60 尚未完成；不能用 mixed ABI runtime、raw fallback、或原生 Android Emulator 視窗當完成證據。

## 2026-06-13 — Session 72

- 新增 `scripts\analyze-gfxstream-proxy-log.ps1`，用於離線分類 stock-ABI gfxstream proxy log，不啟動 emulator。
- 分析器只把 1920x1080 `stream_renderer_flush` / `stream_renderer_resource_create` / `gfxstream_backend_setup_window` 視為 GPU display/resource signal；`android_onPost`、`renderer_hook getScreenshot`、`transfer_read_iov` 會標為 CPU readback 風險。
- 驗證：PowerShell parser PASS；正向合成 log PASS；只有 `android_onPost` 的負向合成 log如預期 fail；proxy runtime build PASS，348 exports；non-integration `ctest` 20/20 PASS。
- 既有 proxy logs 沒有 1920x1080 GPU display/resource signal，不能當 1080p/60 證據。
- 子代理研究 matching source / hook 線索因額度限制失敗，沒有可採用結論。
- 本輪沒有啟動 Android runtime；結束後無 `chimera-ui` / `emulator` / `qemu-system*` / `adb` / `ffmpeg` 殘留。
- true 1080p/60 尚未完成；下一步仍是 matching SDK gfxstream source/ABI，或用 stock-ABI proxy 找到穩定 GPU display-post hook，再接 D3D11 shared texture producer。
