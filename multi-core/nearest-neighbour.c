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

    size_t dataSize = res.width * res.height * PIXEL_SIZE;
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

// Nearest-neighbor scaling function
void scaleResolution(Resolution* src, Resolution* dst) {
    float x_ratio = (float)src->width / dst->width;
    float y_ratio = (float)src->height / dst->height;

    #pragma omp parallel for collapse(2)
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

    int src_width = atoi(argv[1]);
    int src_height = atoi(argv[2]);

    if (src_width <= 0 || src_height <= 0) {
        printf("Invalid source resolution.\n");
        return 1;
    }

    // Allocate and initialize source resolution
    Resolution srcRes = initResolution(src_width, src_height);

    // Start measuring time
    double start_time = omp_get_wtime();

    // Allocate destination resolution buffer
    Resolution dstRes;
    dstRes.width = DST_WIDTH;
    dstRes.height = DST_HEIGHT;
    size_t dstSize = dstRes.width * dstRes.height * PIXEL_SIZE;
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
        scaleResolution(&srcRes, &dstRes);
        writeResolution(&dstRes, destMemory);
    }

    // Free memory
    free(srcRes.data);
    free(dstRes.data);
    free(destMemory);

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;

    printf("Completed %d read/scale/write operations in %.6f seconds\n", MAX_ITERATIONS, total_time);

    return 0;
}

