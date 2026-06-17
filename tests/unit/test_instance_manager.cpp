#include <QTest>
#include <QTemporaryDir>
#include "InstanceManager.h"
#include <fstream>
#include <filesystem>

using namespace chimera::instance;

class TestInstanceManager : public QObject {
    Q_OBJECT

private slots:
    void probeEmulatorRuntimeRejectsStockGfxstream() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll").put('\0');

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(!caps.hasLegacyOpenglRender);
        QVERIFY(!caps.supportsChimeraEmuglSharedTexture);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(!caps.hasChimeraShmemPublisher);
        QVERIFY(caps.status.find("stock gfxstream") != std::string::npos);
        QVERIFY(caps.status.find("bridge marker") != std::string::npos);
    }

    void probeEmulatorRuntimeDetectsShmemPublisher() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraShmemFramePublisher";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraShmemPublisher);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(!caps.supportsChimeraEmuglSharedTexture);
        QVERIFY(caps.status.find("shmem frame publisher") != std::string::npos);
    }

    void probeEmulatorRuntimeAcceptsModifiedGfxstreamRuntime() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "source.properties") << "Pkg.Revision=36.5.11\nPkg.BuildId=15261927\n";
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamSharedTextureBridge "
            << "gfxstream_backend_set_screen_background "
            << "libandroid-emu-agents.dll libandroid-emu-protos.dll libandroid-emu-metrics.dll";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"gfxstreamSourceSnapBuildId\":\"15261927\","
               "\"baseEmulatorBuildId\":\"15261927\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(caps.hasCompatibleGfxstreamAbi);
        QVERIFY(caps.hasSdkGfxstreamRuntimeImports);
        QVERIFY(caps.hasMatchingGfxstreamBuildId);
        QVERIFY(caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(!caps.supportsChimeraEmuglSharedTexture);
        QVERIFY(caps.status.find("modified gfxstream") != std::string::npos);
    }

    void probeEmulatorRuntimeAcceptsVulkanGfxstreamRuntimeMarker() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "source.properties") << "Pkg.Revision=36.5.11\nPkg.BuildId=15261927\n";
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamVulkanSharedTextureBridge "
            << "ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopy "
            << "gfxstream_backend_set_screen_background "
            << "libandroid-emu-agents.dll libandroid-emu-protos.dll libandroid-emu-metrics.dll";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"gfxstreamSourceSnapBuildId\":\"15261927\","
               "\"baseEmulatorBuildId\":\"15261927\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(caps.hasCompatibleGfxstreamAbi);
        QVERIFY(caps.hasSdkGfxstreamRuntimeImports);
        QVERIFY(caps.hasMatchingGfxstreamBuildId);
        QVERIFY(caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("modified gfxstream") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsIncompatibleGfxstreamAbi() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamSharedTextureBridge";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(!caps.hasCompatibleGfxstreamAbi);
        QVERIFY(!caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("incompatible gfxstream runtime ABI") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsGfxstreamWithoutSdkRuntimeImports() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamVulkanSharedTextureBridge "
            << "ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopy "
            << "gfxstream_backend_set_screen_background";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(caps.hasCompatibleGfxstreamAbi);
        QVERIFY(!caps.hasSdkGfxstreamRuntimeImports);
        QVERIFY(!caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("SDK runtime imports") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsMismatchedGfxstreamSourceBuildId() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "source.properties") << "Pkg.Revision=36.5.11\nPkg.BuildId=15261927\n";
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamVulkanSharedTextureBridge "
            << "ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopy "
            << "gfxstream_backend_set_screen_background "
            << "libandroid-emu-agents.dll libandroid-emu-protos.dll libandroid-emu-metrics.dll";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"gfxstreamSourceSnapBuildId\":\"13278158\","
               "\"baseEmulatorBuildId\":\"15261927\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(caps.hasCompatibleGfxstreamAbi);
        QVERIFY(caps.hasSdkGfxstreamRuntimeImports);
        QVERIFY(!caps.hasMatchingGfxstreamBuildId);
        QVERIFY(caps.hasMismatchedGfxstreamBuildId);
        QVERIFY(!caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("source snapshot build id") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsManifestWithoutGfxstreamMarker() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll").put('\0');
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(!caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(!caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("bridge marker") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsInvalidGfxstreamManifest() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamSharedTextureBridge "
            << "gfxstream_backend_set_screen_background "
            << "libandroid-emu-agents.dll libandroid-emu-protos.dll libandroid-emu-metrics.dll";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"Other\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"renderPath\":\"VulkanDisplayVkPost\","
               "\"abi\":\"sdk-emulator-36\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(!caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("gfxstream bridge") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsGfxstreamManifestWithoutVulkanPostPath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "libgfxstream_backend.dll")
            << "binary marker: ChimeraGfxstreamSharedTextureBridge "
            << "gfxstream_backend_set_screen_background "
            << "libandroid-emu-agents.dll libandroid-emu-protos.dll libandroid-emu-metrics.dll";
        std::ofstream(root / "lib64" / "chimera-gfxstream-shared-texture.json")
            << "{\"producer\":\"ChimeraGfxstreamSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasGfxstreamBackend);
        QVERIFY(caps.hasChimeraGfxstreamBridgeMarker);
        QVERIFY(caps.hasCompatibleGfxstreamAbi);
        QVERIFY(!caps.hasChimeraGfxstreamSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraGfxstreamSharedTexture);
        QVERIFY(caps.status.find("gfxstream bridge") != std::string::npos);
    }

    void probeEmulatorRuntimeRequiresChimeraManifest() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "lib64OpenglRender.dll").put('\0');
        std::ofstream(root / "lib64" / "lib64EGL_translator.dll").put('\0');
        std::ofstream(root / "lib64" / "lib64GLES_CM_translator.dll").put('\0');
        std::ofstream(root / "lib64" / "lib64GLES_V2_translator.dll").put('\0');

        auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasLegacyOpenglRender);
        QVERIFY(caps.hasRequiredEmuglRuntimeDlls);
        QVERIFY(!caps.hasChimeraSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraEmuglSharedTexture);

        std::ofstream(root / "lib64" / "chimera-emugl-shared-texture.json")
            << "{\"producer\":\"ChimeraSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";
        caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasChimeraSharedTextureManifest);
        QVERIFY(caps.supportsChimeraEmuglSharedTexture);
    }

    void probeEmulatorRuntimeRejectsIncompleteLegacyRuntime() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "lib64OpenglRender.dll").put('\0');
        std::ofstream(root / "lib64" / "chimera-emugl-shared-texture.json")
            << "{\"producer\":\"ChimeraSharedTextureBridge\","
               "\"transport\":\"D3D11SharedTexture\","
               "\"minWidth\":1920,\"minHeight\":1080,\"targetFps\":60}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasLegacyOpenglRender);
        QVERIFY(!caps.hasRequiredEmuglRuntimeDlls);
        QVERIFY(caps.hasChimeraSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraEmuglSharedTexture);
        QVERIFY(caps.status.find("translator DLLs") != std::string::npos);
    }

    void probeEmulatorRuntimeRejectsInvalidManifest() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdWString());
        std::filesystem::create_directories(root / "lib64");
        std::ofstream(root / "emulator.exe").put('\0');
        std::ofstream(root / "lib64" / "lib64OpenglRender.dll").put('\0');
        std::ofstream(root / "lib64" / "lib64EGL_translator.dll").put('\0');
        std::ofstream(root / "lib64" / "lib64GLES_CM_translator.dll").put('\0');
        std::ofstream(root / "lib64" / "lib64GLES_V2_translator.dll").put('\0');
        std::ofstream(root / "lib64" / "chimera-emugl-shared-texture.json")
            << "{\"producer\":\"Other\",\"transport\":\"D3D11SharedTexture\"}";

        const auto caps = InstanceManager::probeEmulatorRuntime(root / "emulator.exe");
        QVERIFY(caps.hasRequiredEmuglRuntimeDlls);
        QVERIFY(!caps.hasChimeraSharedTextureManifest);
        QVERIFY(!caps.supportsChimeraEmuglSharedTexture);
        QVERIFY(caps.status.find("manifest") != std::string::npos);
    }

    void testCreateInstance() {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "UnitTest";
        cfg.cpus = 2;
        cfg.ramMB = 2048;
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        auto names = mgr.listInstances();
        QVERIFY(std::find(names.begin(), names.end(), "UnitTest") != names.end());

        auto state = mgr.getInstanceState("UnitTest");
        QCOMPARE(state, VMState::Created);

        mgr.deleteInstance("UnitTest");
    }

    void createInstanceKeeps1080pFloor() {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "LowResUnitTest";
        cfg.width = 800;
        cfg.height = 450;
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_lowres_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        const auto saved = mgr.getInstanceConfig("LowResUnitTest");
        QCOMPARE(saved.width, 1920);
        QCOMPARE(saved.height, 1080);

        mgr.deleteInstance("LowResUnitTest");
    }

    void createInstanceForcesHeadlessUnlessUnsafeVisibleWindowAllowed() {
        auto &mgr = InstanceManager::instance();
        qunsetenv("CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW");
        qunsetenv("CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION");
        InstanceConfig cfg;
        cfg.name = "HeadlessDefaultUnitTest";
        cfg.headless = false;
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_headless_default_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        auto saved = mgr.getInstanceConfig("HeadlessDefaultUnitTest");
        QVERIFY(saved.headless);
        mgr.deleteInstance("HeadlessDefaultUnitTest");

        cfg.name = "UnsafeVisibleWindowUnitTest";
        cfg.allowVisibleEmulatorWindow = true;
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_unsafe_visible_window_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        saved = mgr.getInstanceConfig("UnsafeVisibleWindowUnitTest");
        QVERIFY(saved.headless);
        mgr.deleteInstance("UnsafeVisibleWindowUnitTest");

        qputenv("CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW", "1");
        cfg.name = "UnsafeEnvOnlyVisibleWindowUnitTest";
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_unsafe_env_only_visible_window_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        saved = mgr.getInstanceConfig("UnsafeEnvOnlyVisibleWindowUnitTest");
        QVERIFY(saved.headless);
        mgr.deleteInstance("UnsafeEnvOnlyVisibleWindowUnitTest");

        qputenv("CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION", "1");
        cfg.name = "UnsafeVisibleWindowDiagnosticUnitTest";
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_unsafe_visible_window_diagnostic_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        saved = mgr.getInstanceConfig("UnsafeVisibleWindowDiagnosticUnitTest");
        QVERIFY(!saved.headless);
        mgr.deleteInstance("UnsafeVisibleWindowDiagnosticUnitTest");
        qunsetenv("CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW");
        qunsetenv("CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION");
    }

    void createInstanceDefaultsToBelowNormalPriority() {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "PriorityDefaultUnitTest";
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_priority_default_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        const auto saved = mgr.getInstanceConfig("PriorityDefaultUnitTest");
        QCOMPARE(QString::fromStdString(saved.processPriority), QStringLiteral("below_normal"));

        mgr.deleteInstance("PriorityDefaultUnitTest");
    }

    void createInstanceCapsHighPriorityToNormal() {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "PriorityCapUnitTest";
        cfg.processPriority = "high";
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_priority_cap_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        const auto saved = mgr.getInstanceConfig("PriorityCapUnitTest");
        QCOMPARE(QString::fromStdString(saved.processPriority), QStringLiteral("normal"));

        mgr.deleteInstance("PriorityCapUnitTest");
    }
};

QTEST_MAIN(TestInstanceManager)
#include "test_instance_manager.moc"
