#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#define DEVICE_PATH "/dev/my_dma_device"
#define DST_WIDTH  1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
// Ensure it's a multiple of the page size
#define DMA_BUFFER_SIZE ((DST_WIDTH * DST_HEIGHT * PIXEL_SIZE) + 4096)

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// Save image as a PPM (Portable PixMap)
void savePPM(const char* filename, Resolution* res) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "[ERROR] Failed to open file for writing: %s\n", filename);
        return;
    }
    
    // Write the PPM header
    fprintf(file, "P6\n%d %d\n255\n", res->width, res->height);
    // Write the RGB data in one shot
    size_t dataSize = res->width * res->height * PIXEL_SIZE;
    if (fwrite(res->data, 1, dataSize, file) != dataSize) {
        fprintf(stderr, "[ERROR] Failed to write image data to file: %s\n", filename);
    }
    fclose(file);
    printf("[INFO] Saved image: %s\n", filename);
}

int main(void) {
    int fd;
    unsigned char *output_buffer;
    
    printf("[INFO] Opening device: %s\n", DEVICE_PATH);
    fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] Failed to open device: %s (errno: %d - %s)\n", 
                DEVICE_PATH, errno, strerror(errno));
        return EXIT_FAILURE;
    }
    
    // Map the output DMA buffer.
    // Using a nonzero offset (e.g., 1) tells the driver to return dma_buffer2.
    off_t offset = 1; 
    output_buffer = (unsigned char *)mmap(NULL, DMA_BUFFER_SIZE,
                                          PROT_READ | PROT_WRITE, MAP_SHARED,
                                          fd, offset);
    if (output_buffer == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Failed to mmap output buffer: %s (errno: %d)\n",
                strerror(errno), errno);
        close(fd);
        return EXIT_FAILURE;
    }
    
    printf("[INFO] Output buffer mapped at %p\n", output_buffer);
    
    // Create Resolution structure for the output image.
    Resolution dstRes;
    dstRes.width = DST_WIDTH;
    dstRes.height = DST_HEIGHT;
    dstRes.data = output_buffer;
    
    // Save the image to a file. The driver has already performed scaling.
    savePPM("output.ppm", &dstRes);
    
    // Cleanup: unmap memory and close the device.
    if (munmap(output_buffer, DMA_BUFFER_SIZE) != 0) {
        fprintf(stderr, "[ERROR] Failed to unmap output buffer: %s (errno: %d)\n",
                strerror(errno), errno);
    }
    close(fd);
    printf("[INFO] Application finished\n");
    
    return EXIT_SUCCESS;
}

