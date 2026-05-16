#pragma once

#include <QObject>

namespace chimera {

class QmlAndroidControls : public QObject {
    Q_OBJECT

public:
    explicit QmlAndroidControls(QObject *parent = nullptr);

    Q_INVOKABLE bool back();
    Q_INVOKABLE bool home();
    Q_INVOKABLE bool recents();
    Q_INVOKABLE bool menu();

private:
    bool sendKey(int keyCode) const;
};

} // namespace chimera
