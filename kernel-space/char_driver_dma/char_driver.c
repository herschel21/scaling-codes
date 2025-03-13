#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DMA_BUFFER_SIZE ((1920 * 1080 * 3) + 4096)  // 6MB buffer

static struct platform_device *etx_pdev;
static struct class *etx_class;
static dev_t dev;
static struct cdev etx_cdev;
static void *dma_buffer;
static dma_addr_t dma_handle;

/*
** File Operations
*/
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    pr_info("etx_read: Read requested: %zu bytes\n", len);
    
    if (copy_to_user(buf, dma_buffer, len)) {
        pr_err("etx_read: copy_to_user failed\n");
        return -EFAULT;
    }
    return len;
}

static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    pr_info("etx_write: Write requested: %zu bytes\n", len);

    if (copy_from_user(dma_buffer, buf, len)) {
        pr_err("etx_write: copy_from_user failed\n");
        return -EFAULT;
    }
    return len;
}

static int etx_open(struct inode *inode, struct file *file)
{
    pr_info("etx_open: Device opened\n");
    return 0;
}

static int etx_release(struct inode *inode, struct file *file)
{
    pr_info("etx_release: Device closed\n");
    return 0;
}

static struct file_operations etx_fops = {
    .owner   = THIS_MODULE,
    .read    = etx_read,
    .write   = etx_write,
    .open    = etx_open,
    .release = etx_release,
};

/*
** Probe Function
*/
static int etx_probe(struct platform_device *pdev)
{
    int ret;

    pr_info("etx_probe: Initializing platform driver\n");

    // Set DMA mask and coherent DMA mask
    if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32))) {
        dev_err(&pdev->dev, "Failed to set DMA mask\n");
        return -EIO;
    }

    // Allocate DMA-coherent memory
    pr_info("etx_probe: Allocating DMA memory: %d bytes\n", DMA_BUFFER_SIZE);
    dma_buffer = dma_alloc_coherent(&pdev->dev, DMA_BUFFER_SIZE, &dma_handle, GFP_KERNEL);
    if (!dma_buffer) {
        dev_err(&pdev->dev, "Failed to allocate DMA memory\n");
        return -ENOMEM;
    }
    pr_info("etx_probe: DMA memory allocated at VA: %p, PA: 0x%llx\n", dma_buffer, (unsigned long long)dma_handle);

    // Allocate Major number
    if ((ret = alloc_chrdev_region(&dev, 0, 1, "etx_device")) < 0) {
        pr_err("etx_probe: Cannot allocate major number\n");
        goto fail_dma;
    }

    // Create class
    etx_class = class_create("etx_class");
    if (IS_ERR(etx_class)) {
        pr_err("etx_probe: Cannot create class\n");
        ret = PTR_ERR(etx_class);
        goto fail_chrdev;
    }

    // Create device
    if (IS_ERR(device_create(etx_class, NULL, dev, NULL, "etx_device"))) {
        pr_err("etx_probe: Cannot create device\n");
        ret = PTR_ERR(etx_class);
        goto fail_class;
    }

    // Initialize character device
    cdev_init(&etx_cdev, &etx_fops);
    if ((ret = cdev_add(&etx_cdev, dev, 1)) < 0) {
        pr_err("etx_probe: Cannot add device\n");
        goto fail_device;
    }

    pr_info("etx_probe: Platform driver initialized successfully!\n");
    return 0;

fail_device:
    device_destroy(etx_class, dev);
fail_class:
    class_destroy(etx_class);
fail_chrdev:
    unregister_chrdev_region(dev, 1);
fail_dma:
    dma_free_coherent(&pdev->dev, DMA_BUFFER_SIZE, dma_buffer, dma_handle);
    return ret;
}

/*
** Remove Function
*/
static int etx_remove(struct platform_device *pdev)
{
    pr_info("etx_remove: Removing platform driver\n");

    cdev_del(&etx_cdev);
    device_destroy(etx_class, dev);
    class_destroy(etx_class);
    unregister_chrdev_region(dev, 1);

    // Free DMA memory
    if (dma_buffer) {
        dma_free_coherent(&pdev->dev, DMA_BUFFER_SIZE, dma_buffer, dma_handle);
        pr_info("etx_remove: DMA buffer freed\n");
    }

    pr_info("etx_remove: Platform driver removed successfully\n");
    return 0;
}

// Platform driver structure
static struct platform_driver etx_platform_driver = {
    .probe  = etx_probe,
    .remove = etx_remove,
    .driver = {
        .name = "etx_device",
        .owner = THIS_MODULE,
    },
};

/*
** Module Init Function
*/
static int __init etx_driver_init(void)
{
    pr_info("etx_driver_init: Registering platform driver\n");

    // Register the platform driver
    return platform_driver_register(&etx_platform_driver);
}

/*
** Module Exit Function
*/
static void __exit etx_driver_exit(void)
{
    pr_info("etx_driver_exit: Unregistering platform driver\n");

    // Unregister the platform driver
    platform_driver_unregister(&etx_platform_driver);
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Platform Driver with DMA Allocation");
MODULE_VERSION("1.0");

