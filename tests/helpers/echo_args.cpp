#include <cstdio>
#include <io.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    for (int i = 1; i < argc; ++i) {
        std::printf("%s", argv[i]);
        if (i + 1 < argc) std::printf("\n");
    }
    return 0;
}
