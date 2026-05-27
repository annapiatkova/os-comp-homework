#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kfifo.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anna Piatkova");
MODULE_DESCRIPTION("Pipebuf: a FIFO character device");
MODULE_VERSION("0.1");

static int ndev = 1;
MODULE_PARM_DESC(ndev, "The number of devices");

static int bufsize = 16;
MODULE_PARM_DESC(bufsize, "The default buffer size for new devices. Must be a power of two");

#define DEVICE_NAME  "pipebuf"
#define CLASS_NAME   "pipebuf_class"
#define MAX_NDEV     1024
#define MAX_NAME_LEN 16
#define TMPBUF_SIZE  16

static dev_t dev;
static struct class *pipebuf_class;

static struct pipebuf *pipebuf;

struct pipebuf {
	struct cdev cdev;
	struct device *device;
	struct mutex readlock;            /* Readers must hold this lock while the file is open */
	struct mutex writelock;           /* Writers acquire this lock before writing */
	atomic_t nwriters;                /* The number of writers, or -1 when the device is about to be deleted */
	struct wait_queue_head wq_head_r; /* Wait queue head for the reader */
	struct wait_queue_head wq_head_w; /* Wait queue head for writers */
	size_t size;
	DECLARE_KFIFO_PTR(fifo, char);
};

DECLARE_WAIT_QUEUE_HEAD(wq);

static ssize_t size_show(struct device *device, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%ld\n", pipebuf[MINOR(device->devt)].size);
}

/*
 * Passing the pointer to kfifo as a parameter creates problems with type checking, so
 * instead we pass the pointer to struct pipebuf and access the pointer to kfifo as its member
 */
static int pipebuf_kfifo_realloc(struct pipebuf *pipebuf, size_t new_bufsize)
{
	STRUCT_KFIFO_PTR(char) new_fifo;
	if (kfifo_alloc(&new_fifo, new_bufsize, GFP_KERNEL))
		return -ENOMEM;

	int elem_left = kfifo_len(&pipebuf->fifo);
	char tmp[TMPBUF_SIZE];
	while (elem_left)
	{
		int elem_copied = kfifo_out(&pipebuf->fifo, &tmp[0], TMPBUF_SIZE);
		if (!elem_copied)
		{
			pr_err("pipebuf: bufsize set failed: couldn't copy the data over to the new buffer\n");
			kfifo_free(&new_fifo);
			return -EAGAIN;
		}
		kfifo_in(&new_fifo, &tmp[0], elem_copied);
		elem_left -= elem_copied;
	}

	kfifo_free(&pipebuf->fifo);
	memcpy(&pipebuf->fifo, &new_fifo, sizeof(STRUCT_KFIFO_PTR(char)));
	return 0;
}

static ssize_t size_store(struct device *device, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int new_bufsize;
	if ((ret = kstrtoint(buf, 10, &new_bufsize)))
	{
		pr_err("pipebuf: invalid value for parameter bufsize: %s\n", buf);
		return ret;
	}
	if (new_bufsize <= 0)
	{
		pr_err("pipebuf: invalid value for parameter bufsize: buffer size must be > 0, requested bufsize = %d\n", new_bufsize);
		return -EINVAL;
	}
	int new_bufsize_rounded_up = roundup_pow_of_two(new_bufsize);
	if (new_bufsize_rounded_up > new_bufsize)
		pr_info("pipebuf: buffer size must be a power of two, rounding up (requested size = %d, rounding up to %d)\n", new_bufsize, new_bufsize_rounded_up);

	if (!mutex_trylock(&pipebuf->readlock))
	{
		pr_err("pipebuf: coudn't change device size: device is open for reading\n");
		return -EBUSY;
	}
	mutex_lock(&pipebuf->writelock);

	if (pipebuf_kfifo_realloc(&pipebuf[MINOR(device->devt)], new_bufsize_rounded_up))
	{
		mutex_unlock(&pipebuf->writelock);
		mutex_unlock(&pipebuf->readlock);
		return -ENOMEM;
	}
	pipebuf[MINOR(device->devt)].size = new_bufsize_rounded_up;
	mutex_unlock(&pipebuf->writelock);
	mutex_unlock(&pipebuf->readlock);
	return count;
}

static DEVICE_ATTR(size, 0644, size_show, size_store);

