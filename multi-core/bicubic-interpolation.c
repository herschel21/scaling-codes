#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define MAX_ITERATIONS 100
#define DST_WIDTH 1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 4  // Assuming 4 bytes per pixel (RGBA)

// Bicubic weight function
float cubicWeight(float x) {
    x = (x < 0) ? -x : x;
    if (x <= 1)
        return (1.5f * x * x * x) - (2.5f * x * x) + 1.0f;
    else if (x < 2)
        return (-0.5f * x * x * x) + (2.5f * x * x) - (4.0f * x) + 2.0f;
    return 0.0f;
}

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Initialize and fill source resolution data
Resolution initResolution(int srcWidth, int srcHeight) {
    Resolution res;
    res.width = srcWidth;
    res.height = srcHeight;

    size_t dataSize = res.width * res.height * PIXEL_SIZE;
    res.data = (unsigned char*)malloc(dataSize);

    if (!res.data) {
        printf("Memory allocation failed for source data\n");
        exit(1);
    }

    // Fill with a pattern once
    #pragma omp parallel for
    for (size_t i = 0; i < dataSize; i++) {
        res.data[i] = rand() % 256;
    }

    printf("Initialized source resolution: %dx%d\n", res.width, res.height);
    return res;
}

// Bicubic interpolation scaler
void scaleResolutionBicubic(Resolution* src, Resolution* dst) {
    float x_ratio = (float)src->width / dst->width;
    float y_ratio = (float)src->height / dst->height;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < dst->height; y++) {
        for (int x = 0; x < dst->width; x++) {
            float srcX = x * x_ratio;
            float srcY = y * y_ratio;
            int xBase = (int)srcX;
            int yBase = (int)srcY;

            float dx = srcX - xBase;
            float dy = srcY - yBase;

            int dstIndex = (y * dst->width + x) * PIXEL_SIZE;

            for (int c = 0; c < PIXEL_SIZE; c++) {
                float value = 0.0f;
                float weightSum = 0.0f;

                for (int m = -1; m <= 2; m++) {
                    for (int n = -1; n <= 2; n++) {
                        int px = xBase + n;
                        int py = yBase + m;

                        // Clamp to boundary
                        if (px < 0) px = 0;
                        if (px >= src->width) px = src->width - 1;
                        if (py < 0) py = 0;
                        if (py >= src->height) py = src->height - 1;

                        int srcIndex = (py * src->width + px) * PIXEL_SIZE;

                        float weight = cubicWeight(n - dx) * cubicWeight(m - dy);
                        value += weight * src->data[srcIndex + c];
                        weightSum += weight;
                    }
                }

                dst->data[dstIndex + c] = (unsigned char)(value / weightSum);
            }
        }
    }
}

// Copy resolution data to destination buffer
void writeResolution(Resolution* res, unsigned char* destMemory) {
    size_t dataSize = res->width * res->height * PIXEL_SIZE;
    memcpy(destMemory, res->data, dataSize);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <src_width> <src_height>\n", argv[0]);
        return 1;
    }

    int srcWidth = atoi(argv[1]);
    int srcHeight = atoi(argv[2]);

    if (srcWidth <= 0 || srcHeight <= 0) {
        printf("Invalid source resolution.\n");
        return 1;
    }

    // Initialize source
    Resolution srcRes = initResolution(srcWidth, srcHeight);

    // Allocate destination
    Resolution dstRes;
    dstRes.width = DST_WIDTH;
    dstRes.height = DST_HEIGHT;
    size_t dstSize = DST_WIDTH * DST_HEIGHT * PIXEL_SIZE;
    dstRes.data = (unsigned char*)malloc(dstSize);

    if (!dstRes.data) {
        printf("Memory allocation failed for destination\n");
        free(srcRes.data);
        return 1;
    }

    // Allocate final memory
    unsigned char* destMemory = (unsigned char*)malloc(dstSize);
    if (!destMemory) {
        printf("Memory allocation failed for output copy\n");
        free(srcRes.data);
        free(dstRes.data);
        return 1;
    }

    double start_time = omp_get_wtime();

    // Perform repeated scaling and write
    #pragma omp parallel for
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        scaleResolutionBicubic(&srcRes, &dstRes);
        writeResolution(&dstRes, destMemory);
    }

    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;

    printf("Completed %d iterations of scaling + writing in %.6f seconds\n", MAX_ITERATIONS, total_time);

    // Cleanup
    free(srcRes.data);
    free(dstRes.data);
    free(destMemory);

    return 0;
}

