#include "SharedMemoryFrameAbi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

using chimera::graphics::shmem::SharedD3D11TextureHeader;

namespace {

struct Args {
    std::wstring metadataName = L"Local\\ChimeraD3D11ProducerMeta";
    std::wstring textureName = L"Local\\ChimeraD3D11ProducerTexture";
    std::wstring eventName = L"";
    UINT width = 1280;
    UINT height = 720;
    int seconds = 10;
    int fps = 60;
};

bool takeValue(int argc, wchar_t **argv, int *i, std::wstring *out) {
    if (*i + 1 >= argc) return false;
    *out = argv[++(*i)];
    return true;
}

bool takeValue(int argc, wchar_t **argv, int *i, int *out) {
    if (*i + 1 >= argc) return false;
    *out = std::stoi(argv[++(*i)]);
    return true;
}

Args parseArgs(int argc, wchar_t **argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::wstring key = argv[i];
        if (key == L"--metadata") takeValue(argc, argv, &i, &args.metadataName);
        else if (key == L"--texture") takeValue(argc, argv, &i, &args.textureName);
        else if (key == L"--event") takeValue(argc, argv, &i, &args.eventName);
        else if (key == L"--width") {
            int value = static_cast<int>(args.width);
            if (takeValue(argc, argv, &i, &value)) args.width = static_cast<UINT>(value);
        } else if (key == L"--height") {
            int value = static_cast<int>(args.height);
            if (takeValue(argc, argv, &i, &value)) args.height = static_cast<UINT>(value);
        } else if (key == L"--seconds") {
            takeValue(argc, argv, &i, &args.seconds);
        } else if (key == L"--fps") {
            takeValue(argc, argv, &i, &args.fps);
        }
    }
    return args;
}

Microsoft::WRL::ComPtr<ID3D11Device> createDevice() {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   &level, 1, D3D11_SDK_VERSION,
                                   &device, nullptr, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                               &level, 1, D3D11_SDK_VERSION,
                               &device, nullptr, &context);
    }
    return SUCCEEDED(hr) ? device : nullptr;
}

void writeHeader(SharedD3D11TextureHeader *header,
                 const Args &args,
                 quint64 sequence) {
    header->sequence = sequence | 1ULL;
    MemoryBarrier();

    header->magic = chimera::graphics::shmem::kD3D11TextureMagic;
    header->version = chimera::graphics::shmem::kVersion;
    header->headerSize = sizeof(SharedD3D11TextureHeader);
    header->width = args.width;
    header->height = args.height;
    header->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    header->flags = chimera::graphics::shmem::kD3D11FlagHasAlpha;
    std::fill(std::begin(header->textureName), std::end(header->textureName), char16_t{});
    const size_t maxChars = chimera::graphics::shmem::kD3D11TextureNameChars - 1;
    const size_t len = (std::min)(args.textureName.size(), maxChars);
    for (size_t i = 0; i < len; ++i)
        header->textureName[i] = static_cast<char16_t>(args.textureName[i]);

    MemoryBarrier();
    header->sequence = sequence;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    const Args args = parseArgs(argc, argv);
    if (args.width == 0 || args.height == 0 || args.seconds <= 0 || args.fps <= 0) {
        std::cerr << "invalid dimensions, seconds, or fps\n";
        return 2;
    }

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, sizeof(SharedD3D11TextureHeader),
                                        args.metadataName.c_str());
    if (!mapping) {
        std::cerr << "CreateFileMappingW failed: " << GetLastError() << "\n";
        return 3;
    }
    auto *header = static_cast<SharedD3D11TextureHeader *>(
        MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(SharedD3D11TextureHeader)));
    if (!header) {
        std::cerr << "MapViewOfFile failed: " << GetLastError() << "\n";
        CloseHandle(mapping);
        return 4;
    }

    HANDLE event = nullptr;
    if (!args.eventName.empty()) {
        event = CreateEventW(nullptr, FALSE, FALSE, args.eventName.c_str());
    }

    auto device = createDevice();
    if (!device) {
        std::cerr << "D3D11CreateDevice failed\n";
        return 5;
    }
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = args.width;
    desc.Height = args.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                     D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) {
        std::cerr << "CreateTexture2D failed\n";
        return 6;
    }

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
    if (FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget))) {
        std::cerr << "CreateRenderTargetView failed\n";
        return 6;
    }

    Microsoft::WRL::ComPtr<IDXGIResource1> resource;
    if (FAILED(texture.As(&resource))) {
        std::cerr << "IDXGIResource1 query failed\n";
        return 7;
    }
    HANDLE sharedHandle = nullptr;
    if (FAILED(resource->CreateSharedHandle(nullptr,
                                            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                            args.textureName.c_str(),
                                            &sharedHandle))) {
        std::cerr << "CreateSharedHandle failed\n";
        return 8;
    }

    *header = {};
    const int totalFrames = args.seconds * args.fps;
    const auto frameInterval = std::chrono::duration<double>(1.0 / args.fps);
    auto nextFrame = std::chrono::steady_clock::now();
    for (int frame = 0; frame < totalFrames; ++frame) {
        const float color[] = {
            static_cast<float>((frame * 3) % 255) / 255.0f,
            static_cast<float>((frame * 5) % 255) / 255.0f,
            static_cast<float>((frame * 7) % 255) / 255.0f,
            1.0f
        };
        context->ClearRenderTargetView(renderTarget.Get(), color);
        context->Flush();
        writeHeader(header, args, static_cast<quint64>((frame + 1) * 2));
        if (event) SetEvent(event);
        nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameInterval);
        std::this_thread::sleep_until(nextFrame);
    }

    if (sharedHandle) CloseHandle(sharedHandle);
    if (event) CloseHandle(event);
    UnmapViewOfFile(header);
    CloseHandle(mapping);
    return 0;
}
