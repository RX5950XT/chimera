#include <QSignalSpy>
#include <QTest>
#include <QImage>
#include <QColor>
#include <QUuid>
#include "SharedMemoryFramebufferCapture.h"
#include "SharedMemoryFrameAbi.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace chimera::graphics;

namespace {

using chimera::graphics::shmem::SharedFrameHeader;

} // namespace

class TestSharedMemoryFramebufferCapture : public QObject {
    Q_OBJECT

private slots:
    void readsNamedMappingFrame();
    void rejectsLowResolutionFrame();
};

void TestSharedMemoryFramebufferCapture::readsNamedMappingFrame() {
#ifndef _WIN32
    QSKIP("Shared-memory framebuffer capture is Windows-only");
#else
    const QString name = QStringLiteral("Local\\ChimeraTestFrame_%1")
                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    constexpr int width = 1920;
    constexpr int height = 1080;
    constexpr int stride = width * 4;
    constexpr qsizetype mappingSize = sizeof(SharedFrameHeader) + stride * height;

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                        nullptr,
                                        PAGE_READWRITE,
                                        0,
                                        static_cast<DWORD>(mappingSize),
                                        reinterpret_cast<LPCWSTR>(name.utf16()));
    QVERIFY(mapping != nullptr);

    auto *view = static_cast<uchar *>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, mappingSize));
    QVERIFY(view != nullptr);

    SharedFrameHeader header;
    header.magic = chimera::graphics::shmem::kMagic;
    header.version = chimera::graphics::shmem::kVersion;
    header.headerSize = sizeof(SharedFrameHeader);
    header.width = width;
    header.height = height;
    header.stride = stride;
    header.format = static_cast<quint32>(chimera::graphics::shmem::PixelFormat::Rgba8888);
    header.sequence = 2;
    header.pixelsOffset = sizeof(SharedFrameHeader);
    header.pixelsSize = stride * height;
    std::memcpy(view, &header, sizeof(header));

    uchar *pixels = view + sizeof(SharedFrameHeader);
    pixels[0] = 10; pixels[1] = 20; pixels[2] = 30; pixels[3] = 255;
    const qsizetype lastPixel = static_cast<qsizetype>(stride) * (height - 1) + (width - 1) * 4;
    pixels[lastPixel + 0] = 40;
    pixels[lastPixel + 1] = 50;
    pixels[lastPixel + 2] = 60;
    pixels[lastPixel + 3] = 255;

    SharedMemoryFramebufferCapture capture(name);
    capture.setIntervalMs(1);
    QSignalSpy spy(&capture, &FramebufferCapture::frameReady);

    QVERIFY(capture.start());
    QTRY_COMPARE(spy.count(), 1);

    const QImage frame = qvariant_cast<QImage>(spy.takeFirst().at(0));
    QCOMPARE(frame.size(), QSize(width, height));
    QCOMPARE(frame.pixelColor(0, 0), QColor(10, 20, 30, 255));
    QCOMPARE(frame.pixelColor(width - 1, height - 1), QColor(40, 50, 60, 255));

    capture.stop();
    UnmapViewOfFile(view);
    CloseHandle(mapping);
#endif
}

void TestSharedMemoryFramebufferCapture::rejectsLowResolutionFrame() {
#ifndef _WIN32
    QSKIP("Shared-memory framebuffer capture is Windows-only");
#else
    const QString name = QStringLiteral("Local\\ChimeraTestFrame_%1")
                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    constexpr int width = 1280;
    constexpr int height = 720;
    constexpr int stride = width * 4;
    constexpr qsizetype mappingSize = sizeof(SharedFrameHeader) + stride * height;

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                        nullptr,
                                        PAGE_READWRITE,
                                        0,
                                        static_cast<DWORD>(mappingSize),
                                        reinterpret_cast<LPCWSTR>(name.utf16()));
    QVERIFY(mapping != nullptr);

    auto *view = static_cast<uchar *>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, mappingSize));
    QVERIFY(view != nullptr);

    SharedFrameHeader header;
    header.magic = chimera::graphics::shmem::kMagic;
    header.version = chimera::graphics::shmem::kVersion;
    header.headerSize = sizeof(SharedFrameHeader);
    header.width = width;
    header.height = height;
    header.stride = stride;
    header.format = static_cast<quint32>(chimera::graphics::shmem::PixelFormat::Rgba8888);
    header.sequence = 2;
    header.pixelsOffset = sizeof(SharedFrameHeader);
    header.pixelsSize = stride * height;
    std::memcpy(view, &header, sizeof(header));

    SharedMemoryFramebufferCapture capture(name);
    capture.setIntervalMs(1);
    QSignalSpy frameSpy(&capture, &FramebufferCapture::frameReady);
    QSignalSpy errorSpy(&capture, &FramebufferCapture::captureError);

    QVERIFY(capture.start());
    if (errorSpy.isEmpty())
        QVERIFY(errorSpy.wait(500));
    QCOMPARE(frameSpy.count(), 0);

    capture.stop();
    UnmapViewOfFile(view);
    CloseHandle(mapping);
#endif
}

QTEST_MAIN(TestSharedMemoryFramebufferCapture)
#include "test_shared_memory_framebuffer_capture.moc"
