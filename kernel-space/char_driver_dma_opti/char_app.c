#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <omp.h>

#define DEVICE_PATH "/dev/my_dma_device"
#define MAX_ITERATIONS 100

// Source image dimensions
#define SRC_WIDTH 640 
#define SRC_HEIGHT 480

// Destination image dimensions
#define DST_WIDTH 1920
#define DST_HEIGHT 1080

#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define DMA_BUFFER_SIZE ((1920 * 1080 * 3) + 4096) 

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Nearest-neighbor scaling function (unchanged for this optimization)
void scaleImage(Resolution* src, Resolution* dst) {
    int x_ratio = (src->width << 16) / dst->width;
    int y_ratio = (src->height << 16) / dst->height;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < dst->height; y++) {
        for (int x = 0; x < dst->width; x++) {
            int srcX = (x * x_ratio) >> 16;
            int srcY = (y * y_ratio) >> 16;
            int srcIndex = (srcY * src->width + srcX) * PIXEL_SIZE;
            int dstIndex = (y * dst->width + x) * PIXEL_SIZE;
            memcpy(&dst->data[dstIndex], &src->data[srcIndex], PIXEL_SIZE);
        }
    }
}

// Cleanup function for resources
static void cleanup(int fd, unsigned char *input_buffer, unsigned char *output_buffer)
{
    if (input_buffer != MAP_FAILED && input_buffer != NULL) {
        if (munmap(input_buffer, DMA_BUFFER_SIZE) == -1) {
            fprintf(stderr, "[ERROR] Failed to unmap input buffer: %s\n", strerror(errno));
        } else {
            printf("[INFO] Input buffer unmapped successfully\n");
        }
    }
    if (output_buffer != MAP_FAILED && output_buffer != NULL) {
        if (munmap(output_buffer, DMA_BUFFER_SIZE) == -1) {
            fprintf(stderr, "[ERROR] Failed to unmap output buffer: %s\n", strerror(errno));
        } else {
            printf("[INFO] Output buffer unmapped successfully\n");
        }
    }
    if (fd >= 0) {
        close(fd);
        printf("[INFO] Device closed successfully\n");
    }
}

int main() {
    int fd = -1;
    unsigned char *input_buffer = MAP_FAILED;  // Maps to DMA buffer1
    unsigned char *output_buffer = MAP_FAILED; // Maps to DMA buffer2
    long page_size = sysconf(_SC_PAGESIZE);

    printf("[INFO] Starting DMA test application\n");
    printf("[INFO] Page size: %ld bytes\n", page_size);
    printf("[INFO] Buffer size: %d bytes\n", DMA_BUFFER_SIZE);
    
    // Open the device without O_SYNC unless required
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] Failed to open device: %s (errno: %d - %s)\n", 
                DEVICE_PATH, errno, strerror(errno));
        return EXIT_FAILURE;
    }
    printf("[INFO] Device opened successfully (fd=%d)\n", fd);
    
    // Map the first DMA buffer (input buffer) at offset 0
    printf("[INFO] Mapping input buffer (%d bytes) at offset 0...\n", DMA_BUFFER_SIZE);
    input_buffer = (unsigned char *)mmap(NULL, DMA_BUFFER_SIZE, 
                                         PROT_READ | PROT_WRITE, MAP_SHARED, 
                                         fd, 0);
    if (input_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Failed to mmap input buffer: %s (errno: %d)\n", 
                strerror(errno), errno);
        cleanup(fd, input_buffer, output_buffer);
        return EXIT_FAILURE;
    }
    printf("[INFO] Input buffer mapped successfully at %p\n", input_buffer);
    
    // Map the second DMA buffer (output buffer) at offset page_size
    printf("[INFO] Mapping output buffer (%d bytes) at offset %ld...\n", 
           DMA_BUFFER_SIZE, page_size);
    output_buffer = (unsigned char *)mmap(NULL, DMA_BUFFER_SIZE, 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, 
                                          fd, page_size);
    if (output_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Failed to mmap output buffer: %s (errno: %d)\n", 
                strerror(errno), errno);
        cleanup(fd, input_buffer, output_buffer);
        return EXIT_FAILURE;
    }
    printf("[INFO] Output buffer mapped successfully at %p\n", output_buffer);
    
    // Create Resolution structs for source and destination
    Resolution srcRes = {SRC_WIDTH, SRC_HEIGHT, input_buffer};
    Resolution dstRes = {DST_WIDTH, DST_HEIGHT, output_buffer};

    // Initialize input buffer with some pattern data
    printf("[INFO] Initializing input buffer with color pattern...\n");
    srand(42);
    #pragma omp parallel for collapse(2)
    for (int y = 0; y < srcRes.height; y++) {
        for (int x = 0; x < srcRes.width; x++) {
            int index = (y * srcRes.width + x) * PIXEL_SIZE;
            srcRes.data[index]     = (x * 255) / srcRes.width;             // R
            srcRes.data[index + 1] = (y * 255) / srcRes.height;            // G
            srcRes.data[index + 2] = ((x+y) * 255) / (srcRes.width + srcRes.height); // B
        }
    }

    // Start measuring time
    printf("[INFO] Starting %d iterations of image scaling...\n", MAX_ITERATIONS);
    double start_time = omp_get_wtime();

    // Process multiple iterations
    #pragma omp parallel for
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        scaleImage(&srcRes, &dstRes);
    }

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;
    double avg_time = total_time / MAX_ITERATIONS;

    printf("[INFO] Completed %d scaling operations in %.6f seconds (avg: %.6f sec/operation)\n", 
           MAX_ITERATIONS, total_time, avg_time);

    // Cleanup
    printf("[INFO] Cleaning up...\n");
    cleanup(fd, input_buffer, output_buffer);

    printf("[SUCCESS] Test application completed\n");
    return EXIT_SUCCESS;
}
