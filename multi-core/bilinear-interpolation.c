#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define MAX_ITERATIONS 100
#define DST_WIDTH 1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 4  // Assuming 4 bytes per pixel (RGBA)

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Function to initialize and fill source resolution data once
Resolution initResolution(int width, int height) {
    Resolution res;
    res.width = width;
    res.height = height;

    size_t dataSize = width * height * PIXEL_SIZE;
    res.data = (unsigned char*)malloc(dataSize);

    if (res.data == NULL) {
        printf("Memory allocation failed\n");
        exit(1);
    }

    // Fill with a pattern once
    #pragma omp parallel for
    for (size_t i = 0; i < dataSize; i++) {
        res.data[i] = rand() % 256;
    }

    printf("Initialized resolution: %dx%d\n", res.width, res.height);
    return res;
}

// Bilinear interpolation scaling function
void scaleResolutionBilinear(Resolution* src, Resolution* dst) {
    float x_ratio = ((float)src->width - 1) / dst->width;
    float y_ratio = ((float)src->height - 1) / dst->height;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < dst->height; y++) {
        for (int x = 0; x < dst->width; x++) {
            float srcX = x * x_ratio;
            float srcY = y * y_ratio;
            int xL = (int)srcX;
            int yT = (int)srcY;
            int xH = (xL + 1 < src->width) ? xL + 1 : xL;
            int yB = (yT + 1 < src->height) ? yT + 1 : yT;

            float xWeight = srcX - xL;
            float yWeight = srcY - yT;

            int indexTL = (yT * src->width + xL) * PIXEL_SIZE;
            int indexTR = (yT * src->width + xH) * PIXEL_SIZE;
            int indexBL = (yB * src->width + xL) * PIXEL_SIZE;
            int indexBR = (yB * src->width + xH) * PIXEL_SIZE;
            int dstIndex = (y * dst->width + x) * PIXEL_SIZE;

            for (int c = 0; c < PIXEL_SIZE; c++) {
                float top = src->data[indexTL + c] * (1 - xWeight) + src->data[indexTR + c] * xWeight;
                float bottom = src->data[indexBL + c] * (1 - xWeight) + src->data[indexBR + c] * xWeight;
                dst->data[dstIndex + c] = (unsigned char)(top * (1 - yWeight) + bottom * yWeight);
            }
        }
    }
}

// Function to write resolution data to memory
void writeResolution(Resolution* res, unsigned char* destMemory) {
    size_t dataSize = res->width * res->height * PIXEL_SIZE;
    memcpy(destMemory, res->data, dataSize);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <source_width> <source_height>\n", argv[0]);
        return 1;
    }

    int srcWidth = atoi(argv[1]);
    int srcHeight = atoi(argv[2]);

    if (srcWidth <= 0 || srcHeight <= 0) {
        printf("Invalid source resolution.\n");
        return 1;
    }

    srand(time(NULL));

    // Allocate and initialize source resolution
    Resolution srcRes = initResolution(srcWidth, srcHeight);

    // Start measuring time
    double start_time = omp_get_wtime();

    // Allocate destination resolution buffer once
    Resolution dstRes;
    dstRes.width = DST_WIDTH;
    dstRes.height = DST_HEIGHT;
    size_t dstSize = DST_WIDTH * DST_HEIGHT * PIXEL_SIZE;
    dstRes.data = (unsigned char*)malloc(dstSize);

    if (dstRes.data == NULL) {
        printf("Memory allocation failed for scaled resolution\n");
        free(srcRes.data);
        return 1;
    }

    // Allocate memory for final output storage
    unsigned char* destMemory = (unsigned char*)malloc(dstSize);
    if (destMemory == NULL) {
        printf("Destination memory allocation failed\n");
        free(srcRes.data);
        free(dstRes.data);
        return 1;
    }

    // Perform read, scale, and write operations multiple times
    #pragma omp parallel for
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        printf("Iteration %d\n", i + 1);
        scaleResolutionBilinear(&srcRes, &dstRes);
        writeResolution(&dstRes, destMemory);
    }

    // Free allocated memory
    free(srcRes.data);
    free(dstRes.data);
    free(destMemory);

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;

    printf("Completed %d read/scale/write operations in %.6f seconds\n", MAX_ITERATIONS, total_time);
    return 0;
}

