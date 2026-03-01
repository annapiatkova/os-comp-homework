#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/printk.h>
#include <asm/current.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anna Piatkova");
MODULE_DESCRIPTION("");
MODULE_VERSION("0.1");

#define DEVICE_NAME "nulldump"
#define CLASS_NAME "nulldump_class"
#define HEXDUMP_ROWSIZE 16
#define HEXDUMP_LINE_LENGTH (4 * HEXDUMP_ROWSIZE + 1)

static dev_t dev;
static struct cdev nulldump_cdev;
static struct class *nulldump_class;
static struct device *sdev;

static ssize_t nulldump_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{	
	pid_t pid = current->pid;
	pr_info("pid: %d\n", pid);
	const char *command = current->comm;
	pr_info("command: %s\n", command);
	pr_info("requested %ld bytes\n", len);
	return 0;
}

static ssize_t nulldump_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	pid_t pid = current->pid;
	pr_info("pid: %d\n", pid);
	const char *command = current->comm;
	pr_info("command: %s\n", command);

	char *data = kmalloc(len, GFP_KERNEL);
	size_t n_bytes_read = len - copy_from_user(data, buf, len);
	pr_info("wrote %ld bytes\n", n_bytes_read);

	char hexdump[HEXDUMP_LINE_LENGTH];
	char *i = data;
	while (n_bytes_read > 0 && n_bytes_read <= len)
	{
		int n_bytes_written = hex_dump_to_buffer(i, n_bytes_read, HEXDUMP_ROWSIZE, 1, hexdump, HEXDUMP_LINE_LENGTH, true);
		if (n_bytes_written <= 0)
			break;
		pr_info("hexdump: %07lx %s\n", len - n_bytes_read, hexdump);
		n_bytes_read -= HEXDUMP_ROWSIZE;
		i += HEXDUMP_ROWSIZE;
	}

	kfree(data);
	return len;
}

static const struct file_operations nulldump_fops = {
		.owner = THIS_MODULE,
		.read = nulldump_read,
		.write = nulldump_write,
};

/* Set permission bits to 0666 */
static char *nulldump_devnode(const struct device *_dev, umode_t *mode)
{
	if (!mode)
		return NULL;
	if (_dev->devt == dev)
		*mode = 0666;
	return NULL;
}

static int __init nulldump_init(void)
{
	int ret;

	if ((ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME)))
	{
		pr_err("nulldump: failed to allocate device number\n");
		return ret;
	}

	pr_info("nulldump: registered (major=%d, minor=%d)\n", MAJOR(dev), MINOR(dev));

	cdev_init(&nulldump_cdev, &nulldump_fops);
	nulldump_cdev.owner = THIS_MODULE;

	if ((ret = cdev_add(&nulldump_cdev, dev, 1)))
	{
		pr_err("nulldump: cdev_add failed\n");
		goto err_unregister;
	}

	if (IS_ERR(nulldump_class = class_create(CLASS_NAME)))
	{
		pr_err("nulldump: class_create failed\n");
		ret = PTR_ERR(nulldump_class);
		goto err_cdev_del;
	}
	nulldump_class->devnode = nulldump_devnode;

	if (IS_ERR(sdev = device_create(nulldump_class, NULL, dev, NULL, DEVICE_NAME)))
	{
		pr_err("nulldump: device_create failed\n");
		ret = PTR_ERR(sdev);
		goto err_class_destroy;
	}
	

	pr_info("nulldump: module loaded\n");
	return 0;

err_class_destroy:
	class_destroy(nulldump_class);
err_cdev_del:
	cdev_del(&nulldump_cdev);
err_unregister:
	unregister_chrdev_region(dev, 1);
	return ret;
}

static void __exit nulldump_exit(void)
{
	device_destroy(nulldump_class, dev);
	class_destroy(nulldump_class);
	cdev_del(&nulldump_cdev);
	unregister_chrdev_region(dev, 1);

	pr_info("nulldump: module unloaded\n");

}

module_init(nulldump_init);
module_exit(nulldump_exit);
