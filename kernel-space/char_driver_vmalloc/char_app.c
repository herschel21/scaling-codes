#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <omp.h>

#define MAX_ITERATIONS 100

// Source image dimensions
#define SRC_WIDTH 1024 
#define SRC_HEIGHT 768

// Destination image dimensions
#define DST_WIDTH 1920
#define DST_HEIGHT 1080

#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define MEM_SIZE (1920*1080*3) // Matching the driver's memory size

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

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
    unsigned char *kernel_buffer;  // Maps to kernel_buffer
    unsigned char *output_buffer; // Maps to output_buffer
    size_t input_size = MEM_SIZE;
    
    printf("Starting etx_device test application (mmap only)\n");
    
    // Open the device
    fd = open("/dev/etx_device", O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return -1;
    }
    printf("Device opened successfully\n");
    
    // Map the kernel_buffer (offset 0)
    printf("Mapping kernel_buffer (input buffer)...\n");
    kernel_buffer = (unsigned char *)mmap(NULL, MEM_SIZE, 
                                         PROT_READ | PROT_WRITE, MAP_SHARED, 
                                         fd, 0);
    if (kernel_buffer == MAP_FAILED) {
        perror("mmap operation failed for kernel_buffer");
        close(fd);
        return -1;
    }
    printf("kernel_buffer mapped successfully at %p\n", kernel_buffer);
    
    // Map the output_buffer (offset 1)
    printf("Mapping output_buffer...\n");
    output_buffer = (unsigned char *)mmap(NULL, MEM_SIZE, 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, 
                                          fd, getpagesize());
    if (output_buffer == MAP_FAILED) {
        perror("mmap operation failed for output_buffer");
        munmap(kernel_buffer, MEM_SIZE);
        close(fd);
        return -1;
    }
    printf("output_buffer mapped successfully at %p\n", output_buffer);
    
    // Allocate a temporary Resolution struct for saving images
    Resolution srcRes = {SRC_WIDTH, SRC_HEIGHT, kernel_buffer};
    Resolution dstRes = {DST_WIDTH, DST_HEIGHT, output_buffer};


    // Initialize kernel buffer with some pattern data
    #pragma omp parallel for
    for (size_t i = 0; i < input_size; i++) {
        kernel_buffer[i] = (rand() % 256);
    }

    // Save the initial input image for debugging
    savePPM("input.ppm", &srcRes);
    
    // Start measuring time
    double start_time = omp_get_wtime();

    // Process multiple iterations
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        printf("Iteration %d\n", i);

        // Scale the image (process the data in user space)
        scaleImage(kernel_buffer, output_buffer, SRC_WIDTH, SRC_HEIGHT, DST_WIDTH, DST_HEIGHT);
    }

    // Stop measuring time
    double end_time = omp_get_wtime();
    double total_time = end_time - start_time;

    // Save the output image for debugging
    savePPM("output.ppm", &dstRes);
    printf("Completed %d scaling operations in %.6f seconds\n", MAX_ITERATIONS, total_time);

cleanup:
    // Unmap buffers
    munmap(kernel_buffer, MEM_SIZE);
    munmap(output_buffer, MEM_SIZE);
    close(fd);
    
    printf("Test application completed\n");
    return 0;
}

