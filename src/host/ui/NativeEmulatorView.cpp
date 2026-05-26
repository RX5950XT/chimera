#include "NativeEmulatorView.h"

#include "ScreenRecorder.h"

#include <QGuiApplication>
#include <QDebug>
#include <QPixmap>
#include <QQuickWindow>
#include <QScreen>

#include <algorithm>

#ifdef Q_OS_WIN
#include <tlhelp32.h>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>
#endif

namespace chimera {

#ifdef Q_OS_WIN
namespace {

std::wstring toLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

bool containsText(const std::wstring &haystack, const std::wstring &needle) {
    if (needle.empty()) return false;
    return haystack.find(needle) != std::wstring::npos;
}

std::wstring windowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring text(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

// Collect rootPid plus all descendant PIDs. The modern Android emulator.exe
// is a thin launcher whose actual Qt GUI window belongs to a child process
// (qemu-system-x86_64.exe), so the window we want to embed lives somewhere
// in this tree rather than under the launcher PID itself.
std::vector<DWORD> collectProcessTree(DWORD rootPid) {
    std::vector<DWORD> result;
    if (rootPid == 0) return result;
    result.push_back(rootPid);

    std::vector<std::pair<DWORD, DWORD>> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    for (size_t i = 0; i < result.size(); ++i) {
        const DWORD parent = result[i];
        for (const auto &[pid, parentPid] : processes) {
            if (parentPid == parent &&
                std::find(result.begin(), result.end(), pid) == result.end()) {
                result.push_back(pid);
            }
        }
    }
    return result;
}

std::wstring processImageName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};

    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
        CloseHandle(process);
        return {};
    }
    CloseHandle(process);
    path.resize(size);
    return toLower(path);
}

struct WindowCandidate {
    HWND hwnd = nullptr;
    int score = 0;
};

struct WindowSearch {
    std::wstring instanceName;
    std::wstring consolePort;
    HWND hostWindow = nullptr;
    std::vector<DWORD> targetTree;  // empty = any; non-empty = pid must be in this set
    WindowCandidate best;
};

struct AuxiliaryWindowSearch {
    HWND mainWindow = nullptr;
    DWORD processId = 0;
    std::vector<HWND> *hiddenWindows = nullptr;
};

std::wstring windowClassName(HWND hwnd) {
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    return toLower(cls);
}

int scoreWindow(HWND hwnd, const WindowSearch &search) {
    if (hwnd == search.hostWindow) return 0;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return 0;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return 0;

    // If we know which process tree to expect, reject windows outside it.
    // This prevents Chimera from accidentally stealing another emulator.exe
    // (e.g. one launched by Android Studio on the same machine). The window
    // may belong to a child process, so we match against the whole tree.
    if (!search.targetTree.empty() &&
        std::find(search.targetTree.begin(), search.targetTree.end(), pid) ==
            search.targetTree.end()) {
        return 0;
    }

    // The window must belong to an emulator/qemu process — never embed an
    // unrelated application window.
    const std::wstring image = processImageName(pid);
    const bool isEmulatorProc = containsText(image, L"\\emulator.exe") ||
                                containsText(image, L"\\qemu-system-x86_64");
    if (!isEmulatorProc) return 0;

    // Only ever embed a genuinely visible window. The emulator process owns a
    // swarm of invisible helper windows (NVIDIA pbuffers, Qt screen-change
    // observers, D3D temp windows, IME) — all of them are invisible, so this
    // single check eliminates the entire class of wrong-window grabs. The real
    // device UI window only appears once the emulator has finished setting up.
    if (!IsWindowVisible(hwnd)) return 0;

    // Reject GPU-driver / helper / IME / Qt-internal windows by class name.
    const std::wstring cls = windowClassName(hwnd);
    if (cls == L"nvogldc" || cls == L"nvopenglpbuffer" || cls == L"dummywin" ||
        cls == L"default ime" || cls == L"msctfime ui" || cls == L"ime" ||
        containsText(cls, L"screenchangeobserver") ||
        containsText(cls, L"temp_d3d_window") ||
        containsText(cls, L"crashpad") ||
        containsText(cls, L"intermediate d3d")) {
        return 0;
    }

    // A genuine GUI window has a real on-screen size.
    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect)) return 0;
    const int width  = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width < 256 || height < 256) return 0;

    const std::wstring title = toLower(windowText(hwnd));
    int score = 40;  // base: a real, visible, correctly-sized emulator window
    if (containsText(title, L"emulator")) score += 30;
    if (!search.instanceName.empty() && containsText(title, search.instanceName))
        score += 60;
    if (!search.consolePort.empty() && containsText(title, search.consolePort))
        score += 45;
    // Prefer the largest window (the device screen) over slim side toolbars.
    score += (std::min)(width * height / 20000, 60);
    return score;
}

BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto *search = reinterpret_cast<WindowSearch *>(lParam);
    const int score = scoreWindow(hwnd, *search);
    if (score > search->best.score) {
        search->best = {hwnd, score};
    }
    return TRUE;
}

bool wasTracked(HWND hwnd, const std::vector<HWND> &windows) {
    return std::find(windows.begin(), windows.end(), hwnd) != windows.end();
}

bool shouldHideAuxiliaryWindow(HWND hwnd, const AuxiliaryWindowSearch &search) {
    if (hwnd == search.mainWindow || !IsWindowVisible(hwnd)) return false;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search.processId) return false;

    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect)) return false;

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const std::wstring title = toLower(windowText(hwnd));
    const bool slimToolbar = (width <= 180 && height >= 180) || (height <= 160 && width >= 220);
    const bool knownAuxiliary = containsText(title, L"extended controls") ||
                                containsText(title, L"emulator controls");
    return slimToolbar || knownAuxiliary;
}

BOOL CALLBACK enumAuxiliaryWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto *search = reinterpret_cast<AuxiliaryWindowSearch *>(lParam);
    if (!search->hiddenWindows || !shouldHideAuxiliaryWindow(hwnd, *search)) return TRUE;

    ShowWindow(hwnd, SW_HIDE);
    if (!wasTracked(hwnd, *search->hiddenWindows)) {
        search->hiddenWindows->push_back(hwnd);
        qDebug() << "Native emulator auxiliary window hidden:" << QString::fromStdWString(windowText(hwnd));
    }
    return TRUE;
}

} // namespace
#endif

NativeEmulatorView::NativeEmulatorView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, false);

    m_probeTimer.setInterval(100);
    connect(&m_probeTimer, &QTimer::timeout, this, [this]() {
        probeAndAttach();
    });
    m_probeTimer.start();

    m_auxiliaryWindowTimer.setInterval(1000);
    connect(&m_auxiliaryWindowTimer, &QTimer::timeout, this, [this]() {
        hideAuxiliaryWindows();
    });

    connect(&m_recordingTimer, &QTimer::timeout, this, [this]() {
        if (!m_nativeRecorder) return;
        const QImage frame = grabFrame();
        if (!frame.isNull()) {
            m_nativeRecorder->feedFrame(frame);
        }
    });
}

NativeEmulatorView::~NativeEmulatorView() {
    stopRecording();
#ifdef Q_OS_WIN
    detachWindow();
#endif
}

QString NativeEmulatorView::instanceName() const {
    return m_instanceName;
}

void NativeEmulatorView::setInstanceName(const QString &name) {
    if (m_instanceName == name) return;
    m_instanceName = name;
    emit instanceNameChanged();
}

int NativeEmulatorView::consolePort() const {
    return m_consolePort;
}

void NativeEmulatorView::setConsolePort(int port) {
    if (m_consolePort == port) return;
    m_consolePort = port;
    emit consolePortChanged();
}

void NativeEmulatorView::setEmulatorPid(uint32_t pid) {
    m_emulatorPid = pid;
    qDebug() << "NativeEmulatorView: pinned to emulator PID" << pid;
}

