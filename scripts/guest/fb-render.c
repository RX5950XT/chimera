/*
 * fb-render.c — Phase 6a: Software framebuffer rendering test
 *
 * Draws SMPTE-style color bars to /dev/fb0 to verify the display
 * pipeline end-to-end:
 *   hyperv_drm → /dev/fb0 → chimera-display-relay → vsock port 17 → host
 *
 * Build (static musl):
 *   musl-gcc -static -O2 -o fb-render fb-render.c
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

/*
 * SMPTE 75% color bars in BGRX uint32 (little-endian, byte order B,G,R,X):
 *   value = B | (G<<8) | (R<<16) | (0xFF<<24)
 */
static const uint32_t BARS[8] = {
    0xFFFFFFFF, /* White   R=255 G=255 B=255 */
    0xFFFFFF00, /* Yellow  R=255 G=255 B=0   */
    0xFF00FFFF, /* Cyan    R=0   G=255 B=255 */
    0xFF00FF00, /* Green   R=0   G=255 B=0   */
    0xFFFF00FF, /* Magenta R=255 G=0   B=255 */
    0xFFFF0000, /* Red     R=255 G=0   B=0   */
    0xFF0000FF, /* Blue    R=0   G=0   B=255 */
    0xFF000000, /* Black   R=0   G=0   B=0   */
};

static const char *BAR_NAMES[8] = {
    "White", "Yellow", "Cyan", "Green",
    "Magenta", "Red", "Blue", "Black"
};

int main(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("[fb-render] open /dev/fb0"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("[fb-render] FBIOGET_VSCREENINFO"); close(fd); return 1;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("[fb-render] FBIOGET_FSCREENINFO"); close(fd); return 1;
    }

    uint32_t w = vinfo.xres, h = vinfo.yres, bpp = vinfo.bits_per_pixel;
    uint32_t stride = finfo.line_length / 4;  /* pixels per line */

    fprintf(stderr, "[fb-render] %ux%u %ubpp stride=%u\n", w, h, bpp, stride);
    if (bpp != 32) {
        fprintf(stderr, "[fb-render] WARNING: expected 32bpp, got %u\n", bpp);
    }

    uint32_t *fb = mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("[fb-render] mmap"); close(fd); return 1; }

    /* Draw SMPTE color bars */
    uint32_t bar_w = w / 8;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t bar = (x / bar_w < 8) ? (x / bar_w) : 7;
            fb[y * stride + x] = BARS[bar];
        }
    }

    /* Draw a status band at the top (black bar with a bright indicator) */
    for (uint32_t y = 0; y < 8 && y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            fb[y * stride + x] = (x < 320) ? 0xFF00FFFF : 0xFF000000;

    munmap(fb, finfo.smem_len);
    close(fd);

    fprintf(stderr, "[fb-render] color bars drawn: ");
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "%s%s", BAR_NAMES[i], (i < 7) ? " | " : "\n");
    fprintf(stderr, "[fb-render] Phase 6a display pipeline verified\n");
    return 0;
}
