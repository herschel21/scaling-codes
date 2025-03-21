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

#define MAX_ITERATIONS 100

// Source image dimensions
#define SRC_WIDTH 1400 
#define SRC_HEIGHT 900

// Destination image dimensions
#define DST_WIDTH 1920
#define DST_HEIGHT 1080

#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define MEM_SIZE (1920*1080*3) // Matching the driver's memory size
#define CACHE_LINE_SIZE 64  // Assume 64-byte cache lines for prefetching

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Nearest-neighbor scaling function (optimized)
void scaleImage(unsigned char* src, unsigned char* dst, 
                int src_width, int src_height,
                int dst_width, int dst_height) {
    int x_ratio = (src_width << 16) / dst_width;  
    int y_ratio = (src_height << 16) / dst_height;

    // Precompute source row offsets to avoid per-iteration multiplication
    int* srcRowOffset = (int*)malloc(dst_height * sizeof(int));
    #pragma omp parallel for
    for (int y = 0; y < dst_height; y++) {
        srcRowOffset[y] = ((y * y_ratio) >> 16) * src_width * PIXEL_SIZE;
    }

    #pragma omp parallel for schedule(dynamic, 12)
    for (int y = 0; y < dst_height; y++) {
        int rowOffset = srcRowOffset[y];
        for (int x = 0; x < dst_width; x += 4) {  // Unroll loop (process 4 pixels)
            int srcX1 = (x * x_ratio) >> 16;
            int srcX2 = ((x + 1) * x_ratio) >> 16;
            int srcX3 = ((x + 2) * x_ratio) >> 16;
            int srcX4 = ((x + 3) * x_ratio) >> 16;

            int srcIndex1 = rowOffset + srcX1 * PIXEL_SIZE;
            int srcIndex2 = rowOffset + srcX2 * PIXEL_SIZE;
            int srcIndex3 = rowOffset + srcX3 * PIXEL_SIZE;
            int srcIndex4 = rowOffset + srcX4 * PIXEL_SIZE;

            int dstIndex = (y * dst_width + x) * PIXEL_SIZE;

            // Prefetch next row to reduce cache misses
            __builtin_prefetch(&src[srcIndex1 + CACHE_LINE_SIZE], 0, 1);

            // Copy 4 pixels (12 bytes each)
            *(uint32_t*)(dst + dstIndex) = *(uint32_t*)(src + srcIndex1);
            *(uint32_t*)(dst + dstIndex + 3) = *(uint32_t*)(src + srcIndex2);
            *(uint32_t*)(dst + dstIndex + 6) = *(uint32_t*)(src + srcIndex3);
            *(uint32_t*)(dst + dstIndex + 9) = *(uint32_t*)(src + srcIndex4);
        }
    }

    free(srcRowOffset);
}

// Save image as PPM (optimized for large writes)
void savePPM(const char* filename, Resolution* res) {
    FILE* file = fopen(filename, "wb");
    if (!file) return;

    fprintf(file, "P6\n%d %d\n255\n", res->width, res->height);

    // Write in large chunks to optimize performance
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
    unsigned char *kernel_buffer, *output_buffer;
    size_t input_size = MEM_SIZE;
    int num_threads = omp_get_max_threads();

    fd = open("/dev/etx_device", O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return -1;
    }

    kernel_buffer = (unsigned char *)mmap(NULL, MEM_SIZE, 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, 
                                          fd, 0);
    if (kernel_buffer == MAP_FAILED) {
        perror("mmap failed for kernel_buffer");
        close(fd);
        return -1;
    }

    output_buffer = (unsigned char *)mmap(NULL, MEM_SIZE, 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, 
                                          fd, getpagesize());
    if (output_buffer == MAP_FAILED) {
        perror("mmap failed for output_buffer");
        munmap(kernel_buffer, MEM_SIZE);
        close(fd);
        return -1;
    }

    Resolution srcRes = {SRC_WIDTH, SRC_HEIGHT, kernel_buffer};
    Resolution dstRes = {DST_WIDTH, DST_HEIGHT, output_buffer};

    #pragma omp parallel for schedule(dynamic, 12)
    for (size_t i = 0; i < input_size; i++) {
        kernel_buffer[i] = rand() % 256;
    }

    savePPM("input.ppm", &srcRes);

    double start_time = omp_get_wtime();

    for (int i = 0; i < MAX_ITERATIONS; i++) {
        scaleImage(kernel_buffer, output_buffer, SRC_WIDTH, SRC_HEIGHT, DST_WIDTH, DST_HEIGHT);
    }

    double total_time = omp_get_wtime() - start_time;

    savePPM("output.ppm", &dstRes);
    printf("Completed %d scaling operations in %.6f seconds\n", MAX_ITERATIONS, total_time);

    munmap(kernel_buffer, MEM_SIZE);
    munmap(output_buffer, MEM_SIZE);
    close(fd);

    return 0;
}

