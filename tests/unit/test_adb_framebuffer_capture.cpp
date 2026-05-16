#include <QTest>
#include <QColor>
#include <QtEndian>
#include "AdbFramebufferCapture.h"

using namespace chimera::graphics;

namespace {

void appendLe32(QByteArray &data, qint32 value) {
    uchar bytes[4];
    qToLittleEndian(value, bytes);
    data.append(reinterpret_cast<const char*>(bytes), 4);
}

QByteArray makeRawFrame(qint32 width, qint32 height, qint32 format, const QByteArray &pixels) {
    QByteArray data;
    appendLe32(data, width);
    appendLe32(data, height);
    appendLe32(data, format);
    appendLe32(data, 1);
    data.append(pixels);
    return data;
}

} // namespace

class TestAdbFramebufferCapture : public QObject {
    Q_OBJECT

private slots:
    void decodesRgba8888RawFrame() {
        QByteArray pixels;
        pixels.append(char(10));
        pixels.append(char(20));
        pixels.append(char(30));
        pixels.append(char(255));

        const QImage image = AdbFramebufferCapture::decodeRawFrameData(
            makeRawFrame(1, 1, 1, pixels));

        QVERIFY(!image.isNull());
        QCOMPARE(image.width(), 1);
        QCOMPARE(image.height(), 1);
        QCOMPARE(image.pixelColor(0, 0), QColor(10, 20, 30, 255));
    }

    void rejectsIncompleteRawFrame() {
        QByteArray pixels;
        pixels.append(char(1));

        const QImage image = AdbFramebufferCapture::decodeRawFrameData(
            makeRawFrame(2, 2, 1, pixels));

        QVERIFY(image.isNull());
    }
};

QTEST_MAIN(TestAdbFramebufferCapture)
#include "test_adb_framebuffer_capture.moc"
