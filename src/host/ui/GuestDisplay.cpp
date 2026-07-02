#include "GuestDisplay.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTouchEvent>
#include <QInputMethodEvent>
#include <QGuiApplication>
#include <QCursor>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtQuick/qsgtexture_platform.h>
#include <QDebug>
#include <algorithm>
#include "InputBridge.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

namespace chimera {

namespace {

class GuestTextureNode final : public QSGSimpleTextureNode {
public:
    ~GuestTextureNode() override { delete texture(); }

    void replaceTexture(QSGTexture *next) {
        QSGTexture *previous = texture();
        setTexture(next);
        delete previous;
    }

    quint64 sequence = 0;
    bool nativeD3D11 = false;
    void *nativeTexture = nullptr;
    QString nativeTextureName;
    QSize nativeTextureSize;
};

} // namespace

struct GuestDisplay::NativeD3D11TextureState {
#ifdef Q_OS_WIN
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    // Producer texture is created with D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX; a
    // cross-process reader that never does AcquireSync gets stale/zero content
    // (verified: no-acquire read = zeros while acquired read shows the frame).
    // Each new sequence is copied under the keyed mutex into this private
    // texture, which is what the scene graph actually samples.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> privateCopy;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutex;
#endif
    QString name;
    QSize size;
    quint64 sequence = 0;
    quint64 copiedSequence = 0;
    bool hasAlpha = false;
    bool uploadTexture = false;
};

GuestDisplay::GuestDisplay(QQuickItem *parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setFlag(QQuickItem::ItemHasContents, true);
    setActiveFocusOnTab(true);
    setFocus(true);
    // Idle safety re-present only. Active frames are driven per-frame by the
    // event-driven setSharedD3D11Texture()->update() (the capture is 1:1 event-driven),
    // so a 16ms (62Hz) timer here just adds GUI-thread wakeups that compete with the
    // queued per-frame texture signals during scroll and depress stream/render cadence
    // below the guest's. 200ms keeps a cheap idle re-present without that contention.
    m_presentTimer.setInterval(200);
    m_presentTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_presentTimer, &QTimer::timeout, this, [this]() {
        if (m_sharedD3D11TextureName.isEmpty()) {
            m_presentTimer.stop();
            return;
        }
        update();
    });
}

GuestDisplay::~GuestDisplay() = default;

QImage GuestDisplay::frame() const {
    return m_frame;
}

void GuestDisplay::setFrame(const QImage &img) {
    const bool sizeChanged = m_frame.isNull() || img.size() != m_frame.size();
    m_frame = img;
    ++m_frameSequence;
    m_nativeD3D11Texture = nullptr;
    m_nativeD3D11TextureSequence = 0;
    m_nativeD3D11TextureHasAlpha = false;
    m_sharedD3D11TextureName.clear();
    m_nativeD3D11State.reset();
    if (m_presentTimer.isActive()) {
        m_presentTimer.stop();
    }
    if (sizeChanged && !m_frame.isNull() && !m_guestSize.isValid()) {
        m_mapper.setGuestSize(m_frame.width(), m_frame.height());
    }
    update();
    if (sizeChanged) emit frameChanged();
}

bool GuestDisplay::hasFrame() const {
    return !m_frame.isNull() || m_nativeD3D11Texture || !m_sharedD3D11TextureName.isEmpty();
}

void GuestDisplay::setNativeD3D11Texture(void *texture,
                                         const QSize &size,
                                         quint64 sequence,
                                         bool hasAlpha) {
    if (!texture || !size.isValid() || sequence == 0) {
        clearNativeD3D11Texture();
        return;
    }

    const bool sizeChanged = m_nativeD3D11TextureSize != size;
    m_nativeD3D11Texture = texture;
    m_nativeD3D11TextureSize = size;
    m_nativeD3D11TextureSequence = sequence;
    m_nativeD3D11TextureHasAlpha = hasAlpha;
    m_sharedD3D11TextureName.clear();
    m_uploadD3D11State.reset();
    m_frame = QImage();
    if (m_presentTimer.isActive()) {
        m_presentTimer.stop();
    }
    if (sizeChanged && !m_guestSize.isValid()) {
        m_mapper.setGuestSize(size.width(), size.height());
    }
    update();
    if (sizeChanged) emit frameChanged();
}

