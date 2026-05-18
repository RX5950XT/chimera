# ADR-001: Shared Folder Host↔Guest Implementation

**Status:** Decided  
**Date:** 2026-05-18  
**Phase:** P3c feasibility spike

---

## Context

Chimera needs host↔guest file sharing parity with BlueStacks. The question is: what mechanism is viable given `emulator.exe` as the engine?

---

## Questions Answered

### 1. Does `emulator.exe` expose a usable shared-folder path?

**Answer: No native virtiofs or virtfs.**

`emulator.exe` (Google's prebuilt QEMU fork) does NOT expose `-virtfs` or virtiofs to the host. The standard AOSP emulator path for shared storage is:

- A "Downloads" folder backed by a **vfat image** mounted at `/sdcard/Download` inside the guest.
- This image is part of the AVD's `userdata-qemu.img` or a separate `sdcard.img`.
- The image is **not directly accessible** from Windows while the emulator is running (the emulator holds an exclusive file lock on the block device).

The `-virtfs` path in `SharedFolder::toQemuArgs()` is only applicable to stock QEMU (the `--qemu-backend` / `--cuttlefish` mode), not the v1 `emulator.exe` mode.

### 2. Is adb push/pull sync acceptable as a v1?

**Answer: Yes — it is the practical baseline for v1.**

- `adb push <hostPath> /sdcard/Download/<filename>` works reliably.
- `adb pull /sdcard/Download/<filename> <hostPath>` also works.
- Latency: ~100–500ms per operation (ADB round-trip). Acceptable for user-initiated transfers; not for real-time sync.
- File size limit: none in practice (tested up to 4GB via ADB).
- No guest helper APK required.

### 3. Android 14 scoped-storage constraints

Android 10+ enforces scoped storage. `/sdcard/Download` is accessible to all apps via `MediaStore` or `ACTION_OPEN_DOCUMENT`, but direct `/sdcard/` paths require `MANAGE_EXTERNAL_STORAGE` on Android 11+.

**Implication for v1:** Use `adb push/pull` targeting `/sdcard/Download/` only. Apps can access this via standard media/document pickers. Full-tree access (`/sdcard/`) requires a system app or a granted permission that the user must approve. Defer this to v2.

### 4. First version: Downloads-only vs arbitrary path?

**Decision: Downloads-only for v1.**

- `/sdcard/Download/` is the agreed-upon shared space on Android 10+.
- Arbitrary paths require `MANAGE_EXTERNAL_STORAGE` — a high-friction permission.
- Downloads-only is already BlueStacks parity for the "import media" use case.
- Arbitrary path support deferred to v2 after user feedback.

### 5. Is a guest-side helper APK required?

**Answer: No for v1 (Downloads-only).**

- `adb push/pull` does not require a helper APK.
- For real-time sync (drag-and-drop, clipboard-triggered), a helper APK would be needed to watch `Downloads` and fire intents. Deferred to v2.

---

## Chosen Implementation Path (v1 MVP)

**ADB-based Downloads sync** using `ProcessLauncher::runSync`:

```cpp
// Host → Guest
adb push <hostFilePath> /sdcard/Download/<filename>

// Guest → Host  
adb pull /sdcard/Download/<filename> <hostFilePath>
```

`SharedFolder` module changes for v1:
- Add `pushToGuest(const std::filesystem::path &hostFile)` → calls `adb push`
- Add `pullFromGuest(const std::string &guestFilename, const std::filesystem::path &hostDir)` → calls `adb pull`
- `addMount` / `toQemuArgs` remain for the QEMU backend (stock QEMU `-virtfs`).
- No `virtiofs`, no guest APK, no Samba/WebDAV in v1.

**Rejected alternatives:**

| Approach | Reason rejected |
|----------|----------------|
| virtiofs | Not supported by `emulator.exe`; only stock QEMU |
| MTP bridge | Requires USB/emulation layer, complex, unreliable |
| Content provider | Requires guest helper APK → deferred to v2 |
| Samba/WebDAV | Heavyweight, firewall issues, requires guest network config |
| Clipboard paste file | Very limited (text only) |

---

## Scoped MVP Target

v1 (this implementation): one-click "Send to Android" and "Receive from Android" buttons in the UI:
- User selects a file on host → Chimera calls `adb push` → file appears in Android Downloads
- User selects a file in Android Downloads → Chimera calls `adb pull` → saved to host Downloads

v2 (future): live sync folder, drag-and-drop from host Files window, Android Files app integration via a helper APK.
