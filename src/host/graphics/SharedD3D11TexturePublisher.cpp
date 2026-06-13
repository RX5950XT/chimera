#include "SharedD3D11TexturePublisher.h"
#include "SharedMemoryFrameAbi.h"

#include <algorithm>
#include <iterator>

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
#include <wrl/client.h>
#endif

namespace chimera::graphics {

namespace {

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

#ifdef _WIN32
QString winError(const char *api) {
    return QStringLiteral("%1 failed (%2)")
        .arg(QString::fromLatin1(api))
        .arg(GetLastError());
}

bool validConfig(const SharedD3D11TexturePublisher::Config &config, QString *error) {
    if (config.metadataName.isEmpty()) {
        setError(error, QStringLiteral("metadata mapping name is empty"));
        return false;
    }
    if (config.textureName.isEmpty()) {
        setError(error, QStringLiteral("shared texture name is empty"));
        return false;
    }
    if (config.size.width() <= 0 || config.size.height() <= 0 ||
        config.size.width() > 7680 || config.size.height() > 4320) {
        setError(error, QStringLiteral("invalid shared texture size"));
        return false;
    }
    if (config.size.width() < static_cast<int>(shmem::kMinimumFrameWidth) ||
        config.size.height() < static_cast<int>(shmem::kMinimumFrameHeight)) {
        setError(error, QStringLiteral("shared texture size below 1920x1080 minimum"));
        return false;
    }
    return true;
}

Microsoft::WRL::ComPtr<ID3D11Device> createDevice(QString *error) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   &level, 1, D3D11_SDK_VERSION,
                                   &device, nullptr, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                               &level, 1, D3D11_SDK_VERSION,
                               &device, nullptr, &context);
    }
    if (FAILED(hr))
        setError(error, QStringLiteral("D3D11CreateDevice failed (0x%1)")
                            .arg(static_cast<qulonglong>(hr), 0, 16));
    return SUCCEEDED(hr) ? device : nullptr;
}

void writeHeader(chimera::graphics::shmem::SharedD3D11TextureHeader *header,
                 const SharedD3D11TexturePublisher::Config &config,
                 quint64 sequence) {
    using namespace chimera::graphics::shmem;
    header->sequence = sequence | 1ULL;
    MemoryBarrier();

    header->magic = kD3D11TextureMagic;
    header->version = kVersion;
    header->headerSize = sizeof(SharedD3D11TextureHeader);
    header->width = static_cast<quint32>(config.size.width());
    header->height = static_cast<quint32>(config.size.height());
    header->dxgiFormat = config.dxgiFormat;
    header->flags = config.hasAlpha ? kD3D11FlagHasAlpha : 0;
    std::fill(std::begin(header->textureName), std::end(header->textureName), char16_t{});

    const auto name = config.textureName.utf16();
    const qsizetype len = (std::min)(config.textureName.size(),
                                     static_cast<qsizetype>(kD3D11TextureNameChars - 1));
    for (qsizetype i = 0; i < len; ++i)
        header->textureName[i] = static_cast<char16_t>(name[i]);

    MemoryBarrier();
    header->sequence = sequence;
}
#endif

} // namespace

struct SharedD3D11TexturePublisher::Impl {
    explicit Impl(Config cfg) : config(std::move(cfg)) {}

    Config config;
    quint64 sequence = 0;
#ifdef _WIN32
    HANDLE mapping = nullptr;
    HANDLE event = nullptr;
    HANDLE sharedHandle = nullptr;
    shmem::SharedD3D11TextureHeader *header = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
#endif
};

SharedD3D11TexturePublisher::SharedD3D11TexturePublisher(Config config)
    : d(std::make_unique<Impl>(std::move(config))) {
}

SharedD3D11TexturePublisher::~SharedD3D11TexturePublisher() {
    stop();
}

bool SharedD3D11TexturePublisher::start(QString *error) {
#ifdef _WIN32
    if (isRunning()) return true;
    if (!validConfig(d->config, error)) return false;

    d->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                    0, sizeof(shmem::SharedD3D11TextureHeader),
                                    reinterpret_cast<LPCWSTR>(d->config.metadataName.utf16()));
    if (!d->mapping) {
        setError(error, winError("CreateFileMappingW"));
        return false;
    }

    d->header = static_cast<shmem::SharedD3D11TextureHeader *>(
        MapViewOfFile(d->mapping, FILE_MAP_WRITE, 0, 0,
                      sizeof(shmem::SharedD3D11TextureHeader)));
    if (!d->header) {
        setError(error, winError("MapViewOfFile"));
        stop();
        return false;
    }

    if (!d->config.frameEventName.isEmpty()) {
        d->event = CreateEventW(nullptr, FALSE, FALSE,
                                reinterpret_cast<LPCWSTR>(d->config.frameEventName.utf16()));
        if (!d->event) {
            setError(error, winError("CreateEventW"));
            stop();
            return false;
        }
    }

    if (d->config.d3d11Device) {
        d->device = static_cast<ID3D11Device *>(d->config.d3d11Device);
    } else {
        d->device = createDevice(error);
    }
    if (!d->device) {
        stop();
        return false;
    }
    d->device->GetImmediateContext(&d->context);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(d->config.size.width());
    desc.Height = static_cast<UINT>(d->config.size.height());
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = static_cast<DXGI_FORMAT>(d->config.dxgiFormat);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                     D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr = d->device->CreateTexture2D(&desc, nullptr, &d->texture);
    if (FAILED(hr)) {
        setError(error, QStringLiteral("CreateTexture2D failed (0x%1)")
                            .arg(static_cast<qulonglong>(hr), 0, 16));
        stop();
        return false;
    }

    hr = d->device->CreateRenderTargetView(d->texture.Get(), nullptr, &d->renderTarget);
    if (FAILED(hr)) {
        setError(error, QStringLiteral("CreateRenderTargetView failed (0x%1)")
                            .arg(static_cast<qulonglong>(hr), 0, 16));
        stop();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIResource1> resource;
    hr = d->texture.As(&resource);
    if (FAILED(hr)) {
        setError(error, QStringLiteral("IDXGIResource1 query failed (0x%1)")
                            .arg(static_cast<qulonglong>(hr), 0, 16));
        stop();
        return false;
    }
    hr = resource->CreateSharedHandle(
        nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        reinterpret_cast<LPCWSTR>(d->config.textureName.utf16()),
        &d->sharedHandle);
    if (FAILED(hr)) {
        setError(error, QStringLiteral("CreateSharedHandle failed (0x%1)")
                            .arg(static_cast<qulonglong>(hr), 0, 16));
        stop();
        return false;
    }

    *d->header = {};
    d->sequence = 0;
    return true;
#else
    setError(error, QStringLiteral("shared D3D11 texture publisher is Windows-only"));
    return false;
#endif
}

