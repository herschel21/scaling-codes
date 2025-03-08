#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "dma_mmap_device"
#define WIDTH 1920
#define HEIGHT 1080
#define PIXEL_SIZE 3
#define BUFFER_SIZE (WIDTH * HEIGHT * PIXEL_SIZE)

static int major;
static void *input_buffer;
static void *output_buffer;
static dma_addr_t input_dma_handle;
static dma_addr_t output_dma_handle;

static int device_open(struct inode *inode, struct file *file) {
    return 0;
}

static int device_release(struct inode *inode, struct file *file) {
    return 0;
}

static int device_mmap(struct file *file, struct vm_area_struct *vma) {
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long physical;

    if (offset == 0) {
        physical = virt_to_phys(input_buffer);
    } else if (offset == BUFFER_SIZE) {
        physical = virt_to_phys(output_buffer);
    } else {
        return -EINVAL;
    }

    if (remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT, BUFFER_SIZE, vma->vm_page_prot)) {
        return -EAGAIN;
    }

    return 0;
}

static struct file_operations fops = {
    .open = device_open,
    .release = device_release,
    .mmap = device_mmap,
};

static int __init dma_mmap_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        pr_err("Failed to register character device\n");
        return major;
    }

    input_buffer = dma_alloc_coherent(NULL, BUFFER_SIZE, &input_dma_handle, GFP_KERNEL);
    if (!input_buffer) {
        pr_err("Failed to allocate input buffer\n");
        unregister_chrdev(major, DEVICE_NAME);
        return -ENOMEM;
    }

    output_buffer = dma_alloc_coherent(NULL, BUFFER_SIZE, &output_dma_handle, GFP_KERNEL);
    if (!output_buffer) {
        pr_err("Failed to allocate output buffer\n");
        dma_free_coherent(NULL, BUFFER_SIZE, input_buffer, input_dma_handle);
        unregister_chrdev(major, DEVICE_NAME);
        return -ENOMEM;
    }

    pr_info("DMA mmap module loaded, major number: %d\n", major);
    return 0;
}

static void __exit dma_mmap_exit(void) {
    dma_free_coherent(NULL, BUFFER_SIZE, input_buffer, input_dma_handle);
    dma_free_coherent(NULL, BUFFER_SIZE, output_buffer, output_dma_handle);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("DMA mmap module unloaded\n");
}

module_init(dma_mmap_init);
module_exit(dma_mmap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("DMA mmap example module");
MODULE_VERSION("1.0");
