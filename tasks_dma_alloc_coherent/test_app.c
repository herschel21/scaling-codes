#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#define DEVICE_PATH "/dev/etx_device"
#define MY_IOCTL_START_DMA _IO('k', 0)
#define MY_IOCTL_STOP_DMA _IO('k', 1)

int main() {
    int fd, ret;
    char write_buf[] = "Hello DMA!";
    char read_buf[100] = {0};

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Cannot open device");
        return errno;
    }

    // Write to the device
    printf("Writing to device: %s\n", write_buf);
    ret = write(fd, write_buf, strlen(write_buf));
    if (ret < 0) {
        perror("Write failed");
        close(fd);
        return errno;
    }

    // Read from the device
    ret = read(fd, read_buf, sizeof(read_buf));
    if (ret < 0) {
        perror("Read failed");
        close(fd);
        return errno;
    }
    printf("Read from device: %s\n", read_buf);

    // Start DMA
    printf("Starting DMA transfer...\n");
    ret = ioctl(fd, MY_IOCTL_START_DMA);
    if (ret < 0) {
        perror("IOCTL Start DMA failed");
    } else {
        printf("DMA transfer started successfully.\n");
    }

    // Stop DMA
    printf("Stopping DMA transfer...\n");
    ret = ioctl(fd, MY_IOCTL_STOP_DMA);
    if (ret < 0) {
        perror("IOCTL Stop DMA failed");
    } else {
        printf("DMA transfer stopped successfully.\n");
    }

    close(fd);
    return 0;
}

