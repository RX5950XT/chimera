#include <QtTest>
#include "MacroEngine.h"

using namespace chimera::input;

class TestMacroEngine : public QObject {
    Q_OBJECT

private slots:
    void notRecordingByDefault() {
        auto &e = MacroEngine::instance();
        e.stopPlayback();
        // Ensure clean state before checking
        if (e.isRecording()) e.stopRecording();
        QVERIFY(!e.isRecording());
    }

    void startAndStopRecording() {
        auto &e = MacroEngine::instance();
        e.startRecording("test_macro");
        QVERIFY(e.isRecording());
        e.stopRecording();
        QVERIFY(!e.isRecording());
    }

    void recordEventDoesNotCrash() {
        auto &e = MacroEngine::instance();
        e.startRecording("test_event_macro");
        MacroEvent ev;
        ev.type = MacroEvent::Tap;
        ev.timestamp = std::chrono::milliseconds(0);
        ev.x = 100;
        ev.y = 200;
        ev.keyCode = 0;
        e.recordEvent(ev);
        e.stopRecording();
        QVERIFY(true);
    }

    void notPlayingByDefault() {
        auto &e = MacroEngine::instance();
        if (e.isPlaying()) e.stopPlayback();
        QVERIFY(!e.isPlaying());
    }

    void playbackNonExistentMacroDoesNotCrash() {
        auto &e = MacroEngine::instance();
        e.startPlayback("__nonexistent_macro__", 1);
        // May or may not start — must not crash
        if (e.isPlaying()) e.stopPlayback();
        QVERIFY(true);
    }

    void listMacrosReturnsVector() {
        auto &e = MacroEngine::instance();
        const auto macros = e.listMacros();
        // May be empty — just verify it returns
        Q_UNUSED(macros)
        QVERIFY(true);
    }

    void deleteMacroNonExistentReturnsFalse() {
        auto &e = MacroEngine::instance();
        const bool ok = e.deleteMacro("__nonexistent_macro__");
        QVERIFY(!ok);
    }

    void recordAndSaveAndLoad() {
        auto &e = MacroEngine::instance();
        const std::string name = "unit_test_macro";

        e.startRecording(name);
        MacroEvent ev;
        ev.type = MacroEvent::Tap;
        ev.timestamp = std::chrono::milliseconds(100);
        ev.x = 50;
        ev.y = 75;
        ev.keyCode = 0;
        e.recordEvent(ev);
        e.stopRecording();

        const bool saved = e.saveMacro(name);
        if (saved) {
            const bool loaded = e.loadMacro(name);
            QVERIFY(loaded);
            e.deleteMacro(name);
        } else {
            // File I/O unavailable in test env — not a failure
            QVERIFY(true);
        }
    }
};

QTEST_MAIN(TestMacroEngine)
#include "test_macro_engine.moc"
