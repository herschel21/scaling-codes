#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define DEVICE_NAME "img_scale_device"

// Source image dimensions
#define SRC_WIDTH 640 
#define SRC_HEIGHT 480

// Destination image dimensions
#define DST_WIDTH 1920
#define DST_HEIGHT 1080

#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)

// Buffer sizes
#define SRC_BUFFER_SIZE (SRC_WIDTH * SRC_HEIGHT * PIXEL_SIZE)
#define DST_BUFFER_SIZE (DST_WIDTH * DST_HEIGHT * PIXEL_SIZE)
#define DMA_BUFFER_SIZE ((DST_BUFFER_SIZE > SRC_BUFFER_SIZE ? DST_BUFFER_SIZE : SRC_BUFFER_SIZE) + 4096)

static void *src_buffer, *dst_buffer;
static dma_addr_t src_handle, dst_handle;
static struct platform_device *my_platform_device;
static struct cdev my_cdev;
static dev_t dev_num;
static struct class *my_class;
static struct task_struct *scaling_task;
static bool scaling_active = false;

// Task control
static int scaling_iterations = 100;
static int current_iteration = 0;
static ktime_t start_time, end_time;

// Platform device release function
static void my_platform_device_release(struct device *dev)
{
    pr_info("Releasing platform device\n");
}

// Image scaling function using fixed-point arithmetic
static void scale_image_fixed_point(unsigned char *src_data, unsigned char *dst_data,
                                    int src_width, int src_height,
                                    int dst_width, int dst_height)
{
    int x, y;
    int x_ratio, y_ratio;
    int srcX, srcY, srcIndex, dstIndex;

    x_ratio = (src_width << 16) / dst_width;
    y_ratio = (src_height << 16) / dst_height;

    for (y = 0; y < dst_height; y++) {
        for (x = 0; x < dst_width; x++) {
            srcX = (x * x_ratio) >> 16;
            srcY = (y * y_ratio) >> 16;
            srcIndex = (srcY * src_width + srcX) * PIXEL_SIZE;
            dstIndex = (y * dst_width + x) * PIXEL_SIZE;

            // Copy RGB pixel data
            memcpy(&dst_data[dstIndex], &src_data[srcIndex], PIXEL_SIZE);
        }
    }
}

// Initialize source buffer with pattern
static void init_source_buffer(void)
{
    int x, y, index;
    unsigned char *data = src_buffer;

    for (y = 0; y < SRC_HEIGHT; y++) {
        for (x = 0; x < SRC_WIDTH; x++) {
            index = (y * SRC_WIDTH + x) * PIXEL_SIZE;
            // Create a colorful pattern
            data[index]     = (x * 255) / SRC_WIDTH;             // R
            data[index + 1] = (y * 255) / SRC_HEIGHT;            // G
            data[index + 2] = ((x+y) * 255) / (SRC_WIDTH + SRC_HEIGHT); // B
        }
    }
}

// Main scaling thread function
static int scaling_thread_fn(void *data)
{
    unsigned long duration_ns;
    unsigned long avg_duration_ns;
    
    pr_info("Starting scaling thread\n");
    
    // Initialize source buffer with pattern
    init_source_buffer();
    
    // Record start time
    start_time = ktime_get();
    
    // Perform scaling iterations
    for (current_iteration = 0; current_iteration < scaling_iterations && !kthread_should_stop(); current_iteration++) {
        // Perform scaling operation
        scale_image_fixed_point(src_buffer, dst_buffer, 
                                SRC_WIDTH, SRC_HEIGHT, 
                                DST_WIDTH, DST_HEIGHT);
        
        // Optional: Add a small delay to avoid hogging CPU
        if (current_iteration % 10 == 0)
            schedule();
    }
    
    // Record end time
    end_time = ktime_get();
    duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    avg_duration_ns = duration_ns / scaling_iterations;
    
    pr_info("Scaling completed: %d iterations in %lu ns (avg: %lu ns/iteration)\n", 
            current_iteration, duration_ns, avg_duration_ns);
    
    scaling_active = false;
    return 0;
}

// Start scaling thread
static int start_scaling(void)
{
    if (scaling_active) {
        pr_warn("Scaling already active\n");
        return -EBUSY;
    }
    
    scaling_active = true;
    current_iteration = 0;
    
    // Create and start the scaling thread
    scaling_task = kthread_run(scaling_thread_fn, NULL, "scaling_thread");
    if (IS_ERR(scaling_task)) {
        pr_err("Failed to create scaling thread\n");
        scaling_active = false;
        return PTR_ERR(scaling_task);
    }
    
    return 0;
}

// Stop scaling thread
static int stop_scaling(void)
{
    int ret = 0;
    
    if (!scaling_active) {
        pr_warn("Scaling not active\n");
        return -EINVAL;
    }
    
    if (scaling_task) {
        ret = kthread_stop(scaling_task);
        scaling_task = NULL;
    }
    
    scaling_active = false;
    return ret;
}

