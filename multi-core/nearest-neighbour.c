#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define MAX_ITERATIONS 100
#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define DST_WIDTH 1024
#define DST_HEIGHT 768
#define PIXEL_SIZE 4  // Assuming 4 bytes per pixel (RGBA)

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Function to initialize and fill source resolution data once
Resolution initResolution() {
    Resolution res;
    res.width = SRC_WIDTH;
    res.height = SRC_HEIGHT;

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

            // Copy RGBA values from the nearest pixel
            memcpy(&dst->data[dstIndex], &src->data[srcIndex], PIXEL_SIZE);
        }
    }
}

// Function to write resolution data to memory
void writeResolution(Resolution* res, unsigned char* destMemory) {
    size_t dataSize = res->width * res->height * PIXEL_SIZE;
    memcpy(destMemory, res->data, dataSize);
}

int main() {

    // Allocate and initialize source resolution once
    Resolution srcRes = initResolution();

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

        // Scale image
        scaleResolution(&srcRes, &dstRes);

        // Write to memory
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

