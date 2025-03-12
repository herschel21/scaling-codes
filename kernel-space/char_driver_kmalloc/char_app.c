#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <math.h>
#include <omp.h>

#define MAX_ITERATIONS 100

// Source image dimensions
#define SRC_WIDTH 10 
#define SRC_HEIGHT 10

// Destination image dimensions
#define DST_WIDTH 20
#define DST_HEIGHT 20

#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define MEM_SIZE 4096 // Must match mem_size from the driver

// Nearest-neighbor scaling function
void scaleImage(unsigned char* src, unsigned char* dst, 
                int src_width, int src_height,
                int dst_width, int dst_height) {
    float x_ratio = (float)src_width / dst_width;
    float y_ratio = (float)src_height / dst_height;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < dst_height; y++) {
        for (int x = 0; x < dst_width; x++) {
            int srcX = (int)(x * x_ratio);
            int srcY = (int)(y * y_ratio);
            int srcIndex = (srcY * src_width + srcX) * PIXEL_SIZE;
            int dstIndex = (y * dst_width + x) * PIXEL_SIZE;

            // Copy RGB values from the nearest pixel
            dst[dstIndex] = src[srcIndex];
            dst[dstIndex + 1] = src[srcIndex + 1];
            dst[dstIndex + 2] = src[srcIndex + 2];
        }
    }
}

int main() {
    int fd;
    unsigned char* kernel_buffer;
    unsigned char* output_buffer;
    size_t input_size = MEM_SIZE;
    
    // Open the device file
    fd = open("/dev/etx_device", O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return -1;
    }
    printf("Device opened successfully\n");

    // Map kernel_buffer (vm_pgoff = 0)
    kernel_buffer = mmap(NULL, input_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (kernel_buffer == MAP_FAILED) {
        perror("Failed to map kernel_buffer");
        close(fd);
        return -1;
    }
    printf("kernel_buffer mapped successfully at %p\n", kernel_buffer);

    // Map output_buffer (vm_pgoff = 1)
    output_buffer = mmap(NULL, input_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, getpagesize());
    if (output_buffer == MAP_FAILED) {
        perror("Failed to map output_buffer");
        munmap(kernel_buffer, input_size);
        close(fd);
        return -1;
    }
    printf("output_buffer mapped successfully at %p\n", output_buffer);

    // Start measuring time
    double start_time = omp_get_wtime();

    // Initialize kernel buffer with some pattern data
    #pragma omp parallel for
    for (size_t i = 0; i < input_size; i++) {
        kernel_buffer[i] = (rand() % 256);
    }
    
    // Process multiple iterations
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        printf("Iteration %d\n", i);

        // Scale the image (process the data in user space)
        scaleImage(kernel_buffer, output_buffer, SRC_WIDTH, SRC_HEIGHT, DST_WIDTH, DST_HEIGHT);
    }

    // Clean up
    if (munmap(kernel_buffer, input_size) == -1) {
        perror("munmap kernel_buffer");
    }
    if (munmap(output_buffer, input_size) == -1) {
        perror("munmap output_buffer");
    }
    close(fd);

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;

    printf("Completed %d scaling operations in %.6f seconds\n", MAX_ITERATIONS, total_time);
    printf("Test application completed successfully\n");

    return 0;
}
