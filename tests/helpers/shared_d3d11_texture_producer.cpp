#include "SharedD3D11TexturePublisher.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct Args {
    std::wstring metadataName = L"Local\\ChimeraD3D11ProducerMeta";
    std::wstring textureName = L"Local\\ChimeraD3D11ProducerTexture";
    std::wstring eventName;
    int width = 1920;
    int height = 1080;
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
        else if (key == L"--width") takeValue(argc, argv, &i, &args.width);
        else if (key == L"--height") takeValue(argc, argv, &i, &args.height);
        else if (key == L"--seconds") takeValue(argc, argv, &i, &args.seconds);
        else if (key == L"--fps") takeValue(argc, argv, &i, &args.fps);
    }
    return args;
}

QString fromWide(const std::wstring &value) {
    return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    const Args args = parseArgs(argc, argv);
    if (args.width <= 0 || args.height <= 0 || args.seconds <= 0 || args.fps <= 0) {
        std::cerr << "invalid dimensions, seconds, or fps\n";
        return 2;
    }

    chimera::graphics::SharedD3D11TexturePublisher::Config config;
    config.metadataName = fromWide(args.metadataName);
    config.textureName = fromWide(args.textureName);
    config.frameEventName = fromWide(args.eventName);
    config.size = QSize(args.width, args.height);
    chimera::graphics::SharedD3D11TexturePublisher publisher(config);

    QString error;
    if (!publisher.start(&error)) {
        std::cerr << "SharedD3D11TexturePublisher start failed: "
                  << error.toStdString() << "\n";
        return 3;
    }

    const int totalFrames = args.seconds * args.fps;
    const auto frameInterval = std::chrono::duration<double>(1.0 / args.fps);
    auto nextFrame = std::chrono::steady_clock::now();
    for (int frame = 0; frame < totalFrames; ++frame) {
        if (!publisher.publishColor(static_cast<float>((frame * 3) % 255) / 255.0f,
                                    static_cast<float>((frame * 5) % 255) / 255.0f,
                                    static_cast<float>((frame * 7) % 255) / 255.0f,
                                    1.0f,
                                    &error)) {
            std::cerr << "publishColor failed: " << error.toStdString() << "\n";
            return 4;
        }
        nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameInterval);
        std::this_thread::sleep_until(nextFrame);
    }
    return 0;
}
