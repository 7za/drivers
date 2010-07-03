#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/rcupdate.h>
#include <asm/uaccess.h>


static DEFINE_SPINLOCK(bool_lock);
static DECLARE_WAIT_QUEUE_HEAD(bool_fileaccess_wait);
static unsigned long bool_filestate_changed = 0;


static int
lktrace_bool_fops_open (struct inode *inode, struct file *file)
{
        void *iprivate = inode->i_private;
    
        /* inode_i_private must have been filled during file_creation*/
        if(unlikely(iprivate == NULL)){
                    BUG_ON(iprivate == NULL);
                    return -EIO;
        }
        file->private_data = iprivate;
        return 0;
}


static int
lktrace_bool_fops_release(struct inode *inode, struct file *file)
{
        file->private_data = NULL;
        return 0;
}


static ssize_t
lktrace_bool_fops_read( struct file *fs, 
                        char __user *ubuf, 
                        size_t      size, 
                        loff_t *where)
{
        char c;
        unsigned long flag;
        int *ptr = fs->private_data;
        int ret;

		*where = 0;

        if(size < 1){
                return -ENOMEM;
        }
        if(!ptr){
                return -EIO;
        }
        
        spin_lock_irqsave(&bool_lock, flag);
        c = *ptr + '0';    
        spin_unlock_irqrestore(&bool_lock, flag);

        if(unlikely(c != '0' && c != '1')){
                return -EINVAL;
        }

        ret = simple_read_from_buffer(ubuf, size, where, &c, 1);
        if(ret > 0){
                clear_bit( 0, &bool_filestate_changed);
        }
        return ret;
}
        

static ssize_t
lktrace_bool_fops_write(    struct file *file,
                            char const __user *ubuff,
                            size_t      size,
                            loff_t      *where)
{
        char c;
        int val;
        unsigned long flag;
        int *ptr;
         
        if(copy_from_user(&c, ubuff, 1)){
                return -EACCES;
        }
        
        if(c != '0' && c!= '1'){
                return -EINVAL;
        }
        val = c - '0';
        spin_lock_irqsave(&bool_lock, flag);
        ptr = file->private_data;
        if(*ptr != val){
                *ptr = val;
                bool_filestate_changed = 1;
				wake_up_interruptible(&bool_fileaccess_wait);
        }
        spin_unlock_irqrestore(&bool_lock, flag);
        return size;
}


static unsigned int
lktrace_bool_fops_poll(struct file *file, poll_table *wait)
{
	    unsigned long ready;
		unsigned long flag;
		poll_wait(file, &bool_fileaccess_wait, wait);
		spin_lock_irqsave(&bool_lock, flag);
		ready = bool_filestate_changed;
		spin_unlock_irqrestore(&bool_lock, flag);

		/* Availability of data is detected from interrupt context */
		if(ready){
		        return POLLIN;
		}
		return  POLLOUT;
}



static struct file_operations lktrace_bool_fops = {
	    .open       =   lktrace_bool_fops_open,
		.release    =   lktrace_bool_fops_release,
		.read       =   lktrace_bool_fops_read,
		.write      =   lktrace_bool_fops_write,
		.poll       =   lktrace_bool_fops_poll,
		.owner      =   THIS_MODULE,
};


extern struct dentry*
lktracefs_create_file(  struct super_block*, 
                        struct dentry*,
                        char *const,
                        struct file_operations*,
                        int );



int
lktracefile_create_enable_file( struct super_block  *sb,
                                struct dentry       *root,
                                int                 *associated_data)
{
        struct dentry *file = lktracefs_create_file(sb,
                                                    root,
                                                    "enable",
                                                    &lktrace_bool_fops,
                                                    S_IFREG | 0644);
        if(file == NULL){
                return -1;
        }
        file->d_inode->i_private = associated_data;
        return 0;
}

