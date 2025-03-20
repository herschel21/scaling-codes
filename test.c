#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

struct buffer_object {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t handle;
    uint32_t size;
    uint32_t fb_id;
    uint8_t *map;
};

struct buffer_object buf;
int drm_fd;
drmModeModeInfo mode;
drmModeCrtc *crtc;
uint32_t conn_id;
uint32_t crtc_id;

static int initialize_drm(void)
{
    drmModeRes *res;
    drmModeConnector *conn;
    drmModeEncoder *enc;
    int i;

    drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        fprintf(stderr, "Failed to open DRM device: %s\n", strerror(errno));
        return -1;
    }

    res = drmModeGetResources(drm_fd);
    if (!res) {
        fprintf(stderr, "Failed to get DRM resources: %s\n", strerror(errno));
        close(drm_fd);
        return -1;
    }

    /* Find a connected connector */
    conn = NULL;
    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (conn->connection == DRM_MODE_CONNECTED)
            break;
        drmModeFreeConnector(conn);
        conn = NULL;
    }

    if (!conn) {
        fprintf(stderr, "No connected connector found\n");
        drmModeFreeResources(res);
        close(drm_fd);
        return -1;
    }

    /* Find preferred mode */
    for (i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = conn->modes[i];
            break;
        }
    }

    if (i == conn->count_modes) {
        /* No preferred mode, use first mode */
        mode = conn->modes[0];
    }

    conn_id = conn->connector_id;

    /* Find encoder */
    enc = NULL;
    for (i = 0; i < res->count_encoders; i++) {
        enc = drmModeGetEncoder(drm_fd, res->encoders[i]);
        if (enc->encoder_id == conn->encoder_id)
            break;
        drmModeFreeEncoder(enc);
        enc = NULL;
    }

    if (enc) {
        crtc_id = enc->crtc_id;
        drmModeFreeEncoder(enc);
    } else {
        /* No encoder found, try to find a CRTC */
        for (i = 0; i < res->count_crtcs; i++) {
            crtc = drmModeGetCrtc(drm_fd, res->crtcs[i]);
            if (crtc) {
                crtc_id = crtc->crtc_id;
                drmModeFreeCrtc(crtc);
                break;
            }
        }
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    return 0;
}

static int create_framebuffer(void)
{
    struct drm_mode_create_dumb create_dumb = {0};
    struct drm_mode_map_dumb map_dumb = {0};
    int ret;

    buf.width = mode.hdisplay;
    buf.height = mode.vdisplay;

    create_dumb.width = buf.width;
    create_dumb.height = buf.height;
    create_dumb.bpp = 32;
    create_dumb.flags = 0;

    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
    if (ret < 0) {
        fprintf(stderr, "Failed to create dumb buffer: %s\n", strerror(errno));
        return -1;
    }

    buf.handle = create_dumb.handle;
    buf.pitch = create_dumb.pitch;
    buf.size = create_dumb.size;

    /* Create framebuffer object for the dumb buffer */
    ret = drmModeAddFB(drm_fd, buf.width, buf.height, 24, 32, buf.pitch,
                       buf.handle, &buf.fb_id);
    if (ret) {
        fprintf(stderr, "Failed to create framebuffer: %s\n", strerror(errno));
        return -1;
    }

    /* Map the buffer for CPU access */
    map_dumb.handle = buf.handle;
    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
    if (ret) {
        fprintf(stderr, "Failed to map dumb buffer: %s\n", strerror(errno));
        return -1;
    }

    buf.map = mmap(0, buf.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   drm_fd, map_dumb.offset);
    if (buf.map == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap buffer: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void draw_pattern(void)
{
    uint32_t *ptr = (uint32_t *)buf.map;
    uint32_t i, j;
    uint32_t color = 0;

    /* Create a simple gradient pattern */
    for (i = 0; i < buf.height; i++) {
        for (j = 0; j < buf.width; j++) {
            color = (i << 16) | (j << 8) | ((i + j) & 0xFF);
            ptr[i * buf.width + j] = color;
        }
    }
}

static void cleanup(void)
{
    struct drm_mode_destroy_dumb destroy_dumb = {0};

    if (buf.map)
        munmap(buf.map, buf.size);

    if (buf.fb_id)
        drmModeRmFB(drm_fd, buf.fb_id);

    if (buf.handle) {
        destroy_dumb.handle = buf.handle;
        drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
    }

    close(drm_fd);
}

int main(void)
{
    int ret;

    ret = initialize_drm();
    if (ret) {
        fprintf(stderr, "Failed to initialize DRM\n");
        return 1;
    }

    ret = create_framebuffer();
    if (ret) {
        fprintf(stderr, "Failed to create framebuffer\n");
        cleanup();
        return 1;
    }

    draw_pattern();

    /* Set the CRTC with our framebuffer */
    ret = drmModeSetCrtc(drm_fd, crtc_id, buf.fb_id, 0, 0, &conn_id, 1, &mode);
    if (ret) {
        fprintf(stderr, "Failed to set CRTC: %s\n", strerror(errno));
        cleanup();
        return 1;
    }

    printf("Display set up successfully with resolution: %dx%d\n", 
           mode.hdisplay, mode.vdisplay);
    printf("Press Enter to exit...\n");
    getchar();

    cleanup();
    return 0;
}
