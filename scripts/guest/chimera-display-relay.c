/*
 * chimera-display-relay.c — Guest-side framebuffer → HvSocket relay
 *
 * Captures the DRM/framebuffer display and streams RGB24 frames to the
 * host Chimera UI over AF_VSOCK port 17 (CHIMERA_HV_SERVICE_DISPLAY).
 *
 * Wire protocol (matches HvSocketFramebufferCapture.cpp):
 *   [uint32 width][uint32 height][width * height * 3 bytes RGB24] ...
 *
 * Build (static musl):
 *   musl-gcc -static -O2 -o chimera-display-relay chimera-display-relay.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/vm_sockets.h>

#define DISPLAY_VSOCK_PORT  17
#define FBDEV               "/dev/fb0"
#define TARGET_FPS          30
#define FRAME_INTERVAL_NS   (1000000000L / TARGET_FPS)

static int open_framebuffer(struct fb_var_screeninfo *vinfo, struct fb_fix_screeninfo *finfo) {
    int fd = open(FBDEV, O_RDONLY);
    if (fd < 0) { perror("open /dev/fb0"); return -1; }
    if (ioctl(fd, FBIOGET_VSCREENINFO, vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO"); close(fd); return -1;
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, finfo) < 0) {
        perror("FBIOGET_FSCREENINFO"); close(fd); return -1;
    }
    fprintf(stderr, "[display-relay] fb: %ux%u %ubpp\n",
            vinfo->xres, vinfo->yres, vinfo->bits_per_pixel);
    return fd;
}

/* Convert BGRA/BGRX (common framebuffer format) to RGB24 */
static void convert_to_rgb24(const uint8_t *src, uint8_t *dst,
                               uint32_t w, uint32_t h, uint32_t bpp,
                               uint32_t line_len) {
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = src + y * line_len;
        for (uint32_t x = 0; x < w; x++) {
            if (bpp == 32) {
                /* BGRX/BGRA → RGB */
                dst[0] = row[x*4 + 2]; /* R */
                dst[1] = row[x*4 + 1]; /* G */
                dst[2] = row[x*4 + 0]; /* B */
            } else if (bpp == 24) {
                dst[0] = row[x*3 + 2];
                dst[1] = row[x*3 + 1];
                dst[2] = row[x*3 + 0];
            } else {
                /* Fallback: copy as-is */
                dst[0] = dst[1] = dst[2] = row[x * (bpp/8)];
            }
            dst += 3;
        }
    }
}

static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

static void stream_loop(int vsock_fd) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int fb_fd = -1;
    uint8_t *fb_mem = MAP_FAILED;
    uint8_t *rgb_buf = NULL;
    size_t fb_size = 0, rgb_size = 0;
    struct timespec next_frame;

    while (1) {
        /* Accept host connection */
        int client = accept(vsock_fd, NULL, NULL);
        if (client < 0) { perror("accept"); continue; }
        fprintf(stderr, "[display-relay] host connected\n");

        /* Open framebuffer if not already open */
        if (fb_fd < 0) {
            fb_fd = open_framebuffer(&vinfo, &finfo);
            if (fb_fd < 0) { close(client); continue; }

            fb_size  = finfo.smem_len;
            rgb_size = vinfo.xres * vinfo.yres * 3;
            fb_mem   = mmap(NULL, fb_size, PROT_READ, MAP_SHARED, fb_fd, 0);
            rgb_buf  = malloc(rgb_size);
            if (fb_mem == MAP_FAILED || !rgb_buf) {
                fprintf(stderr, "[display-relay] mmap/alloc failed\n");
                close(client); close(fb_fd); fb_fd = -1; continue;
            }
        }

        /* Re-read current vinfo (resolution may change) */
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);

        /* Frame header (little-endian uint32 width, height) */
        uint32_t hdr[2] = {vinfo.xres, vinfo.yres};
        uint32_t new_rgb_size = vinfo.xres * vinfo.yres * 3;

        if (new_rgb_size != rgb_size) {
            free(rgb_buf);
            rgb_buf  = malloc(new_rgb_size);
            rgb_size = new_rgb_size;
        }

        clock_gettime(CLOCK_MONOTONIC, &next_frame);

        while (1) {
            /* Capture frame */
            convert_to_rgb24(fb_mem, rgb_buf,
                             vinfo.xres, vinfo.yres,
                             vinfo.bits_per_pixel,
                             finfo.line_length);

            /* Send: header + pixels */
            if (write_all(client, hdr, sizeof(hdr)) < 0) break;
            if (write_all(client, rgb_buf, rgb_size) < 0) break;

            /* Rate limit */
            next_frame.tv_nsec += FRAME_INTERVAL_NS;
            if (next_frame.tv_nsec >= 1000000000L) {
                next_frame.tv_sec++;
                next_frame.tv_nsec -= 1000000000L;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
        }
        close(client);
        fprintf(stderr, "[display-relay] host disconnected\n");
    }
}

int main(void) {
    fprintf(stderr, "[display-relay] starting, port %d\n", DISPLAY_VSOCK_PORT);

    int vsock_fd = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (vsock_fd < 0) { perror("socket AF_VSOCK"); return 1; }

    struct sockaddr_vm addr = {
        .svm_family = AF_VSOCK,
        .svm_port   = DISPLAY_VSOCK_PORT,
        .svm_cid    = VMADDR_CID_ANY,
    };
    if (bind(vsock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(vsock_fd, 2) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "[display-relay] listening on VSOCK port %d\n", DISPLAY_VSOCK_PORT);
    stream_loop(vsock_fd);
    return 0;
}
