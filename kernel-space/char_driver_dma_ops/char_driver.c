#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>

#define DEVICE_NAME "my_dma_device"

static void *dma_buffer1, *dma_buffer2;
static dma_addr_t dma_handle1, dma_handle2;
static struct platform_device *my_platform_device;
static struct cdev my_cdev;
static dev_t dev_num;
static struct class *my_class;

#define DMA_BUFFER_SIZE ((1920*1080*3) + 4096)

// Define ioctl commands
#define MY_MAGIC 'M'
#define IOCTL_SCALE_IMAGE _IOW(MY_MAGIC, 1, struct scale_params)

// Define parameters for scaling
struct scale_params {
    int src_width;
    int src_height;
    int dst_width;
    int dst_height;
    int pixel_size;
};

// Nearest-neighbor scaling function (kernel version)
static void scale_image(unsigned char *src_data, unsigned char *dst_data,
                        int src_width, int src_height,
                        int dst_width, int dst_height, int pixel_size)
{
    int x, y;
    int x_ratio = (src_width << 16) / dst_width;
    int y_ratio = (src_height << 16) / dst_height;
    int srcX, srcY, src_index, dst_index;

    pr_info("Starting image scaling in kernel space\n");

    for (y = 0; y < dst_height; y++) {
        for (x = 0; x < dst_width; x++) {
            srcX = (x * x_ratio) >> 16;
            srcY = (y * y_ratio) >> 16;
            src_index = (srcY * src_width + srcX) * pixel_size;
            dst_index = (y * dst_width + x) * pixel_size;
            
            // Copy pixel data
            memcpy(&dst_data[dst_index], &src_data[src_index], pixel_size);
        }
    }

    pr_info("Image scaling completed\n");
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

    // Allocate first DMA buffer
    dma_buffer1 = dma_alloc_coherent(dev, DMA_BUFFER_SIZE, &dma_handle1, GFP_KERNEL);
    if (!dma_buffer1) {
        pr_err("Failed to allocate DMA buffer1\n");
        return -ENOMEM;
    }

    // Allocate second DMA buffer
    dma_buffer2 = dma_alloc_coherent(dev, DMA_BUFFER_SIZE, &dma_handle2, GFP_KERNEL);
    if (!dma_buffer2) {
        pr_err("Failed to allocate DMA buffer2\n");
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dma_buffer1, dma_handle1);
        return -ENOMEM;
    }

    pr_info("DMA buffer1 allocated: virt=%p, phys=%pa\n", dma_buffer1, &dma_handle1);
    pr_info("DMA buffer2 allocated: virt=%p, phys=%pa\n", dma_buffer2, &dma_handle2);

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

// IOCTL handler
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct scale_params params;
    
    switch (cmd) {
    case IOCTL_SCALE_IMAGE:
        if (copy_from_user(&params, (void __user *)arg, sizeof(params)))
            return -EFAULT;
            
        pr_info("Scaling image from %dx%d to %dx%d with pixel size %d\n", 
                params.src_width, params.src_height,
                params.dst_width, params.dst_height,
                params.pixel_size);
                
        scale_image(dma_buffer1, dma_buffer2,
                    params.src_width, params.src_height,
                    params.dst_width, params.dst_height,
                    params.pixel_size);
        return 0;
    default:
        return -ENOTTY;
    }
}

// mmap function to map DMA buffers to user space
static int my_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct device *dev;
    size_t size = vma->vm_end - vma->vm_start;
    unsigned long pgoff = vma->vm_pgoff << PAGE_SHIFT; // Convert to byte offset

    if (!my_platform_device) {
        pr_err("Error: my_platform_device is NULL!\n");
        return -ENODEV;
    }

    dev = &my_platform_device->dev;
    if (!dev) {
        pr_err("Error: Device structure is NULL!\n");
        return -ENODEV;
    }

    pr_info("mmap request: vm_start=%lx, vm_end=%lx, vm_pgoff=%lu, size=%zu, pgoff_bytes=%lu\n",
           vma->vm_start, vma->vm_end, vma->vm_pgoff, size, pgoff);

    if (vma->vm_pgoff == 0) {
        pr_info("Mapping Buffer 1: vma->vm_start=%lx, size=%zu\n",
                vma->vm_start, size);
        return dma_mmap_coherent(dev, vma, dma_buffer1, dma_handle1, size);
    } else {
        pr_info("Mapping Buffer 2: vma->vm_start=%lx, size=%zu\n",
                vma->vm_start, size);
        // We need to reset vm_pgoff to 0 since we're mapping from the start of buffer2
        vma->vm_pgoff = 0;
        return dma_mmap_coherent(dev, vma, dma_buffer2, dma_handle2, size);
    }
}

// File operations structure
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .mmap = my_mmap,
    .unlocked_ioctl = my_ioctl,
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

    // Register character device
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

    // Register platform device
    my_platform_device = platform_device_alloc("my_platform_device", -1);
    if (!my_platform_device) {
        ret = -ENOMEM;
        goto destroy_device;
    }

    my_platform_device->dev.release = my_platform_device_release;
    ret = platform_device_add(my_platform_device);
    if (ret)
        goto put_device;

    // Register platform driver
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
MODULE_DESCRIPTION("Platform Driver with Two DMA Buffers, mmap Support and Image Scaling");
