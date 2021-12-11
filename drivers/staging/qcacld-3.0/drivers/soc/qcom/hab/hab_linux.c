/* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "hab.h"

unsigned int get_refcnt(struct kref ref)
{
	return kref_read(&ref);
}

static int hab_open(struct inode *inodep, struct file *filep)
{
	int result = 0;
	struct uhab_context *ctx;

	ctx = hab_ctx_alloc(0);

	if (!ctx) {
		pr_err("hab_ctx_alloc failed\n");
		filep->private_data = NULL;
		return -ENOMEM;
	}

	ctx->owner = task_pid_nr(current);
	filep->private_data = ctx;
	pr_debug("ctx owner %d refcnt %d\n", ctx->owner,
			get_refcnt(ctx->refcount));

	return result;
}

static int hab_release(struct inode *inodep, struct file *filep)
{
	struct uhab_context *ctx = filep->private_data;
	struct virtual_channel *vchan, *tmp;
	struct hab_open_node *node;

	if (!ctx)
		return 0;

	pr_debug("inode %pK, filep %pK ctx %pK\n", inodep, filep, ctx);

	write_lock(&ctx->ctx_lock);
	/* notify remote side on vchan closing */
	list_for_each_entry_safe(vchan, tmp, &ctx->vchannels, node) {
		/* local close starts */
		vchan->closed = 1;

		list_del(&vchan->node); /* vchan is not in this ctx anymore */
		ctx->vcnt--;

		write_unlock(&ctx->ctx_lock);
		hab_vchan_stop_notify(vchan);
		hab_vchan_put(vchan); /* there is a lock inside */
		write_lock(&ctx->ctx_lock);
	}

	/* notify remote side on pending open */
	list_for_each_entry(node, &ctx->pending_open, node) {
		/* no touch to the list itself. it is allocated on the stack */
		if (hab_open_cancel_notify(&node->request))
			pr_err("failed to send open cancel vcid %x subid %d openid %d pchan %s\n",
					node->request.xdata.vchan_id,
					node->request.xdata.sub_id,
					node->request.xdata.open_id,
					node->request.pchan->habdev->name);
	}
	write_unlock(&ctx->ctx_lock);

	hab_ctx_put(ctx);
	filep->private_data = NULL;

	return 0;
}

static long hab_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct uhab_context *ctx = (struct uhab_context *)filep->private_data;
	struct hab_open *open_param;
	struct hab_close *close_param;
	struct hab_recv *recv_param;
	struct hab_send *send_param;
	struct hab_info *info_param;
	struct hab_message *msg = NULL;
	void *send_data;
	unsigned char data[256] = { 0 };
	long ret = 0;
	char names[30];

	if (_IOC_SIZE(cmd) && (cmd & IOC_IN)) {
		if (_IOC_SIZE(cmd) > sizeof(data))
			return -EINVAL;

		if (copy_from_user(data, (void __user *)arg, _IOC_SIZE(cmd))) {
			pr_err("copy_from_user failed cmd=%x size=%d\n",
				cmd, _IOC_SIZE(cmd));
			return -EFAULT;
		}
	}

	switch (cmd) {
	case IOCTL_HAB_VC_OPEN:
		open_param = (struct hab_open *)data;
		ret = hab_vchan_open(ctx, open_param->mmid,
			&open_param->vcid,
			open_param->timeout,
			open_param->flags);
		break;
	case IOCTL_HAB_VC_CLOSE:
		close_param = (struct hab_close *)data;
		ret = hab_vchan_close(ctx, close_param->vcid);
		break;
	case IOCTL_HAB_SEND:
		send_param = (struct hab_send *)data;
		if (send_param->sizebytes > (uint32_t)(HAB_HEADER_SIZE_MASK)) {
			ret = -EINVAL;
			break;
		}

		send_data = kzalloc(send_param->sizebytes, GFP_KERNEL);
		if (!send_data) {
			ret = -ENOMEM;
			break;
		}

		if (copy_from_user(send_data, (void __user *)send_param->data,
				send_param->sizebytes)) {
			ret = -EFAULT;
		} else {
			ret = hab_vchan_send(ctx, send_param->vcid,
						send_param->sizebytes,
						send_data,
						send_param->flags);
		}
		kfree(send_data);
		break;
	case IOCTL_HAB_RECV:
		recv_param = (struct hab_recv *)data;
		if (!recv_param->data) {
			ret = -EINVAL;
			break;
		}

		ret = hab_vchan_recv(ctx, &msg, recv_param->vcid,
				&recv_param->sizebytes, recv_param->flags);

		if (ret == 0 && msg) {
			if (copy_to_user((void __user *)recv_param->data,
					msg->data,
					msg->sizebytes)) {
				pr_err("copy_to_user failed: vc=%x size=%d\n",
				   recv_param->vcid, (int)msg->sizebytes);
				recv_param->sizebytes = 0;
				ret = -EFAULT;
			}
		} else if (ret && msg) {
			pr_warn("vcid %X recv failed %d and msg is still of %zd bytes\n",
				recv_param->vcid, (int)ret, msg->sizebytes);
		}

		if (msg)
			hab_msg_free(msg);
		break;
	case IOCTL_HAB_VC_EXPORT:
		ret = hab_mem_export(ctx, (struct hab_export *)data, 0);
		break;
	case IOCTL_HAB_VC_IMPORT:
		ret = hab_mem_import(ctx, (struct hab_import *)data, 0);
		break;
	case IOCTL_HAB_VC_UNEXPORT:
		ret = hab_mem_unexport(ctx, (struct hab_unexport *)data, 0);
		break;
	case IOCTL_HAB_VC_UNIMPORT:
		ret = hab_mem_unimport(ctx, (struct hab_unimport *)data, 0);
		break;
	case IOCTL_HAB_VC_QUERY:
		info_param = (struct hab_info *)data;
		if (!info_param->names || !info_param->namesize ||
			info_param->namesize > sizeof(names)) {
			pr_err("wrong param for vm info vcid %X, names %llX, sz %d\n",
					info_param->vcid, info_param->names,
					info_param->namesize);
			ret = -EINVAL;
			break;
		}
		ret = hab_vchan_query(ctx, info_param->vcid,
				(uint64_t *)&info_param->ids,
				 names, info_param->namesize, 0);
		if (!ret) {
			if (copy_to_user((void __user *)info_param->names,
						 names,
						 info_param->namesize)) {
				pr_err("copy_to_user failed: vc=%x size=%d\n",
						info_param->vcid,
						info_param->namesize*2);
				info_param->namesize = 0;
				ret = -EFAULT;
			}
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	if (_IOC_SIZE(cmd) && (cmd & IOC_OUT))
		if (copy_to_user((void __user *) arg, data, _IOC_SIZE(cmd))) {
			pr_err("copy_to_user failed: cmd=%x\n", cmd);
			ret = -EFAULT;
		}

	return ret;
}

static long hab_compat_ioctl(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	return hab_ioctl(filep, cmd, arg);
}

static const struct file_operations hab_fops = {
	.owner = THIS_MODULE,
	.open = hab_open,
	.release = hab_release,
	.mmap = habmem_imp_hyp_mmap,
	.unlocked_ioctl = hab_ioctl,
	.compat_ioctl = hab_compat_ioctl
};

/*
 * These map sg functions are pass through because the memory backing the
 * sg list is already accessible to the kernel as they come from a the
 * dedicated shared vm pool
 */

static int hab_map_sg(struct device *dev, struct scatterlist *sgl,
	int nelems, enum dma_data_direction dir,
	unsigned long attrs)
{
	/* return nelems directly */
	return nelems;
}

static void hab_unmap_sg(struct device *dev,
	struct scatterlist *sgl, int nelems,
	enum dma_data_direction dir,
	unsigned long attrs)
{
	/*Do nothing */
}

static const struct dma_map_ops hab_dma_ops = {
	.map_sg		= hab_map_sg,
	.unmap_sg	= hab_unmap_sg,
};

static int hab_power_down_callback(
		struct notifier_block *nfb, unsigned long action, void *data)
{

	switch (action) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		pr_debug("reboot called %ld\n", action);
		hab_hypervisor_unregister(); /* only for single VM guest */
		break;
	}
	pr_debug("reboot called %ld done\n", action);
	return NOTIFY_DONE;
}