bool NativeEmulatorView::attached() const {
    return m_attached;
}

bool NativeEmulatorView::isRecording() const {
    return m_recording;
}

bool NativeEmulatorView::nativeEmbeddingEnabled() const {
    return m_nativeEmbeddingEnabled;
}

void NativeEmulatorView::setNativeEmbeddingEnabled(bool enabled) {
    if (m_nativeEmbeddingEnabled == enabled) return;
    m_nativeEmbeddingEnabled = enabled;
#ifdef Q_OS_WIN
    if (!enabled) detachWindow();
#endif
    emit nativeEmbeddingEnabledChanged();
}

bool NativeEmulatorView::saveScreenshot(const QString &filePath) const {
    const QImage frame = grabFrame();
    return !frame.isNull() && frame.save(filePath);
}

bool NativeEmulatorView::startRecording(const QString &filePath, int fps) {
    if (m_recording) return true;
    if (fps <= 0) return false;
#ifdef Q_OS_WIN
    if (!m_childWindow) return false;
#else
    return false;
#endif

    m_nativeRecorder = new ScreenRecorder(this);
    m_nativeRecorder->startRecording(filePath, fps);
    m_recording = true;
    emit recordingChanged();

    m_recordingTimer.start((std::max)(1, 1000 / fps));
    return true;
}

void NativeEmulatorView::stopRecording() {
    if (!m_recording) return;
    m_recordingTimer.stop();
    if (m_nativeRecorder) {
        m_nativeRecorder->stopRecording();
        m_nativeRecorder->deleteLater();
        m_nativeRecorder = nullptr;
    }
    m_recording = false;
    emit recordingChanged();
}

void NativeEmulatorView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    updateNativeGeometry();
}

void NativeEmulatorView::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange) {
        updateNativeGeometry();
    }
}

void NativeEmulatorView::probeAndAttach() {
#ifdef Q_OS_WIN
    if (!m_nativeEmbeddingEnabled || m_childWindow) return;
    HWND hwnd = findEmulatorWindow();
    if (hwnd) attachWindow(hwnd);
#endif
}

void NativeEmulatorView::hideAuxiliaryWindows() {
#ifdef Q_OS_WIN
    if (!m_childWindow || !m_childProcessId) return;

    AuxiliaryWindowSearch search;
    search.mainWindow = m_childWindow;
    search.processId = m_childProcessId;
    search.hiddenWindows = &m_hiddenAuxiliaryWindows;
    EnumWindows(enumAuxiliaryWindowsCallback, reinterpret_cast<LPARAM>(&search));
#endif
}

