#include <linux/videodev2.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    unsigned char payload[0x111];
    memset(payload, 0xff, sizeof(payload));
    struct v4l2_ext_control ctrl = { .id = 0x98e904, .size = sizeof(payload), .ptr = payload };
    struct v4l2_ext_controls request = { .count = 1, .controls = &ctrl };
    int fd = open("/dev/video9", O_RDONLY | O_CLOEXEC);
    if (fd < 0 || ioctl(fd, VIDIOC_G_EXT_CTRLS, &request) < 0) {
        perror("eARC latency query");
        return 1;
    }
    close(fd);
    if (payload[0] == 0xff && payload[0x110] == 0xff) {
        fprintf(stderr, "Driver did not return the latency payload\n");
        return 1;
    }
    printf("V670 eARC receiver latency byte = %u\n", payload[0xe]);
    return 0;
}