static ssize_t pipebuf_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	int ret;
	struct pipebuf *pipebuf = file->private_data;
	while (kfifo_is_empty(&pipebuf->fifo))
	{
		if (!atomic_read(&pipebuf->nwriters)) {
			return 0;
		}
		pr_info("pipebuf: read: buf is empty\n");
		if ((ret = wait_event_interruptible(pipebuf->wq_head_r,
			!kfifo_is_empty(&pipebuf->fifo) || !atomic_read(&pipebuf->nwriters))))
			return ret;
	}

	int bytes_copied;
	if ((ret = kfifo_to_user(&pipebuf->fifo, buf, len, &bytes_copied)))
	{
		pr_err("pipebuf: read: kfifo_to_user failed\n");
		return ret;
	}
	pr_info("pipebuf: read: waking up the writers\n");
	wake_up_interruptible(&pipebuf->wq_head_w);
	return bytes_copied;
}

static ssize_t pipebuf_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	int ret;
	struct pipebuf *pipebuf = file->private_data;
	mutex_lock(&pipebuf->writelock);
	while (kfifo_is_full(&pipebuf->fifo))
	{
		mutex_unlock(&pipebuf->writelock);
		pr_info("pipebuf: write: buf is full\n");
		if ((ret = wait_event_interruptible(pipebuf->wq_head_w, !kfifo_is_full(&pipebuf->fifo))))
			return ret;
		mutex_lock(&pipebuf->writelock);
	} 
	int bytes_copied;
	if ((ret = kfifo_from_user(&pipebuf->fifo, buf, len, &bytes_copied)))
	{
		mutex_unlock(&pipebuf->writelock);
		pr_err("pipebuf: write: kfifo_from_user failed\n");
		return ret;
	}
	mutex_unlock(&pipebuf->writelock);

	pr_info("pipebuf: write: waking up the reader\n");
	wake_up_interruptible(&pipebuf->wq_head_r);
	return bytes_copied;
}

static int pipebuf_open(struct inode *inode, struct file *file)
{
	struct pipebuf *curr_pipebuf = container_of(inode->i_cdev, struct pipebuf, cdev);

	if (file->f_mode & FMODE_READ)
	{
		pr_info("pipebuf: open for reading\n");
		if (!mutex_trylock(&curr_pipebuf->readlock))
		{
			return -EWOULDBLOCK;
		}
		file->private_data = ((void *)curr_pipebuf);
	}
	if (file->f_mode & FMODE_WRITE)
	{
		pr_info("pipebuf: open for writing\n");
		if (!atomic_inc_unless_negative(&curr_pipebuf->nwriters))
		{
			pr_info("pipebuf_open: requested device has been deleted\n");
			return -ENODEV;
		}
		file->private_data = ((void *)curr_pipebuf);
	}
	return 0;
}

static int pipebuf_release(struct inode *inode, struct file *file)
{
	struct pipebuf *curr_pipebuf = container_of(inode->i_cdev, struct pipebuf, cdev);

	if (file->f_mode & FMODE_READ)
	{
		pr_info("pipebuf: release called by the reader\n");
		mutex_unlock(&curr_pipebuf->readlock);
	}
	if (file->f_mode & FMODE_WRITE)
	{
		pr_info("pipebuf: release called by a writer\n");
		if (!atomic_dec_return(&curr_pipebuf->nwriters))
		{
			pr_info("pipebuf: release: no writers left, waking up the reader\n");
			wake_up_interruptible(&curr_pipebuf->wq_head_r);
		}
	}
	return 0;
}

static const struct file_operations pipebuf_fops = {
	.owner   = THIS_MODULE,
	.read    = pipebuf_read,
	.write   = pipebuf_write,
	.open    = pipebuf_open,
	.release = pipebuf_release,
};

/* Set permission bits to 0666 */
static char *pipebuf_devnode(const struct device *_dev, umode_t *mode)
{
	if (!mode)
		return NULL;
	if (MAJOR(_dev->devt) == MAJOR(dev))
		*mode = 0666;
	return NULL;
}

