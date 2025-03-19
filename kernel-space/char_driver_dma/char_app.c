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

// Ensure it's a multiple of page size
#define DMA_BUFFER_SIZE ((1920 * 1080 * 3) + 4096) 

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Nearest-neighbor scaling function
#if 0
void scaleImage(Resolution* src, Resolution* dst) {
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
            // Copy RGB values from the nearest pixel
            // dst->data[dstIndex] = src->data[srcIndex];
            // dst->data[dstIndex + 1] = src->data[srcIndex + 1];
            // dst->data[dstIndex + 2] = src->data[srcIndex + 2];
        }
    }
}
#endif

#if 1
void scaleImage(Resolution* src, Resolution* dst) {
    int x_ratio = (src->width << 16) / dst->width;  // Using fixed-point scaling (Q16 format)
    int y_ratio = (src->height << 16) / dst->height;

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < dst->height; y++) {
        for (int x = 0; x < dst->width; x++) {
            int srcX = (x * x_ratio) >> 16;  // Convert back from fixed-point
            int srcY = (y * y_ratio) >> 16;
            int srcIndex = (srcY * src->width + srcX) * PIXEL_SIZE;
            int dstIndex = (y * dst->width + x) * PIXEL_SIZE;

            memcpy(&dst->data[dstIndex], &src->data[srcIndex], PIXEL_SIZE);
        }
    }
}
#endif

// Function to write resolution data to memory
void writeResolution(Resolution* res, unsigned char* destMemory) {
    size_t dataSize = res->width * res->height * PIXEL_SIZE;
    memcpy(destMemory, res->data, dataSize);
}

// Save image as PPM (Portable PixMap)
void savePPM(const char* filename, Resolution* res) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("Failed to open file for writing: %s\n", filename);
        return;
    }

    // Write PPM header
    fprintf(file, "P6\n%d %d\n255\n", res->width, res->height);

    // Write RGB data
    for (int i = 0; i < res->width * res->height * PIXEL_SIZE; i += PIXEL_SIZE) {
        fwrite(&res->data[i], 1, 3, file);  // Write only R, G, B
    }

    fclose(file);
    printf("Saved image: %s\n", filename);
}

int main() {
    int fd;
    unsigned char *input_buffer;  // Maps to DMA buffer1
    unsigned char *output_buffer; // Maps to DMA buffer2
    long page_size = sysconf(_SC_PAGESIZE);
    off_t buffer2_offset = DMA_BUFFER_SIZE; // Offset for buffer2
    
    printf("[INFO] Starting DMA test application\n");
    printf("[INFO] Page size: %ld bytes\n", page_size);
    printf("[INFO] Buffer size: %d bytes\n", DMA_BUFFER_SIZE);
    printf("[INFO] Buffer2 offset: %ld bytes\n", buffer2_offset);
    
    // Open the device
    fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] Failed to open device: %s (errno: %d - %s)\n", 
                DEVICE_PATH, errno, strerror(errno));
        return EXIT_FAILURE;
    }
    printf("[INFO] Device opened successfully (fd=%d)\n", fd);
    
    // Map the first DMA buffer (input buffer)
    printf("[INFO] Mapping input buffer (%d bytes)...\n", DMA_BUFFER_SIZE);
    input_buffer = (unsigned char *)mmap(NULL, DMA_BUFFER_SIZE, 
                                         PROT_READ | PROT_WRITE, MAP_SHARED, 
                                         fd, 0);
    if (input_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Failed to mmap input buffer: %s (errno: %d)\n", 
                strerror(errno), errno);
        close(fd);
        return EXIT_FAILURE;
    }
    printf("[INFO] Input buffer mapped successfully at %p\n", input_buffer);
    
    // Map the second DMA buffer (output buffer)
    // Ensure the offset is a multiple of page size
    off_t aligned_offset = (buffer2_offset + page_size - 1) & ~(page_size - 1);
    printf("[INFO] Mapping output buffer (%d bytes) with aligned offset: %ld...\n", 
           DMA_BUFFER_SIZE, aligned_offset);
    
    output_buffer = (unsigned char *)mmap(NULL, DMA_BUFFER_SIZE, 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, 
                                          fd, aligned_offset);
    if (output_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Failed to mmap output buffer: %s (errno: %d)\n", 
                strerror(errno), errno);
        munmap(input_buffer, DMA_BUFFER_SIZE);
        close(fd);
        return EXIT_FAILURE;
    }
    printf("[INFO] Output buffer mapped successfully at %p\n", output_buffer);
    
    // Create Resolution structs for source and destination
    Resolution srcRes = {SRC_WIDTH, SRC_HEIGHT, input_buffer};
    Resolution dstRes = {DST_WIDTH, DST_HEIGHT, output_buffer};

    // Initialize input buffer with some pattern data
    printf("[INFO] Initializing input buffer with color pattern...\n");
    srand(42); // Fixed seed for reproducible results
    #pragma omp parallel for collapse(2)
    for (int y = 0; y < srcRes.height; y++) {
        for (int x = 0; x < srcRes.width; x++) {
            int index = (y * srcRes.width + x) * PIXEL_SIZE;
            // Create a colorful pattern
            srcRes.data[index]     = (x * 255) / srcRes.width;             // R
            srcRes.data[index + 1] = (y * 255) / srcRes.height;            // G
            srcRes.data[index + 2] = ((x+y) * 255) / (srcRes.width + srcRes.height); // B
        }
    }

    // Save the initial input image for debugging
    printf("[INFO] Saving input image to input.ppm\n");
    savePPM("input.ppm", &srcRes);
    
    // Start measuring time
    printf("[INFO] Starting %d iterations of image scaling...\n", MAX_ITERATIONS);
    double start_time = omp_get_wtime();

    // Process multiple iterations
    #pragma omp parallel for
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        // Scale the image using the Resolution structs
        scaleImage(&srcRes, &dstRes);
    }

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;
    double avg_time = total_time / MAX_ITERATIONS;

    // Save the output image for debugging
    printf("[INFO] Saving output image to output.ppm\n");
    savePPM("output.ppm", &dstRes);
    
    printf("[INFO] Completed %d scaling operations in %.6f seconds (avg: %.6f sec/operation)\n", 
           MAX_ITERATIONS, total_time, avg_time);

    // Cleanup
    printf("[INFO] Cleaning up...\n");
    if (munmap(input_buffer, DMA_BUFFER_SIZE) == 0) {
        printf("[INFO] Input buffer unmapped successfully\n");
    } else {
        fprintf(stderr, "[ERROR] Failed to unmap input buffer: %s (errno: %d)\n", 
                strerror(errno), errno);
    }

    if (munmap(output_buffer, DMA_BUFFER_SIZE) == 0) {
        printf("[INFO] Output buffer unmapped successfully\n");
    } else {
        fprintf(stderr, "[ERROR] Failed to unmap output buffer: %s (errno: %d)\n", 
                strerror(errno), errno);
    }

    close(fd);
    printf("[INFO] Device closed successfully\n");
    printf("[SUCCESS] Test application completed\n");
    return EXIT_SUCCESS;
}
