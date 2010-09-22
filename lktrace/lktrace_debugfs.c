#include <linux/fs.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>


#define LKTRACE_FUNCNAME_MAXLEN (32) 
#define LKTRACE_READBUFF_MAXLEN (512)

static LIST_HEAD(lktrace_probelist_head);
static DEFINE_MUTEX(lktrace_probelist_mutex);

struct lktrace_probelist
{
	struct list_head	lkpl_list;
	spinlock_t		lkpl_lock;

	char			lkpl_fname[LKTRACE_FUNCNAME_MAXLEN];
	off_t			lkpl_offset;
	char			lkpl_cbname[LKTRACE_FUNCNAME_MAXLEN];

	struct kprobe		lkpl_probe;
};


static int lktrace_debugfs_fops_open(struct inode *, struct file *);

static int lktrace_debugfs_fops_release(struct inode *, struct file *);

static ssize_t lktrace_debugfs_fops_read(struct file *,
					char __user *,
					size_t,
					loff_t* );

static ssize_t lktrace_debugfs_fops_write(struct file *,
					char const __user *,
					size_t,
					loff_t*);

static struct file_operations lktrace_debugfs_fops = {
	.read		=	lktrace_debugfs_fops_read,
	.write		=	lktrace_debugfs_fops_write,
	.open		=	lktrace_debugfs_fops_open,
	.release	=	lktrace_debugfs_fops_release,
};

static int lktrace_init_probelist_elem(struct lktrace_probelist *const ptr,
					char  fname[],
					off_t off,
					char cbname[])
{
	int ret;
	strlcpy(ptr->lkpl_fname,  fname, sizeof(ptr->lkpl_fname));
	ptr->lkpl_offset = off;
	strlcpy(ptr->lkpl_cbname, fname, sizeof(ptr->lkpl_cbname));

	ptr->lkpl_probe.addr = (kprobe_opcode_t*)kallsyms_lookup_name(fname);
	if(ptr->lkpl_probe.addr == NULL) {
		printk("error, can't resolv %s func\n", fname);
		return -EINVAL; 
	} else {
		printk("ok, we resolv %s func at %p addr\n", fname, 
							ptr->lkpl_probe.addr);
	}

	ptr->lkpl_probe.addr += off;

	ptr->lkpl_probe.pre_handler = (kprobe_pre_handler_t )
						kallsyms_lookup_name(cbname);

	if(ptr->lkpl_probe.pre_handler == NULL) {
		printk("error, can't resolv %s handler\n", cbname);
		return -EINVAL;
	} else {
		printk("ok we resolv %s func at %p addr\n", cbname,
						ptr->lkpl_probe.pre_handler);
	}

	ret = register_kprobe(&ptr->lkpl_probe);

	if(ret < 0) {
		printk("can't register kprobe: cause = %d\n", ret);
	}

	return ret;
}


static struct lktrace_probelist* lktrace_alloc_new_probelist_elem(void)
{
	struct lktrace_probelist *ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if(ret) {
		INIT_LIST_HEAD( &ret->lkpl_list );
		spin_lock_init( &ret->lkpl_lock );
	}
	return ret;
}

static int lktrace_debugfs_fops_open( struct inode *inode, struct file *f)
{
	/* open just get first link in list */
	struct lktrace_probelist *ptr;
	mutex_lock(&lktrace_probelist_mutex);
	ptr = list_first_entry(&lktrace_probelist_head, 
				struct lktrace_probelist,
				lkpl_list);
	mutex_unlock(&lktrace_probelist_mutex);
	f->private_data = ptr;

	return 0;
}

static int lktrace_debugfs_fops_release( struct inode *inode, struct file *f)
{
	f->private_data = NULL;
	return 0;
}