static int pipebuf_create_device(int minor, const char *devname)
{
	int ret;

	cdev_init(&pipebuf[minor].cdev, &pipebuf_fops);
	pipebuf[minor].cdev.owner = THIS_MODULE;

	if ((ret = cdev_add(&pipebuf[minor].cdev, MKDEV(MAJOR(dev), minor), 1)))
	{
		pr_err("pipebuf: cdev_add failed for device (major=%d, minor=%d)\n", MAJOR(dev), minor);
		return ret;
	}

	if (IS_ERR(pipebuf[minor].device = device_create(pipebuf_class, NULL, MKDEV(MAJOR(dev), minor), NULL, devname)))
	{
		pr_err("pipebuf: device_create failed for device (major=%d, minor=%d)\n", MAJOR(dev), minor);
		ret = PTR_ERR(pipebuf[minor].device);
		goto err_cdev_del;
	}

	if ((ret = kfifo_alloc(&pipebuf[minor].fifo, bufsize, GFP_KERNEL)))
	{
		pr_err("pipebuf: kfifo_alloc failed for device (major=%d, minor=%d)\n", MAJOR(dev), minor);
		goto err_device_destroy;
	}

	mutex_init(&pipebuf[minor].readlock);
	mutex_init(&pipebuf[minor].writelock);
	atomic_set(&pipebuf[minor].nwriters, 0);
	init_waitqueue_head(&pipebuf[minor].wq_head_r);
	init_waitqueue_head(&pipebuf[minor].wq_head_w);

	pipebuf[minor].size = bufsize;
	device_create_file(pipebuf[minor].device, &dev_attr_size);
	
	pr_info("pipebuf: device (major=%d, minor=%d) created\n", MAJOR(dev), minor);
	return 0;

err_device_destroy:
	device_destroy(pipebuf_class, MKDEV(MAJOR(dev), minor));
err_cdev_del:
	cdev_del(&pipebuf[minor].cdev);
	return ret;
}

static int pipebuf_destroy_device(int minor)
{
	if (!mutex_trylock(&pipebuf[minor].readlock))
	{
		pr_err("pipebuf: couldn't remove device (major=%d, minor=%d): device currently in use\n", MAJOR(dev), minor);
		return -EBUSY;
	}

	if (!atomic_dec_unless_positive(&pipebuf[minor].nwriters))
	{
		mutex_unlock(&pipebuf[minor].readlock);
		pr_err("pipebuf: couldn't remove device (major=%d, minor=%d): device currently in use\n", MAJOR(dev), minor);
		return -EBUSY;
	}

	device_remove_file(pipebuf[minor].device, &dev_attr_size);

	kfifo_free(&pipebuf[minor].fifo);

	device_destroy(pipebuf_class, MKDEV(MAJOR(dev), minor));
	cdev_del(&pipebuf[minor].cdev);
	pr_info("pipebuf: device (major=%d, minor=%d) destroyed\n", MAJOR(dev), minor);
	return 0;
}

static int __init pipebuf_init(void)
{
	int ret;

	/* Allocate major + minor numbers for MAX_NDEV devices so we don't have to allocate device numbers when ndev changes */
	if ((ret = alloc_chrdev_region(&dev, 0, MAX_NDEV, DEVICE_NAME)))
	{
		pr_err("pipebuf: failed to allocate device number\n");
		return ret;
	}
	pr_info("pipebuf: registered device (major=%d, minor=0..%d)\n", MAJOR(dev), MAX_NDEV - 1);

	if (IS_ERR(pipebuf_class = class_create(CLASS_NAME)))
	{
		pr_err("pipebuf: class_create failed\n");
		ret = PTR_ERR(pipebuf_class);
		goto err_unregister;
	}

	/* Set permission bits to 0666 */
	pipebuf_class->devnode = pipebuf_devnode;

	/* Reallocating these would invalidate some pointers created by cdev_add, hence MAX_NDEV */
	if (!(pipebuf = kmalloc(sizeof(struct pipebuf) * MAX_NDEV, GFP_KERNEL)))
	{
		pr_err("pipebuf: memory allocation failed\n");
		ret = -ENOMEM;
		goto err_class_destroy;
	}

	int bufsize_rounded_up = roundup_pow_of_two(bufsize);
	if (bufsize_rounded_up > bufsize)
	{
		pr_info("pipebuf: buffer size must be a power of two, rounding up (requested size = %d, rounding up to %d)\n", bufsize, bufsize_rounded_up);
		bufsize = bufsize_rounded_up;
	}

	char devname[MAX_NAME_LEN] = DEVICE_NAME;
	int len = strlen(devname);
	int minor = 0;
	for (; minor < ndev; ++minor)
	{
		/* Append the minor number to the name of the device */
		snprintf(&devname[len], MAX_NAME_LEN - len, "%d", minor);
		if ((ret = pipebuf_create_device(minor, devname)))
		{
			--minor;
			goto err_devices_destroy;
		}
	}

	pr_info("pipebuf: module loaded\n");
	return 0;

err_devices_destroy:
	for (; minor >= 0; --minor)
		pipebuf_destroy_device(minor);
	kfree(pipebuf);
err_class_destroy:
	class_destroy(pipebuf_class);
err_unregister:
	unregister_chrdev_region(dev, MAX_NDEV);
	return ret;
}

