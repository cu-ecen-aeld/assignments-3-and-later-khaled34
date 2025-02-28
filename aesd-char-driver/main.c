/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

#define SUCCESS   0
#define MIN(a,b)  (((a) < (b))? (a):(b));

MODULE_AUTHOR("Khaled Ahmed Ali (khaled34)"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/* This function is used to handle the file offset of the filp correctly */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset) 
{

    struct aesd_dev *ptr_aesd_dev = filp->private_data;
    long retval = SUCCESS;
    int virt_dev_len_till_write_cmd = 0;
    uint8_t index;

    PDEBUG("aesd_adjust_file_offset cmd %u, offset %u\n", write_cmd,write_cmd_offset);
    
    if (mutex_lock_interruptible(&ptr_aesd_dev->virt_device_lock)) 
    {
        PDEBUG("aesd_adjust_file_offset Failure: Waiting mutex\n");
        retval = -ERESTARTSYS;
        goto func_exit;
    }
    
    /* validate the passed parameters*/
    if ((write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) ||
        (ptr_aesd_dev->virt_device.entry[write_cmd].size == 0) || 
        (ptr_aesd_dev->virt_device.entry[write_cmd].size < write_cmd_offset))
    {
        PDEBUG("aesd_adjust_file_offset Failure: seeking out of range memory\n");
        retval = -EINVAL;
        goto func_unlock;
    }

    if (write_cmd < ptr_aesd_dev->virt_device.out_offs) 
    {
        /* Handle case of wrapping */
        write_cmd += AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    /* Start with out_offs to the write_cmd = 11 , out = 2  */
    for (index = ptr_aesd_dev->virt_device.out_offs; index < write_cmd ; index++) 
    {
        virt_dev_len_till_write_cmd += ptr_aesd_dev->virt_device.entry[index % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size;
        PDEBUG("aesd_adjust_file_offset: Command index %u, command size %d, curr_total %d\n",
               (index % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED), 
               ptr_aesd_dev->virt_device.entry[index % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size, 
               virt_dev_len_till_write_cmd);
    }

    /* Update the fpos by the actual seek */
    filp->f_pos = virt_dev_len_till_write_cmd + write_cmd_offset;
    PDEBUG("aesd_adjust_file_offset: NEW FILE POSITION IS %d\n", filp->f_pos);

func_unlock:
    mutex_unlock(&ptr_aesd_dev->virt_device_lock);
func_exit:
    return retval;

}

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev* ptr_aesd_device;
    PDEBUG("open");
    /* Remember that: [Please refer to the scull implmenetation that is provided in the ldd]
     *
     *      - inode structure contains the cdev that you linked in the initialization that is inside to the aesd_device strcut
     *        to get the device we will use the container_of macro to get the device to open
     * 
     *      - Then we need to save the device pointer into the filp
     *      
     */
    
    ptr_aesd_device = container_of(inode->i_cdev, struct aesd_dev, cdev);

    if (ptr_aesd_device == NULL)
    {
        PDEBUG("Open Operation Failure: Fatal Issue device memory not found\n");
        return -ENOMEM;
    }

    filp->private_data = ptr_aesd_device;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    /* As no allocation made in the aesd_open then nothing to be done in the aesd_release 
       Everything is done in the initialization (init_module)*/
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_buffer_entry * aesd_buff_entry = NULL;
    struct aesd_dev *ptr_aesd_device           = filp->private_data;
    ssize_t retval                             = 0;
    size_t entry_offset                        = 0;
    size_t read_bytes                          = 0;
    /* Prepare the seek*/
    if (*f_pos == 0)
    {
        *f_pos += filp->f_pos;
    }
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    
    if (mutex_lock_interruptible(&ptr_aesd_device->virt_device_lock))
    {
        PDEBUG("Read Operation Failure: Wait for Mutex\n");
		retval = -ERESTARTSYS;
        goto func_exit;
    }
    /* NOTE THAT : Read implementation is as described in the session that the driver will handle the single entry read and 
    it is the user space app responsibility to call this read multiple times*/
    aesd_buff_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&ptr_aesd_device->virt_device, *f_pos, &entry_offset);
    if (aesd_buff_entry == NULL)
    { 
        /* Entry Not found */
        PDEBUG("Read Operation Failure: Entry Not Found in the Virtual device\n");
        goto func_unlock;
    }
    /* Get MIN(available in the virtual device entry , requiested from the user space)*/
    read_bytes = MIN((aesd_buff_entry->size - entry_offset),count);
    /* Returns 0 → Success (all bytes copied).
     * Returns > 0 → Partial copy (some bytes not copied).
     * Never returns -1 or negative values. 
     * 
     * in case of partial copy return with failure
     * */
    if (copy_to_user(buf, aesd_buff_entry->buffptr + entry_offset, read_bytes)) 
    {
        PDEBUG("Read Operation Failure: Can't fully copy to the user space memory\n");
        retval = -EFAULT;
        goto func_unlock;
    }
    /* Update the f_pos with the read bytes in case of successful read */
    *f_pos += read_bytes;
    retval = read_bytes;

func_unlock:
    mutex_unlock(&ptr_aesd_device->virt_device_lock);
func_exit:
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *ptr_aesd_device           = filp->private_data;
    ssize_t retval                             = 0;
    size_t  required_new_mem                   = 0 ;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    if (mutex_lock_interruptible(&ptr_aesd_device->virt_device_lock)) 
    {
        PDEBUG("Write Operation Failure: Wait for Mutex\n");
        retval = -ERESTARTSYS;
        goto func_exit;
    }
    /* Allocate initialized memory to be used for write
       in case the entry is used before but the \n isn't received yet then we need to realloc the dynamic memory
       of the buffer entry with the old size + the newly passed one  */
    if (ptr_aesd_device->buffer_entry.size == 0) 
    {
        PDEBUG("new write entry\n");
        ptr_aesd_device->buffer_entry.buffptr = kmalloc(count, GFP_KERNEL);
    } 
    else 
    {
        PDEBUG("modifying old entry\n");
        required_new_mem = ptr_aesd_device->buffer_entry.size + count;
        ptr_aesd_device->buffer_entry.buffptr = krealloc(ptr_aesd_device->buffer_entry.buffptr, required_new_mem , GFP_KERNEL);

    }
    /* Memory Check */
    
    if (ptr_aesd_device->buffer_entry.buffptr == 0)
   	{
        PDEBUG("Write Operation Failure: Not Enough memory issue\n");
        retval = -ENOMEM;
        goto func_unlock; /* retval is already initialized to -ENOMEM*/
    }
    /* copy memory from user space to kernel space starting from the previous size */
    if (copy_from_user((void *)(&ptr_aesd_device->buffer_entry.buffptr[ptr_aesd_device->buffer_entry.size]), buf, count))
    {
        PDEBUG("Write Operation Failure: Can't fully copy from the user space memory\n");
        retval = -EFAULT;
        goto func_unlock;
    }
    /* Update the size */
    ptr_aesd_device->buffer_entry.size += count;
    retval = count;
    *f_pos += retval;
    /* Check if the last received char is \n then add the buffer entry to the virtual circular buffer */
    if (strchr(ptr_aesd_device->buffer_entry.buffptr, '\n') != 0)
    {
        aesd_circular_buffer_add_entry(&ptr_aesd_device->virt_device, &ptr_aesd_device->buffer_entry);
        // reset the buffer_entry 
        ptr_aesd_device->buffer_entry.buffptr = NULL;
        ptr_aesd_device->buffer_entry.size    = 0;
    }
func_unlock:
    mutex_unlock(&ptr_aesd_device->virt_device_lock);
func_exit:
    return retval;
}

/* As mentioned in the sessions we will start with the implementation of fixed size llseek */
loff_t aesd_llseek(struct file *filp, loff_t off, int whence) 
{
    struct aesd_dev *ptr_aesd_device = filp->private_data;
    struct aesd_buffer_entry* entry = NULL;
    size_t virtual_dev_total_len = 0;
    loff_t retval = 0;
    uint8_t index = 0;

    PDEBUG("llseek %llu, whence %d\n", off, whence);

    if (mutex_lock_interruptible(&(ptr_aesd_device->virt_device_lock))) 
    {
        PDEBUG("Llseek Failure: Waiting Mutex\n");
        retval = -ERESTARTSYS;
        goto func_exit;
    }
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.virt_device,index) 
    {
        virtual_dev_total_len += entry->size;
    }
    
    mutex_unlock(&(ptr_aesd_device->virt_device_lock));
    retval = fixed_size_llseek(filp, off, whence, virtual_dev_total_len);
func_exit:
    return retval;
}


