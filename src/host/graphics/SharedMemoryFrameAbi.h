#pragma once

#include <QtGlobal>
#include <cstddef>

namespace chimera::graphics::shmem {

constexpr quint32 kMagic = 0x43484D46; // CHMF
constexpr quint32 kD3D11TextureMagic = 0x43485458; // CHTX
constexpr quint32 kVersion = 1;
constexpr quint32 kFlagBottomUp = 0x1;
constexpr quint32 kD3D11FlagHasAlpha = 0x1;
constexpr quint32 kD3D11TextureNameChars = 260;

enum class PixelFormat : quint32 {
    Rgba8888 = 1,
    Bgra8888 = 2,
    Rgbx8888 = 3,
    Rgb888   = 4,
};

#pragma pack(push, 1)
struct SharedFrameHeader {
    quint32 magic = 0;
    quint32 version = 0;
    quint32 headerSize = 0;
    quint32 width = 0;
    quint32 height = 0;
    quint32 stride = 0;
    quint32 format = 0;
    quint32 flags = 0;
    quint64 sequence = 0; // odd = producer writing, even = complete
    quint64 pixelsOffset = 0;
    quint64 pixelsSize = 0;
};

struct SharedD3D11TextureHeader {
    quint32 magic = 0;
    quint32 version = 0;
    quint32 headerSize = 0;
    quint32 width = 0;
    quint32 height = 0;
    quint32 dxgiFormat = 0;
    quint32 flags = 0;
    quint32 reserved = 0;
    quint64 sequence = 0; // odd = producer writing metadata, even = complete
    char16_t textureName[kD3D11TextureNameChars] = {};
};
#pragma pack(pop)

static_assert(sizeof(SharedFrameHeader) == 56);
static_assert(offsetof(SharedFrameHeader, sequence) == 32);
static_assert(sizeof(SharedD3D11TextureHeader) == 560);
static_assert(offsetof(SharedD3D11TextureHeader, sequence) == 32);

inline quint32 bytesPerPixel(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8888:
    case PixelFormat::Bgra8888:
    case PixelFormat::Rgbx8888:
        return 4;
    case PixelFormat::Rgb888:
        return 3;
    }
    return 0;
}

} // namespace chimera::graphics::shmem
