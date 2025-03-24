#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "xf86drm.h"
#include "xf86drmMode.h"
#include <sys/mman.h>
#include <time.h>
#include <string.h>

#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define PIXEL_SIZE 4  // 4 bytes per pixel (RGBA)

// Structure for Image Resolution
typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Function to initialize source resolution with random pixel values
Resolution initResolution(int width, int height) {
    Resolution res;
    res.width = width;
    res.height = height;
    size_t dataSize = res.width * res.height * PIXEL_SIZE;
    res.data = (unsigned char*)malloc(dataSize);

    if (res.data == NULL) {
        printf("Memory allocation failed\n");
        exit(1);
    }

    srand(time(NULL));
    for (size_t i = 0; i < dataSize; i++) {
        res.data[i] = rand() % 256;
    }

    return res;
}

// Nearest-neighbor scaling function
void scaleResolution(Resolution* src, Resolution* dst) {
    float x_ratio = (float)src->width / dst->width;
    float y_ratio = (float)src->height / dst->height;

    for (int y = 0; y < dst->height; y++) {
        for (int x = 0; x < dst->width; x++) {
            int srcX = (int)(x * x_ratio);
            int srcY = (int)(y * y_ratio);
            int srcIndex = (srcY * src->width + srcX) * PIXEL_SIZE;
            int dstIndex = (y * dst->width + x) * PIXEL_SIZE;

            memcpy(&dst->data[dstIndex], &src->data[srcIndex], PIXEL_SIZE);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <connector_id> <crtc_id>\n", argv[0]);
        return -1;
    }

    uint32_t connector_id = atoi(argv[1]);
    uint32_t crtc_id = atoi(argv[2]);

    int fd;
    drmModeConnector *connector = NULL;
    drmModeRes *resources = NULL;
    uint32_t fb_id;
    void *fb_base;
    int ret;

    // Open the DRM device
    fd = open("/dev/dri/card2", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("Cannot open DRM device");
        return -1;
    }

    // Get DRM resources
    resources = drmModeGetResources(fd);
    if (!resources) {
        perror("drmModeGetResources failed");
        close(fd);
        return -1;
    }

    // Get the manually entered connector
    connector = drmModeGetConnector(fd, connector_id);
    if (!connector || connector->connection != DRM_MODE_CONNECTED) {
        fprintf(stderr, "Invalid or disconnected connector ID: %d\n", connector_id);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Get display mode (resolution)
    drmModeModeInfo mode = connector->modes[0];
    printf("Using mode: %dx%d@%dHz on Connector %d, CRTC %d\n", mode.hdisplay, mode.vdisplay, mode.vrefresh, connector_id, crtc_id);

    // Create dumb buffer (framebuffer)
    struct drm_mode_create_dumb create = {0};
    create.width = mode.hdisplay;
    create.height = mode.vdisplay;
    create.bpp = 32;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (ret < 0) {
        perror("Cannot create dumb buffer");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Create framebuffer from dumb buffer
    ret = drmModeAddFB(fd, create.width, create.height, 24, 32, create.pitch, create.handle, &fb_id);
    if (ret) {
        perror("Cannot create framebuffer");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Map framebuffer to user space
    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
    if (ret) {
        perror("Cannot map dumb buffer");
        drmModeRmFB(fd, fb_id);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    fb_base = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
    if (fb_base == MAP_FAILED) {
        perror("Cannot mmap framebuffer");
        drmModeRmFB(fd, fb_id);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Initialize source image (random pixels)
    Resolution srcRes = initResolution(SRC_WIDTH, SRC_HEIGHT);

    // Initialize destination resolution (matching framebuffer size)
    Resolution dstRes;
    dstRes.width = create.width;
    dstRes.height = create.height;
    size_t dstSize = dstRes.width * dstRes.height * PIXEL_SIZE;
    dstRes.data = (unsigned char*)malloc(dstSize);

    if (dstRes.data == NULL) {
        printf("Memory allocation failed for scaled resolution\n");
        free(srcRes.data);
        munmap(fb_base, create.size);
        drmModeRmFB(fd, fb_id);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    for(int i = 0; i < 100; i++) {
        scaleResolution(&srcRes, &dstRes);
    }

    // Copy scaled image to framebuffer
    memcpy(fb_base, dstRes.data, dstSize);

    // Set CRTC to display the framebuffer
    ret = drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0, &connector_id, 1, &mode);
    if (ret) {
        perror("Cannot set CRTC");
        free(srcRes.data);
        free(dstRes.data);
        munmap(fb_base, create.size);
        drmModeRmFB(fd, fb_id);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    printf("Displaying scaled random image. Press Ctrl+C to exit.\n");

    // Keep the display active
    while (1) {
        sleep(1);
    }

    // Cleanup (unreachable in this example, but included for completeness)
    free(srcRes.data);
    free(dstRes.data);
    munmap(fb_base, create.size);
    drmModeRmFB(fd, fb_id);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    close(fd);

    return 0;
}

