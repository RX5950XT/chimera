#pragma once

#include <QSize>
#include <QString>
#include <memory>

namespace chimera::graphics {

class SharedD3D11TexturePublisher {
public:
    struct Config {
        QString metadataName;
        QString textureName;
        QString frameEventName;
        QSize size = QSize(1920, 1080);
        quint32 dxgiFormat = 87; // DXGI_FORMAT_B8G8R8A8_UNORM
        bool hasAlpha = true;
        void *d3d11Device = nullptr;
    };

    explicit SharedD3D11TexturePublisher(Config config);
    ~SharedD3D11TexturePublisher();

    SharedD3D11TexturePublisher(const SharedD3D11TexturePublisher &) = delete;
    SharedD3D11TexturePublisher &operator=(const SharedD3D11TexturePublisher &) = delete;

    bool start(QString *error = nullptr);
    void stop();
    bool isRunning() const;

    bool publishColor(float red, float green, float blue, float alpha = 1.0f,
                      QString *error = nullptr);
    bool publishTexture(void *d3d11Texture, QString *error = nullptr);
    bool publishBgraFrame(const void *data, int bytesPerLine, QString *error = nullptr);

    void *texture() const;
    QString metadataName() const;
    QString textureName() const;
    QString frameEventName() const;
    quint64 sequence() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace chimera::graphics
