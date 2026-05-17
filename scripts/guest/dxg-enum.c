/*
 * dxg-enum.c — Phase 5f: Verify dxgkrnl GPU-PV adapter enumeration
 *
 * Opens /dev/dxg and calls LX_DXENUMADAPTERS2 to list GPU adapters
 * via the dxgkrnl IOCTL interface. Runs in the initrd before switch_root.
 *
 * Compile (musl static):
 *   musl-gcc -static -O2 -o dxg-enum dxg-enum.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

/* Minimal subset of d3dkmthk.h UAPI (from WSL2 kernel include/uapi/misc/d3dkmthk.h) */
struct winluid        { uint32_t lo; uint32_t hi; };
struct d3dkmthandle   { uint32_t v; };

struct d3dkmt_adapterinfo {
    struct d3dkmthandle adapter_handle;
    struct winluid      adapter_luid;
    uint32_t            num_sources;
    uint32_t            present_move_regions_preferred;
};

struct d3dkmt_enumadapters2 {
    uint32_t                    num_adapters;
    uint32_t                    _pad;
    struct d3dkmt_adapterinfo  *adapters;
};

/* _IOWR('G'=0x47, 0x14, struct d3dkmt_enumadapters2) — from d3dkmthk.h LX_DXENUMADAPTERS2 */
#define LX_DXENUMADAPTERS2  _IOWR('G', 0x14, struct d3dkmt_enumadapters2)

int main(void) {
    int fd = open("/dev/dxg", O_RDWR);
    if (fd < 0) {
        perror("[dxg-enum] open O_RDWR failed");
        /* dxgkrnl may only require read access for enumeration */
        fd = open("/dev/dxg", O_RDONLY);
        if (fd < 0) {
            perror("[dxg-enum] open O_RDONLY failed");
            printf("[dxg-enum] ERROR: cannot open /dev/dxg\n");
            return 1;
        }
        printf("[dxg-enum] /dev/dxg opened O_RDONLY\n");
    } else {
        printf("[dxg-enum] /dev/dxg opened O_RDWR\n");
    }

    /* First call: get adapter count (adapters ptr = NULL) */
    struct d3dkmt_enumadapters2 args = {0};
    if (ioctl(fd, LX_DXENUMADAPTERS2, &args) < 0) {
        perror("[dxg-enum] IOCTL count");
        close(fd);
        return 1;
    }
    printf("[dxg-enum] GPU adapters found: %u\n", args.num_adapters);

    if (args.num_adapters == 0) {
        printf("[dxg-enum] no adapters — GPU-PV not active\n");
        close(fd);
        return 1;
    }

    /* Second call: enumerate adapter info */
    struct d3dkmt_adapterinfo *ad = calloc(args.num_adapters, sizeof(*ad));
    if (!ad) { close(fd); return 1; }
    args.adapters = ad;
    if (ioctl(fd, LX_DXENUMADAPTERS2, &args) < 0) {
        perror("[dxg-enum] IOCTL info");
        free(ad); close(fd);
        return 1;
    }

    for (uint32_t i = 0; i < args.num_adapters; i++) {
        printf("[dxg-enum] GPU %u: LUID=%08x:%08x handle=%u sources=%u\n",
               i,
               ad[i].adapter_luid.hi, ad[i].adapter_luid.lo,
               ad[i].adapter_handle.v,
               ad[i].num_sources);
    }

    printf("[dxg-enum] dxgkrnl GPU-PV enumeration PASS\n");
    free(ad);
    close(fd);
    return 0;
}
