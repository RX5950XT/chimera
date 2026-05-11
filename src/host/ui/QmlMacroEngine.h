#pragma once

#include <QObject>
#include <QStringList>

namespace chimera {

/**
 * @brief QML-friendly wrapper around MacroEngine.
 */
class QmlMacroEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)

public:
    explicit QmlMacroEngine(QObject *parent = nullptr);

    Q_INVOKABLE QStringList listMacros() const;
    Q_INVOKABLE void startRecording(const QString &name);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void startPlayback(const QString &name, int loopCount);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void deleteMacro(const QString &name);

    bool isRecording() const;
    bool isPlaying() const;

signals:
    void recordingChanged();
    void playingChanged();
};

} // namespace chimera
