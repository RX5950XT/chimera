/*
 * chimera-input-relay.c — Guest-side HvSocket → uinput relay
 *
 * Listens on AF_VSOCK port 16 (CHIMERA_HV_SERVICE_INPUT) and forwards
 * Linux input_event structs received from the host Chimera UI to
 * /dev/uinput, creating a virtual input device inside the Android guest.
 *
 * Protocol: host sends 16-byte linux_input_event structs (same layout as
 * HvSocketTransport.cpp):
 *   [int64 tv_sec][uint16 type][uint16 code][int32 value]
 *
 * Build (static musl):
 *   musl-gcc -static -O2 -o chimera-input-relay chimera-input-relay.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/vm_sockets.h>

#define INPUT_VSOCK_PORT  16
#define UINPUT_DEVICE     "/dev/uinput"

/* Wire protocol: must match HvSocketTransport.cpp LinuxInputEvent */
struct wire_event {
    int64_t  tv_sec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};

static int create_uinput_device(void) {
    int fd = open(UINPUT_DEVICE, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return -1;
    }

    /* Enable event types */
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_REL);

    /* Mouse buttons */
    for (int i = BTN_LEFT; i <= BTN_TASK; i++)
        ioctl(fd, UI_SET_KEYBIT, i);

    /* Keyboard keys */
    for (int i = 0; i < KEY_MAX; i++)
        ioctl(fd, UI_SET_KEYBIT, i);

    /* Absolute axes (mouse absolute positioning) */
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);

    struct uinput_setup usetup = {0};
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    strncpy(usetup.name, "Chimera HvSocket Input", UINPUT_MAX_NAME_SIZE - 1);

    struct uinput_abs_setup abs_x = {.code = ABS_X, .absinfo = {.maximum = 32767}};
    struct uinput_abs_setup abs_y = {.code = ABS_Y, .absinfo = {.maximum = 32767}};
    ioctl(fd, UI_ABS_SETUP, &abs_x);
    ioctl(fd, UI_ABS_SETUP, &abs_y);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        perror("UI_DEV_SETUP");
        close(fd);
        return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_CREATE");
        close(fd);
        return -1;
    }

    fprintf(stderr, "[input-relay] uinput device created\n");
    return fd;
}

static void relay_loop(int vsock_fd, int uinput_fd) {
    struct sockaddr_vm peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    int client;

    while (1) {
        client = accept(vsock_fd, (struct sockaddr *)&peer_addr, &peer_len);
        if (client < 0) {
            perror("accept");
            continue;
        }
        fprintf(stderr, "[input-relay] host connected (cid=%u)\n", peer_addr.svm_cid);

        struct wire_event we;
        while (1) {
            ssize_t n = read(client, &we, sizeof(we));
            if (n != (ssize_t)sizeof(we)) {
                if (n < 0) perror("read");
                break;
            }

            struct input_event ie = {
                .type  = we.type,
                .code  = we.code,
                .value = we.value,
            };
            if (write(uinput_fd, &ie, sizeof(ie)) < 0)
                perror("write uinput");
        }
        close(client);
        fprintf(stderr, "[input-relay] host disconnected\n");
    }
}

int main(void) {
    fprintf(stderr, "[input-relay] starting, port %d\n", INPUT_VSOCK_PORT);

    int uinput_fd = create_uinput_device();
    if (uinput_fd < 0) return 1;

    int vsock_fd = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (vsock_fd < 0) { perror("socket AF_VSOCK"); return 1; }

    struct sockaddr_vm addr = {
        .svm_family = AF_VSOCK,
        .svm_port   = INPUT_VSOCK_PORT,
        .svm_cid    = VMADDR_CID_ANY,
    };
    if (bind(vsock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(vsock_fd, 4) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "[input-relay] listening on VSOCK port %d\n", INPUT_VSOCK_PORT);
    relay_loop(vsock_fd, uinput_fd);
    return 0;
}
