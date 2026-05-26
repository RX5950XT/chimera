#include <QtTest/QtTest>

#include "GrpcFramebufferCapture.h"
#include <QMap>

using chimera::graphics::GrpcFramebufferCapture;

namespace {

QMap<int, quint64> parseVarintFields(const QByteArray &payload) {
    QMap<int, quint64> fields;
    int offset = 0;
    while (offset < payload.size()) {
        quint64 tag = 0;
        if (!GrpcFramebufferCapture::readVarint(payload, &offset, &tag))
            return {};
        const int field = static_cast<int>(tag >> 3);
        const int wire = static_cast<int>(tag & 0x07);
        if (wire != 0)
            return {};
        quint64 value = 0;
        if (!GrpcFramebufferCapture::readVarint(payload, &offset, &value))
            return {};
        fields.insert(field, value);
    }
    return fields;
}

} // namespace

class TestGrpcFramebufferCapture : public QObject {
    Q_OBJECT

private slots:
    void clampsCaptureRequestsTo1080pFloor() {
        QCOMPARE(GrpcFramebufferCapture::normalizedCaptureSize(800, 450), QSize(1920, 1080));
        QCOMPARE(GrpcFramebufferCapture::normalizedCaptureSize(1920, 1080), QSize(1920, 1080));
        QCOMPARE(GrpcFramebufferCapture::normalizedCaptureSize(2560, 1440), QSize(2560, 1440));
    }

    void encodesFullResolutionImageRequest() {
        const QByteArray payload =
            GrpcFramebufferCapture::buildImageFormatRequest(1920, 1080);
        const QMap<int, quint64> fields = parseVarintFields(payload);

        QCOMPARE(fields.value(1), 2ULL);
        QCOMPARE(fields.value(3), 1920ULL);
        QCOMPARE(fields.value(4), 1080ULL);
    }
};

QTEST_MAIN(TestGrpcFramebufferCapture)
#include "test_grpc_framebuffer_capture.moc"
