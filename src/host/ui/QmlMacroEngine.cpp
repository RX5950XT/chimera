#include "QmlMacroEngine.h"
#include "MacroEngine.h"

namespace chimera {

QmlMacroEngine::QmlMacroEngine(QObject *parent)
    : QObject(parent)
{
}

QStringList QmlMacroEngine::listMacros() const {
    QStringList list;
    for (auto &name : chimera::input::MacroEngine::instance().listMacros()) {
        list.append(QString::fromStdString(name));
    }
    return list;
}

void QmlMacroEngine::startRecording(const QString &name) {
    chimera::input::MacroEngine::instance().startRecording(name.toStdString());
    emit recordingChanged();
}

void QmlMacroEngine::stopRecording() {
    chimera::input::MacroEngine::instance().stopRecording();
    emit recordingChanged();
}

void QmlMacroEngine::startPlayback(const QString &name, int loopCount) {
    chimera::input::MacroEngine::instance().startPlayback(name.toStdString(), loopCount);
    emit playingChanged();
}

void QmlMacroEngine::stopPlayback() {
    chimera::input::MacroEngine::instance().stopPlayback();
    emit playingChanged();
}

void QmlMacroEngine::deleteMacro(const QString &name) {
    chimera::input::MacroEngine::instance().deleteMacro(name.toStdString());
}

bool QmlMacroEngine::isRecording() const {
    return chimera::input::MacroEngine::instance().isRecording();
}

bool QmlMacroEngine::isPlaying() const {
    return chimera::input::MacroEngine::instance().isPlaying();
}

} // namespace chimera
