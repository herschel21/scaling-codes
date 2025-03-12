/***************************************************************************//**
*  \file       driver.c
*
*  \details    Simple Linux device driver (Real Linux Device Driver)
*              Modified to use DMA coherent memory
*
*  \author     EmbeTronicX
*
*  \Tested with Linux raspberrypi 5.10.27-v7l-embetronicx-custom+
*
*******************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>                 //kmalloc()
#include <linux/uaccess.h>              //copy_to/from_user()
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>          // For dma_alloc_coherent
 
#define mem_size        ((1920*1080*3) + 4096)           //Memory Size
 
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
uint8_t *kernel_buffer;
uint8_t *output_buffer;
dma_addr_t kernel_buffer_dma;
dma_addr_t output_buffer_dma;
struct device *etx_device;

/*
** Function Prototypes
*/
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static int      etx_mmap(struct file *filp, struct vm_area_struct *vma);

/*
** File Operations structure
*/
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
        .mmap           = etx_mmap,
};
 
/*
** This function will be called when we open the Device file
*/
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("etx_open: Device File Opened...!!!\n");
        return 0;
}

/*
** This function will be called when we close the Device file
*/
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("etx_release: Device File Closed...!!!\n");
        return 0;
}

/*
** This function will be called when we read the Device file
*/
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("etx_read: Read operation started. Requested len = %zu\n", len);

        if( copy_to_user(buf, kernel_buffer, mem_size) )
        {
                pr_err("etx_read: Failed to copy data from kernel to user\n");
                return -EFAULT;
        }

        pr_info("etx_read: Data Read: %d bytes sent to user\n", mem_size);
        return mem_size;
}

/*
 * This function will be called when we map the device to user space
 */
static int etx_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    phys_addr_t phys_addr;
    int ret;

    pr_info("etx_mmap: mmap operation started, vma->vm_pgoff = %ld\n", vma->vm_pgoff);

    // Select the correct buffer based on vma->vm_pgoff
    if (vma->vm_pgoff == 0) {
        phys_addr = kernel_buffer_dma;
        pr_info("etx_mmap: Mapping kernel_buffer at physical address 0x%pap\n", &phys_addr);
    } else {
        phys_addr = output_buffer_dma;
        pr_info("etx_mmap: Mapping output_buffer at physical address 0x%pap\n", &phys_addr);
    }

    // Set cache flags - no caching for DMA memory
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    
    // Map the DMA memory to userspace
    ret = remap_pfn_range(vma, 
                        vma->vm_start, 
                        phys_addr >> PAGE_SHIFT, 
                        size, 
                        vma->vm_page_prot);
    if (ret) {
        pr_err("etx_mmap: remap_pfn_range failed, error %d\n", ret);
        return -EAGAIN;
    }

    pr_info("etx_mmap: Memory successfully mapped to user space (vma->vm_start = %lx, size = %lu)\n", 
            vma->vm_start, size);
    return 0;
}

/*
** This function will be called when we write the Device file
*/
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("etx_write: Write operation started. Requested len = %zu\n", len);

        // Copy the data to kernel space from the user-space
        if( copy_from_user(kernel_buffer, buf, len) )
        {
                pr_err("etx_write: Failed to copy data from user to kernel\n");
                return -EFAULT;
        }

        pr_info("etx_write: Data copied to kernel buffer. Copying %d bytes to output buffer\n", mem_size);
        
        memcpy(output_buffer, kernel_buffer, mem_size);

        pr_info("etx_write: Data Write: %d bytes written to output buffer\n", mem_size);
        return len;
}

/*
** Module Init function
*/
static int __init etx_driver_init(void)
{
        pr_info("etx_driver_init: Initializing the etx driver\n");

        /* Allocating Major number */
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
                pr_err("etx_driver_init: Cannot allocate major number\n");
                return -1;
        }
        pr_info("etx_driver_init: Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        /* Creating cdev structure */
        cdev_init(&etx_cdev, &fops);

        /* Adding character device to the system */
        if((cdev_add(&etx_cdev, dev, 1)) < 0) {
            pr_err("etx_driver_init: Cannot add the device to the system\n");
            goto r_class;
        }

        /* Creating struct class */
        if(IS_ERR(dev_class = class_create("etx_class"))) {
            pr_err("etx_driver_init: Cannot create the struct class\n");
            goto r_class;
        }

        /* Creating device */
        if(IS_ERR(etx_device = device_create(dev_class, NULL, dev, NULL, "etx_device"))) {
            pr_err("etx_driver_init: Cannot create the Device\n");
            goto r_device;
        }

        /* Set DMA mask for device */
        if (dma_set_mask_and_coherent(etx_device, DMA_BIT_MASK(32))) {
            pr_err("etx_driver_init: No suitable DMA available\n");
            goto r_device;
        }

        /* Allocating coherent DMA memory for kernel buffer */
        kernel_buffer = dma_alloc_coherent(etx_device, mem_size, &kernel_buffer_dma, GFP_KERNEL);
        if(kernel_buffer == NULL) {
            pr_err("etx_driver_init: Cannot allocate DMA memory for kernel buffer\n");
            goto r_device;
        }
        pr_info("etx_driver_init: Allocated kernel_buffer at virt=0x%p, phys=0x%pad\n", 
                kernel_buffer, &kernel_buffer_dma);

        /* Allocating coherent DMA memory for output buffer */
        output_buffer = dma_alloc_coherent(etx_device, mem_size, &output_buffer_dma, GFP_KERNEL);
        if(output_buffer == NULL) {
            pr_err("etx_driver_init: Cannot allocate DMA memory for output buffer\n");
            goto r_output_buffer;
        }
        pr_info("etx_driver_init: Allocated output_buffer at virt=0x%p, phys=0x%pad\n", 
                output_buffer, &output_buffer_dma);
        
        pr_info("etx_driver_init: Device Driver Insert...Done!!!\n");
        return 0;

r_output_buffer:
        dma_free_coherent(etx_device, mem_size, kernel_buffer, kernel_buffer_dma);
r_device:
        device_destroy(dev_class, dev);
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev, 1);
        pr_err("etx_driver_init: Device Driver Insert failed\n");
        return -1;
}

/*
** Module exit function
*/
static void __exit etx_driver_exit(void)
{
    pr_info("etx_driver_exit: Removing the etx driver\n");

    dma_free_coherent(etx_device, mem_size, kernel_buffer, kernel_buffer_dma);
    dma_free_coherent(etx_device, mem_size, output_buffer, output_buffer_dma);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&etx_cdev);
    unregister_chrdev_region(dev, 1);

    pr_info("etx_driver_exit: Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (Real Linux Device Driver) with DMA coherent memory");
MODULE_VERSION("1.5");
