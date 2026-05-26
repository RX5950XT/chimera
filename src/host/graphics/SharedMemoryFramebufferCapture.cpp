#include "SharedMemoryFramebufferCapture.h"
#include "SharedMemoryFrameAbi.h"

#include <QImage>
#include <QDebug>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace chimera::graphics {

namespace {

using shmem::PixelFormat;
using shmem::SharedFrameHeader;

quint64 readSequence(const uchar *view) {
    quint64 sequence = 0;
    std::memcpy(&sequence, view + offsetof(SharedFrameHeader, sequence), sizeof(sequence));
    return sequence;
}

QImage::Format toImageFormat(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8888:
        return QImage::Format_RGBA8888;
    case PixelFormat::Bgra8888:
        return QImage::Format_ARGB32;
    case PixelFormat::Rgbx8888:
        return QImage::Format_RGBX8888;
    case PixelFormat::Rgb888:
        return QImage::Format_RGB888;
    }
    return QImage::Format_Invalid;
}

bool checkedFrameBounds(const SharedFrameHeader &h, qsizetype viewSize) {
    if (h.magic != shmem::kMagic || h.version != shmem::kVersion) return false;
    if (h.headerSize < sizeof(SharedFrameHeader)) return false;
    if (h.width == 0 || h.height == 0 || h.stride == 0) return false;
    if (h.width > 7680 || h.height > 4320) return false;
    if ((h.flags & ~shmem::kFlagBottomUp) != 0) return false;
    if ((h.flags & shmem::kFlagBottomUp) != 0) return false;

    const auto format = static_cast<PixelFormat>(h.format);
    const quint32 bpp = shmem::bytesPerPixel(format);
    if (bpp == 0) return false;
    if (h.width > (std::numeric_limits<quint32>::max)() / bpp) return false;

    const quint64 rowBytes = static_cast<quint64>(h.width) * bpp;
    if (h.stride < rowBytes) return false;
    const quint64 minBytes = static_cast<quint64>(h.stride) * (h.height - 1u) + rowBytes;
    const quint64 boundedViewSize = static_cast<quint64>(viewSize);
    if (h.pixelsOffset < h.headerSize || h.pixelsOffset > boundedViewSize) return false;
    if (h.pixelsSize < minBytes || h.pixelsSize > boundedViewSize) return false;
    if (h.pixelsOffset > boundedViewSize - h.pixelsSize) return false;
    return minBytes <= h.pixelsSize;
}

} // namespace

SharedMemoryFramebufferCapture::SharedMemoryFramebufferCapture(QString mappingName,
                                                               QString frameEventName,
                                                               QObject *parent)
    : FramebufferCapture(parent),
      m_mappingName(std::move(mappingName)),
      m_frameEventName(std::move(frameEventName)) {
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, &SharedMemoryFramebufferCapture::pollFrame);
}

SharedMemoryFramebufferCapture::~SharedMemoryFramebufferCapture() {
    stop();
}

bool SharedMemoryFramebufferCapture::start() {
    if (m_running) return true;
    if (!openMapping()) return false;

    m_running = true;
    m_pollTimer.start((std::max)(1, intervalMs()));
    pollFrame();
    return true;
}

void SharedMemoryFramebufferCapture::stop() {
    m_pollTimer.stop();
    m_running = false;
    closeMapping();
}

void SharedMemoryFramebufferCapture::pollFrame() {
    if (!m_running) return;
#ifdef _WIN32
    if (m_frameEvent) {
        const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(m_frameEvent), 0);
        if (wait == WAIT_TIMEOUT) return;
        if (wait != WAIT_OBJECT_0) {
            emit captureError(QStringLiteral("shared-memory frame event wait failed"));
            return;
        }
    }
#endif
    if (!readFrame()) {
        emit captureError(QStringLiteral("shared-memory frame read failed"));
    }
}

bool SharedMemoryFramebufferCapture::openMapping() {
#ifdef _WIN32
    if (m_mappingName.isEmpty()) {
        emit captureError(QStringLiteral("shared-memory mapping name is empty"));
        return false;
    }

    m_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, reinterpret_cast<LPCWSTR>(m_mappingName.utf16()));
    if (!m_mapping) {
        emit captureError(QStringLiteral("OpenFileMappingW failed for %1").arg(m_mappingName));
        return false;
    }

    m_view = static_cast<uchar *>(MapViewOfFile(static_cast<HANDLE>(m_mapping), FILE_MAP_READ, 0, 0, 0));
    if (!m_view) {
        emit captureError(QStringLiteral("MapViewOfFile failed for %1").arg(m_mappingName));
        closeMapping();
        return false;
    }

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(m_view, &info, sizeof(info)) == 0) {
        emit captureError(QStringLiteral("VirtualQuery failed for shared-memory view"));
        closeMapping();
        return false;
    }
    m_viewSize = static_cast<qsizetype>(info.RegionSize);

    if (!m_frameEventName.isEmpty()) {
        m_frameEvent = OpenEventW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(m_frameEventName.utf16()));
        if (!m_frameEvent) {
            qWarning() << "Shared-memory frame event not found; polling only:" << m_frameEventName;
        }
    }

    return true;
#else
    emit captureError(QStringLiteral("shared-memory capture is only implemented on Windows"));
    return false;
#endif
}

void SharedMemoryFramebufferCapture::closeMapping() {
#ifdef _WIN32
    if (m_view) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
        m_viewSize = 0;
    }
    if (m_frameEvent) {
        CloseHandle(static_cast<HANDLE>(m_frameEvent));
        m_frameEvent = nullptr;
    }
    if (m_mapping) {
        CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
    }
#endif
}

bool SharedMemoryFramebufferCapture::readFrame() {
#ifdef _WIN32
    if (!m_view || m_viewSize < static_cast<qsizetype>(sizeof(SharedFrameHeader))) return false;

    const quint64 sequenceBefore = readSequence(m_view);
    if (sequenceBefore == 0 || sequenceBefore == m_lastSequence) return true;
    if ((sequenceBefore & 1u) != 0) return true;
    MemoryBarrier();

    SharedFrameHeader header{};
    std::memcpy(&header, m_view, sizeof(header));
    if (header.sequence != sequenceBefore) return true;
    if (!checkedFrameBounds(header, m_viewSize)) return false;

    const auto format = toImageFormat(static_cast<PixelFormat>(header.format));
    if (format == QImage::Format_Invalid) return false;

    const uchar *pixels = m_view + static_cast<qsizetype>(header.pixelsOffset);
    QImage frame(static_cast<int>(header.width), static_cast<int>(header.height), format);
    if (frame.isNull()) return false;
    for (quint32 y = 0; y < header.height; ++y) {
        std::memcpy(frame.scanLine(static_cast<int>(y)),
                    pixels + static_cast<qsizetype>(header.stride) * y,
                    static_cast<size_t>(static_cast<quint64>(header.width) *
                                        shmem::bytesPerPixel(static_cast<PixelFormat>(header.format))));
    }
    MemoryBarrier();
    const quint64 sequenceAfter = readSequence(m_view);
    if (sequenceAfter != sequenceBefore || (sequenceAfter & 1u) != 0) return true;

    m_lastSequence = sequenceAfter;
    emit streamFrameReceived(true);
    emit frameReady(frame);
    return true;
#else
    return false;
#endif
}

} // namespace chimera::graphics
