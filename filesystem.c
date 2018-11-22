#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/quotaops.h>
#include <linux/seq_file.h>
#include <linux/exportfs.h>
#include <linux/buffer_head.h>

/********************************************************
 *    mount -t tfs /mnt/test /root/test
 *********************************************************/
#define TFS_MAGIC 0x19891115

static char test_buff[PAGE_CACHE_SIZE];
static char test1_buff[PAGE_CACHE_SIZE];

static int tfs_open(struct inode *inode, struct file *filp)
{
	return filp->private_data = inode->i_private;
}

static ssize_t tfs_read_file(struct file *filp, char *buf,
				size_t count, loff_t *offset)
{
	int len = 0;

	if (unlikely(*offset >= PAGE_CACHE_SIZE)) {
		return -EINVAL;
	}

	len = PAGE_CACHE_SIZE - *offset > count? count: 
			PAGE_CACHE_SIZE - *offset;
	if (copy_to_user(buf, filp->private_data + *offset, len)) {
		return -EFAULT;
	}
	*offset += len;

	return len;
}

static ssize_t tfs_write_file(struct file *filp, const char *buf,
				size_t count, loff_t *offset)
{
	if (unlikely(count + *offset >= PAGE_CACHE_SIZE)) {
		return -EINVAL;
	}
	if (copy_from_user(filp->private_data + *offset, buf, count)) {
		return -EFAULT;
	}

	return count;
}

static struct file_operations tfs_file_ops = {
	.open       = tfs_open,
	.read       = tfs_read_file,
	.write  	= tfs_write_file,
};

static struct inode *tfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);

	if (unlikely(inode == NULL)) {
		goto out;
	}
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_blocks = 0;
	inode->i_mode = mode;
	inode->i_atime = jiffies;
	inode->i_mtime = jiffies;
	inode->i_ctime = jiffies;

out:
	return inode;
}

static struct dentry *tfs_create_file (struct super_block *sb,
				struct dentry *dir, const char *name,
				char *buffer)
{
	struct qstr qname;
	struct inode *inode;
	struct dentry *dentry;

	qname.name = name;
	qname.len = strlen (name);
	qname.hash = full_name_hash(name, qname.len);

	dentry = d_alloc(dir, &qname);
	if (unlikely(!dentry)) goto out;

	inode = tfs_make_inode(sb, S_IFREG | 0644);
	if (unlikely(!inode)) goto put;

	inode->i_private = buffer;
	inode->i_fop = &tfs_file_ops;
	d_add(dentry, inode);

	return dentry;
put:
	dput(dentry);
out:
	return 0;
}

static void tfs_create_files (struct super_block *sb, struct dentry *root)
{
	struct dentry *dir = NULL;

	tfs_create_file(sb, root, "test", test_buff);
	subdir = tfs_create_dir(sb, root, "testdir");
	if (likely(dir != NULL)) {
		tfs_create_file(sb, dir, "test", test1_buff);
	}
}

static struct dentry *tfs_create_dir (struct super_block *sb,
				struct dentry *parent, const char *name)
{
	struct qstr qname;
	struct inode *inode;
	struct dentry *dentry;

	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(name, qname.len);

	dentry = d_alloc(parent, &qname);
	if (unlikely(!dentry)) goto out;

	inode = tfs_make_inode(sb, S_IFDIR | 0644);
	if (unlikely(!inode)) goto put;

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	d_add(dentry, inode);

	return dentry;
put:
	dput(dentry);
out:
	return 0;
}

static struct super_operations tfs_s_ops = {
	.statfs            = simple_statfs,
	.drop_inode        = generic_delete_inode,
};

static int tfs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct dentry *root_dentry;

	// 设置超级快属性
	sb->s_op = &tfs_s_ops;
	sb->s_magic = TFS_MAGIC;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;

	// 创建一个inode,这个inode这里用内存模拟,实际上应该在flash里面
	root = tfs_make_inode (sb, S_IFDIR | 0755);
	if (unlikely(root == NULL)) goto out;

	// 赋值一个通用的 目录操作函数
	root->i_fop = &simple_dir_operations;
	root->i_op = &simple_dir_inode_operations;

	// 将inode和文件系统根目录关联
	root_dentry = d_alloc_root(root);
	if (unlikely(!root_dentry)) goto put;

	// 将root dentry结点赋值给super block
	sb->s_root = root_dentry;

	// 在这个root下面， 默认创建几个文件和目录
	tfs_create_files (sb, root_dentry);

	return 0;
put:
	iput(root);
out:
	return -ENOMEM;
}

static int tfs_get_super(struct file_system_type *fst,int flags, const char *devname, void *data,struct vfsmount *mount)
{
	return get_sb_single(fst, flags, data, tfs_fill_super, mount);
}

static struct file_system_type tfs_type = {
	.name           = "tfs",
	.owner          = THIS_MODULE,
	.get_sb         = tfs_get_super,
	.kill_sb        = kill_litter_super,
};

static int __init tfs_init(void)
{
	struct file_system_type * tmp;  

	printk("%s:register filesystem ok\n", __func__);
	return register_filesystem(&tfs_type);
}

static void __exit tfs_exit(void)
{
	printk("%s: exit\n",__func__);
	unregister_filesystem(&tfs_type);
}

MODULE_AUTHOR("lc");
MODULE_LICENSE("GPL");
module_init(tfs_init);
module_exit(tfs_exit);
