#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "xf86drm.h"
#include "xf86drmMode.h"

int main(int argc, char *argv[]) {
    int fd;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *crtc = NULL;
    drmModeRes *resources = NULL;

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

    // Print information about connectors
    printf("Found %d connectors:\n", resources->count_connectors);
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (!connector) {
            printf("Cannot get connector %d\n", resources->connectors[i]);
            continue;
        }

        printf("Connector ID: %d\n", connector->connector_id);
        printf("  Connection Status: %s\n", connector->connection == DRM_MODE_CONNECTED ? "Connected" : "Disconnected");
        printf("  Connector Type: %d\n", connector->connector_type);
        printf("  Encoder ID: %d\n", connector->encoder_id);

        // Print the available modes for this connector
        printf("  Available modes:\n");
        for (int j = 0; j < connector->count_modes; j++) {
            drmModeModeInfo mode = connector->modes[j];
            printf("    Mode: %dx%d @ %dHz\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);
        }

        // Check if an encoder is available and print it
        if (connector->encoder_id != 0) {
            encoder = drmModeGetEncoder(fd, connector->encoder_id);
            if (encoder) {
                printf("    Encoder ID: %d\n", encoder->encoder_id);
                printf("    CRTC ID: %d\n", encoder->crtc_id);
                drmModeFreeEncoder(encoder);
            }
        }

        drmModeFreeConnector(connector);
    }

    // Optionally, if you want to print more details, you can get CRTC information
    printf("Found %d CRTCs:\n", resources->count_crtcs);
    for (int i = 0; i < resources->count_crtcs; i++) {
        crtc = drmModeGetCrtc(fd, resources->crtcs[i]);
        if (crtc) {
            printf("CRTC ID: %d\n", crtc->crtc_id);
            printf("  x: %d, y: %d, width: %d, height: %d\n", crtc->x, crtc->y, crtc->width, crtc->height);
            drmModeFreeCrtc(crtc);
        }
    }

    drmModeFreeResources(resources);
    close(fd);

    return 0;
}

