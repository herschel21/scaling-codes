#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#define DEVICE_PATH "/dev/etx_device"

#define MY_IOCTL_START_DMA _IO('k', 0)
#define MY_IOCTL_STOP_DMA _IO('k', 1)

int main() {
    int fd;
    
    // Open the device file
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }
    printf("Device opened successfully: %s\n", DEVICE_PATH);

    // Start DMA Transfer
    printf("Starting DMA Transfer...\n");
    if (ioctl(fd, MY_IOCTL_START_DMA) < 0) {
        perror("IOCTL START_DMA failed");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("DMA Transfer Started Successfully!\n");

    // Read data from the device (not mandatory for DMA, but for verification)
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    if (read(fd, buffer, sizeof(buffer)) < 0) {
        perror("Read failed");
    } else {
        printf("Read success: %s\n", buffer);
    }

    // Stop DMA Transfer
    printf("Stopping DMA Transfer...\n");
    if (ioctl(fd, MY_IOCTL_STOP_DMA) < 0) {
        perror("IOCTL STOP_DMA failed");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("DMA Transfer Stopped Successfully!\n");

    // Close the device
    close(fd);
    printf("Device closed successfully\n");

    return EXIT_SUCCESS;
}

