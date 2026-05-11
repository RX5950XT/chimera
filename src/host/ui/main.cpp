#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSurfaceFormat>
#include <QTimer>
#include <QProcess>
#include <QDebug>
#include <QImage>
#include <QQmlContext>
#include "ChimeraWindow.h"
#include "GuestDisplay.h"
#include "QmlInstanceManager.h"
#include "QmlMacroEngine.h"
#include "ScreenRecorder.h"
#include "InstanceManager.h"
#include "ConfigManager.h"
#include "InputBridge.h"
#include "GamepadManager.h"
#include "QmpInput.h"
#include "AudioBridge.h"
#include "DeviceSpoofer.h"
#include "AdbFramebufferCapture.h"
#include "PerformanceMonitor.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using namespace chimera::instance;
using namespace chimera::config;
using namespace chimera;

static std::filesystem::path g_projectRoot;
static std::filesystem::path g_adbPath;

static std::filesystem::path findProjectRoot() {
    auto path = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::exists(path / "configs" / "android_sdk.json")) {
            return path;
        }
        path = path.parent_path();
    }
    return std::filesystem::current_path();
}

static bool loadSdkConfig() {
    g_projectRoot = findProjectRoot();
    auto cfgPath = g_projectRoot / "configs" / "android_sdk.json";
    if (!std::filesystem::exists(cfgPath)) {
        qWarning() << "android_sdk.json not found at" << QString::fromStdString(cfgPath.string());
        return false;
    }
    std::ifstream f(cfgPath);
    nlohmann::json j;
    f >> j;
    if (j.contains("adb")) {
        g_adbPath = j["adb"].get<std::string>();
    }
    if (j.contains("sdk_root")) {
        qputenv("ANDROID_SDK_ROOT", QByteArray::fromStdString(j["sdk_root"].get<std::string>()));
    }
    if (j.contains("avd_home")) {
        qputenv("ANDROID_AVD_HOME", QByteArray::fromStdString(j["avd_home"].get<std::string>()));
    }
    return !g_adbPath.empty();
}

