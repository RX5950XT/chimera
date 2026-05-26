#include <QtTest/QtTest>

#include "FramebufferCapture.h"
#include "SharedD3D11TextureCapture.h"
#include "SharedMemoryFrameAbi.h"
#include <QUuid>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgiformat.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

#ifdef Q_OS_WIN
namespace {

Microsoft::WRL::ComPtr<ID3D11Device> createTestDevice() {
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
    return SUCCEEDED(hr) ? device : nullptr;
}

} // namespace
#endif

class TestSharedD3D11TextureCapture : public QObject {
    Q_OBJECT

private slots:
    void emitsSharedTextureMetadata();
};

void TestSharedD3D11TextureCapture::emitsSharedTextureMetadata() {
#ifndef Q_OS_WIN
    QSKIP("Shared D3D11 texture capture is Windows-only");
#else
    using namespace chimera::graphics::shmem;

    const QString mappingName = QStringLiteral("Local\\ChimeraD3D11Meta_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString textureName = QStringLiteral("Local\\ChimeraD3D11Texture_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, sizeof(SharedD3D11TextureHeader),
                                        reinterpret_cast<LPCWSTR>(mappingName.utf16()));
    QVERIFY(mapping);

    auto *header = static_cast<SharedD3D11TextureHeader *>(
        MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(SharedD3D11TextureHeader)));
    QVERIFY(header);

    Microsoft::WRL::ComPtr<ID3D11Device> producerDevice = createTestDevice();
    QVERIFY(producerDevice);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> producerTexture;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 64;
    desc.Height = 64;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                     D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    QVERIFY(SUCCEEDED(producerDevice->CreateTexture2D(&desc, nullptr, &producerTexture)));

    Microsoft::WRL::ComPtr<IDXGIResource1> sharedResource;
    QVERIFY(SUCCEEDED(producerTexture.As(&sharedResource)));
    HANDLE sharedHandle = nullptr;
    QVERIFY(SUCCEEDED(sharedResource->CreateSharedHandle(
        nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        reinterpret_cast<LPCWSTR>(textureName.utf16()),
        &sharedHandle)));
    QVERIFY(sharedHandle);

    Microsoft::WRL::ComPtr<ID3D11Device> consumerDevice = createTestDevice();
    QVERIFY(consumerDevice);
    Microsoft::WRL::ComPtr<ID3D11Device1> consumerDevice1;
    QVERIFY(SUCCEEDED(consumerDevice.As(&consumerDevice1)));
    Microsoft::WRL::ComPtr<ID3D11Texture2D> openedTexture;
    QVERIFY(SUCCEEDED(consumerDevice1->OpenSharedResourceByName(
        reinterpret_cast<LPCWSTR>(textureName.utf16()),
        DXGI_SHARED_RESOURCE_READ,
        IID_PPV_ARGS(&openedTexture))));

    *header = {};
    header->magic = kD3D11TextureMagic;
    header->version = kVersion;
    header->headerSize = sizeof(SharedD3D11TextureHeader);
    header->width = desc.Width;
    header->height = desc.Height;
    header->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    header->flags = kD3D11FlagHasAlpha;
    const auto nameUtf16 = textureName.utf16();
    for (qsizetype i = 0; i < textureName.size() && i < kD3D11TextureNameChars - 1; ++i)
        header->textureName[i] = static_cast<char16_t>(nameUtf16[i]);
    MemoryBarrier();
    header->sequence = 2;

    chimera::graphics::SharedD3D11TextureCapture capture(mappingName);
    capture.setIntervalMs(1);
    QSignalSpy spy(&capture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady);
    QSignalSpy streamSpy(&capture, &chimera::graphics::FramebufferCapture::streamFrameReceived);

    QVERIFY(capture.start());
    if (spy.isEmpty())
        QVERIFY(spy.wait(500));

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), textureName);
    QCOMPARE(args.at(1).toSize(), QSize(static_cast<int>(desc.Width), static_cast<int>(desc.Height)));
    QCOMPARE(args.at(2).toULongLong(), 2ULL);
    QCOMPARE(args.at(3).toBool(), true);
    QCOMPARE(streamSpy.size(), 1);

    QTest::qWait(30);
    QCOMPARE(spy.size(), 0);
    QCOMPARE(streamSpy.size(), 1);

    capture.stop();
    UnmapViewOfFile(header);
    CloseHandle(mapping);
    CloseHandle(sharedHandle);
#endif
}

QTEST_MAIN(TestSharedD3D11TextureCapture)
#include "test_shared_d3d11_texture_capture.moc"
