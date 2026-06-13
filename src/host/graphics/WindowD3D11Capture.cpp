#include "WindowD3D11Capture.h"

#include "SharedD3D11TexturePublisher.h"
#include "SharedMemoryFrameAbi.h"

#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <cwctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#endif

namespace chimera::graphics {

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;
namespace wg = winrt::Windows::Graphics;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wgd11 = winrt::Windows::Graphics::DirectX::Direct3D11;

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

std::wstring windowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring text(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

std::wstring windowClassName(HWND hwnd) {
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    return lower(cls);
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
    return lower(path);
}

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

struct WindowSearch {
    std::vector<DWORD> processTree;
    std::wstring instanceName;
    std::wstring consolePort;
    HWND best = nullptr;
    int bestScore = 0;
};

int scoreWindow(HWND hwnd, const WindowSearch &search) {
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return 0;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!search.processTree.empty() &&
        std::find(search.processTree.begin(), search.processTree.end(), pid) ==
            search.processTree.end()) {
        return 0;
    }

    const std::wstring image = processImageName(pid);
    if (image.find(L"\\emulator.exe") == std::wstring::npos &&
        image.find(L"\\qemu-system-x86_64") == std::wstring::npos) {
        return 0;
    }

    const std::wstring cls = windowClassName(hwnd);
    if (cls == L"nvogldc" || cls == L"nvopenglpbuffer" || cls == L"dummywin" ||
        cls == L"default ime" || cls == L"msctfime ui" || cls == L"ime" ||
        cls.find(L"screenchangeobserver") != std::wstring::npos ||
        cls.find(L"temp_d3d_window") != std::wstring::npos ||
        cls.find(L"crashpad") != std::wstring::npos ||
        cls.find(L"intermediate d3d") != std::wstring::npos) {
        return 0;
    }

    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect)) return 0;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width < 800 || height < 600) return 0;

    const std::wstring title = lower(windowText(hwnd));
    int score = width * height / 20000;
    if (IsWindowVisible(hwnd)) score += 20;
    if (title.find(L"emulator") != std::wstring::npos) score += 30;
    if (!search.instanceName.empty() && title.find(search.instanceName) != std::wstring::npos)
        score += 60;
    if (!search.consolePort.empty() && title.find(search.consolePort) != std::wstring::npos)
        score += 45;
    return score;
}

BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto *search = reinterpret_cast<WindowSearch *>(lParam);
    const int score = scoreWindow(hwnd, *search);
    if (score > search->bestScore) {
        search->bestScore = score;
        search->best = hwnd;
    }
    return TRUE;
}

HWND findEmulatorWindow(DWORD rootPid, const QString &instanceName, int consolePort) {
    WindowSearch search;
    search.processTree = collectProcessTree(rootPid);
    search.instanceName = lower(instanceName.toStdWString());
    search.consolePort = std::to_wstring(consolePort);
    EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

void parkWindowOffscreen(HWND hwnd) {
    if (!hwnd) return;
    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect)) return;
    const int width = (std::max)(static_cast<int>(rect.right - rect.left), 128);
    const int height = (std::max)(static_cast<int>(rect.bottom - rect.top), 128);
    SetWindowPos(hwnd, HWND_BOTTOM, -32000, -32000, width, height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

BOOL CALLBACK parkProcessWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto *pids = reinterpret_cast<std::vector<DWORD> *>(lParam);
    if (!pids || GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (std::find(pids->begin(), pids->end(), pid) == pids->end()) return TRUE;
    parkWindowOffscreen(hwnd);
    return TRUE;
}

void parkProcessWindowsOffscreen(DWORD rootPid) {
    std::vector<DWORD> pids = collectProcessTree(rootPid);
    EnumWindows(parkProcessWindowsCallback, reinterpret_cast<LPARAM>(&pids));
}

ComPtr<ID3D11Device> createDevice(QString *error) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, 2, D3D11_SDK_VERSION,
                                   &device, nullptr, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, 2, D3D11_SDK_VERSION,
                               &device, nullptr, &context);
    }
    if (FAILED(hr) && error) {
        *error = QStringLiteral("D3D11CreateDevice failed (0x%1)")
                     .arg(static_cast<qulonglong>(hr), 0, 16);
    }
    return SUCCEEDED(hr) ? device : nullptr;
}

wgd11::IDirect3DDevice createWinrtDevice(ID3D11Device *device) {
    ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));

    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
        dxgiDevice.Get(), inspectable.put()));
    return inspectable.as<wgd11::IDirect3DDevice>();
}

wgc::GraphicsCaptureItem createCaptureItem(HWND hwnd) {
    auto interop = winrt::get_activation_factory<
        wgc::GraphicsCaptureItem,
        IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(
        hwnd,
        winrt::guid_of<wgc::GraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

ID3D11Texture2D *textureFromSurface(const wgd11::IDirect3DSurface &surface) {
    auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ID3D11Texture2D *texture = nullptr;
    winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),
                                              reinterpret_cast<void **>(&texture)));
    return texture;
}

} // namespace
#endif

struct WindowD3D11Capture::Impl {
#ifdef _WIN32
    HWND hwnd = nullptr;
    ComPtr<ID3D11Device> device;
    std::unique_ptr<SharedD3D11TexturePublisher> publisher;
    wgd11::IDirect3DDevice winrtDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool framePool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrived;
    QSize size;
#endif
};

