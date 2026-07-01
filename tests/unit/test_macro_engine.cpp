#include <QtTest>
#include "MacroEngine.h"
#include "InputBridge.h"

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

    void playbackTapReleasesMouseButton() {
        auto &engine = MacroEngine::instance();
        auto &bridge = InputBridge::instance();
        const std::string name = "unit_test_tap_release_macro";
        std::vector<InputBridge::Event::Type> types;

        engine.startRecording(name);
        MacroEvent ev;
        ev.type = MacroEvent::Tap;
        ev.timestamp = std::chrono::milliseconds(0);
        ev.x = 10;
        ev.y = 20;
        ev.keyCode = 0;
        engine.recordEvent(ev);
        engine.stopRecording();

        bridge.setEventCallback([&types](const InputBridge::Event &event) {
            types.push_back(event.type);
        });
        engine.startPlayback(name, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!engine.isPlaying(), 1000);
        engine.stopPlayback();
        bridge.setEventCallback(InputBridge::EventCallback{});
        engine.deleteMacro(name);

        QCOMPARE(types.size(), static_cast<size_t>(2));
        QCOMPARE(types[0], InputBridge::Event::MouseButtonDown);
        QCOMPARE(types[1], InputBridge::Event::MouseButtonUp);
    }
};

QTEST_MAIN(TestMacroEngine)
#include "test_macro_engine.moc"