void GuestDisplay::setSharedD3D11Texture(const QString &textureName,
                                         const QSize &size,
                                         quint64 sequence,
                                         bool hasAlpha) {
    if (textureName.isEmpty() || !size.isValid() || sequence == 0) {
        clearNativeD3D11Texture();
        return;
    }

    const bool sizeChanged = m_nativeD3D11TextureSize != size;
    m_sharedD3D11TextureName = textureName;
    m_nativeD3D11Texture = nullptr;
    m_nativeD3D11TextureSize = size;
    m_nativeD3D11TextureSequence = sequence;
    m_nativeD3D11TextureHasAlpha = hasAlpha;
    m_uploadD3D11State.reset();
    m_frame = QImage();
    if (!m_presentTimer.isActive()) {
        m_presentTimer.start();
    }
    if (sizeChanged && !m_guestSize.isValid()) {
        m_mapper.setGuestSize(size.width(), size.height());
    }
    update();
    if (sizeChanged) emit frameChanged();
}

void GuestDisplay::clearNativeD3D11Texture() {
    if (!m_nativeD3D11Texture && m_sharedD3D11TextureName.isEmpty() && !m_nativeD3D11State) return;
    m_nativeD3D11Texture = nullptr;
    m_nativeD3D11TextureSize = QSize();
    m_nativeD3D11TextureSequence = 0;
    m_nativeD3D11TextureHasAlpha = false;
    m_sharedD3D11TextureName.clear();
    m_nativeD3D11State.reset();
    m_uploadD3D11State.reset();
    if (m_presentTimer.isActive()) {
        m_presentTimer.stop();
    }
    update();
    emit frameChanged();
}

void GuestDisplay::setGuestSize(const QSize &size) {
    if (size.width() <= 0 || size.height() <= 0 || m_guestSize == size) return;
    m_guestSize = size;
    m_mapper.setGuestSize(size.width(), size.height());
}

void GuestDisplay::setRotation(int degrees) {
    m_mapper.setRotation(degrees);
    update();
}

void GuestDisplay::geometryChange(const QRectF &newGeom, const QRectF &oldGeom) {
    QQuickItem::geometryChange(newGeom, oldGeom);
    m_mapper.setHostViewSize(static_cast<int>(newGeom.width()),
                             static_cast<int>(newGeom.height()));
}

bool GuestDisplay::saveScreenshot(const QString &filePath) const {
    if (m_frame.isNull()) return false;
    return m_frame.save(filePath);
}

void GuestDisplay::setMouseLocked(bool locked) {
    if (m_mouseLocked == locked) return;
    m_mouseLocked = locked;
    if (locked) {
        // Initialize virtual cursor at widget center
        m_virtualMouse = QPointF(width() / 2.0, height() / 2.0);
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);
        if (window()) {
            const QPoint globalCenter = window()->mapToGlobal(
                mapToScene(m_virtualMouse).toPoint());
            QCursor::setPos(globalCenter);
        }
    } else {
        QGuiApplication::restoreOverrideCursor();
        // Restore item cursor based on current cursor mode
        if (m_cursorMode == 1) setCursor(Qt::CrossCursor);
        else unsetCursor();
    }
    emit mouseLockChanged();
}

void GuestDisplay::setCursorMode(int mode) {
    if (m_cursorMode == mode) return;
    m_cursorMode = mode;
    if (!m_mouseLocked) {
        if (mode == 1) setCursor(Qt::CrossCursor);
        else unsetCursor();
    }
    emit cursorModeChanged();
}