WindowD3D11Capture::WindowD3D11Capture(quint32 rootProcessId,
                                       QString instanceName,
                                       int consolePort,
                                       QObject *parent)
    : FramebufferCapture(parent),
      d(std::make_unique<Impl>()),
      m_rootProcessId(rootProcessId),
      m_instanceName(std::move(instanceName)),
      m_consolePort(consolePort) {
    m_retryTimer.setInterval(250);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (tryStartCapture())
            m_retryTimer.stop();
    });
}

WindowD3D11Capture::~WindowD3D11Capture() {
    stop();
}

bool WindowD3D11Capture::start() {
#ifdef _WIN32
    if (m_running) return true;
    m_running = true;
    if (tryStartCapture()) return true;
    m_retryTimer.start();
    return true;
#else
    emit captureError(QStringLiteral("window D3D11 capture is Windows-only"));
    return false;
#endif
}

void WindowD3D11Capture::stop() {
    m_retryTimer.stop();
    teardownCapture();
    m_running = false;
}

bool WindowD3D11Capture::isRunning() const {
    return m_running;
}

bool WindowD3D11Capture::tryStartCapture() {
#ifdef _WIN32
    if (!m_running || d->session) return d->session != nullptr;
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        emit captureError(QStringLiteral("Windows Graphics Capture is not supported"));
        m_running = false;
        return false;
    }

    d->hwnd = findEmulatorWindow(static_cast<DWORD>(m_rootProcessId),
                                 m_instanceName, m_consolePort);
    if (!d->hwnd) return false;
    parkProcessWindowsOffscreen(static_cast<DWORD>(m_rootProcessId));

    try {
        QString error;
        d->device = createDevice(&error);
        if (!d->device) {
            emit captureError(error);
            m_running = false;
            return false;
        }
        d->winrtDevice = createWinrtDevice(d->device.Get());
        d->item = createCaptureItem(d->hwnd);
        const wg::SizeInt32 itemSize = d->item.Size();
        if (itemSize.Width < static_cast<int>(shmem::kMinimumFrameWidth) ||
            itemSize.Height < static_cast<int>(shmem::kMinimumFrameHeight)) {
            emit captureError(QStringLiteral("window capture size below 1920x1080 minimum"));
            m_running = false;
            return false;
        }
        d->size = QSize(itemSize.Width, itemSize.Height);

        const QString suffix = QString::number(QCoreApplication::applicationPid());
        SharedD3D11TexturePublisher::Config config;
        config.metadataName = QStringLiteral("Local\\ChimeraWindowD3D11Meta_%1").arg(suffix);
        config.textureName = QStringLiteral("Local\\ChimeraWindowD3D11Texture_%1").arg(suffix);
        config.frameEventName = QStringLiteral("Local\\ChimeraWindowD3D11Event_%1").arg(suffix);
        config.size = d->size;
        config.d3d11Device = d->device.Get();
        d->publisher = std::make_unique<SharedD3D11TexturePublisher>(config);
        if (!d->publisher->start(&error)) {
            emit captureError(error);
            m_running = false;
            return false;
        }

        d->framePool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            d->winrtDevice,
            wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            itemSize);
        d->session = d->framePool.CreateCaptureSession(d->item);
        d->session.IsCursorCaptureEnabled(false);
        d->frameArrived = d->framePool.FrameArrived(
            winrt::auto_revoke,
            [this](const wgc::Direct3D11CaptureFramePool &sender, const winrt::Windows::Foundation::IInspectable &) {
                if (!m_running || !d->publisher) return;
                try {
                    const auto frame = sender.TryGetNextFrame();
                    if (!frame) return;
                    const auto contentSize = frame.ContentSize();
                    if (contentSize.Width != d->size.width() ||
                        contentSize.Height != d->size.height()) {
                        emit captureError(QStringLiteral("window capture size changed; restart required"));
                        return;
                    }
                    ComPtr<ID3D11Texture2D> texture;
                    texture.Attach(textureFromSurface(frame.Surface()));
                    QString error;
                    if (!d->publisher->publishTexture(texture.Get(), &error)) {
                        emit captureError(error);
                        return;
                    }
                    emit streamFrameReceived(true);
                    emit sharedD3D11TextureReady(d->publisher->textureName(),
                                                 d->size,
                                                 d->publisher->sequence(),
                                                 true);
                } catch (const winrt::hresult_error &e) {
                    emit captureError(QStringLiteral("window capture frame failed (0x%1)")
                                          .arg(static_cast<qulonglong>(e.code()), 0, 16));
                }
            });
        d->session.StartCapture();
        qDebug() << "Window D3D11 capture started at" << d->size;
        return true;
    } catch (const winrt::hresult_error &e) {
        emit captureError(QStringLiteral("window D3D11 capture start failed (0x%1)")
                              .arg(static_cast<qulonglong>(e.code()), 0, 16));
        teardownCapture();
        m_running = false;
        return false;
    }
#else
    return false;
#endif
}

void WindowD3D11Capture::teardownCapture() {
#ifdef _WIN32
    if (d->frameArrived) d->frameArrived.revoke();
    if (d->session) d->session.Close();
    if (d->framePool) d->framePool.Close();
    if (d->publisher) d->publisher->stop();
    d->frameArrived = {};
    d->session = nullptr;
    d->framePool = nullptr;
    d->item = nullptr;
    d->winrtDevice = nullptr;
    d->publisher.reset();
    d->device.Reset();
    d->hwnd = nullptr;
    d->size = QSize();
#endif
}

} // namespace chimera::graphics