// Frame capture is now handled by chimera::graphics::AdbFramebufferCapture

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Chimera");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("chimera-emulator");

    if (!loadSdkConfig()) {
        qWarning() << "Failed to load Android SDK config. Emulator will not start.";
    }

    // Ensure screenshots and recordings directories exist
    std::filesystem::create_directories(g_projectRoot / "screenshots");
    std::filesystem::create_directories(g_projectRoot / "recordings");

    // Request OpenGL 4.1 Core for ANGLE / native rendering
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(4, 1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QQmlApplicationEngine engine;
    qmlRegisterType<chimera::ChimeraWindow>("Chimera.UI", 1, 0, "ChimeraWindow");
    qmlRegisterType<chimera::GuestDisplay>("Chimera.UI", 1, 0, "GuestDisplay");

    // Expose instance manager to QML
    chimera::QmlInstanceManager qmlInstanceMgr;
    engine.rootContext()->setContextProperty("InstanceManager", &qmlInstanceMgr);

    // Expose macro engine to QML
    chimera::QmlMacroEngine qmlMacroEngine;
    engine.rootContext()->setContextProperty("MacroEngine", &qmlMacroEngine);

    // Screen recorder
    chimera::ScreenRecorder screenRecorder;
    engine.rootContext()->setContextProperty("ScreenRecorder", &screenRecorder);

    // Start emulator instance
    if (!g_adbPath.empty()) {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "chimera_dev";
        cfg.cpus = 4;
        cfg.ramMB = 4096;
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.deviceProfile = "Samsung Galaxy S24 Ultra"; // Unlock high FPS/quality in games
        cfg.dataDir = (g_projectRoot / "instances" / cfg.name).make_preferred();

        // Remove existing instance with same name to avoid duplicates in memory
        mgr.deleteInstance(cfg.name);
        if (mgr.createInstance(cfg)) {
            qDebug() << "Instance created:" << QString::fromStdString(cfg.name);
            mgr.setStateCallback([](const std::string &name, VMState s) {
                qDebug() << "Instance" << QString::fromStdString(name)
                         << "state:" << static_cast<int>(s);
            });
            if (mgr.startInstance(cfg.name)) {
                qDebug() << "Instance started:" << QString::fromStdString(cfg.name);
            } else {
                qWarning() << "Failed to start instance" << QString::fromStdString(cfg.name);
            }
        } else {
            qWarning() << "Failed to create instance" << QString::fromStdString(cfg.name);
        }

        // Configure input bridge for ADB forwarding (fallback)
        chimera::input::InputBridge::instance().setAdbConfig(g_adbPath, 5555);

        // Try QMP low-latency input with auto-reconnect
        auto *qmpInput = new chimera::input::QmpInput(&app);
        qmpInput->setAutoReconnect(true, 5000);
        bool qmpConnected = qmpInput->connectToHost("localhost", 5554);
        if (qmpConnected) {
            qDebug() << "QMP input connected (low-latency mode)";
            chimera::input::InputBridge::instance().setQmpInput(qmpInput);
        } else {
            qWarning() << "QMP input not available, falling back to ADB. Will retry every 5s.";
        }

        // Initialize audio bridge
        chimera::audio::AudioBridge::Config audioCfg;
        audioCfg.sampleRate = 48000;
        audioCfg.channels = 2;
        audioCfg.bufferSize = 1024;
        if (chimera::audio::AudioBridge::instance().initialize(audioCfg)) {
            qDebug() << "Audio bridge initialized (WASAPI)";
        } else {
            qWarning() << "Audio bridge initialization failed";
        }

        // Wire gamepad to input bridge
        auto &gp = chimera::input::GamepadManager::instance();
        gp.initialize();
        gp.setButtonCallback([](int deviceId, int button, bool pressed) {
            chimera::input::InputBridge::instance().onGamepadButton(deviceId, button, pressed);
        });
        gp.setAxisCallback([](int deviceId, int axis, float value) {
            chimera::input::InputBridge::instance().onGamepadAxis(deviceId, axis, value);
        });
        auto *gpTimer = new QTimer(&app);
        QObject::connect(gpTimer, &QTimer::timeout, &app, [&gp]() {
            gp.poll();
        });
        gpTimer->start(16); // ~60 Hz polling
    }

    const QUrl url(QStringLiteral("qrc:/ChimeraWindow.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    // Set up ADB screen capture using the new FramebufferCapture abstraction
    auto *capture = new chimera::graphics::AdbFramebufferCapture(
        QString::fromStdString(g_adbPath.string()), 5555, false, &app);
    capture->setIntervalMs(33); // Target ~30 FPS

    // Performance monitoring
    auto *perfMonitor = new chimera::graphics::PerformanceMonitor(&app);
    engine.rootContext()->setContextProperty("PerfMonitor", perfMonitor);

    // Find GuestDisplay in QML and connect frame updates
    QObject::connect(capture, &chimera::graphics::FramebufferCapture::frameReady,
                     &engine, [&engine, &screenRecorder, perfMonitor](const QImage &img) {
        perfMonitor->onFrameReceived();
        auto roots = engine.rootObjects();
        for (auto *obj : roots) {
            auto *guest = obj->findChild<GuestDisplay*>("guestDisplay");
            if (guest) {
                guest->setFrame(img);
                screenRecorder.feedFrame(img);
                return;
            }
        }
    });
    QObject::connect(capture, &chimera::graphics::FramebufferCapture::captureError,
                     &app, [perfMonitor](const QString &msg) {
        qWarning() << "Frame capture error:" << msg;
        perfMonitor->onFrameDropped();
    });

    // Log FPS every 5 seconds
    auto *perfTimer = new QTimer(&app);
    QObject::connect(perfTimer, &QTimer::timeout, [perfMonitor]() {
        qDebug() << QStringLiteral("[Perf] FPS: %1 | Avg: %2ms | Max: %3ms | Dropped: %4 / %5")
                    .arg(perfMonitor->fps(), 0, 'f', 1)
                    .arg(perfMonitor->averageFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->maxFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->droppedFrames())
                    .arg(perfMonitor->totalFrames());
    });
    perfTimer->start(5000);

    // Start capture after 15s to allow Android boot
    QTimer::singleShot(15000, [capture]() {
        if (capture->start()) {
            qDebug() << "ADB raw screen capture started (" << capture->intervalMs() << "ms interval)";
        }
    });

    return app.exec();
}

#include "main.moc"
