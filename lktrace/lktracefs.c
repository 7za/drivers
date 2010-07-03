#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/spinlock_types.h>


#define LKTRACE_FSNAME "lktracefs"
#define LKTRACE_FSMAGIC 0xef1244dd 

extern  int
lktracefile_create_enable_file( struct super_block  *sb,
                                struct dentry       *root,
                                int                 *associated_data);


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


struct dentry*
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


__attribute__((unused))
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


static void
lktracefs_create_files(struct super_block *sb, struct dentry *root)
{
        if(lktracefile_create_enable_file(sb, root, &lktrace_state.lk_enabled)){
                printk(KERN_ERR "unable to create enable file\n");
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


