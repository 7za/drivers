#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/spinlock_types.h>


#define LKTRACE_FSNAME "lktracefs"
#define LKTRACE_FSMAGIC 0xef1244dd 


static struct lktrace_state
{
		int lk_enabled;
}		lktrace_state = { .lk_enabled = 0 };


static int
lktracefs_get_super(struct file_system_type		*fs,
					int							flags,
					const char					*devname,
					void						*mountoption,
					struct vfsmount				*vfsmount);



static struct inode*
lktracefs_create_inode(struct super_block *sb, int mode)
{
		struct inode *ret = new_inode(sb);
		if(likely(ret)){
				ret->i_mode		=	mode;
				ret->i_uid		=	0;
				ret->i_gid		=	0;
				ret->i_atime	=	CURRENT_TIME;
				ret->i_mtime	=	CURRENT_TIME;
				ret->i_ctime	=	CURRENT_TIME;
		}
		return ret;
}

static struct super_operations lktrace_sb_fops = {
		.statfs		=	simple_statfs,
		.drop_inode	=	generic_delete_inode,
};


static struct dentry*
lktracefs_create_file(	struct super_block		*sb,
						struct dentry			*rootdir,
						char   const			*const fname,
						struct file_operations	*const fops,
						int						perm)
{
		struct dentry	*file;
		struct inode	*inode;

		/* add file entry in directory */
		file	=	d_alloc_name(rootdir, fname);
		if(!file){
				return NULL;
		}

		inode	=	lktracefs_create_inode(sb, perm);
		if(!inode){
				dput(file);
				return NULL;
		}

		inode->i_fop = fops;
		d_add(file, inode);
		return file;
}


static struct dentry*
lktracefs_create_dir(	struct super_block      *sb,	
						struct dentry			*rootdir,
						char   const			*const fname)
{
		struct dentry	*file;
		struct inode	*inode;

		/* add file entry in directory */
		file	=	d_alloc_name(rootdir, fname);
		if(!file){
				return NULL;
		}

		inode	=	lktracefs_create_inode(sb, S_IFDIR | 0755);
		if(!inode){
				dput(file);
				return NULL;
		}
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		d_add(file, inode);
		return file;
}

DEFINE_SPINLOCK(lktrace_state_lock);

static ssize_t
lktracefs_bool_fops_read(	struct file *file,
							char __user *buf,
							size_t count,
							loff_t *offset)
{
		unsigned long val;
		int copied_val, ret;
		char tmpbuff[2];
		

		spin_lock_irqsave(&lktrace_state_lock, val);
		copied_val = lktrace_state.lk_enabled;
		spin_unlock_irqrestore(&lktrace_state_lock, val);

		ret = snprintf(tmpbuff, sizeof(tmpbuff), "%d", copied_val);
		return simple_read_from_buffer(buf, count, offset, tmpbuff, ret);
}


static ssize_t
lktracefs_bool_fops_write(	struct file *file,
							char const __user *buf,
							size_t count, 
							loff_t *offset)
{
		char c;
		unsigned long value;
		int val;

		if (copy_from_user(&c, buf, 1)){
				return -EFAULT;
		}
		
		val = c - '0';
		if(val != 0 && val != 1){
				return -EINVAL;
		}

		spin_lock_irqsave(&lktrace_state_lock, value);
		lktrace_state.lk_enabled = val;
		spin_unlock_irqrestore(&lktrace_state_lock, value);

		return count;
}


static struct file_operations lktracefs_bool_fops = {
		.read		=	lktracefs_bool_fops_read,
		.write		=	lktracefs_bool_fops_write,
};


static struct file_operations lktracefs_data_fops = {
		.read		=	lktracefs_bool_fops_read,
		.write		=	lktracefs_bool_fops_write,
};


static void
lktracefs_create_files(struct super_block *sb, struct dentry *root)
{
		struct dentry *enable_file = lktracefs_create_file(	sb,
															root,
															"enable",
															&lktracefs_bool_fops,
															S_IFREG | 0644);
		struct dentry *buffer_file = lktracefs_create_file(	sb,
															root,
															"data",
															&lktracefs_data_fops,
															S_IFREG | 0644);


		if(IS_ERR(enable_file)){
				printk(KERN_ERR "unable to create enable file \n");
		}
		if(IS_ERR(buffer_file)){
				printk(KERN_ERR "unable to create buffer file \n");
		}
}


static int
lktracefs_fill_super(	struct super_block	*sb,
						void				*data,
						int					silent)
{
		struct inode	*root_inode;
		struct dentry	*root_dentry;

		sb->s_blocksize			= PAGE_CACHE_SIZE;
		sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
		sb->s_magic				= LKTRACE_FSMAGIC;
		sb->s_op				= &lktrace_sb_fops;

		root_inode	=	lktracefs_create_inode(sb, S_IFDIR | 0755);
		if(unlikely(root_inode == NULL)){
				return -ENOMEM;
		}
		root_inode->i_op	=	&simple_dir_inode_operations;
		root_inode->i_fop	=	&simple_dir_operations;

		root_dentry			=	d_alloc_root(root_inode);
		if(unlikely(root_dentry == NULL)){
			goto d_alloc_root_error;
		}
		sb->s_root			=	root_dentry;
		lktracefs_create_files(sb, root_dentry);
		return 0;

d_alloc_root_error:
		iput(root_inode);
		return -ENOMEM;
}


static int
lktracefs_get_super(struct file_system_type		*fs,
					int							flags,
					const char					*devname,
					void						*mountoption,
					struct vfsmount				*vfsmount)
{
		return get_sb_single(fs, flags, mountoption, lktracefs_fill_super, vfsmount);
}


static struct file_system_type lktracefs_type = {
		.owner		=	THIS_MODULE,
		.name		=	LKTRACE_FSNAME,
		.get_sb		=	lktracefs_get_super,
		.kill_sb	=	kill_litter_super,
};


/* register lktracefs filesystem */
static int __init
lktracefs_init(void)
{
		int ret = register_filesystem(&lktracefs_type);
		return ret;
}


/* unregister ltracefs filesystem */
static void __exit
lktracefs_exit(void)
{
		unregister_filesystem(&lktracefs_type);
}

module_init(lktracefs_init);

module_exit(lktracefs_exit);

MODULE_AUTHOR("frederic ferrandis");

MODULE_LICENSE("GPL");


