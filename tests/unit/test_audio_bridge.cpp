#include <QtTest>
#include "AudioBridge.h"

using namespace chimera::audio;

class TestAudioBridge : public QObject {
    Q_OBJECT

private slots:
    void isNotRunningByDefault() {
        QVERIFY(!AudioBridge::instance().isRunning());
    }

    void writeGuestFramesBeforeInitDoesNotCrash() {
        const std::vector<float> frames(1024, 0.0f);
        AudioBridge::instance().writeGuestFrames(frames);
        QVERIFY(true);
    }

    void readHostMicrophoneBeforeInitDoesNotCrash() {
        std::vector<float> out;
        AudioBridge::instance().readHostMicrophone(out);
        QVERIFY(true);
    }

    void shutdownBeforeInitDoesNotCrash() {
        AudioBridge::instance().shutdown();
        QVERIFY(!AudioBridge::instance().isRunning());
    }

    void initializeMayFailInHeadlessEnv() {
        AudioBridge::Config cfg;
        cfg.sampleRate = 48000;
        cfg.channels = 2;
        cfg.bufferSize = 1024;

        // In a headless test env WASAPI may not be available — we just must not crash
        const bool ok = AudioBridge::instance().initialize(cfg);
        Q_UNUSED(ok)
        AudioBridge::instance().shutdown();
        QVERIFY(!AudioBridge::instance().isRunning());
    }

    void forcedNonNativeRateInitializesViaAutoConvert() {
        // Probe whether a render endpoint exists at all in this environment.
        AudioBridge::Config probe;
        probe.sampleRate = 48000; probe.channels = 2; probe.bufferSize = 1024;
        const bool haveDevice = AudioBridge::instance().initialize(probe);
        AudioBridge::instance().shutdown();
        if (!haveDevice) QSKIP("no WASAPI render endpoint in this environment");

        // A deliberately non-native rate/channel layout differs from the device mix
        // format. In shared mode IAudioClient::Initialize then needs AUTOCONVERTPCM
        // (and a consistent cbSize=0 WAVEFORMATEX); without the fix it fails with
        // AUDCLNT_E_UNSUPPORTED_FORMAT, so this is a real RED→GREEN on real hardware.
        AudioBridge::Config odd;
        odd.sampleRate = 22050; odd.channels = 1; odd.bufferSize = 1024;
        QVERIFY(AudioBridge::instance().initialize(odd));
        AudioBridge::instance().shutdown();
        QVERIFY(!AudioBridge::instance().isRunning());
    }

    void writeFramesAfterShutdownDoesNotCrash() {
        AudioBridge::instance().shutdown();
        const std::vector<float> frames(512, 0.1f);
        AudioBridge::instance().writeGuestFrames(frames);
        QVERIFY(true);
    }

    void emptyFrameWriteDoesNotCrash() {
        AudioBridge::instance().writeGuestFrames({});
        QVERIFY(true);
    }
};

QTEST_MAIN(TestAudioBridge)
#include "test_audio_bridge.moc"