static void __exit pipebuf_exit(void)
{
	for (int minor = ndev - 1; minor >= 0; --minor)
		pipebuf_destroy_device(minor);
	kfree(pipebuf);
	class_destroy(pipebuf_class);
	unregister_chrdev_region(dev, MAX_NDEV);
	pr_info("pipebuf: module unloaded\n");
}

module_init(pipebuf_init);
module_exit(pipebuf_exit);

static int ndev_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	int ndev_new;
	if ((ret = kstrtoint(val, 10, &ndev_new)))
	{
		pr_err("pipebuf: invalid value for parameter ndev: %s\n", val);
		return ret;
	}
	if (ndev_new < 0)
	{
		pr_err("pipebuf: invalid value for parameter ndev: the number of devices must be nonnegative, requested ndev = %d\n", ndev_new);
		return -EINVAL;
	}

	if (ndev_new > MAX_NDEV)
	{
		pr_err("pipebuf: couldn't set ndev: the number of devices must be <= %d (requested ndev = %d)\n", MAX_NDEV, ndev_new);
		return -EINVAL;		
	}

	/*
	 * ndev_set will be called before pipebuf_init when the module is loaded. When this happens, don't try
	 * allocating devices, pipebuf_init will do that for us
	 */
	if (!MAJOR(dev))
	{
		ndev = ndev_new;
		return 0;
	}

	int minor;
	if (ndev_new > ndev)
	{
		pr_info("pipebuf: adding devices\n");
		char devname[MAX_NAME_LEN] = DEVICE_NAME;
		int len = strlen(devname);
		for (minor = ndev; minor < ndev_new; ++minor)
		{
			/* Append the minor number to the name of the device */
			snprintf(&devname[len], MAX_NAME_LEN - len, "%d", minor);
			if ((ret = pipebuf_create_device(minor, devname)))
			{
				--minor;
				goto err_devices_destroy;
			}
		}
		ndev = ndev_new;
	}

	if (ndev_new < ndev)
	{
		pr_info("pipebuf: removing devices\n");
		for (minor = ndev - 1; minor >= ndev_new; --minor)
		{
			if ((ret = pipebuf_destroy_device(minor)))
			{
				ndev = minor + 1;
				return ret;
			}
		}
		ndev = ndev_new;
	}
	return 0;

err_devices_destroy:
	for (; minor >= 0; --minor)
		pipebuf_destroy_device(minor);
	return ret;
}

static const struct kernel_param_ops ndev_param_ops = {
	.set = ndev_set,
	.get = param_get_int,
};

module_param_cb(ndev, &ndev_param_ops, &ndev, 0644);

static int bufsize_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	int new_bufsize;
	if ((ret = kstrtoint(val, 10, &new_bufsize)))
	{
		pr_err("pipebuf: invalid value for parameter bufsize: %s\n", val);
		return ret;
	}
	if (new_bufsize <= 0)
	{
		pr_err("pipebuf: invalid value for parameter bufsize: buffer size must be > 0, requested bufsize = %d\n", new_bufsize);
		return -EINVAL;
	}

	int new_bufsize_rounded_up = roundup_pow_of_two(new_bufsize);
	if (new_bufsize_rounded_up > new_bufsize)
		pr_info("pipebuf: buffer size must be a power of two, rounding up (requested size = %d, rounding up to %d)\n", new_bufsize, new_bufsize_rounded_up);
		
	bufsize = new_bufsize_rounded_up;
	return 0;
}

static const struct kernel_param_ops bufsize_param_ops = {
	.set = bufsize_set,
	.get = param_get_int,
};

module_param_cb(bufsize, &bufsize_param_ops, &bufsize, 0644);