QSGNode *GuestDisplay::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    auto *node = static_cast<GuestTextureNode *>(oldNode);
    if (m_frame.isNull() && !m_nativeD3D11Texture && m_sharedD3D11TextureName.isEmpty()) {
        delete node;
        return nullptr;
    }

    QQuickWindow *quickWindow = window();
    if (!quickWindow) {
        return node;
    }

    if (!node) {
        node = new GuestTextureNode();
    }

    if (m_nativeD3D11Texture || !m_sharedD3D11TextureName.isEmpty()) {
        const auto api = quickWindow->rendererInterface()->graphicsApi();
        if (api != QSGRendererInterface::Direct3D11) {
            delete node;
            return nullptr;
        }

#ifdef Q_OS_WIN
        void *nativeTexture = m_nativeD3D11Texture;
        if (!nativeTexture && !m_sharedD3D11TextureName.isEmpty()) {
            const bool shouldOpen = !m_nativeD3D11State ||
                                    m_nativeD3D11State->name != m_sharedD3D11TextureName ||
                                    m_nativeD3D11State->size != m_nativeD3D11TextureSize;
            if (shouldOpen) {
                auto state = std::make_unique<NativeD3D11TextureState>();
                state->name = m_sharedD3D11TextureName;
                state->size = m_nativeD3D11TextureSize;
                state->sequence = m_nativeD3D11TextureSequence;
                state->hasAlpha = m_nativeD3D11TextureHasAlpha;

                auto *renderer = quickWindow->rendererInterface();
                auto *device = static_cast<ID3D11Device *>(
                    renderer->getResource(quickWindow, QSGRendererInterface::DeviceResource));
                Microsoft::WRL::ComPtr<ID3D11Device1> device1;
                if (device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
                    const std::wstring name = m_sharedD3D11TextureName.toStdWString();
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                    const HRESULT hr = device1->OpenSharedResourceByName(
                        name.c_str(),
                        DXGI_SHARED_RESOURCE_READ,
                        IID_PPV_ARGS(&texture));
                    if (SUCCEEDED(hr)) {
                        D3D11_TEXTURE2D_DESC desc = {};
                        texture->GetDesc(&desc);
                        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> probeSrv;
                        const HRESULT srvHr = device->CreateShaderResourceView(
                            texture.Get(), nullptr, &probeSrv);
                        qDebug() << "Opened shared D3D11 texture"
                                 << m_sharedD3D11TextureName
                                 << "size" << desc.Width << desc.Height
                                 << "format" << desc.Format
                                 << "bind" << QStringLiteral("0x%1").arg(desc.BindFlags, 0, 16)
                                 << "misc" << QStringLiteral("0x%1").arg(desc.MiscFlags, 0, 16)
                                 << "srvProbe" << QStringLiteral("0x%1")
                                                       .arg(static_cast<quint32>(srvHr), 8, 16, QLatin1Char('0'));
                        state->texture = texture;
                        texture.As(&state->keyedMutex);
                        D3D11_TEXTURE2D_DESC copyDesc = desc;
                        copyDesc.MiscFlags = 0;
                        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                        copyDesc.Usage = D3D11_USAGE_DEFAULT;
                        copyDesc.CPUAccessFlags = 0;
                        if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, &state->privateCopy))) {
                            qWarning() << "Shared D3D11 private copy texture creation failed";
                        }
                        m_nativeD3D11State = std::move(state);
                    } else {
                        qWarning() << "OpenSharedResourceByName failed for"
                                   << m_sharedD3D11TextureName
                                   << QStringLiteral("hr=0x%1")
                                          .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
                    }
                }
            }
            if (m_nativeD3D11State && m_nativeD3D11State->texture && m_nativeD3D11State->privateCopy) {
                // Sample a private copy, not the shared texture: reads without the
                // keyed mutex return stale/zero content cross-process, and the scene
                // graph cannot hold the mutex across its render pass.
                if (m_nativeD3D11State->copiedSequence != m_nativeD3D11TextureSequence) {
                    auto *renderer = quickWindow->rendererInterface();
                    auto *context = static_cast<ID3D11DeviceContext *>(
                        renderer->getResource(quickWindow, QSGRendererInterface::DeviceContextResource));
                    if (context) {
                        bool acquired = false;
                        if (m_nativeD3D11State->keyedMutex) {
                            // WAIT_TIMEOUT (0x102) passes SUCCEEDED(); only S_OK means held.
                            acquired = (m_nativeD3D11State->keyedMutex->AcquireSync(0, 4) == S_OK);
                        }
                        if (acquired || !m_nativeD3D11State->keyedMutex) {
                            context->CopyResource(m_nativeD3D11State->privateCopy.Get(),
                                                  m_nativeD3D11State->texture.Get());
                            m_nativeD3D11State->copiedSequence = m_nativeD3D11TextureSequence;
                        }
                        if (acquired) {
                            m_nativeD3D11State->keyedMutex->ReleaseSync(0);
                        }
                    }
                }
                nativeTexture = m_nativeD3D11State->privateCopy.Get();
            }
        }
#else
        void *nativeTexture = m_nativeD3D11Texture;
