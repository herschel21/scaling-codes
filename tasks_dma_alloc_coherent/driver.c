/***************************************************************************//**
 *  \file       driver.c
 *
 *  \details    Linux device driver (IOCTL) with DMA support on Raspberry Pi 3.
 *
 *  \author     EmbeTronicX
 *
 *******************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/completion.h>

#define WR_VALUE _IOW('a', 'a', int32_t*)
#define RD_VALUE _IOR('a', 'b', int32_t*)
#define MY_IOCTL_START_DMA _IO('k', 0)
#define MY_IOCTL_STOP_DMA _IO('k', 1)

#define DMA_BUFFER_SIZE 1024

struct etx_device {
    struct cdev cdev;
    struct device *device;
    dev_t dev;

    int32_t value;

    // DMA-related members
    void *src_buf;
    void *dst_buf;
    dma_addr_t src_addr;
    dma_addr_t dst_addr;
    struct dma_chan *chan;
    struct dma_async_tx_descriptor *desc;
    dma_cookie_t cookie;
};

// Define one device instance (can be expanded for multiple devices)
static struct etx_device etx_dev;
static struct class *etx_class;

// Function Prototypes
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static long etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .read           = etx_read,
    .write          = etx_write,
    .open           = etx_open,
    .unlocked_ioctl = etx_ioctl,
    .release        = etx_release,
};

// DMA Completion Callback
static void my_dma_transfer_completed(void *param)
{
    struct completion *cmp = (struct completion *)param;
    complete(cmp);
}

// DMA Initialization
static int my_dma_init(struct etx_device *dev)
{
    dma_cap_mask_t mask;
    struct completion cmp;
    int ret;

    pr_info("Requesting DMA channel...\n");

    dma_cap_zero(mask);
    dma_cap_set(DMA_MEMCPY, mask);

    dev->chan = dma_request_channel(mask, NULL, NULL);
    if (!dev->chan) {
        pr_err("Failed to request DMA channel\n");
        return -ENODEV;
    }

    dev->src_buf = dma_alloc_coherent(dev->device, DMA_BUFFER_SIZE, &dev->src_addr, GFP_KERNEL);
    if (!dev->src_buf) {
        pr_err("Failed to allocate source buffer\n");
        ret = -ENOMEM;
        goto free_chan;
    }

    dev->dst_buf = dma_alloc_coherent(dev->device, DMA_BUFFER_SIZE, &dev->dst_addr, GFP_KERNEL);
    if (!dev->dst_buf) {
        pr_err("Failed to allocate destination buffer\n");
        ret = -ENOMEM;
        goto free_src_buf;
    }

    memset(dev->src_buf, 0x12, DMA_BUFFER_SIZE);
    memset(dev->dst_buf, 0x0, DMA_BUFFER_SIZE);

    dev->desc = dmaengine_prep_dma_memcpy(dev->chan, dev->dst_addr, dev->src_addr, DMA_BUFFER_SIZE, 0);
    if (!dev->desc) {
        pr_err("Failed to prepare DMA memcpy\n");
        ret = -EINVAL;
        goto free_dst_buf;
    }

    init_completion(&cmp);
    dev->desc->callback = my_dma_transfer_completed;
    dev->desc->callback_param = &cmp;

    dev->cookie = dmaengine_submit(dev->desc);
    dma_async_issue_pending(dev->chan);

    if (wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000)) <= 0) {
        pr_err("DMA transfer timeout\n");
        ret = -ETIMEDOUT;
        goto free_dst_buf;
    }

    pr_info("DMA Transfer Completed Successfully\n");
    return 0;

free_dst_buf:
    dma_free_coherent(dev->device, DMA_BUFFER_SIZE, dev->dst_buf, dev->dst_addr);
free_src_buf:
    dma_free_coherent(dev->device, DMA_BUFFER_SIZE, dev->src_buf, dev->src_addr);
free_chan:
    dma_release_channel(dev->chan);
    return ret;
}

// DMA Cleanup
static void my_dma_exit(struct etx_device *dev)
{
    if (dev->chan) {
        dma_free_coherent(dev->device, DMA_BUFFER_SIZE, dev->dst_buf, dev->dst_addr);
        dma_free_coherent(dev->device, DMA_BUFFER_SIZE, dev->src_buf, dev->src_addr);
        dma_release_channel(dev->chan);
    }
}

// Open
static int etx_open(struct inode *inode, struct file *file)
{
    struct etx_device *dev = container_of(inode->i_cdev, struct etx_device, cdev);
    file->private_data = dev;
    pr_info("Device Opened\n");
    return 0;
}

// Release
static int etx_release(struct inode *inode, struct file *file)
{
    pr_info("Device Closed\n");
    return 0;
}

// Read
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    pr_info("Read Function\n");
    return 0;
}

// Write
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    pr_info("Write Function\n");
    return len;
}

// IOCTL
static long etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct etx_device *dev = file->private_data;

    switch (cmd) {
        case MY_IOCTL_START_DMA:
            return my_dma_init(dev);
        case MY_IOCTL_STOP_DMA:
            my_dma_exit(dev);
            return 0;
        default:
            return -EINVAL;
    }
}

// Module Init
static int __init etx_driver_init(void)
{
    alloc_chrdev_region(&etx_dev.dev, 0, 1, "etx_Dev");
    cdev_init(&etx_dev.cdev, &fops);
    cdev_add(&etx_dev.cdev, etx_dev.dev, 1);

    etx_class = class_create("etx_class");
    etx_dev.device = device_create(etx_class, NULL, etx_dev.dev, NULL, "etx_device");

    pr_info("Device Driver Inserted\n");
    return 0;
}

// Module Exit
static void __exit etx_driver_exit(void)
{
    device_destroy(etx_class, etx_dev.dev);
    class_destroy(etx_class);
    cdev_del(&etx_dev.cdev);
    unregister_chrdev_region(etx_dev.dev, 1);
    pr_info("Device Driver Removed\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX");
MODULE_DESCRIPTION("Linux device driver with DMA support on Raspberry Pi 3");
MODULE_VERSION("3.0");