void SharedD3D11TexturePublisher::stop() {
#ifdef _WIN32
    d->renderTarget.Reset();
    d->texture.Reset();
    d->context.Reset();
    d->device.Reset();
    if (d->sharedHandle) {
        CloseHandle(d->sharedHandle);
        d->sharedHandle = nullptr;
    }
    if (d->header) {
        UnmapViewOfFile(d->header);
        d->header = nullptr;
    }
    if (d->event) {
        CloseHandle(d->event);
        d->event = nullptr;
    }
    if (d->mapping) {
        CloseHandle(d->mapping);
        d->mapping = nullptr;
    }
#endif
    d->sequence = 0;
}

bool SharedD3D11TexturePublisher::isRunning() const {
#ifdef _WIN32
    return d->header && d->texture && d->context;
#else
    return false;
#endif
}

bool SharedD3D11TexturePublisher::publishColor(float red,
                                               float green,
                                               float blue,
                                               float alpha,
                                               QString *error) {
#ifdef _WIN32
    if (!isRunning()) {
        setError(error, QStringLiteral("shared D3D11 texture publisher is not running"));
        return false;
    }
    const float color[] = {red, green, blue, alpha};
    d->context->ClearRenderTargetView(d->renderTarget.Get(), color);
    d->context->Flush();
    d->sequence += 2;
    writeHeader(d->header, d->config, d->sequence);
    if (d->event) SetEvent(d->event);
    return true;
#else
    setError(error, QStringLiteral("shared D3D11 texture publisher is Windows-only"));
    return false;
#endif
}

bool SharedD3D11TexturePublisher::publishTexture(void *d3d11Texture, QString *error) {
#ifdef _WIN32
    if (!isRunning()) {
        setError(error, QStringLiteral("shared D3D11 texture publisher is not running"));
        return false;
    }
    if (!d3d11Texture) {
        setError(error, QStringLiteral("source D3D11 texture is null"));
        return false;
    }
    d->context->CopyResource(d->texture.Get(), static_cast<ID3D11Texture2D *>(d3d11Texture));
    d->context->Flush();
    d->sequence += 2;
    writeHeader(d->header, d->config, d->sequence);
    if (d->event) SetEvent(d->event);
    return true;
#else
    Q_UNUSED(d3d11Texture);
    setError(error, QStringLiteral("shared D3D11 texture publisher is Windows-only"));
    return false;
#endif
}

bool SharedD3D11TexturePublisher::publishBgraFrame(const void *data,
                                                   int bytesPerLine,
                                                   QString *error) {
#ifdef _WIN32
    if (!isRunning()) {
        setError(error, QStringLiteral("shared D3D11 texture publisher is not running"));
        return false;
    }
    if (!data) {
        setError(error, QStringLiteral("BGRA frame data is null"));
        return false;
    }
    const int minBytesPerLine = d->config.size.width() * 4;
    if (bytesPerLine < minBytesPerLine) {
        setError(error, QStringLiteral("BGRA frame stride is too small"));
        return false;
    }
    d->context->UpdateSubresource(d->texture.Get(), 0, nullptr, data,
                                  static_cast<UINT>(bytesPerLine), 0);
    d->context->Flush();
    d->sequence += 2;
    writeHeader(d->header, d->config, d->sequence);
    if (d->event) SetEvent(d->event);
    return true;
#else
    Q_UNUSED(data);
    Q_UNUSED(bytesPerLine);
    setError(error, QStringLiteral("shared D3D11 texture publisher is Windows-only"));
    return false;
#endif
}

void *SharedD3D11TexturePublisher::texture() const {
#ifdef _WIN32
    return d->texture.Get();
#else
    return nullptr;
#endif
}

QString SharedD3D11TexturePublisher::metadataName() const {
    return d->config.metadataName;
}

QString SharedD3D11TexturePublisher::textureName() const {
    return d->config.textureName;
}

QString SharedD3D11TexturePublisher::frameEventName() const {
    return d->config.frameEventName;
}

quint64 SharedD3D11TexturePublisher::sequence() const {
    return d->sequence;
}

} // namespace chimera::graphics