void NativeEmulatorView::updateNativeGeometry() {
#ifdef Q_OS_WIN
    if (!m_childWindow || !window()) return;
    if (!isVisible() || width() <= 0 || height() <= 0) {
        SetWindowPos(m_childWindow, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        return;
    }

    const qreal scale = window()->devicePixelRatio();
    const QRectF sceneRect = mapRectToScene(boundingRect());
    const int x = static_cast<int>(std::round(sceneRect.x() * scale));
    const int y = static_cast<int>(std::round(sceneRect.y() * scale));
    const int w = static_cast<int>(std::round(sceneRect.width() * scale));
    const int h = static_cast<int>(std::round(sceneRect.height() * scale));
    const QRect targetRect(x, y, w, h);
    if (targetRect == m_lastNativeRect) return;
    if (w < 16 || h < 16) return;  // ignore transient zero/tiny layout passes

    // Atomically position and show; SWP_SHOWWINDOW avoids the flicker from
    // calling ShowWindow then MoveWindow separately.
    SetWindowPos(m_childWindow, HWND_TOP, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_lastNativeRect = targetRect;
#endif
}

void NativeEmulatorView::setAttached(bool attached) {
    if (m_attached == attached) return;
    m_attached = attached;
    emit attachedChanged();
}

QImage NativeEmulatorView::grabFrame() const {
#ifdef Q_OS_WIN
    if (!m_childWindow || !window()) return {};
    QScreen *screen = window()->screen() ? window()->screen() : QGuiApplication::primaryScreen();
    if (!screen) return {};

    const QPixmap pixmap = screen->grabWindow(reinterpret_cast<WId>(m_childWindow));
    return pixmap.isNull() ? QImage() : pixmap.toImage();
#else
    return {};
#endif
}

#ifdef Q_OS_WIN
HWND NativeEmulatorView::findEmulatorWindow() const {
    WindowSearch search;
    search.instanceName = toLower(m_instanceName.toStdWString());
    search.consolePort = std::to_wstring(m_consolePort);
    search.hostWindow = window() ? reinterpret_cast<HWND>(window()->winId()) : nullptr;
    if (m_emulatorPid != 0)
        search.targetTree = collectProcessTree(static_cast<DWORD>(m_emulatorPid));

    EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&search));
    if (!search.best.hwnd) return nullptr;

    // When the emulator PID is known, any correctly-sized Qt window inside that
    // exact process tree is unambiguously ours, so a low threshold is safe.
    // Without a PID we require strong title evidence (instance name + port) so
    // that an unrelated emulator window is never stolen.
    const int minScore = search.targetTree.empty() ? 150 : 40;
    return search.best.score >= minScore ? search.best.hwnd : nullptr;
}

void NativeEmulatorView::attachWindow(HWND hwnd) {
    if (!hwnd || !window()) return;

    // Hide if already visible. When STARTF_USESHOWWINDOW|SW_HIDE was passed at
    // launch the window is already hidden, so no-op in the common path.
    if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
    }

    HWND host = reinterpret_cast<HWND>(window()->winId());
    GetWindowThreadProcessId(hwnd, &m_childProcessId);
    m_originalStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
    m_originalExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    // Ensure the host window clips its children so Qt's D3D11 swap chain
    // doesn't paint over the embedded emulator child window.
    LONG_PTR hostStyle = GetWindowLongPtrW(host, GWL_STYLE);
    if (!(hostStyle & WS_CLIPCHILDREN)) {
        SetWindowLongPtrW(host, GWL_STYLE, hostStyle | WS_CLIPCHILDREN);
        SetWindowPos(host, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    LONG_PTR style = m_originalStyle;
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

    LONG_PTR exStyle = m_originalExStyle;
    exStyle &= ~(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW | WS_EX_TOPMOST);

    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    SetParent(hwnd, host);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    m_childWindow = hwnd;
    qDebug() << "Native emulator window attached:"
             << QString::fromStdWString(windowText(hwnd))
             << "class" << QString::fromStdWString(windowClassName(hwnd));
    setAttached(true);
    m_probeTimer.stop();
    updateNativeGeometry();
    // NOTE: the emulator's auxiliary windows (toolbar / extended controls) are
    // deliberately NOT hidden. The modern Android emulator destroys its main
    // device window when sibling windows in its group are hidden, which blanks
    // the embedded display — so leaving them alone is required for rendering.
}

void NativeEmulatorView::detachWindow() {
    m_auxiliaryWindowTimer.stop();
    if (!m_childWindow) {
        setAttached(false);
        return;
    }

    if (IsWindow(m_childWindow)) {
        SetParent(m_childWindow, nullptr);
        SetWindowLongPtrW(m_childWindow, GWL_STYLE, m_originalStyle);
        SetWindowLongPtrW(m_childWindow, GWL_EXSTYLE, m_originalExStyle);
        SetWindowPos(m_childWindow, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    m_childWindow = nullptr;
    m_childProcessId = 0;
    m_lastNativeRect = QRect();
    m_hiddenAuxiliaryWindows.clear();
    setAttached(false);
    if (m_nativeEmbeddingEnabled && !m_probeTimer.isActive()) {
        m_probeTimer.start();
    }
}
#endif

} // namespace chimera
