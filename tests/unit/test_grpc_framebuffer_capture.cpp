#include <QtTest/QtTest>

#include "AdbH264FramebufferCapture.h"
#include "GrpcFramebufferCapture.h"
#include "GrpcMmapFramebufferCapture.h"
#include <QMap>

using chimera::graphics::AdbH264FramebufferCapture;
using chimera::graphics::GrpcFramebufferCapture;
using chimera::graphics::GrpcMmapFramebufferCapture;

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

bool readLengthDelimited(const QByteArray &payload, int *offset, QByteArray *out) {
    quint64 len = 0;
    if (!GrpcFramebufferCapture::readVarint(payload, offset, &len)) return false;
    if (len > static_cast<quint64>(payload.size() - *offset)) return false;
    *out = QByteArray(payload.constData() + *offset, static_cast<qsizetype>(len));
    *offset += static_cast<int>(len);
    return true;
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

        QCOMPARE(fields.value(1), 1ULL);
        QCOMPARE(fields.value(3), 1920ULL);
        QCOMPARE(fields.value(4), 1080ULL);
    }

    void encodesMmapTransportWithoutDownscaling() {
        const QByteArray payload =
            GrpcMmapFramebufferCapture::buildMmapImageFormatRequest(
                1920, 1080, QStringLiteral("file:///C:/Temp/chimera.rgb"));

        QMap<int, quint64> scalarFields;
        QByteArray transport;
        int offset = 0;
        while (offset < payload.size()) {
            quint64 tag = 0;
            QVERIFY(GrpcFramebufferCapture::readVarint(payload, &offset, &tag));
            const int field = static_cast<int>(tag >> 3);
            const int wire = static_cast<int>(tag & 0x07);
            if (wire == 0) {
                quint64 value = 0;
                QVERIFY(GrpcFramebufferCapture::readVarint(payload, &offset, &value));
                scalarFields.insert(field, value);
            } else if (field == 6 && wire == 2) {
                QVERIFY(readLengthDelimited(payload, &offset, &transport));
            } else {
                QFAIL("unexpected field in ImageFormat");
            }
        }

        QCOMPARE(scalarFields.value(1), 1ULL);
        QCOMPARE(scalarFields.value(3), 1920ULL);
        QCOMPARE(scalarFields.value(4), 1080ULL);
        QVERIFY(!transport.isEmpty());

        QMap<int, quint64> transportScalarFields;
        QByteArray handle;
        int transportOffset = 0;
        while (transportOffset < transport.size()) {
            quint64 tag = 0;
            QVERIFY(GrpcFramebufferCapture::readVarint(transport, &transportOffset, &tag));
            const int field = static_cast<int>(tag >> 3);
            const int wire = static_cast<int>(tag & 0x07);
            if (wire == 0) {
                quint64 value = 0;
                QVERIFY(GrpcFramebufferCapture::readVarint(transport, &transportOffset, &value));
                transportScalarFields.insert(field, value);
            } else if (field == 2 && wire == 2) {
                QVERIFY(readLengthDelimited(transport, &transportOffset, &handle));
            } else {
                QFAIL("unexpected field in ImageTransport");
            }
        }

        QCOMPARE(transportScalarFields.value(1), 1ULL);
        QCOMPARE(QString::fromUtf8(handle), QStringLiteral("file:///C:/Temp/chimera.rgb"));
    }

    void screenrecordTransportKeeps1080pFloor() {
        QCOMPARE(AdbH264FramebufferCapture::normalizedCaptureSize(1280, 720), QSize(1920, 1080));

        const QStringList adbArgs = AdbH264FramebufferCapture::buildAdbArgs(
            QStringLiteral("emulator-5554"), QSize(1280, 720), 8000000);
        QVERIFY(adbArgs.contains(QStringLiteral("screenrecord")));
        QVERIFY(adbArgs.contains(QStringLiteral("--output-format=h264")));
        QVERIFY(adbArgs.contains(QStringLiteral("1920x1080")));
        QVERIFY(adbArgs.contains(QStringLiteral("24000000")));

        const QStringList ffmpegArgs =
            AdbH264FramebufferCapture::buildFfmpegArgs(QSize(1280, 720));
        QVERIFY(!ffmpegArgs.contains(QStringLiteral("-vf")));
        QVERIFY(ffmpegArgs.contains(QStringLiteral("bgra")));
    }

    // The per-request abort timeout must outlive the stall watchdog, or a slow
    // 1080p readback is killed before any frame completes (the total=0-under-load
    // freeze). Guards against re-coupling the two timeouts.
    void requestTimeoutDecoupledFromStallWatchdog() {
        GrpcFramebufferCapture cap(QStringLiteral("127.0.0.1"), 8554, 1920, 1080);
        QVERIFY(cap.requestTimeoutMs() >= cap.stallTimeoutMs());
        QVERIFY(cap.requestTimeoutMs() > cap.stallTimeoutMs());
    }

    void requestTimeoutEnvOverrideHonored() {
        qputenv("CHIMERA_GRPC_REQUEST_TIMEOUT_MS", "8000");
        GrpcFramebufferCapture cap(QStringLiteral("127.0.0.1"), 8554, 1920, 1080);
        qunsetenv("CHIMERA_GRPC_REQUEST_TIMEOUT_MS");
        QCOMPARE(cap.requestTimeoutMs(), 8000);
    }

    void requestTimeoutEnvBelowStallRejected() {
        qputenv("CHIMERA_GRPC_REQUEST_TIMEOUT_MS", "500");
        GrpcFramebufferCapture cap(QStringLiteral("127.0.0.1"), 8554, 1920, 1080);
        qunsetenv("CHIMERA_GRPC_REQUEST_TIMEOUT_MS");
        QVERIFY(cap.requestTimeoutMs() >= cap.stallTimeoutMs());
    }
};

QTEST_MAIN(TestGrpcFramebufferCapture)
#include "test_grpc_framebuffer_capture.moc"
