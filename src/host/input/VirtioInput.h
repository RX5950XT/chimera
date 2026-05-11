#pragma once

#include <QObject>
#include <QString>

namespace chimera::input {

/**
 * @brief VirtIO Input backend (future replacement for QMP/ADB input).
 *
 * When using a custom QEMU build with virtio-input devices:
 *   -device virtio-keyboard-pci
 *   -device virtio-mouse-pci
 *   -device virtio-tablet-pci
 *
 * The Android Emulator prebuilt binary does NOT support virtio-input.
 * This class is a framework ready for when we switch to custom QEMU.
 */
class VirtioInput : public QObject {
    Q_OBJECT
public:
    explicit VirtioInput(QObject *parent = nullptr);

    /**
     * @brief Check if virtio-input is available (custom QEMU only).
     */
    static bool isAvailable();

    /**
     * @brief Get QEMU arguments for virtio-input devices.
     */
    static QStringList qemuArgs();

    /**
     * @brief Open the virtio-input event device.
     *
     * On Linux: /dev/input/event*
     * On Windows: custom virtio channel
     */
    bool openDevice(const QString &path);
    void closeDevice();

    bool sendKey(int linuxKeyCode, bool pressed);
    bool sendMouseMove(int x, int y);
    bool sendMouseButton(int button, bool pressed);

signals:
    void error(const QString &message);

private:
    int m_fd = -1;
};

} // namespace chimera::input
