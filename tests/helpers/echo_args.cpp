#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <windows.h>

namespace {

std::string wideToUtf8(const wchar_t *text) {
    if (!text || text[0] == L'\0') return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    for (int i = 1; i < argc; ++i) {
        const std::string utf8 = wideToUtf8(argv[i]);
        std::fwrite(utf8.data(), 1, utf8.size(), stdout);
        if (i + 1 < argc) std::printf("\n");
    }
    return 0;
}
