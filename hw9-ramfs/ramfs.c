#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

MODULE_DESCRIPTION("Ramfs: a simple no-dev filesystem");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

#define MYFS_BLOCKSIZE		4096
#define MYFS_BLOCKSIZE_BITS	12
#define MYFS_MAGIC		0xbeefcafe
#define FS_NAME             "ram-fs"

static int ramfs_mknod(struct mnt_idmap *idmap, struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev);
static int ramfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl);
static int ramfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode);

static const struct super_operations ramfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_drop_inode,
};

static const struct inode_operations ramfs_dir_inode_operations = {
	.create         = ramfs_create,
	.lookup         = simple_lookup,
	.link           = simple_link,
	.unlink         = simple_unlink,
	.mkdir          = ramfs_mkdir,
	.rmdir          = simple_rmdir,
	.mknod          = ramfs_mknod,
	.rename         = simple_rename,
};

static const struct file_operations ramfs_file_operations = {
	.read_iter      = generic_file_read_iter,
	.write_iter     = generic_file_write_iter,
	.mmap           = generic_file_mmap,
	.llseek         = generic_file_llseek,
};

static const struct inode_operations ramfs_file_inode_operations = {
	.getattr        = simple_getattr,
};

static int simple_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct folio *folio, void *fsdata)
{
	struct inode *inode = folio->mapping->host;
	loff_t last_pos = pos + copied;

	/* zero the stale part of the folio if we did a short copy */
	if (!folio_test_uptodate(folio)) {
		if (copied < len) {
			size_t from = offset_in_folio(folio, pos);

			folio_zero_range(folio, from + copied, len - copied);
		}
		folio_mark_uptodate(folio);
	}
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	folio_mark_dirty(folio);
	folio_unlock(folio);
	folio_put(folio);

	return copied;
}

static bool ramfs_dirty_folio(struct address_space *addr_space, struct folio* folio)
{
	return 0;
}

static const struct address_space_operations ramfs_aops = {
	.dirty_folio	= ramfs_dirty_folio,
	.write_begin    = simple_write_begin,
	.write_end      = simple_write_end,
};

static struct inode *ramfs_get_inode(struct mnt_idmap *idmap, struct super_block *sb, const struct inode *dir,
		int mode)
{
	struct inode *inode = new_inode(sb);

	if (!inode)
		return NULL;

	inode_init_owner(idmap, inode, dir, mode);
	inode_set_ctime_current(inode);
	inode->i_ino = 1;

	inode->i_ino = get_next_ino();

	inode->i_mapping->a_ops = &ramfs_aops;

	if (S_ISDIR(mode)) {
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		inode->i_op = &ramfs_dir_inode_operations;

		inc_nlink(inode);
	}

	if (S_ISREG(mode)) {
		inode->i_op = &ramfs_file_inode_operations;
		inode->i_fop = &ramfs_file_operations;
	}

	return inode;
}

static int ramfs_mknod(struct mnt_idmap *idmap, struct inode *dir,
		struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = ramfs_get_inode(idmap, dir->i_sb, dir, mode);

	if (inode == NULL)
		return -ENOSPC;

	d_instantiate(dentry, inode);
	dget(dentry);
	inode_set_ctime_current(dir);

	return 0;
}

static int ramfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl)
{
	return ramfs_mknod(idmap, dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int ret;

	ret = ramfs_mknod(idmap, dir, dentry, mode | S_IFDIR, 0);
	if (ret != 0)
		return ret;

	inc_nlink(dir);

	return 0;
}

static int ramfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct dentry *root_dentry;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = MYFS_BLOCKSIZE;
	sb->s_blocksize_bits = MYFS_BLOCKSIZE_BITS;
	sb->s_magic = MYFS_MAGIC;
	sb->s_op = &ramfs_ops;

	root_inode = ramfs_get_inode(&nop_mnt_idmap, sb, NULL, S_IFDIR | 0755);

	pr_info("ramfs: root inode has %d link(s)\n", root_inode->i_nlink);

	if (!root_inode)
		return -ENOMEM;

	root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		goto out_no_root;
	sb->s_root = root_dentry;

	return 0;

out_no_root:
	iput(root_inode);
	return -ENOMEM;
}

static struct dentry *ramfs_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, ramfs_fill_super);
}

static struct file_system_type ramfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= FS_NAME,
	.mount		= ramfs_mount,
	.kill_sb	= kill_litter_super,
};

static int __init ramfs_init(void)
{
	int err;

	err = register_filesystem(&ramfs_fs_type);
	if (err) {
		pr_err("ramfs: register_filesystem failed\n");
		return err;
	}

	pr_info("ramfs: module loaded\n");
	return 0;
}

static void __exit ramfs_exit(void)
{
	unregister_filesystem(&ramfs_fs_type);
	pr_info("ramfs: module unloaded\n");
}

module_init(ramfs_init);
module_exit(ramfs_exit);