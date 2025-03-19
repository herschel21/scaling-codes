#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#define DEVICE_NAME "my_dma_device"
#define SRC_WIDTH 640 
#define SRC_HEIGHT 480
#define DST_WIDTH 1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define DMA_BUFFER_SIZE ((DST_WIDTH * DST_HEIGHT * PIXEL_SIZE) + 4096)

static void *dma_buffer1, *dma_buffer2;
static dma_addr_t dma_handle1, dma_handle2;
static struct cdev my_cdev;
static dev_t dev_num;
static struct class *my_class;
static struct mutex dma_lock;

// Image scaling function
static void scale_image(void) {
    int x_ratio = (SRC_WIDTH << 16) / DST_WIDTH;
    int y_ratio = (SRC_HEIGHT << 16) / DST_HEIGHT;
    unsigned char *src = (unsigned char *)dma_buffer1;
    unsigned char *dst = (unsigned char *)dma_buffer2;

    int x, y;
    for (y = 0; y < DST_HEIGHT; y++) {
        for (x = 0; x < DST_WIDTH; x++) {
            int srcX = (x * x_ratio) >> 16;
            int srcY = (y * y_ratio) >> 16;
            int srcIndex = (srcY * SRC_WIDTH + srcX) * PIXEL_SIZE;
            int dstIndex = (y * DST_WIDTH + x) * PIXEL_SIZE;
            memcpy(&dst[dstIndex], &src[srcIndex], PIXEL_SIZE);
        }
    }
}

// Write operation triggers image scaling
static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    mutex_lock(&dma_lock);
    scale_image();
    mutex_unlock(&dma_lock);
    return count;
}

// File operations structure
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = my_write,
};

// Initialization function
static int __init my_init(void) {
    int ret;
    struct device *device;
    mutex_init(&dma_lock);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;
    
    cdev_init(&my_cdev, &fops);
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

    dma_buffer1 = dma_alloc_coherent(NULL, DMA_BUFFER_SIZE, &dma_handle1, GFP_KERNEL);
    dma_buffer2 = dma_alloc_coherent(NULL, DMA_BUFFER_SIZE, &dma_handle2, GFP_KERNEL);
    if (!dma_buffer1 || !dma_buffer2) {
        ret = -ENOMEM;
        goto destroy_device;
    }

    pr_info("Module loaded successfully\n");
    return 0;

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
static void __exit my_exit(void) {
    dma_free_coherent(NULL, DMA_BUFFER_SIZE, dma_buffer1, dma_handle1);
    dma_free_coherent(NULL, DMA_BUFFER_SIZE, dma_buffer2, dma_handle2);
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
MODULE_DESCRIPTION("Kernel-Space Image Scaling with DMA");