// File operations
static int my_open(struct inode *inode, struct file *file)
{
    pr_info("Device opened\n");
    return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
    pr_info("Device closed\n");
    return 0;
}

static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    char status[128];
    int len;
    
    // Provide status information
    if (scaling_active) {
        len = snprintf(status, sizeof(status), "Scaling active: %d/%d iterations completed\n", 
                       current_iteration, scaling_iterations);
    } else {
        if (current_iteration > 0) {
            unsigned long duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));
            unsigned long avg_duration_ns = duration_ns / scaling_iterations;
            len = snprintf(status, sizeof(status), 
                           "Scaling completed: %d iterations in %lu ns (avg: %lu ns/iteration)\n", 
                           current_iteration, duration_ns, avg_duration_ns);
        } else {
            len = snprintf(status, sizeof(status), "Scaling not active\n");
        }
    }
    
    if (*pos >= len)
        return 0;
    
    if (count > len - *pos)
        count = len - *pos;
    
    if (copy_to_user(buf, status + *pos, count))
        return -EFAULT;
    
    *pos += count;
    return count;
}

static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    char command[32];
    int ret;
    
    if (count >= sizeof(command))
        return -EINVAL;
    
    if (copy_from_user(command, buf, count))
        return -EFAULT;
    
    command[count] = '\0';
    
    // Simple command interface
    if (strncmp(command, "start", 5) == 0) {
        ret = start_scaling();
        if (ret)
            return ret;
    } else if (strncmp(command, "stop", 4) == 0) {
        ret = stop_scaling();
        if (ret)
            return ret;
    } else if (strncmp(command, "iterations=", 11) == 0) {
        int iterations;
        if (sscanf(command + 11, "%d", &iterations) == 1 && iterations > 0) {
            scaling_iterations = iterations;
            pr_info("Set iterations to %d\n", scaling_iterations);
        } else {
            return -EINVAL;
        }
    } else {
        return -EINVAL;
    }
    
    return count;
}

// mmap function to map DMA buffers to user space (optional for debugging)
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
        pr_info("Mapping Source Buffer: vma->vm_start=%lx, size=%zu\n",
                vma->vm_start, size);
        return dma_mmap_coherent(dev, vma, src_buffer, src_handle, size);
    } else {
        pr_info("Mapping Destination Buffer: vma->vm_start=%lx, size=%zu\n",
                vma->vm_start, size);
        // We need to reset vm_pgoff to 0 since we're mapping from the start of dst_buffer
        vma->vm_pgoff = 0;
        return dma_mmap_coherent(dev, vma, dst_buffer, dst_handle, size);
    }
}

// File operations structure
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write,
    .mmap = my_mmap,
};

// Platform driver probe function
static int my_platform_driver_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;

    // Allocate source buffer
    src_buffer = dma_alloc_coherent(dev, DMA_BUFFER_SIZE, &src_handle, GFP_KERNEL);
    if (!src_buffer) {
        pr_err("Failed to allocate source buffer\n");
        return -ENOMEM;
    }

    // Allocate destination buffer
    dst_buffer = dma_alloc_coherent(dev, DMA_BUFFER_SIZE, &dst_handle, GFP_KERNEL);
    if (!dst_buffer) {
        pr_err("Failed to allocate destination buffer\n");
        dma_free_coherent(dev, DMA_BUFFER_SIZE, src_buffer, src_handle);
        return -ENOMEM;
    }

    pr_info("Source buffer allocated: virt=%p, phys=%pa\n", src_buffer, &src_handle);
    pr_info("Destination buffer allocated: virt=%p, phys=%pa\n", dst_buffer, &dst_handle);

    return 0;
}

// Platform driver remove function
static int my_platform_driver_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;

    // Stop scaling if active
    if (scaling_active) {
        stop_scaling();
    }

    if (src_buffer) {
        dma_free_coherent(dev, DMA_BUFFER_SIZE, src_buffer, src_handle);
        pr_info("Source buffer freed\n");
    }

    if (dst_buffer) {
        dma_free_coherent(dev, DMA_BUFFER_SIZE, dst_buffer, dst_handle);
        pr_info("Destination buffer freed\n");
    }

    return 0;
}

// Platform driver structure
static struct platform_driver my_platform_driver = {
    .driver = {
        .name = "img_scale_platform_device",
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
    my_platform_device = platform_device_alloc("img_scale_platform_device", -1);
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

    pr_info("Image scaling module loaded successfully\n");
    
    // Optional: Auto-start scaling
    // start_scaling();
    
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
    // Stop scaling thread if active
    if (scaling_active) {
        stop_scaling();
    }

    platform_driver_unregister(&my_platform_driver);
    platform_device_unregister(my_platform_device);
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("Image scaling module unloaded successfully\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Standalone Image Scaling Kernel Driver");
