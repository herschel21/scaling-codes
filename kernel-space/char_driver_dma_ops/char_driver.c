#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#define DEVICE_NAME "my_dma_device"

static void *dma_buffer1, *dma_buffer2;
static dma_addr_t dma_handle1, dma_handle2;
static struct platform_device *my_platform_device;
static struct cdev my_cdev;
static dev_t dev_num;
static struct class *my_class;

#define SRC_WIDTH  640
#define SRC_HEIGHT 480
#define DST_WIDTH  1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define DMA_BUFFER_SIZE ((DST_WIDTH * DST_HEIGHT * PIXEL_SIZE) + 4096)

// Image scaling function (nearest-neighbor)
static void scale_image(unsigned char *src, unsigned char *dst)
{
    int x_ratio = (SRC_WIDTH << 16) / DST_WIDTH;
    int y_ratio = (SRC_HEIGHT << 16) / DST_HEIGHT;
    
    for (int y = 0; y < DST_HEIGHT; y++) {
        for (int x = 0; x < DST_WIDTH; x++) {
            int srcX = (x * x_ratio) >> 16;
            int srcY = (y * y_ratio) >> 16;
            int srcIndex = (srcY * SRC_WIDTH + srcX) * PIXEL_SIZE;
            int dstIndex = (y * DST_WIDTH + x) * PIXEL_SIZE;

            memcpy(&dst[dstIndex], &src[srcIndex], PIXEL_SIZE);
        }
    }
}

// Platform device release function
static void my_platform_device_release(struct device *dev)
{
    pr_info("Releasing my platform device\n");
}

// Platform driver probe function
static int my_platform_driver_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;

    // Allocate first DMA buffer (input buffer)
    dma_buffer1 = dma_alloc_coherent(dev, DMA_BUFFER_SIZE, &dma_handle1, GFP_KERNEL);
    if (!dma_buffer1) {
        pr_err("Failed to allocate DMA buffer1\n");
        return -ENOMEM;
    }

    // Allocate second DMA buffer (output buffer)
    dma_buffer2 = dma_alloc_coherent(dev, DMA_BUFFER_SIZE, &dma_handle2, GFP_KERNEL);
    if (!dma_buffer2) {
        pr_err("Failed to allocate DMA buffer2\n");
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dma_buffer1, dma_handle1);
        return -ENOMEM;
    }

    pr_info("DMA buffer1 allocated: virt=%p, phys=%pa\n", dma_buffer1, &dma_handle1);
    pr_info("DMA buffer2 allocated: virt=%p, phys=%pa\n", dma_buffer2, &dma_handle2);

    // Initialize the input buffer with a simple pattern (color gradient)
    pr_info("Initializing input buffer with color pattern...\n");
    for (int y = 0; y < SRC_HEIGHT; y++) {
        for (int x = 0; x < SRC_WIDTH; x++) {
            int index = (y * SRC_WIDTH + x) * PIXEL_SIZE;
            dma_buffer1[index] = (x * 255) / SRC_WIDTH;     // R
            dma_buffer1[index + 1] = (y * 255) / SRC_HEIGHT; // G
            dma_buffer1[index + 2] = ((x + y) * 255) / (SRC_WIDTH + SRC_HEIGHT); // B
        }
    }

    // Perform image scaling within the driver
    pr_info("Performing image scaling in kernel...\n");
    scale_image(dma_buffer1, dma_buffer2);

    pr_info("Image scaling complete.\n");

    return 0;
}

// Platform driver remove function
static int my_platform_driver_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;

    if (dma_buffer1) {
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dma_buffer1, dma_handle1);
        pr_info("DMA buffer1 freed\n");
    }

    if (dma_buffer2) {
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dma_buffer2, dma_handle2);
        pr_info("DMA buffer2 freed\n");
    }

    return 0;
}

// mmap function to map DMA buffers to user space
static int my_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct device *dev;
    size_t size = vma->vm_end - vma->vm_start;

    if (!my_platform_device) {
        pr_err("Error: my_platform_device is NULL!\n");
        return -ENODEV;
    }

    dev = &my_platform_device->dev;
    if (!dev) {
        pr_err("Error: Device structure is NULL!\n");
        return -ENODEV;
    }

    if (vma->vm_pgoff == 0) {
        return dma_mmap_coherent(dev, vma, dma_buffer1, dma_handle1, size);
    } else {
        return dma_mmap_coherent(dev, vma, dma_buffer2, dma_handle2, size);
    }
}

// File operations structure
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .mmap = my_mmap,
};

// Platform driver structure
static struct platform_driver my_platform_driver = {
    .driver = {
        .name = "my_platform_device",
    },
    .probe = my_platform_driver_probe,
    .remove = my_platform_driver_remove,
};

// Initialization function
static int __init my_init(void)
{
    int ret;
    struct device *device;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret)
        goto unregister_chrdev;

    my_class = class_create(DEVICE_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        goto del_cdev;
    }

    device = device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(device)) {
        ret = PTR_ERR(device);
        goto destroy_class;
    }

    my_platform_device = platform_device_alloc("my_platform_device", -1);
    if (!my_platform_device) {
        ret = -ENOMEM;
        goto destroy_device;
    }

    my_platform_device->dev.release = my_platform_device_release;
    ret = platform_device_add(my_platform_device);
    if (ret)
        goto put_device;

    ret = platform_driver_register(&my_platform_driver);
    if (ret)
        goto del_platform_device;

    pr_info("Module loaded successfully\n");
    return 0;

del_platform_device:
    platform_device_del(my_platform_device);
put_device:
    platform_device_put(my_platform_device);
destroy_device:
    device_destroy(my_class, dev_num);
destroy_class:
    class_destroy(my_class);
del_cdev:
    cdev_del(&my_cdev);
unregister_chrdev:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

// Cleanup function
static void __exit my_exit(void)
{
    platform_driver_unregister(&my_platform_driver);
    platform_device_unregister(my_platform_device);
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("Module unloaded successfully\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Platform Driver with DMA Buffers and Kernel-based Image Scaling");

