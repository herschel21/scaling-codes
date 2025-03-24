#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "xf86drm.h"
#include "xf86drmMode.h"
#include <sys/mman.h>

int main(int argc, char *argv[]) {
    int fd;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *crtc = NULL;
    drmModeRes *resources = NULL;
    uint32_t connector_id = 34; // hard coded for now
    uint32_t crtc_id = 33; // hard coded for now
    uint32_t fb_id;
    void *fb_base;
    int ret;

    // Open the DRM device
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
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

    // Find the connector
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector->connector_id == connector_id) {
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    if (!connector) {
        fprintf(stderr, "Connector not found\n");
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    if (connector->connection != DRM_MODE_CONNECTED) {
        fprintf(stderr, "Connector not connected\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Get the encoder
    encoder = drmModeGetEncoder(fd, connector->encoder_id);
    if (!encoder) {
        fprintf(stderr, "Cannot get encoder\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Get the CRTC
    crtc = drmModeGetCrtc(fd, encoder->crtc_id);
    if (!crtc) {
        fprintf(stderr, "Cannot get CRTC\n");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Get the mode (resolution, refresh rate, etc.)
    drmModeModeInfo mode = connector->modes[0]; // Use the first mode
    printf("Using mode: %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);

    // Create a dumb buffer (framebuffer)
    struct drm_mode_create_dumb create = {0};
    create.width = mode.hdisplay;
    create.height = mode.vdisplay;
    create.bpp = 32; // 32 bits per pixel (RGBA)

    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (ret < 0) {
        perror("Cannot create dumb buffer");
        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Create a framebuffer from the dumb buffer
    ret = drmModeAddFB(fd, create.width, create.height, 24, 32, create.pitch, create.handle, &fb_id);
    if (ret) {
        perror("Cannot create framebuffer");
        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Map the framebuffer to user space
    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
    if (ret) {
        perror("Cannot map dumb buffer");
        drmModeRmFB(fd, fb_id);
        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    fb_base = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
    if (fb_base == MAP_FAILED) {
        perror("Cannot mmap framebuffer");
        drmModeRmFB(fd, fb_id);
        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    // Fill the framebuffer with a solid color (e.g., red)
    uint32_t *pixels = (uint32_t *)fb_base;
    for (int i = 0; i < (create.size / 4); i++) {
        pixels[i] =  rand() % 256; // RGBA: Red
    }

   // Set the CRTC to display the framebuffer
    ret = drmModeSetCrtc(fd, crtc->crtc_id, fb_id, 0, 0, &connector_id, 1, &mode);
    if (ret) {
        perror("Cannot set CRTC");
        munmap(fb_base, create.size);
        drmModeRmFB(fd, fb_id);
        drmModeFreeCrtc(crtc);
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    printf("Displaying blue screen. Press Ctrl+C to exit.\n");

    // Keep the program running to keep the display active
    while (1) {
        sleep(1);
    }

    // Cleanup (unreachable in this example, but good practice)
    munmap(fb_base, create.size);
    drmModeRmFB(fd, fb_id);
    drmModeFreeCrtc(crtc);
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    close(fd);

    return 0;
}
