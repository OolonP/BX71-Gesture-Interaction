/* BX71 camera smoke test. Build on Ubuntu:
 * arm-linux-gnueabihf-gcc-9 -O2 -Wall -Wextra -static bx71_capture.c -o bx71_capture
 * Run: ./bx71_capture /dev/video0 /run/media/mmcblk0p1/camera_test
 * Saves one MJPEG JPEG or raw YUYV frame after four warmup frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

static int xio(int fd, unsigned long op, void *p) {
    int r; do { r = ioctl(fd, op, p); } while (r < 0 && errno == EINTR); return r;
}
int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/video0";
    const char *prefix = argc > 2 ? argv[2] : "camera_test";
    int fd = -1, streaming = 0, rc = 1;
    void *map[4] = {0}; size_t lengths[4] = {0}; unsigned int count = 0;
    FILE *out = NULL; char path[1024];
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("open camera"); goto done; }
    struct v4l2_capability cap = {0};
    if (xio(fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("QUERYCAP"); goto done; }
    printf("Camera: %.32s; driver: %.16s\n", cap.card, cap.driver);
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Single-plane capture/streaming unavailable\n"); goto done;
    }
    int mjpeg = 0, yuyv = 0;
    struct v4l2_fmtdesc desc = {0}; desc.type = type;
    for (desc.index = 0; ; desc.index++) {
        if (xio(fd, VIDIOC_ENUM_FMT, &desc) < 0) {
            if (errno != EINVAL) { perror("ENUM_FMT"); goto done; }
            break;
        }
        printf("Format: %.32s\n", desc.description);
        if (desc.pixelformat == V4L2_PIX_FMT_MJPEG) mjpeg = 1;
        if (desc.pixelformat == V4L2_PIX_FMT_YUYV) yuyv = 1;
    }
    if (!mjpeg && !yuyv) { fprintf(stderr, "No MJPEG/YUYV capture format\n"); goto done; }
    struct v4l2_format fmt = {0}; fmt.type = type;
    fmt.fmt.pix.width = 640; fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = mjpeg ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xio(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT"); goto done; }
    uint32_t pix = fmt.fmt.pix.pixelformat;
    if (pix != V4L2_PIX_FMT_MJPEG && pix != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "Camera returned an unsupported format\n"); goto done;
    }
    printf("Selected: %ux%u, %c%c%c%c, bytesperline=%u\n", fmt.fmt.pix.width,
           fmt.fmt.pix.height, pix & 255, (pix >> 8) & 255, (pix >> 16) & 255,
           (pix >> 24) & 255, fmt.fmt.pix.bytesperline);
    if (snprintf(path, sizeof(path), "%s.%s", prefix,
            pix == V4L2_PIX_FMT_MJPEG ? "jpg" : "yuyv") >= (int)sizeof(path)) {
        fprintf(stderr, "Output path too long\n"); goto done;
    }
    struct v4l2_requestbuffers req = {0};
    req.count = 4; req.type = type; req.memory = V4L2_MEMORY_MMAP;
    if (xio(fd, VIDIOC_REQBUFS, &req) < 0) { perror("REQBUFS"); goto done; }
    if (!req.count) { fprintf(stderr, "No capture buffers\n"); goto done; }
    count = req.count > 4 ? 4 : req.count;
    for (unsigned int i = 0; i < count; i++) {
        struct v4l2_buffer b = {0}; b.type = type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        if (xio(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); goto done; }
        lengths[i] = b.length;
        map[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (map[i] == MAP_FAILED) { map[i] = NULL; perror("mmap"); goto done; }
        if (xio(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); goto done; }
    }
    if (xio(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); goto done; }
    streaming = 1;
    int frames = 0;
    for (int attempt = 0; attempt < 60; attempt++) {
        struct pollfd p = {fd, POLLIN, 0};
        int r; do { r = poll(&p, 1, 5000); } while (r < 0 && errno == EINTR);
        if (r <= 0) { if (r < 0) perror("poll"); else fprintf(stderr,"Frame timeout (5 seconds)\n"); goto done; }
        struct v4l2_buffer b = {0}; b.type = type; b.memory = V4L2_MEMORY_MMAP;
        if (xio(fd, VIDIOC_DQBUF, &b) < 0) { if (errno == EAGAIN) continue; perror("DQBUF"); goto done; }
        if (b.index >= count || b.bytesused > lengths[b.index]) { fprintf(stderr,"Invalid frame bounds\n"); goto done; }
        if (!(b.flags & V4L2_BUF_FLAG_ERROR) && b.bytesused) {
            printf("Frame %d: %u bytes\n", ++frames, b.bytesused);
            if (frames >= 5) {
                unsigned char *bytes = map[b.index];
                if (pix == V4L2_PIX_FMT_MJPEG && (b.bytesused < 2 || bytes[0] != 255 || bytes[1] != 216)) {
                    fprintf(stderr,"Invalid JPEG start marker\n"); goto done;
                }
                out = fopen(path, "wb");
                if (!out) { perror("open output"); goto done; }
                if (fwrite(bytes, 1, b.bytesused, out) != b.bytesused) { perror("write output"); goto done; }
                int closed = fclose(out); out = NULL;
                if (closed) { perror("close output"); goto done; }
                printf("CAPTURE_OK: %s (%u bytes)\n", path, b.bytesused);
                rc = 0; break;
            }
        }
        if (xio(fd, VIDIOC_QBUF, &b) < 0) { perror("requeue"); goto done; }
    }
    if (rc) fprintf(stderr,"Capture did not produce five valid frames\n");
done:
    if (out) fclose(out);
    if (streaming) xio(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned int i=0; i<count; i++) if (map[i]) munmap(map[i], lengths[i]);
    if (fd >= 0) close(fd);
    return rc;
}
