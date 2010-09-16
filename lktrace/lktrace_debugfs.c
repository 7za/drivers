#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>


#define LKTRACE_FUNCNAME_MAXLEN (32) 
#define LKTRACE_READBUFF_MAXLEN (512)

static LIST_HEAD(lktrace_probelist_head);

struct lktrace_probelist
{
	struct rcu_head		lkpl_rcu;
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

static void lktrace_init_probelist_elem(struct lktrace_probelist *const ptr,
					char  fname[],
					off_t off,
					char cbname[])
{
	strlcpy(ptr->lkpl_fname,  fname, sizeof(ptr->lkpl_fname));
	ptr->lkpl_offset = off;
	strlcpy(ptr->lkpl_cbname, fname, sizeof(ptr->lkpl_cbname));
}


static struct lktrace_probelist* lktrace_alloc_new_probelist_elem(void)
{
	struct lktrace_probelist *ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if(ret) {
		INIT_RCU_HEAD(  &ret->lkpl_rcu  );
		INIT_LIST_HEAD( &ret->lkpl_list );
		spin_lock_init( &ret->lkpl_lock );
	}
	return ret;
}

static int lktrace_debugfs_fops_open( struct inode *inode, struct file *f)
{
	/* open just get first link in list */
	struct lktrace_probelist *ptr;
	rcu_read_lock();
	ptr = list_first_entry_rcu(&lktrace_probelist_head, 
				struct lktrace_probelist,
				lkpl_list);
	printk("first addr = %p\n", ptr);
	f->private_data = ptr;

	rcu_read_unlock();
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
	if(walker == NULL) {
		return count;
	}

	rcu_read_lock();
	list_for_each_entry_continue_rcu(walker, 
					&lktrace_probelist_head, 
					lkpl_list) {
		printk("loop\n");
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
				count =  -ENOMEM;
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
	rcu_read_unlock();
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
		struct lktrace_probelist *elt = lktrace_alloc_new_probelist_elem();
		if(elt == NULL) {
			count = -ENOMEM;
			goto end_write;
		}

		lktrace_init_probelist_elem(elt, fname, off, cbname);
		printk("add elt addr=%p\n", elt);
		list_add_rcu(&elt->lkpl_list, &lktrace_probelist_head);
		count = bufflen;
	} else {
		printk(KERN_ERR "I/O error, sscanf return %d \n", ret);
		count = -EIO;
	}

end_write:
	kfree(tmpbuff);
	printk("%s : return %zd\n", __func__, count);
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

void lktrace_free_elem_rcu (struct rcu_head *head)
{
	struct lktrace_probelist *walker = container_of(head,
							struct lktrace_probelist,
							lkpl_rcu);

	kfree(walker);
}


void lktrace_destroy_debugfs(void)
{
	struct lktrace_probelist *walker;
	if(entry) {
		debugfs_remove(entry);
		entry = NULL;
	}
	if(main) {
		debugfs_remove(main);
		main = NULL;
	}
	list_for_each_entry_rcu(walker, 
			&lktrace_probelist_head,
			lkpl_list) {

		list_del_rcu( &walker->lkpl_list );
		call_rcu(&walker->lkpl_rcu, lktrace_free_elem_rcu);
	}
}


