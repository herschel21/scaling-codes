#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define MODULE_NAME "kernel_image_scaler"

// Image dimensions
#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define DST_WIDTH 1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)

// Buffer sizes
#define SRC_BUFFER_SIZE (SRC_WIDTH * SRC_HEIGHT * PIXEL_SIZE)
#define DST_BUFFER_SIZE (DST_WIDTH * DST_HEIGHT * PIXEL_SIZE)

// Ensure alignment
#define ALIGN_SIZE 4096
#define ALIGNED_SRC_SIZE ((SRC_BUFFER_SIZE + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1))
#define ALIGNED_DST_SIZE ((DST_BUFFER_SIZE + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1))

// Test parameters
#define NUM_ITERATIONS 100

// Module data structure
struct kernel_scaler_data {
    void *src_buffer;
    void *dst_buffer;
    dma_addr_t src_handle;
    dma_addr_t dst_handle;
    struct platform_device *pdev;
    struct proc_fs_entry *proc_entry;
    ktime_t scaling_time;
    int iterations_completed;
};

static struct kernel_scaler_data *scaler_data;
static struct proc_dir_entry *proc_file;

// Generate color pattern in source buffer
static void generate_source_image(unsigned char *buffer)
{
    int x, y, index;
    
    pr_info("%s: Generating %dx%d RGB source image pattern\n", 
            MODULE_NAME, SRC_WIDTH, SRC_HEIGHT);
    
    for (y = 0; y < SRC_HEIGHT; y++) {
        for (x = 0; x < SRC_WIDTH; x++) {
            index = (y * SRC_WIDTH + x) * PIXEL_SIZE;
            
            // Create a colorful pattern (similar to user-space version)
            buffer[index]     = (x * 255) / SRC_WIDTH;               // R
            buffer[index + 1] = (y * 255) / SRC_HEIGHT;              // G
            buffer[index + 2] = ((x+y) * 255) / (SRC_WIDTH + SRC_HEIGHT); // B
        }
    }
    
    pr_info("%s: Source image generated successfully\n", MODULE_NAME);
}

// Nearest-neighbor scaling function
static void scale_image(unsigned char *src_data, unsigned char *dst_data)
{
    int x, y;
    int x_ratio = (SRC_WIDTH << 16) / DST_WIDTH;
    int y_ratio = (SRC_HEIGHT << 16) / DST_HEIGHT;
    int srcX, srcY, src_index, dst_index;
    
    pr_info("%s: Starting image scaling from %dx%d to %dx%d\n", 
            MODULE_NAME, SRC_WIDTH, SRC_HEIGHT, DST_WIDTH, DST_HEIGHT);
    
    for (y = 0; y < DST_HEIGHT; y++) {
        for (x = 0; x < DST_WIDTH; x++) {
            srcX = (x * x_ratio) >> 16;
            srcY = (y * y_ratio) >> 16;
            src_index = (srcY * SRC_WIDTH + srcX) * PIXEL_SIZE;
            dst_index = (y * DST_WIDTH + x) * PIXEL_SIZE;
            
            // Copy pixel data
            memcpy(&dst_data[dst_index], &src_data[src_index], PIXEL_SIZE);
        }
        
        // Yield CPU occasionally to prevent system lockup during scaling
        if (y % 100 == 0)
            cond_resched();
    }
    
    pr_info("%s: Image scaling completed\n", MODULE_NAME);
}

// Run the scaling benchmark
static int run_scaling_benchmark(void)
{
    int i;
    ktime_t start_time, end_time, total_time = 0;
    
    pr_info("%s: Starting %d iterations of kernel-space image scaling benchmark\n", 
            MODULE_NAME, NUM_ITERATIONS);
    
    // Generate source image once
    generate_source_image(scaler_data->src_buffer);
    
    // Run multiple iterations
    for (i = 0; i < NUM_ITERATIONS; i++) {
        start_time = ktime_get();
        
        scale_image(scaler_data->src_buffer, scaler_data->dst_buffer);
        
        end_time = ktime_get();
        total_time += ktime_to_ns(ktime_sub(end_time, start_time));
        
        // Allow other processes to run occasionally
        if (i % 10 == 0)
            msleep(1);
    }
    
    scaler_data->scaling_time = total_time;
    scaler_data->iterations_completed = NUM_ITERATIONS;
    
    pr_info("%s: Completed %d scaling operations in %lld ns (avg: %lld ns/operation)\n", 
            MODULE_NAME, NUM_ITERATIONS, total_time, total_time / NUM_ITERATIONS);
            
    return 0;
}

