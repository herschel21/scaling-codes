#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
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

int main() {
    // Open the framebuffer device `/dev/fb0`
    int fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("Cannot open framebuffer device");
        return -1;
    }

    // Get framebuffer information
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading framebuffer info");
        close(fb_fd);
        return -1;
    }

    int fb_width = vinfo.xres;
    int fb_height = vinfo.yres;
    size_t fb_size = fb_width * fb_height * PIXEL_SIZE;

    printf("Framebuffer resolution: %dx%d\n", fb_width, fb_height);

    // Map framebuffer memory
    unsigned char *fb_base = (unsigned char *)mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_base == MAP_FAILED) {
        perror("Cannot mmap framebuffer");
        close(fb_fd);
        return -1;
    }

    // Initialize source image (random pixels)
    Resolution srcRes = initResolution(SRC_WIDTH, SRC_HEIGHT);

    // Initialize destination resolution (matching framebuffer size)
    Resolution dstRes;
    dstRes.width = fb_width;
    dstRes.height = fb_height;
    size_t dstSize = dstRes.width * dstRes.height * PIXEL_SIZE;
    dstRes.data = (unsigned char*)malloc(dstSize);

    if (dstRes.data == NULL) {
        printf("Memory allocation failed for scaled resolution\n");
        free(srcRes.data);
        munmap(fb_base, fb_size);
        close(fb_fd);
        return -1;
    }

    // Scale the image
    for (int i=0; i < 101; i++) {
            scaleResolution(&srcRes, &dstRes);
    }

    // Copy scaled image to framebuffer
    memcpy(fb_base, dstRes.data, dstSize);

    printf("Displaying scaled random image. Press Ctrl+C to exit.\n");

    // Keep running to display image
    while (1) {
        sleep(1);
    }

    // Cleanup
    free(srcRes.data);
    free(dstRes.data);
    munmap(fb_base, fb_size);
    close(fb_fd);

    return 0;
}

