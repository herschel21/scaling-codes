#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <omp.h>
#include <stdint.h>

#define DEVICE_PATH "/dev/my_dma_device"
#define MAX_ITERATIONS 100

// Source image dimensions
#define SRC_WIDTH 1152
#define SRC_HEIGHT 864

// Destination image dimensions
#define DST_WIDTH 1920
#define DST_HEIGHT 1080

#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)

// Ensure it's a multiple of page size
#define DMA_BUFFER_SIZE ((1920 * 1080 * 3) + 4096)
#define CACHE_LINE_SIZE 64  // Assume 64-byte cache lines for prefetching

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Optimized Nearest-Neighbor Scaling Function
void scaleImage(Resolution* src, Resolution* dst) {
    int x_ratio = (src->width << 16) / dst->width;
    int y_ratio = (src->height << 16) / dst->height;

    // Precompute row offsets to avoid per-iteration multiplication
    int* srcRowOffset = (int*)malloc(dst->height * sizeof(int));
    if (!srcRowOffset) {
        fprintf(stderr, "[ERROR] Memory allocation failed for srcRowOffset\n");
        return;
    }

    #pragma omp parallel for
    for (int y = 0; y < dst->height; y++) {
        srcRowOffset[y] = ((y * y_ratio) >> 16) * src->width * PIXEL_SIZE;
    }

    #pragma omp parallel for schedule(dynamic, 8)
    for (int y = 0; y < dst->height; y++) {
        int rowOffset = srcRowOffset[y];

        for (int x = 0; x < dst->width; x += 4) {  // Unroll loop (process 4 pixels)
            int srcX1 = (x * x_ratio) >> 16;
            int srcX2 = ((x + 1) * x_ratio) >> 16;
            int srcX3 = ((x + 2) * x_ratio) >> 16;
            int srcX4 = ((x + 3) * x_ratio) >> 16;

            int srcIndex1 = rowOffset + srcX1 * PIXEL_SIZE;
            int srcIndex2 = rowOffset + srcX2 * PIXEL_SIZE;
            int srcIndex3 = rowOffset + srcX3 * PIXEL_SIZE;
            int srcIndex4 = rowOffset + srcX4 * PIXEL_SIZE;

            int dstIndex = (y * dst->width + x) * PIXEL_SIZE;

            // Prefetch next cache line to reduce cache misses
            __builtin_prefetch(&src->data[srcIndex1 + CACHE_LINE_SIZE], 0, 1);

            // Copy 4 pixels efficiently
            *(uint32_t*)(dst->data + dstIndex) = *(uint32_t*)(src->data + srcIndex1);
            *(uint32_t*)(dst->data + dstIndex + 3) = *(uint32_t*)(src->data + srcIndex2);
            *(uint32_t*)(dst->data + dstIndex + 6) = *(uint32_t*)(src->data + srcIndex3);
            *(uint32_t*)(dst->data + dstIndex + 9) = *(uint32_t*)(src->data + srcIndex4);
        }
    }

    free(srcRowOffset);
}

// Optimized Save PPM Function (Large Writes)
void savePPM(const char* filename, Resolution* res) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "[ERROR] Failed to open file: %s\n", filename);
        return;
    }

    fprintf(file, "P6\n%d %d\n255\n", res->width, res->height);

    // Optimize file writes with large buffers
    size_t chunk_size = 64 * 1024;  // 64 KB chunks
    size_t total_size = res->width * res->height * PIXEL_SIZE;
    size_t written = 0;

    while (written < total_size) {
        size_t write_size = (total_size - written > chunk_size) ? chunk_size : (total_size - written);
        fwrite(res->data + written, 1, write_size, file);
        written += write_size;
    }

    fclose(file);
}

int main() {
    int fd;
    unsigned char *input_buffer, *output_buffer;
    long page_size = sysconf(_SC_PAGESIZE);
    off_t buffer2_offset = DMA_BUFFER_SIZE;

    printf("[INFO] Starting DMA test application\n");

    // Open the device
    fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] Failed to open device: %s (errno: %d - %s)\n", DEVICE_PATH, errno, strerror(errno));
        return EXIT_FAILURE;
    }

    // Map the first DMA buffer (input buffer)
    input_buffer = (unsigned char*)mmap(NULL, DMA_BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (input_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] mmap failed for input buffer: %s\n", strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }

    // Map the second DMA buffer (output buffer)
    off_t aligned_offset = (buffer2_offset + page_size - 1) & ~(page_size - 1);
    output_buffer = (unsigned char*)mmap(NULL, DMA_BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, aligned_offset);
    if (output_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] mmap failed for output buffer: %s\n", strerror(errno));
        munmap(input_buffer, DMA_BUFFER_SIZE);
        close(fd);
        return EXIT_FAILURE;
    }

    Resolution srcRes = {SRC_WIDTH, SRC_HEIGHT, input_buffer};
    Resolution dstRes = {DST_WIDTH, DST_HEIGHT, output_buffer};

    // Initialize input buffer with a pattern
    #pragma omp parallel for collapse(2)
    for (int y = 0; y < srcRes.height; y++) {
        for (int x = 0; x < srcRes.width; x++) {
            int index = (y * srcRes.width + x) * PIXEL_SIZE;
            srcRes.data[index] = (x * 255) / srcRes.width;
            srcRes.data[index + 1] = (y * 255) / srcRes.height;
            srcRes.data[index + 2] = ((x + y) * 255) / (srcRes.width + srcRes.height);
        }
    }

    double start_time = omp_get_wtime();

    #pragma omp parallel for
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        scaleImage(&srcRes, &dstRes);
    }

    double total_time = omp_get_wtime() - start_time;

    savePPM("output.ppm", &dstRes);
    printf("[INFO] Completed %d scaling operations in %.6f seconds\n", MAX_ITERATIONS, total_time);

    munmap(input_buffer, DMA_BUFFER_SIZE);
    munmap(output_buffer, DMA_BUFFER_SIZE);
    close(fd);

    return EXIT_SUCCESS;
}

