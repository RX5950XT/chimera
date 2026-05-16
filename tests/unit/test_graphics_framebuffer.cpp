#include <QTest>
#include "Framebuffer.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace chimera::graphics;

class TestGraphicsFramebuffer : public QObject {
    Q_OBJECT

private slots:
    void testWriteBackBufferCanResize() {
        auto framebuffer = std::make_shared<Framebuffer>(2, 2);
        auto done = std::make_shared<std::atomic_bool>(false);
        std::vector<uint8_t> pixels(4 * 4 * 4, 0x7f);

        std::thread worker([framebuffer, done, pixels]() {
            framebuffer->writeBackBuffer(pixels, 4, 4);
            done->store(true);
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!done->load() && std::chrono::steady_clock::now() < deadline) {
            QTest::qWait(10);
        }

        if (!done->load()) {
            worker.detach();
            QFAIL("writeBackBuffer deadlocked while resizing the back buffer");
        }

        worker.join();
        QCOMPARE(framebuffer->width(), 4u);
        QCOMPARE(framebuffer->height(), 4u);
    }
};

QTEST_MAIN(TestGraphicsFramebuffer)
#include "test_graphics_framebuffer.moc"
