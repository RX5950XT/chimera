#!/usr/bin/env bash
cat > /tmp/test-fb.c << 'EOF'
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
int main(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) return 1;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    ioctl(fd, FBIOGET_VSCREENINFO, &v);
    ioctl(fd, FBIOGET_FSCREENINFO, &f);
    printf("[fb-render] %dx%d\n", v.xres, v.yres);
    return 0;
}
EOF

# Use gcc with static link
gcc -static -O2 -o /tmp/test-fb /tmp/test-fb.c 2>&1
echo "exit: $?"
file /tmp/test-fb 2>&1
