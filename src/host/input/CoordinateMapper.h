#pragma once

#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <algorithm>

namespace chimera::input {

/**
 * @brief Maps host-view coordinates to guest framebuffer coordinates.
 *
 * Centralises all spatial coordinate math so callers (GuestDisplay,
 * InputBridge) carry no coordinate logic.
 *
 * Coordinate spaces:
 *   Host view   — logical pixels in the Qt item (may be letterboxed)
 *   Guest frame — guest framebuffer pixels (0..guestW-1, 0..guestH-1)
 *   HvSocket    — 0..32767 absolute range expected by HvSocket/uinput
 *
 * Rotation semantics: the guest image is displayed rotated CW by
 * m_rotation degrees relative to the host view. Applies to games locked
 * in a specific orientation.
 *
 * Log format: [DBG] CoordinateMapper host(820,440)→fb(640,360)→raw(640,360) rot=0
 */
class CoordinateMapper {
public:
    void setHostViewSize(int w, int h)   { m_hostW = w; m_hostH = h; }
    void setGuestSize(int w, int h)      { m_guestW = w; m_guestH = h; }
    void setRotation(int degrees)        { m_rotation = degrees; }
    void setDevicePixelRatio(qreal dpr)  { m_dpr = dpr > 0.0 ? dpr : 1.0; }

    int   guestWidth()  const { return m_guestW; }
    int   guestHeight() const { return m_guestH; }
    int   rotation()    const { return m_rotation; }
    qreal dpr()         const { return m_dpr; }

    /**
     * Returns the sub-rect of the host view where the guest image is rendered
     * (aspect-ratio letterboxed, accounting for rotation).
     */
    QRectF displayRect() const;

    /**
     * Maps a host-view position to guest framebuffer coordinates.
     * Returns false if hostPos falls in the letterbox border (outside the guest image).
     */
    bool mapToGuest(QPointF hostPos, QPoint &guestOut) const;

    /**
     * Converts guest framebuffer coordinates to the 0..32767 range
     * expected by HvSocket/uinput absolute-position events.
     */
    QPoint guestToHvSocket(QPoint guest) const;

private:
    int   m_hostW = 0, m_hostH = 0;
    int   m_guestW = 1280, m_guestH = 720;
    int   m_rotation = 0;  // degrees CW: 0, 90, 180, 270
    qreal m_dpr = 1.0;
};

// ——— inline implementation ———————————————————————————————————————————————————

inline QRectF CoordinateMapper::displayRect() const {
    if (m_hostW <= 0 || m_hostH <= 0 || m_guestW <= 0 || m_guestH <= 0)
        return {};

    // For 90°/270° rotation the guest aspect ratio appears transposed on screen
    const double srcW = (m_rotation == 90 || m_rotation == 270) ? m_guestH : m_guestW;
    const double srcH = (m_rotation == 90 || m_rotation == 270) ? m_guestW : m_guestH;

    const double hostAR  = double(m_hostW) / m_hostH;
    const double guestAR = srcW / srcH;
    double nw, nh;
    if (hostAR > guestAR) {
        nh = m_hostH;
        nw = nh * guestAR;
    } else {
        nw = m_hostW;
        nh = nw / guestAR;
    }
    return QRectF((m_hostW - nw) / 2.0, (m_hostH - nh) / 2.0, nw, nh);
}

inline bool CoordinateMapper::mapToGuest(QPointF hostPos, QPoint &guestOut) const {
    const QRectF dr = displayRect();
    if (dr.isEmpty() || !dr.contains(hostPos)) return false;

    // Normalise to [0,1] within the display rect
    const double nx = (hostPos.x() - dr.x()) / dr.width();
    const double ny = (hostPos.y() - dr.y()) / dr.height();

    // Apply inverse rotation to recover guest coordinates
    double gx, gy;
    switch (m_rotation) {
    case 90:  // display appears portrait; nx~guestY, ny~(reversed)guestX
        gx = (1.0 - ny) * (m_guestW - 1);
        gy = nx           * (m_guestH - 1);
        break;
    case 180:
        gx = (1.0 - nx) * (m_guestW - 1);
        gy = (1.0 - ny) * (m_guestH - 1);
        break;
    case 270: // display appears portrait; ny~guestX, nx~(reversed)guestY
        gx = ny           * (m_guestW - 1);
        gy = (1.0 - nx) * (m_guestH - 1);
        break;
    case 0:
    default:
        gx = nx * (m_guestW - 1);
        gy = ny * (m_guestH - 1);
        break;
    }

    guestOut = QPoint(
        static_cast<int>(std::clamp(gx, 0.0, double(m_guestW - 1))),
        static_cast<int>(std::clamp(gy, 0.0, double(m_guestH - 1))));
    return true;
}

inline QPoint CoordinateMapper::guestToHvSocket(QPoint guest) const {
    const int hvX = m_guestW > 1 ? (guest.x() * 32767) / (m_guestW - 1) : 0;
    const int hvY = m_guestH > 1 ? (guest.y() * 32767) / (m_guestH - 1) : 0;
    return QPoint(std::clamp(hvX, 0, 32767), std::clamp(hvY, 0, 32767));
}

} // namespace chimera::input
