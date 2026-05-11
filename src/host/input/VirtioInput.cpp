#include "VirtioInput.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace chimera::input;

VirtioInput::VirtioInput(QObject *parent)
    : QObject(parent) {
}

bool VirtioInput::isAvailable() {
    // The Android Emulator prebuilt does NOT support virtio-input devices.
    // Custom QEMU build is required.
    return false;
}

QStringList VirtioInput::qemuArgs() {
    // These args work with upstream QEMU but not Android Emulator prebuilt.
    return {
        "-device", "virtio-keyboard-pci",
        "-device", "virtio-mouse-pci",
        "-device", "virtio-tablet-pci"
    };
}

bool VirtioInput::openDevice(const QString &path) {
    Q_UNUSED(path)
    qWarning() << "VirtioInput: Not available with Android Emulator prebuilt. "
                  "Use custom QEMU build to enable virtio-input.";
    return false;
}

void VirtioInput::closeDevice() {
    m_fd = -1;
}

bool VirtioInput::sendKey(int linuxKeyCode, bool pressed) {
    Q_UNUSED(linuxKeyCode)
    Q_UNUSED(pressed)
    return false;
}

bool VirtioInput::sendMouseMove(int x, int y) {
    Q_UNUSED(x)
    Q_UNUSED(y)
    return false;
}

bool VirtioInput::sendMouseButton(int button, bool pressed) {
    Q_UNUSED(button)
    Q_UNUSED(pressed)
    return false;
}