static struct notifier_block hab_reboot_notifier = {
	.notifier_call = hab_power_down_callback,
};

static int __init hab_init(void)
{
	int result;
	dev_t dev;

	result = alloc_chrdev_region(&hab_driver.major, 0, 1, "hab");

	if (result < 0) {
		pr_err("alloc_chrdev_region failed: %d\n", result);
		return result;
	}

	cdev_init(&hab_driver.cdev, &hab_fops);
	hab_driver.cdev.owner = THIS_MODULE;
	hab_driver.cdev.ops = &hab_fops;
	dev = MKDEV(MAJOR(hab_driver.major), 0);

	result = cdev_add(&hab_driver.cdev, dev, 1);

	if (result < 0) {
		unregister_chrdev_region(dev, 1);
		pr_err("cdev_add failed: %d\n", result);
		return result;
	}

	hab_driver.class = class_create(THIS_MODULE, "hab");

	if (IS_ERR(hab_driver.class)) {
		result = PTR_ERR(hab_driver.class);
		pr_err("class_create failed: %d\n", result);
		goto err;
	}

	hab_driver.dev = device_create(hab_driver.class, NULL,
					dev, &hab_driver, "hab");

	if (IS_ERR(hab_driver.dev)) {
		result = PTR_ERR(hab_driver.dev);
		pr_err("device_create failed: %d\n", result);
		goto err;
	}

	result = register_reboot_notifier(&hab_reboot_notifier);
	if (result)
		pr_err("failed to register reboot notifier %d\n", result);

	/* read in hab config, then configure pchans */
	result = do_hab_parse();

	if (!result) {
		hab_driver.kctx = hab_ctx_alloc(1);
		if (!hab_driver.kctx) {
			pr_err("hab_ctx_alloc failed");
			result = -ENOMEM;
			hab_hypervisor_unregister();
			goto err;
		} else
			set_dma_ops(hab_driver.dev, &hab_dma_ops);
	}
	hab_stat_init(&hab_driver);
	return result;

err:
	if (!IS_ERR_OR_NULL(hab_driver.dev))
		device_destroy(hab_driver.class, dev);
	if (!IS_ERR_OR_NULL(hab_driver.class))
		class_destroy(hab_driver.class);
	cdev_del(&hab_driver.cdev);
	unregister_chrdev_region(dev, 1);

	pr_err("Error in hab init, result %d\n", result);
	return result;
}

static void __exit hab_exit(void)
{
	dev_t dev;

	hab_hypervisor_unregister();
	hab_stat_deinit(&hab_driver);
	hab_ctx_put(hab_driver.kctx);
	dev = MKDEV(MAJOR(hab_driver.major), 0);
	device_destroy(hab_driver.class, dev);
	class_destroy(hab_driver.class);
	cdev_del(&hab_driver.cdev);
	unregister_chrdev_region(dev, 1);
	unregister_reboot_notifier(&hab_reboot_notifier);
	pr_debug("hab exit called\n");
}

subsys_initcall(hab_init);
module_exit(hab_exit);

MODULE_DESCRIPTION("Hypervisor abstraction layer");
MODULE_LICENSE("GPL v2");
