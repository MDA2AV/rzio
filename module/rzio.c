// SPDX-License-Identifier: GPL-2.0
/*
 * rzio: socket I/O rings for web servers, in the kernel.
 *
 * Milestone 0 is the skeleton: a misc device, /dev/rzio. Every open() gets
 * its own context, which will grow into one reactor (rings, buffer pool,
 * connections). For now a context answers INFO and counts PINGs.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "rzio.h"

struct rzio_ctx {
	u32 id;
	atomic64_t pings;
};

static atomic_t rzio_last_id = ATOMIC_INIT(0);

static int rzio_open(struct inode *inode, struct file *file)
{
	struct rzio_ctx *ctx;

	/* _ACCOUNT charges the allocation to the caller's memory cgroup. */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL_ACCOUNT);
	if (!ctx)
		return -ENOMEM;

	ctx->id = atomic_inc_return(&rzio_last_id);

	/* misc_open() left our miscdevice here; the context replaces it. */
	file->private_data = ctx;

	pr_debug("ctx %u opened by pid %d\n", ctx->id, task_pid_nr(current));
	return 0;
}

static int rzio_release(struct inode *inode, struct file *file)
{
	struct rzio_ctx *ctx = file->private_data;

	pr_debug("ctx %u released after %lld pings\n", ctx->id,
		 atomic64_read(&ctx->pings));
	kfree(ctx);
	return 0;
}

static long rzio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rzio_ctx *ctx = file->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case RZIO_IOC_INFO: {
		/* No padding and every field set: no kernel bytes leak out. */
		struct rzio_info info = {
			.abi_version	= RZIO_ABI_VERSION,
			.ctx_id		= ctx->id,
		};

		return copy_to_user(argp, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case RZIO_IOC_PING: {
		/* Atomic, because threads sharing the fd can call this at once. */
		u64 pings = atomic64_inc_return(&ctx->pings);

		return put_user(pings, (u64 __user *)argp);
	}
	default:
		return -ENOTTY;
	}
}

static const struct file_operations rzio_fops = {
	/* An open fd holds a module reference, so rmmod fails until close. */
	.owner		= THIS_MODULE,
	.open		= rzio_open,
	.release	= rzio_release,
	.unlocked_ioctl	= rzio_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static struct miscdevice rzio_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "rzio",
	.fops	= &rzio_fops,
	.mode	= 0600,
};

static int __init rzio_init(void)
{
	int ret;

	ret = misc_register(&rzio_misc);
	if (ret)
		return ret;

	pr_info("/dev/%s ready, abi %d\n", rzio_misc.name, RZIO_ABI_VERSION);
	return 0;
}

static void __exit rzio_exit(void)
{
	misc_deregister(&rzio_misc);
	pr_info("unloaded\n");
}

module_init(rzio_init);
module_exit(rzio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Diogo Martins");
MODULE_DESCRIPTION("Socket I/O rings for web servers");