long int aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long passed_seek)
{
    struct aesd_dev * ptr_aesd_device = filp->private_data;
    struct aesd_seekto kernel_seek;
    struct aesd_buffer_entry *entry = NULL;
    size_t entry_offset = 0;
    
    long int retval =  SUCCESS;
    PDEBUG("ioctl function: Start\n");

    switch (cmd)
    {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&kernel_seek, (void __user *)passed_seek, sizeof(struct aesd_seekto)))
            {
                PDEBUG("aesd_unlocked_ioctl Operation Failure: can't copy to kernel space\n");
                retval = -EFAULT;
                goto func_exit;
            }
            else
            {
                PDEBUG("ioctl function: Copy from user space %d, %d\n", kernel_seek.write_cmd, kernel_seek.write_cmd_offset);
                retval = aesd_adjust_file_offset(filp, kernel_seek.write_cmd, kernel_seek.write_cmd_offset);
                if (retval != SUCCESS)
                {
                    PDEBUG("aesd_unlocked_ioctl Operation Failure: aesd_adjust_file issue\n");
                    goto func_exit;
                }
            }
            break;
        
        default:
            retval = -EINVAL;
            goto func_exit;

    }

func_exit:
    return retval;
}

struct file_operations aesd_fops = {
    .owner          =    THIS_MODULE,
    .read           =    aesd_read,
    .write          =    aesd_write,
    .open           =    aesd_open,
    .release        =    aesd_release,
    .llseek         =    aesd_llseek,
    .unlocked_ioctl =    aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.virt_device);
    aesd_device.buffer_entry.buffptr = NULL;
    aesd_device.buffer_entry.size    = 0;
    mutex_init(&aesd_device.virt_device_lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry = NULL;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    /*  Free all virtual device memory */
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.virt_device,index) 
    {
#ifdef __KERNEL__
       kfree((void*)entry->buffptr);
#else
       free((void*)entry->buffptr);
#endif 
    }
    /*  Free any uncompleted memory */
    if (aesd_device.buffer_entry.buffptr != NULL)
    {
#ifdef __KERNEL__
        kfree((void*)aesd_device.buffer_entry.buffptr);
#else
        free((void*)aesd_device.buffer_entry.buffptr);
#endif 
    }
    
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