#endif

        if (!nativeTexture) {
            delete node;
            return nullptr;
        }

        const bool textureChanged = !node->nativeD3D11 ||
                                    node->nativeTexture != nativeTexture ||
                                    node->nativeTextureName != m_sharedD3D11TextureName ||
                                    node->nativeTextureSize != m_nativeD3D11TextureSize;
        if (textureChanged) {
            auto options = m_nativeD3D11TextureHasAlpha
                ? QQuickWindow::CreateTextureOptions(QQuickWindow::TextureHasAlphaChannel)
                : QQuickWindow::CreateTextureOptions();
            QSGTexture *texture = QNativeInterface::QSGD3D11Texture::fromNative(
                nativeTexture,
                quickWindow,
                m_nativeD3D11TextureSize,
                options);
            if (texture) {
                texture->setFiltering(QSGTexture::Nearest);
                node->replaceTexture(texture);
                node->sequence = m_nativeD3D11TextureSequence;
                node->nativeD3D11 = true;
                node->nativeTexture = nativeTexture;
                node->nativeTextureName = m_sharedD3D11TextureName;
                node->nativeTextureSize = m_nativeD3D11TextureSize;
            }
        }
        const bool frameAdvanced = textureChanged || node->sequence != m_nativeD3D11TextureSequence;
        node->sequence = m_nativeD3D11TextureSequence;

        const QRectF dr = m_mapper.displayRect();
        node->setRect(dr.isEmpty() ? boundingRect() : dr);
        if (frameAdvanced) {
            emit framePainted();
        }
        return node;
    }

    QImage textureImage = m_frame;
    if (textureImage.format() != QImage::Format_RGBA8888 &&
        textureImage.format() != QImage::Format_RGBX8888) {
        textureImage = textureImage.convertToFormat(QImage::Format_RGBA8888);
    }

#ifdef Q_OS_WIN
    if (quickWindow->rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11) {
        auto *renderer = quickWindow->rendererInterface();
        auto *device = static_cast<ID3D11Device *>(
            renderer->getResource(quickWindow, QSGRendererInterface::DeviceResource));
        auto *context = static_cast<ID3D11DeviceContext *>(
            renderer->getResource(quickWindow, QSGRendererInterface::DeviceContextResource));
        const QSize imageSize = textureImage.size();
        const bool needTexture = !m_uploadD3D11State ||
                                 m_uploadD3D11State->size != imageSize ||
                                 !m_uploadD3D11State->texture;
        if (device && context && (needTexture || m_uploadD3D11State)) {
            if (needTexture) {
                auto state = std::make_unique<NativeD3D11TextureState>();
                state->name = QStringLiteral("$upload");
                state->size = imageSize;
                state->uploadTexture = true;
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = static_cast<UINT>(imageSize.width());
                desc.Height = static_cast<UINT>(imageSize.height());
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &state->texture))) {
                    m_uploadD3D11State = std::move(state);
                }
            }
            if (m_uploadD3D11State && m_uploadD3D11State->texture) {
                if (m_uploadD3D11State->sequence != m_frameSequence) {
                    context->UpdateSubresource(m_uploadD3D11State->texture.Get(),
                                               0,
                                               nullptr,
                                               textureImage.constBits(),
                                               static_cast<UINT>(textureImage.bytesPerLine()),
                                               0);
                    m_uploadD3D11State->sequence = m_frameSequence;
                }
                void *nativeTexture = m_uploadD3D11State->texture.Get();
                const bool textureChanged = !node->nativeD3D11 ||
                                            node->nativeTexture != nativeTexture ||
                                            node->nativeTextureName != m_uploadD3D11State->name ||
                                            node->nativeTextureSize != imageSize;
                if (textureChanged) {
                    QSGTexture *texture = QNativeInterface::QSGD3D11Texture::fromNative(
                        nativeTexture,
                        quickWindow,
                        imageSize,
                        QQuickWindow::TextureHasAlphaChannel);
                    if (texture) {
                        texture->setFiltering(QSGTexture::Nearest);
                        node->replaceTexture(texture);
                        node->nativeD3D11 = true;
                        node->nativeTexture = nativeTexture;
                        node->nativeTextureName = m_uploadD3D11State->name;
                        node->nativeTextureSize = imageSize;
                    }
                }
                const bool frameAdvanced = textureChanged || node->sequence != m_frameSequence;
                node->sequence = m_frameSequence;
                const QRectF dr = m_mapper.displayRect();
                node->setRect(dr.isEmpty() ? boundingRect() : dr);
                if (frameAdvanced) {
                    emit framePainted();
                }
                return node;
            }
        }
    }
