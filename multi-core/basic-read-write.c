#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define MAX_ITERATIONS 100
#define WIDTH 1920
#define HEIGHT 1080
#define PIXEL_SIZE 4  // Assuming 4 bytes per pixel (RGBA)

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Function to allocate and initialize resolution data once
Resolution initResolution() {
    Resolution res;
    res.width = WIDTH;
    res.height = HEIGHT;

    size_t dataSize = WIDTH * HEIGHT * PIXEL_SIZE;
    res.data = (unsigned char*)malloc(dataSize);
    
    if (res.data == NULL) {
        printf("Memory allocation failed\n");
        exit(1);
    }

    // Fill with a pattern once
    for (size_t i = 0; i < dataSize; i++) {
        res.data[i] = i % 256;
    }

    printf("Initialized resolution: %dx%d\n", res.width, res.height);
    return res;
}

// Function to write resolution data to memory
void writeResolution(Resolution* res, unsigned char* destMemory) {
    size_t dataSize = res->width * res->height * PIXEL_SIZE;
    
    // Copy the data to destination memory
    memcpy(destMemory, res->data, dataSize);
}

int main() {
    // Start measuring time using omp_get_wtime()
    double start_time = omp_get_wtime();

    // Allocate destination memory once
    size_t maxDataSize = WIDTH * HEIGHT * PIXEL_SIZE;
    unsigned char* destMemory = (unsigned char*)malloc(maxDataSize);
    
    if (destMemory == NULL) {
        printf("Destination memory allocation failed\n");
        return 1;
    }

    // Initialize resolution once
    Resolution res = initResolution();

    // Set the number of threads for OpenMP (optional)
    omp_set_num_threads(4);  // You can adjust the number of threads based on your CPU

    // Perform read and write operations 100 times
    #pragma omp parallel for
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        printf("Iteration %d\n", i + 1);
        
        // Read and write operation
        writeResolution(&res, destMemory);
    }

    // Free allocated memory
    free(res.data);
    free(destMemory);

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;

    printf("Completed %d read/write operations in %.6f seconds\n", MAX_ITERATIONS, total_time);
    
    return 0;
}