// Proc file show function
static int proc_show(struct seq_file *m, void *v)
{
    if (scaler_data->iterations_completed > 0) {
        seq_printf(m, "Image Scaling Benchmark Results:\n");
        seq_printf(m, "Source Size: %dx%d (%d bytes)\n", 
                  SRC_WIDTH, SRC_HEIGHT, SRC_BUFFER_SIZE);
        seq_printf(m, "Destination Size: %dx%d (%d bytes)\n", 
                  DST_WIDTH, DST_HEIGHT, DST_BUFFER_SIZE);
        seq_printf(m, "Iterations: %d\n", scaler_data->iterations_completed);
        seq_printf(m, "Total Time: %lld nanoseconds\n", scaler_data->scaling_time);
        seq_printf(m, "Average Time: %lld nanoseconds per operation\n", 
                  scaler_data->scaling_time / scaler_data->iterations_completed);
        seq_printf(m, "Average Time: %lld microseconds per operation\n", 
                  (scaler_data->scaling_time / scaler_data->iterations_completed) / 1000);
    } else {
        seq_printf(m, "Benchmark not yet run or completed.\n");
    }
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

// Proc file write function to trigger the benchmark
static ssize_t proc_write(struct file *file, const char __user *buffer,
                         size_t count, loff_t *pos)
{
    char cmd[16];
    size_t cmd_size = min(count, sizeof(cmd) - 1);
    
    if (copy_from_user(cmd, buffer, cmd_size))
        return -EFAULT;
    
    cmd[cmd_size] = '\0';
    
    if (strncmp(cmd, "run", 3) == 0) {
        run_scaling_benchmark();
    }
    
    return count;
}

static const struct proc_ops proc_fops = {
    .proc_open = proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
    .proc_write = proc_write,
};

// Platform device release function
static void scaler_platform_device_release(struct device *dev)
{
    pr_info("%s: Releasing platform device\n", MODULE_NAME);
}

// Platform driver probe function
static int scaler_platform_driver_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    
    // Allocate source buffer
    scaler_data->src_buffer = dma_alloc_coherent(dev, ALIGNED_SRC_SIZE, 
                                                &scaler_data->src_handle, GFP_KERNEL);
    if (!scaler_data->src_buffer) {
        pr_err("%s: Failed to allocate source buffer\n", MODULE_NAME);
        return -ENOMEM;
    }
    
    // Allocate destination buffer
    scaler_data->dst_buffer = dma_alloc_coherent(dev, ALIGNED_DST_SIZE,
                                                &scaler_data->dst_handle, GFP_KERNEL);
    if (!scaler_data->dst_buffer) {
        pr_err("%s: Failed to allocate destination buffer\n", MODULE_NAME);
        dma_free_coherent(dev, ALIGNED_SRC_SIZE, 
                         scaler_data->src_buffer, scaler_data->src_handle);
        return -ENOMEM;
    }
    
    pr_info("%s: Source buffer allocated: virt=%p, phys=%pad, size=%u\n",
            MODULE_NAME, scaler_data->src_buffer, &scaler_data->src_handle, ALIGNED_SRC_SIZE);
    pr_info("%s: Destination buffer allocated: virt=%p, phys=%pad, size=%u\n",
            MODULE_NAME, scaler_data->dst_buffer, &scaler_data->dst_handle, ALIGNED_DST_SIZE);
    
    return 0;
}

// Platform driver remove function
static int scaler_platform_driver_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    
    if (scaler_data->src_buffer) {
        dma_free_coherent(dev, ALIGNED_SRC_SIZE, 
                         scaler_data->src_buffer, scaler_data->src_handle);
        pr_info("%s: Source buffer freed\n", MODULE_NAME);
    }
    
    if (scaler_data->dst_buffer) {
        dma_free_coherent(dev, ALIGNED_DST_SIZE, 
                         scaler_data->dst_buffer, scaler_data->dst_handle);
        pr_info("%s: Destination buffer freed\n", MODULE_NAME);
    }
    
    return 0;
}

// Platform driver structure
static struct platform_driver scaler_platform_driver = {
    .driver = {
        .name = MODULE_NAME,
    },
    .probe = scaler_platform_driver_probe,
    .remove = scaler_platform_driver_remove,
};

// Module initialization function
static int __init kernel_scaler_init(void)
{
    int ret;
    
    // Allocate module data structure
    scaler_data = kzalloc(sizeof(struct kernel_scaler_data), GFP_KERNEL);
    if (!scaler_data)
        return -ENOMEM;
    
    // Create proc file for interaction and results
    proc_file = proc_create(MODULE_NAME, 0644, NULL, &proc_fops);
    if (!proc_file) {
        pr_err("%s: Failed to create proc entry\n", MODULE_NAME);
        kfree(scaler_data);
        return -ENOMEM;
    }
    
    // Register platform device
    scaler_data->pdev = platform_device_alloc(MODULE_NAME, -1);
    if (!scaler_data->pdev) {
        pr_err("%s: Failed to allocate platform device\n", MODULE_NAME);
        remove_proc_entry(MODULE_NAME, NULL);
        kfree(scaler_data);
        return -ENOMEM;
    }
    
    scaler_data->pdev->dev.release = scaler_platform_device_release;
    ret = platform_device_add(scaler_data->pdev);
    if (ret) {
        pr_err("%s: Failed to add platform device\n", MODULE_NAME);
        platform_device_put(scaler_data->pdev);
        remove_proc_entry(MODULE_NAME, NULL);
        kfree(scaler_data);
        return ret;
    }
    
    // Register platform driver
    ret = platform_driver_register(&scaler_platform_driver);
    if (ret) {
        pr_err("%s: Failed to register platform driver\n", MODULE_NAME);
        platform_device_unregister(scaler_data->pdev);
        remove_proc_entry(MODULE_NAME, NULL);
        kfree(scaler_data);
        return ret;
    }
    
    pr_info("%s: Module loaded successfully\n", MODULE_NAME);
    
    // Optionally run the benchmark immediately on load
    // run_scaling_benchmark();
    
    return 0;
}

// Module cleanup function
static void __exit kernel_scaler_exit(void)
{
    platform_driver_unregister(&scaler_platform_driver);
    platform_device_unregister(scaler_data->pdev);
    remove_proc_entry(MODULE_NAME, NULL);
    kfree(scaler_data);
    
    pr_info("%s: Module unloaded successfully\n", MODULE_NAME);
}

module_init(kernel_scaler_init);
module_exit(kernel_scaler_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Kernel Space Image Scaling Benchmark");
