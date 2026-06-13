#include <QtTest/QtTest>

#include "FramebufferCapture.h"
#include "SharedD3D11TextureCapture.h"
#include "SharedD3D11TexturePublisher.h"
#include "SharedMemoryFrameAbi.h"
#include <QUuid>
#include <vector>

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

void writeLowResolutionHeader(void *view, const QString &textureName) {
    auto *header = static_cast<chimera::graphics::shmem::SharedD3D11TextureHeader *>(view);
    *header = {};
    header->magic = chimera::graphics::shmem::kD3D11TextureMagic;
    header->version = chimera::graphics::shmem::kVersion;
    header->headerSize = sizeof(chimera::graphics::shmem::SharedD3D11TextureHeader);
    header->width = 1280;
    header->height = 720;
    header->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    header->flags = chimera::graphics::shmem::kD3D11FlagHasAlpha;
    const auto name = textureName.utf16();
    const qsizetype len = qMin(textureName.size(),
                               static_cast<qsizetype>(
                                   chimera::graphics::shmem::kD3D11TextureNameChars - 1));
    for (qsizetype i = 0; i < len; ++i)
        header->textureName[i] = static_cast<char16_t>(name[i]);
    header->sequence = 2;
}

} // namespace
#endif

class TestSharedD3D11TextureCapture : public QObject {
    Q_OBJECT

private slots:
    void emitsSharedTextureMetadata();
    void publishesBgraFrameMetadata();
    void rejectsLowResolutionPublisher();
    void rejectsLowResolutionTextureMetadata();
};

void TestSharedD3D11TextureCapture::emitsSharedTextureMetadata() {
#ifndef Q_OS_WIN
    QSKIP("Shared D3D11 texture capture is Windows-only");
#else
    const QString mappingName = QStringLiteral("Local\\ChimeraD3D11Meta_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString textureName = QStringLiteral("Local\\ChimeraD3D11Texture_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));

    chimera::graphics::SharedD3D11TexturePublisher::Config config;
    config.metadataName = mappingName;
    config.textureName = textureName;
    config.size = QSize(1920, 1080);
    config.dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    config.hasAlpha = true;
    chimera::graphics::SharedD3D11TexturePublisher publisher(config);
    QString error;
    QVERIFY2(publisher.start(&error), qPrintable(error));
    QVERIFY2(publisher.publishColor(0.1f, 0.2f, 0.3f, 1.0f, &error), qPrintable(error));

    Microsoft::WRL::ComPtr<ID3D11Device> consumerDevice = createTestDevice();
    QVERIFY(consumerDevice);
    Microsoft::WRL::ComPtr<ID3D11Device1> consumerDevice1;
    QVERIFY(SUCCEEDED(consumerDevice.As(&consumerDevice1)));
    Microsoft::WRL::ComPtr<ID3D11Texture2D> openedTexture;
    QVERIFY(SUCCEEDED(consumerDevice1->OpenSharedResourceByName(
        reinterpret_cast<LPCWSTR>(textureName.utf16()),
        DXGI_SHARED_RESOURCE_READ,
        IID_PPV_ARGS(&openedTexture))));

    chimera::graphics::SharedD3D11TextureCapture capture(mappingName);
    capture.setIntervalMs(1);
    QSignalSpy spy(&capture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady);
    QSignalSpy streamSpy(&capture, &chimera::graphics::FramebufferCapture::streamFrameReceived);

    QVERIFY(capture.start());
    if (spy.isEmpty())
        QVERIFY(spy.wait(500));

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), textureName);
    QCOMPARE(args.at(1).toSize(), config.size);
    QCOMPARE(args.at(2).toULongLong(), 2ULL);
    QCOMPARE(args.at(3).toBool(), true);
    QCOMPARE(streamSpy.size(), 1);

    QTest::qWait(30);
    QCOMPARE(spy.size(), 0);
    QCOMPARE(streamSpy.size(), 1);

    capture.stop();
    publisher.stop();
#endif
}