#endif

    QSGTexture *texture = quickWindow->createTextureFromImage(
        textureImage,
        QQuickWindow::TextureHasAlphaChannel);
    if (!texture) {
        return node;
    }
    texture->setFiltering(QSGTexture::Nearest);
    node->replaceTexture(texture);
    const bool frameAdvanced = node->sequence != m_frameSequence;
    node->sequence = m_frameSequence;
    node->nativeD3D11 = false;
    node->nativeTexture = nullptr;
    node->nativeTextureName.clear();
    node->nativeTextureSize = QSize();

    const QRectF dr = m_mapper.displayRect();
    node->setRect(dr.isEmpty() ? boundingRect() : dr);
    if (frameAdvanced) {
        emit framePainted();
    }
    return node;
}

void GuestDisplay::keyPressEvent(QKeyEvent *event) {
    // Escape unlocks mouse (always, so users can escape FPS lock)
    if (m_mouseLocked && event->key() == Qt::Key_Escape) {
        setMouseLocked(false);
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onKeyEvent(
        true, event->nativeScanCode(), event->key());
    event->accept();
}

void GuestDisplay::keyReleaseEvent(QKeyEvent *event) {
    chimera::input::InputBridge::instance().onKeyEvent(
        false, event->nativeScanCode(), event->key());
    event->accept();
}

void GuestDisplay::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus();
    QPoint guestPos;
    if (!m_mapper.mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseButton(
        true, event->button(), guestPos.x(), guestPos.y());
    event->accept();
}

void GuestDisplay::mouseReleaseEvent(QMouseEvent *event) {
    QPoint guestPos;
    if (!m_mapper.mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseButton(
        false, event->button(), guestPos.x(), guestPos.y());
    event->accept();
}

void GuestDisplay::mouseMoveEvent(QMouseEvent *event) {
    if (m_mouseLocked) {
        // FPS relative mode: compute delta from widget center, accumulate into virtual cursor
        const QPointF center(width() / 2.0, height() / 2.0);
        const QPointF delta = event->position() - center;
        if (!delta.isNull()) {
            m_virtualMouse.rx() = qBound(0.0, m_virtualMouse.x() + delta.x(), width());
            m_virtualMouse.ry() = qBound(0.0, m_virtualMouse.y() + delta.y(), height());
            QPoint guestPos;
            if (m_mapper.mapToGuest(m_virtualMouse, guestPos))
                chimera::input::InputBridge::instance().onMouseMove(
                    guestPos.x(), guestPos.y(), 0, 0);
            // Warp physical cursor back to center
            if (window()) {
                const QPoint globalCenter = window()->mapToGlobal(
                    mapToScene(center).toPoint());
                QCursor::setPos(globalCenter);
            }
        }
        event->accept();
        return;
    }
    QPoint guestPos;
    if (!m_mapper.mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseMove(guestPos.x(), guestPos.y(), 0, 0);
    event->accept();
}

void GuestDisplay::wheelEvent(QWheelEvent *event) {
    QPoint delta = event->angleDelta();
    if (delta.isNull() && !event->pixelDelta().isNull())
        delta = event->pixelDelta() * 8;
    QPoint guestPos;
    if (m_mapper.mapToGuest(event->position(), guestPos)) {
        chimera::input::InputBridge::instance().onWheel(
            delta.x(), delta.y(), guestPos.x(), guestPos.y());
    } else {
        chimera::input::InputBridge::instance().onWheel(delta.x(), delta.y());
    }
    event->accept();
}

void GuestDisplay::touchEvent(QTouchEvent *event) {
    for (const QEventPoint &tp : event->points()) {
        if (tp.state() == QEventPoint::Stationary) continue;
        const bool active = (tp.state() != QEventPoint::Released);
        QPoint guestPos;
        if (active && !m_mapper.mapToGuest(tp.position(), guestPos)) {
            chimera::input::InputBridge::instance().onTouchPoint(
                static_cast<int>(tp.id()), 0, 0, false);
            continue;
        }
        chimera::input::InputBridge::instance().onTouchPoint(
            static_cast<int>(tp.id()),
            active ? guestPos.x() : 0,
            active ? guestPos.y() : 0,
            active);
    }
    event->accept();
}

void GuestDisplay::inputMethodEvent(QInputMethodEvent *event) {
    const QString committed = event->commitString();
    if (!committed.isEmpty()) {
        chimera::input::InputBridge::instance().onTextInput(
            committed.toStdString());
    }
    event->accept();
}

QVariant GuestDisplay::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) return QVariant(true);
    return QQuickItem::inputMethodQuery(query);
}

} // namespace chimera
