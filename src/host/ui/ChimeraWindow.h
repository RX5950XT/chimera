#pragma once

#include <QQuickWindow>
#include <QOpenGLFramebufferObject>
#include <memory>

namespace chimera {

/**
 * @brief Main application window embedding the Android guest display.
 *
 * Renders the guest framebuffer (from QEMU/VirtIO-GPU) into a Qt Quick scene.
 * Handles window resize, fullscreen toggle, and input event forwarding.
 */
class ChimeraWindow : public QQuickWindow {
    Q_OBJECT

public:
    explicit ChimeraWindow(QWindow *parent = nullptr);
    ~ChimeraWindow() override;

    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void showInputMapper();
    Q_INVOKABLE void takeScreenshot();

    /**
     * @brief Update the guest framebuffer texture.
     *
     * Called from the graphics bridge thread when a new frame is ready.
     */
    void updateGuestTexture(uint32_t textureId, int width, int height);

signals:
    void frameSizeChanged(int width, int height);
    void fullscreenChanged(bool isFullscreen);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace chimera
