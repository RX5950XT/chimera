#include "QmlAndroidControls.h"

#include "InputBridge.h"
#include "KeyCodes.h"

namespace chimera {

QmlAndroidControls::QmlAndroidControls(QObject *parent)
    : QObject(parent)
{
}

bool QmlAndroidControls::back() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::Back));
}

bool QmlAndroidControls::home() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::Home));
}

bool QmlAndroidControls::recents() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::AppSwitch));
}

bool QmlAndroidControls::menu() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::Menu));
}

bool QmlAndroidControls::sendKey(int keyCode) const {
    return input::InputBridge::instance().sendAndroidKeyCode(keyCode);
}

} // namespace chimera