void TestSharedD3D11TextureCapture::publishesBgraFrameMetadata() {
#ifndef Q_OS_WIN
    QSKIP("Shared D3D11 texture capture is Windows-only");
#else
    const QString mappingName = QStringLiteral("Local\\ChimeraD3D11Meta_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString textureName = QStringLiteral("Local\\ChimeraD3D11Texture_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));

    chimera::graphics::SharedD3D11TexturePublisher::Config config;
    config.metadataName = mappingName;
    config.textureName = textureName;
    config.size = QSize(1920, 1080);
    config.dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    config.hasAlpha = true;
    chimera::graphics::SharedD3D11TexturePublisher publisher(config);
    QString error;
    QVERIFY2(publisher.start(&error), qPrintable(error));

    std::vector<unsigned char> frame(
        static_cast<size_t>(config.size.width()) * static_cast<size_t>(config.size.height()) * 4,
        0x7f);
    QVERIFY2(publisher.publishBgraFrame(frame.data(), config.size.width() * 4, &error),
             qPrintable(error));
    QCOMPARE(publisher.sequence(), 2ULL);

    chimera::graphics::SharedD3D11TextureCapture capture(mappingName);
    capture.setIntervalMs(1);
    QSignalSpy spy(&capture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady);
    QVERIFY(capture.start());
    if (spy.isEmpty())
        QVERIFY(spy.wait(500));

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), textureName);
    QCOMPARE(args.at(1).toSize(), config.size);
    QCOMPARE(args.at(2).toULongLong(), 2ULL);
    QCOMPARE(args.at(3).toBool(), true);

    capture.stop();
    publisher.stop();
#endif
}

void TestSharedD3D11TextureCapture::rejectsLowResolutionPublisher() {
#ifndef Q_OS_WIN
    QSKIP("Shared D3D11 texture publisher is Windows-only");
#else
    chimera::graphics::SharedD3D11TexturePublisher::Config config;
    config.metadataName = QStringLiteral("Local\\ChimeraD3D11Meta_%1")
                              .arg(QUuid::createUuid().toString(QUuid::Id128));
    config.textureName = QStringLiteral("Local\\ChimeraD3D11Texture_%1")
                             .arg(QUuid::createUuid().toString(QUuid::Id128));
    config.size = QSize(1280, 720);
    config.dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    chimera::graphics::SharedD3D11TexturePublisher publisher(config);
    QString error;
    QVERIFY(!publisher.start(&error));
    QVERIFY2(error.contains(QStringLiteral("1920x1080")), qPrintable(error));
#endif
}

void TestSharedD3D11TextureCapture::rejectsLowResolutionTextureMetadata() {
#ifndef Q_OS_WIN
    QSKIP("Shared D3D11 texture capture is Windows-only");
#else
    const QString mappingName = QStringLiteral("Local\\ChimeraD3D11Meta_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString textureName = QStringLiteral("Local\\ChimeraD3D11Texture_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::Id128));

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0,
                                        sizeof(chimera::graphics::shmem::SharedD3D11TextureHeader),
                                        reinterpret_cast<LPCWSTR>(mappingName.utf16()));
    QVERIFY(mapping);
    void *view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0,
                               sizeof(chimera::graphics::shmem::SharedD3D11TextureHeader));
    QVERIFY(view);
    writeLowResolutionHeader(view, textureName);

    chimera::graphics::SharedD3D11TextureCapture capture(mappingName);
    QSignalSpy errorSpy(&capture, &chimera::graphics::FramebufferCapture::captureError);
    QSignalSpy textureSpy(&capture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady);

    QVERIFY(capture.start());
    if (errorSpy.isEmpty())
        QVERIFY(errorSpy.wait(500));
    QCOMPARE(textureSpy.size(), 0);
    QVERIFY(errorSpy.takeFirst().at(0).toString().contains(QStringLiteral("1920x1080")));

    capture.stop();
    UnmapViewOfFile(view);
    CloseHandle(mapping);
#endif
}

QTEST_MAIN(TestSharedD3D11TextureCapture)
#include "test_shared_d3d11_texture_capture.moc"
