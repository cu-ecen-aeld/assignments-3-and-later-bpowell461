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
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("bpowell461"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *ad = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t off;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    if (mutex_lock_interruptible(&ad->mutex)) {
        return -ERESTARTSYS;
    }
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&ad->cbuf, *f_pos, &off);
    if (entry) {
        ssize_t sz = entry->size - off;
        if (sz > count) {
            sz = count;
        }
        if (copy_to_user(buf, entry->buffptr + off, sz) != 0) {
            retval = -EFAULT;
            mutex_unlock(&ad->mutex);
            return retval;
        }
        *f_pos += sz;
        retval = sz;
    }
    
    mutex_unlock(&ad->mutex);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *ad = filp->private_data;
    char *new_buf;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    if (mutex_lock_interruptible(&ad->mutex)) {
        return -ERESTARTSYS;
    }
    new_buf = kmalloc(ad->working_entry.size + count, GFP_KERNEL);
    if (!new_buf) {
        retval = -ENOMEM;
        goto exit;
    }
    if (ad->working_entry.size) {
        memcpy(new_buf, ad->working_entry.buffptr, ad->working_entry.size);
    }
    if (copy_from_user(new_buf + ad->working_entry.size, buf, count) != 0) {
        retval = -EFAULT;
        goto exit;
    }
    kfree(ad->working_entry.buffptr);
    ad->working_entry.buffptr = new_buf;
    ad->working_entry.size += count;
    retval = count;
    while (ad->working_entry.size > 0) {
        char *p = memchr(ad->working_entry.buffptr, '\n', ad->working_entry.size);
        struct aesd_buffer_entry entry;
        size_t sz;
        if (!p) {
            break;
        }
        sz = p + 1 - ad->working_entry.buffptr;
        entry.buffptr = ad->working_entry.buffptr;
        entry.size = sz;
        kfree(aesd_circular_buffer_add_entry(&ad->cbuf, &entry));
        if (ad->working_entry.size == sz) {
            ad->working_entry.buffptr = NULL;
            ad->working_entry.size = 0;
            break;
        } else {
            new_buf = kmemdup(ad->working_entry.buffptr + sz, ad->working_entry.size - sz, GFP_KERNEL); 
            if (!new_buf) {
                retval = -ENOMEM;
                goto exit;
            }
            ad->working_entry.buffptr = new_buf;
            ad->working_entry.size -= sz;
        }
    }  
exit:
    mutex_unlock(&ad->mutex);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
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
    mutex_init(&aesd_device.mutex);
    aesd_circular_buffer_init(&aesd_device.cbuf);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.cbuf,index) 
    {
        kfree(entry->buffptr);
    }
    kfree(aesd_device.working_entry.buffptr);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
