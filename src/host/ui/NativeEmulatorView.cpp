#include "NativeEmulatorView.h"

#include "ScreenRecorder.h"

#include <QGuiApplication>
#include <QDebug>
#include <QPixmap>
#include <QQuickWindow>
#include <QScreen>

#include <algorithm>

#ifdef Q_OS_WIN
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
    WindowCandidate best;
};

struct AuxiliaryWindowSearch {
    HWND mainWindow = nullptr;
    DWORD processId = 0;
    std::vector<HWND> *hiddenWindows = nullptr;
};

int scoreWindow(HWND hwnd, const WindowSearch &search) {
    if (hwnd == search.hostWindow) return 0;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return 0;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return 0;

    const std::wstring image = processImageName(pid);
    const bool isKnownEmulator = containsText(image, L"\\emulator.exe") ||
                                 containsText(image, L"\\qemu-system-x86_64");

    // Score invisible windows only from known emulator processes; this lets us
    // attach the window before it becomes visible, eliminating the flash.
    if (!IsWindowVisible(hwnd) && !isKnownEmulator) return 0;

    const std::wstring title = toLower(windowText(hwnd));
    int score = 0;

    if (containsText(image, L"\\emulator.exe")) score += 35;
    if (containsText(image, L"\\qemu-system-x86_64")) score += 35;
    if (containsText(title, L"android emulator")) score += 35;
    if (containsText(title, search.instanceName)) score += 80;
    if (containsText(title, search.consolePort)) score += 45;

    if (IsWindowVisible(hwnd)) {
        RECT rect = {};
        if (GetWindowRect(hwnd, &rect)) {
            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;
            if (width >= 320 && height >= 240) score += 10;
        }
    }
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

    EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&search));
    if (!search.best.hwnd) return nullptr;

    // Pre-visible windows from emulator.exe only need a process-name match (35 pts).
    // Visible windows require stronger evidence (70 pts) to avoid false positives.
    const int minScore = IsWindowVisible(search.best.hwnd) ? 70 : 35;
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
    qDebug() << "Native emulator window attached:" << QString::fromStdWString(windowText(hwnd));
    setAttached(true);
    m_probeTimer.stop();
    hideAuxiliaryWindows();
    m_auxiliaryWindowTimer.start();
    updateNativeGeometry();
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