static ssize_t lktrace_debugfs_fops_read(struct file *file,
					char __user  *ubuff,
					size_t	bufflen,
					loff_t	*loff)
{
	char buff[512];
	size_t remaining = bufflen;
	ssize_t count = 0;
	int ret = 0;
	/* get current item */
	struct lktrace_probelist *walker =  file->private_data;
	
	/* return EOF */
	if(walker == NULL || &walker->lkpl_list == &lktrace_probelist_head) {
		return count;
	}

	mutex_lock(&lktrace_probelist_mutex);
	list_for_each_entry(walker, 
			&lktrace_probelist_head, 
			lkpl_list) {
		if(remaining <= 0) {
			goto end_read;
		}
		ret = snprintf(	buff, 
				sizeof(buff), 
				"%s+%ld %s\n", 
				walker->lkpl_fname, 
				walker->lkpl_offset,
				walker->lkpl_cbname);	
		
		// can we add ret bytes in buffer? 
		if(ret < remaining) {
			if(copy_to_user(ubuff + count, buff, ret)) {
				count =  -EIO;
				goto end_read;
			}
		} else {
			// no space left
			goto end_read;
		}

		remaining -= ret;
		count	  += ret;
	}

end_read:	
	mutex_unlock(&lktrace_probelist_mutex);
	file->private_data = walker;
	return count;
}

static ssize_t lktrace_debugfs_fops_write(struct file *file,
					const char __user *ubuff,
					size_t	bufflen,
					loff_t	*loff)
{
	char *tmpbuff;
	size_t alloclen = bufflen - (size_t) *loff;
	int ret;
	ssize_t count = 0;
	char fname[LKTRACE_FUNCNAME_MAXLEN], cbname[LKTRACE_FUNCNAME_MAXLEN];
	off_t off;

	tmpbuff = kzalloc(alloclen, GFP_KERNEL);

	if(unlikely(tmpbuff == NULL)) {
		printk("can't allocate buffer to get user data\n");
		return -ENOMEM;
	}

	ret = copy_from_user(tmpbuff, ubuff, bufflen);
	if(unlikely(ret != 0)){
		printk(KERN_ERR "unable to read entry\n");
		count = -EIO;
		goto end_write;
	}

	ret = sscanf(	tmpbuff, 
			"%31s %lx %31s", 
			fname,
			&off,
			cbname);
	if(ret == 3) {
		int hasprobe;
		struct lktrace_probelist *elt = lktrace_alloc_new_probelist_elem();
		if(elt == NULL) {
			count = -ENOMEM;
			goto end_write;
		}

		hasprobe = lktrace_init_probelist_elem(elt, fname, off, cbname);
		if(hasprobe < 0) {
			count = hasprobe;
			goto end_write;
		}
		mutex_lock(&lktrace_probelist_mutex);
		list_add(&elt->lkpl_list, &lktrace_probelist_head);
		mutex_unlock(&lktrace_probelist_mutex);
		count = bufflen;
	} else {
		printk(KERN_ERR "I/O error, sscanf return %d \n", ret);
		count = -EIO;
	}

end_write:
	kfree(tmpbuff);
	printk("%s : return %zd (expected=%zd)\n", __func__, count, bufflen);
	return count;
}

static struct dentry *entry	= NULL;
static struct dentry *main	= NULL;

int lktrace_create_debugfs(void *data)
{
		/*create lktrace directory*/
	int ret = 0;
		
	main = debugfs_create_dir("lktrace", NULL);
	if(unlikely(main == NULL)){
		printk(KERN_ERR "unable to create lktrace dir\n");
				//return -1;
	}

	entry = debugfs_create_file("list", 0666, main ,data, &lktrace_debugfs_fops);
	if(unlikely(entry == NULL)){
		printk(KERN_ERR "unable to create entry file\n");
		debugfs_remove(main);
		ret = -1;
	}
	return ret;
}

void lktrace_destroy_debugfs(void)
{
	struct lktrace_probelist *walker = NULL, *tmp = NULL;
	if(entry) {
		debugfs_remove(entry);
		entry = NULL;
	}
	if(main) {
		debugfs_remove(main);
		main = NULL;
	}
	mutex_lock(&lktrace_probelist_mutex);
	list_for_each_entry_safe(walker,
				tmp,
				&lktrace_probelist_head,
				lkpl_list) {
		if(walker->lkpl_probe.addr)
			unregister_kprobe(&walker->lkpl_probe);
		kfree(walker);
	}
	mutex_unlock(&lktrace_probelist_mutex);
}


