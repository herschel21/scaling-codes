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

#define DMA_BUFFER_SIZE ((1920*1080*3) + 4096)

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

    pr_debug("DMA buffer1 allocated: virt=%p, phys=%pa\n", dma_buffer1, &dma_handle1);
    pr_debug("DMA buffer2 allocated: virt=%p, phys=%pa\n", dma_buffer2, &dma_handle2);

    return 0;
}

// Platform driver remove function
static int my_platform_driver_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;

    if (dma_buffer2) {
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dma_buffer2, dma_handle2);
        pr_debug("DMA buffer2 freed\n");
    }
    if (dma_buffer1) {
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dma_buffer1, dma_handle1);
        pr_debug("DMA buffer1 freed\n");
    }

    return 0;
}

// Optimized mmap function
static int my_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct device *dev = &my_platform_device->dev; // Assumes my_platform_device is valid
    size_t size = vma->vm_end - vma->vm_start;

    if (vma->vm_pgoff == 0) {
        pr_debug("Mapping Buffer 1: size=%zu\n", size);
        return dma_mmap_coherent(dev, vma, dma_buffer1, dma_handle1, size);
    } else if (vma->vm_pgoff == 1) {
        pr_debug("Mapping Buffer 2: size=%zu\n", size);
        return dma_mmap_coherent(dev, vma, dma_buffer2, dma_handle2, size);
    }

    pr_err("Invalid vm_pgoff: %lu\n", vma->vm_pgoff);
    return -EINVAL;
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

    // Register character device
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret) {
        pr_err("Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }

    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret) {
        pr_err("Failed to add cdev: %d\n", ret);
        goto unregister_chrdev;
    }

    my_class = class_create(DEVICE_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        pr_err("Failed to create class: %d\n", ret);
        goto del_cdev;
    }

    device = device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(device)) {
        ret = PTR_ERR(device);
        pr_err("Failed to create device: %d\n", ret);
        goto destroy_class;
    }

    // Register platform device
    my_platform_device = platform_device_alloc("my_platform_device", -1);
    if (!my_platform_device) {
        ret = -ENOMEM;
        pr_err("Failed to allocate platform device\n");
        goto destroy_device;
    }

    my_platform_device->dev.release = my_platform_device_release;
    ret = platform_device_add(my_platform_device);
    if (ret) {
        pr_err("Failed to add platform device: %d\n", ret);
        goto put_device;
    }

    // Register platform driver
    ret = platform_driver_register(&my_platform_driver);
    if (ret) {
        pr_err("Failed to register platform driver: %d\n", ret);
        goto del_platform_device;
    }

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
    if (my_platform_device)
        platform_device_unregister(my_platform_device);
    if (my_class) {
        device_destroy(my_class, dev_num);
        class_destroy(my_class);
    }
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("Module unloaded successfully\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Platform Driver with Two DMA Buffers and mmap Support");
