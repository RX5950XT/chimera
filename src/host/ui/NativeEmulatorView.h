#pragma once

#include <QQuickItem>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QImage>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace chimera {

class ScreenRecorder;

class NativeEmulatorView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString instanceName READ instanceName WRITE setInstanceName NOTIFY instanceNameChanged)
    Q_PROPERTY(int consolePort READ consolePort WRITE setConsolePort NOTIFY consolePortChanged)
    Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool nativeEmbeddingEnabled READ nativeEmbeddingEnabled WRITE setNativeEmbeddingEnabled NOTIFY nativeEmbeddingEnabledChanged)

public:
    explicit NativeEmulatorView(QQuickItem *parent = nullptr);
    ~NativeEmulatorView() override;

    QString instanceName() const;
    void setInstanceName(const QString &name);

    int consolePort() const;
    void setConsolePort(int port);

    bool attached() const;
    bool isRecording() const;

    bool nativeEmbeddingEnabled() const;
    void setNativeEmbeddingEnabled(bool enabled);

    Q_INVOKABLE bool saveScreenshot(const QString &filePath) const;
    Q_INVOKABLE bool startRecording(const QString &filePath, int fps);
    Q_INVOKABLE void stopRecording();

signals:
    void instanceNameChanged();
    void consolePortChanged();
    void attachedChanged();
    void recordingChanged();
    void nativeEmbeddingEnabledChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void probeAndAttach();
    void hideAuxiliaryWindows();
    void updateNativeGeometry();
    void setAttached(bool attached);
    QImage grabFrame() const;

    QString m_instanceName;
    int m_consolePort = 5554;
    bool m_attached = false;
    bool m_recording = false;
    bool m_nativeEmbeddingEnabled = true;
    QTimer m_probeTimer;
    QTimer m_recordingTimer;
    ScreenRecorder *m_nativeRecorder = nullptr;

#ifdef Q_OS_WIN
    HWND m_childWindow = nullptr;
    DWORD m_childProcessId = 0;
    LONG_PTR m_originalStyle = 0;
    LONG_PTR m_originalExStyle = 0;
    QRect m_lastNativeRect;
    QTimer m_auxiliaryWindowTimer;
    std::vector<HWND> m_hiddenAuxiliaryWindows;

    HWND findEmulatorWindow() const;
    void attachWindow(HWND hwnd);
    void detachWindow();
#endif
};

} // namespace chimera
