#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/gfp.h>
#include <asm/uaccess.h>


#define LKTRACE_READBUFF_MAXLEN (512)

struct lktrace_probelist
{
		struct list_head	lkpl_next;
		struct rcu_head		lkpl_rcu;
		char				lkpl_where[12];
		char				lkpl_cbfunc[LKTRACE_READBUFF_MAXLEN/2];
		spinlock_t			lkpl_lock;
};

static LIST_HEAD(lktrace_probelist_head);

static int 
lktrace_debugfs_fops_open(struct inode *, struct file *);

static int 
lktrace_debugfs_fops_release(struct inode *, struct file *);

static ssize_t
lktrace_debugfs_fops_read(struct file *, char __user *, size_t, loff_t* );

static ssize_t
lktrace_debugfs_fops_write(struct file *, char const __user *, size_t, loff_t*);

static struct file_operations lktrace_debugfs_fops = {
		.read		=	lktrace_debugfs_fops_read,
		.write		=	lktrace_debugfs_fops_write,
		.open		=	lktrace_debugfs_fops_open,
		.release	=	lktrace_debugfs_fops_release,
};


static struct lktrace_probelist*
lktrace_alloc_new_probelist_elem(void)
{
		struct lktrace_probelist *ret = kmalloc(sizeof(*ret), GFP_KERNEL);
		if(ret){
				INIT_LIST_HEAD( &ret->lkpl_next );
				INIT_RCU_HEAD(  &ret->lkpl_rcu  );
				spin_lock_init( &ret->lkpl_lock );
		}
		return ret;
}

static int
lktrace_debugfs_fops_open( struct inode *inode, struct file *f)
{
		struct lktrace_probelist *ptr;
		rcu_read_lock();
		ptr = list_first_entry(&lktrace_probelist_head, 
								struct lktrace_probelist,
								lkpl_next);
		f->private_data = ptr;

		rcu_read_unlock();
		return 0;
}


static int
lktrace_debugfs_fops_release( struct inode *inode, struct file *f)
{
		f->private_data = 0;
		return 0;
}



static ssize_t
lktrace_debugfs_fops_read(	struct file *file,
							char __user *ubuff,
							size_t		bufflen,
							loff_t		*loff)
{
		char buff[512];
		size_t remaining = bufflen;
		size_t count = 0;
		int ret;
		struct lktrace_probelist *walker =  file->private_data;
		
		if(walker == NULL){
				return 0;
		}

		rcu_read_lock();
		list_for_each_entry_continue_rcu(walker, 
										&lktrace_probelist_head, 
										lkpl_next){
				int this_shot;
				if(remaining <= 0){
						return count;
				}
				ret = snprintf(	buff, 
								sizeof(buff), 
								"%p %s\n", 
								walker->lkpl_where, 
								walker->lkpl_cbfunc);	
				this_shot = min((int)ret, (int)remaining);
				if(copy_to_user(ubuff + count, buff, this_shot)){
						rcu_read_unlock();
						return -ENOMEM;
				}
				remaining -= this_shot;
				count	  += this_shot;
		}
		rcu_read_unlock();
		file->private_data = NULL;
		return (ssize_t)count;
}


static ssize_t
lktrace_debugfs_fops_write(	struct file *file,
							const char __user *ubuff,
							size_t		bufflen,
							loff_t		*loff)
{
		char line[LKTRACE_READBUFF_MAXLEN];
		char funcname[LKTRACE_READBUFF_MAXLEN/2];
		char funcaddr[12];
		unsigned long ret;
		size_t max_read = (bufflen > ARRAY_SIZE(line))? ARRAY_SIZE(line) : bufflen;


		ret = copy_from_user(line, ubuff, max_read);
		if(unlikely(ret != 0)){
				printk(KERN_ERR "unable to read entry\n");
				return -EIO;
		}
		ret = sscanf(line, "%s %s", funcaddr, funcname);
		if(ret == 2){
				struct lktrace_probelist *elt = lktrace_alloc_new_probelist_elem();
				if(elt == NULL){
						return -ENOMEM;
				}
			
				strncpy(elt->lkpl_where,  funcaddr, sizeof(elt->lkpl_where));
				strncpy(elt->lkpl_cbfunc, funcname, sizeof(elt->lkpl_cbfunc));
				list_add_rcu(&elt->lkpl_next, &lktrace_probelist_head);
				return max_read;
		} else {
				printk(KERN_ERR "I/O error, sscanf return %lu \n", ret);
		}
		return -EIO;
}

static struct dentry *entry	= NULL;
static struct dentry *main	= NULL;

int
lktrace_create_debugfs(void *data)
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


void
lktrace_destroy_debugfs(void)
{
		struct lktrace_probelist *walker;
		if(main){
				debugfs_remove(main);
				main = NULL;
		}
		if(entry){
				debugfs_remove(entry);
				entry = NULL;
		}
		list_for_each_entry(walker, 
							&lktrace_probelist_head,
							lkpl_next){
				list_del_rcu( &walker->lkpl_next );
				call_rcu(&walker->lkpl_rcu, lktrace_free_elem_rcu);
		}
}



