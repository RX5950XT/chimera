#include "SharedD3D11TextureCapture.h"
#include "SharedMemoryFrameAbi.h"

#include <QDebug>

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgiformat.h>
#endif

namespace chimera::graphics {

namespace {

using Header = chimera::graphics::shmem::SharedD3D11TextureHeader;

QString winError(const char *api) {
#ifdef _WIN32
    return QStringLiteral("%1 failed (%2)").arg(QString::fromLatin1(api)).arg(GetLastError());
#else
    return QStringLiteral("%1 is only supported on Windows").arg(QString::fromLatin1(api));
#endif
}

bool isSupportedDxgiFormat(quint32 format) {
#ifdef _WIN32
    return format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           format == DXGI_FORMAT_R8G8B8A8_UNORM ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
#else
    Q_UNUSED(format);
    return false;
#endif
}

QString textureNameFromHeader(const Header &header) {
    qsizetype len = 0;
    while (len < shmem::kD3D11TextureNameChars && header.textureName[len] != u'\0')
        ++len;
    if (len == 0 || len == shmem::kD3D11TextureNameChars)
        return {};
    return QString::fromUtf16(header.textureName, len);
}

bool validateHeader(const Header &header, QString *error) {
    if (header.magic != shmem::kD3D11TextureMagic) {
        *error = QStringLiteral("invalid shared D3D11 texture magic");
        return false;
    }
    if (header.version != shmem::kVersion || header.headerSize < sizeof(Header)) {
        *error = QStringLiteral("unsupported shared D3D11 texture header");
        return false;
    }
    if (header.width == 0 || header.height == 0 ||
        header.width > 7680 || header.height > 4320) {
        *error = QStringLiteral("invalid shared D3D11 texture dimensions");
        return false;
    }
    if ((header.flags & ~shmem::kD3D11FlagHasAlpha) != 0) {
        *error = QStringLiteral("unsupported shared D3D11 texture flags");
        return false;
    }
    if (!isSupportedDxgiFormat(header.dxgiFormat)) {
        *error = QStringLiteral("unsupported shared D3D11 texture format");
        return false;
    }
    if (textureNameFromHeader(header).isEmpty()) {
        *error = QStringLiteral("empty shared D3D11 texture name");
        return false;
    }
    return true;
}

} // namespace

SharedD3D11TextureCapture::SharedD3D11TextureCapture(QString mappingName,
                                                     QString frameEventName,
                                                     QObject *parent)
    : FramebufferCapture(parent),
      m_mappingName(std::move(mappingName)),
      m_frameEventName(std::move(frameEventName)) {
}

SharedD3D11TextureCapture::~SharedD3D11TextureCapture() {
    stop();
}

bool SharedD3D11TextureCapture::start() {
    if (m_running.load(std::memory_order_acquire)) return true;
    if (!openMapping()) return false;
    m_stopRequested.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    readFrame();
    m_worker = std::thread(&SharedD3D11TextureCapture::workerLoop, this);
    return true;
}

void SharedD3D11TextureCapture::stop() {
    m_stopRequested.store(true, std::memory_order_release);
    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id())
        m_worker.join();
    m_running.store(false, std::memory_order_release);
    closeMapping();
}

void SharedD3D11TextureCapture::workerLoop() {
#ifdef _WIN32
    const DWORD timeoutMs = static_cast<DWORD>(qMax(1, m_intervalMs));
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        if (m_frameEvent) {
            const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(m_frameEvent), timeoutMs);
            if (m_stopRequested.load(std::memory_order_acquire)) break;
            if (wait == WAIT_TIMEOUT) continue;
            if (wait != WAIT_OBJECT_0) {
                emit captureError(QStringLiteral("shared D3D11 texture event wait failed"));
                std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
                continue;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        }
        readFrame();
    }
#else
    Q_UNUSED(this);
#endif
}

bool SharedD3D11TextureCapture::openMapping() {
#ifdef _WIN32
    if (m_mappingName.isEmpty()) {
        emit captureError(QStringLiteral("shared D3D11 texture metadata mapping name is empty"));
        return false;
    }

    m_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
                                 reinterpret_cast<LPCWSTR>(m_mappingName.utf16()));
    if (!m_mapping) {
        emit captureError(winError("OpenFileMappingW"));
        return false;
    }

    m_viewSize = static_cast<qsizetype>(sizeof(Header));
    m_view = static_cast<uchar *>(MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0,
                                                static_cast<SIZE_T>(m_viewSize)));
    if (!m_view) {
        emit captureError(winError("MapViewOfFile"));
        closeMapping();
        return false;
    }

    if (!m_frameEventName.isEmpty()) {
        m_frameEvent = OpenEventW(SYNCHRONIZE, FALSE,
                                  reinterpret_cast<LPCWSTR>(m_frameEventName.utf16()));
        if (!m_frameEvent) {
            qWarning() << "OpenEventW failed for shared D3D11 texture metadata; polling continues";
        }
    }
    return true;
#else
    emit captureError(QStringLiteral("shared D3D11 texture capture is only supported on Windows"));
    return false;
#endif
}

void SharedD3D11TextureCapture::closeMapping() {
#ifdef _WIN32
    if (m_view) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    if (m_mapping) {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
    }
    if (m_frameEvent) {
        CloseHandle(m_frameEvent);
        m_frameEvent = nullptr;
    }
#else
    m_view = nullptr;
    m_mapping = nullptr;
    m_frameEvent = nullptr;
#endif
    m_viewSize = 0;
    m_lastSequence = 0;
}

bool SharedD3D11TextureCapture::readFrame() {
#ifdef _WIN32
    if (!m_view || m_viewSize < static_cast<qsizetype>(sizeof(Header))) {
        emit captureError(QStringLiteral("shared D3D11 texture metadata view is not mapped"));
        return false;
    }

    const auto *live = reinterpret_cast<const Header *>(m_view);
    const quint64 sequenceBefore = live->sequence;
    if (sequenceBefore == 0 || sequenceBefore == m_lastSequence || (sequenceBefore & 1U))
        return true;

    MemoryBarrier();
    const Header header = *live;
    MemoryBarrier();
    const quint64 sequenceAfter = live->sequence;
    if (sequenceBefore != sequenceAfter || (sequenceAfter & 1U))
        return true;

    QString error;
    if (!validateHeader(header, &error)) {
        emit captureError(error);
        return false;
    }

    const QString name = textureNameFromHeader(header);
    m_lastSequence = sequenceAfter;
    emit streamFrameReceived(true);
    emit sharedD3D11TextureReady(name,
                                 QSize(static_cast<int>(header.width), static_cast<int>(header.height)),
                                 sequenceAfter,
                                 (header.flags & shmem::kD3D11FlagHasAlpha) != 0);
    return true;
#else
    return false;
#endif
}

} // namespace chimera::graphics
