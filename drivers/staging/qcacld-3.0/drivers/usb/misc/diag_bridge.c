/* Copyright (c) 2011-2015, 2019-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* add additional information to our printk's */
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/debugfs.h>
#include <linux/usb/diag_bridge.h>

#define DRIVER_DESC	"USB host diag bridge driver"
#define DRIVER_VERSION	"1.0"

#define MAX_DIAG_BRIDGE_DEVS	2
#define AUTOSUSP_DELAY_WITH_USB 1000

struct diag_bridge {
	struct usb_device	*udev;
	struct usb_interface	*ifc;
	struct usb_anchor	submitted;
	__u8			in_epAddr;
	__u8			out_epAddr;
	int			err;
	struct kref		kref;
	struct mutex		ifc_mutex;
	struct diag_bridge_ops	*ops;
	struct platform_device	*pdev;
	unsigned int		default_autosusp_delay;
	int			id;

	/* Support INT IN instead of BULK IN */
	bool			use_int_in_pipe;
	unsigned int		period;

	/* debugging counters */
	unsigned long		bytes_to_host;
	unsigned long		bytes_to_mdm;
	unsigned int		pending_reads;
	unsigned int		pending_writes;
	unsigned int		drop_count;
};
static struct diag_bridge *__dev[MAX_DIAG_BRIDGE_DEVS];

int diag_bridge_open(int id, struct diag_bridge_ops *ops)
{
	struct diag_bridge	*dev;

	if (id < 0 || id >= MAX_DIAG_BRIDGE_DEVS) {
		pr_err("Invalid device ID\n");
		return -ENODEV;
	}

	dev = __dev[id];
	mutex_lock(&dev->ifc_mutex);
	if (!dev->ifc) {
		pr_err("device is disconnected\n");
		mutex_unlock(&dev->ifc_mutex);
		return -ENODEV;
	}

	if (dev->ops) {
		pr_err("bridge already opened\n");
		mutex_unlock(&dev->ifc_mutex);
		return -EALREADY;
	}

	dev_dbg(&dev->ifc->dev, "%s\n", __func__);
	dev->ops = ops;
	dev->err = 0;

#ifdef CONFIG_PM_RUNTIME
	dev->default_autosusp_delay =
		dev->udev->dev.power.autosuspend_delay;
#endif
	pm_runtime_set_autosuspend_delay(&dev->udev->dev,
			AUTOSUSP_DELAY_WITH_USB);

	kref_get(&dev->kref);
	mutex_unlock(&dev->ifc_mutex);

	return 0;
}
EXPORT_SYMBOL(diag_bridge_open);

static void diag_bridge_delete(struct kref *kref)
{
	struct diag_bridge *dev = container_of(kref, struct diag_bridge, kref);
	struct usb_interface *ifc = dev->ifc;

	dev_dbg(&dev->ifc->dev, "%s\n", __func__);
	usb_set_intfdata(ifc, NULL);
	usb_kill_anchored_urbs(&dev->submitted);
	usb_put_intf(ifc);
	dev->ifc = NULL;
	usb_put_dev(dev->udev);
}

void diag_bridge_close(int id)
{
	struct diag_bridge	*dev;

	if (id < 0 || id >= MAX_DIAG_BRIDGE_DEVS) {
		pr_err("Invalid device ID\n");
		return;
	}

	dev = __dev[id];
	mutex_lock(&dev->ifc_mutex);
	if (!dev->ifc) {
		pr_err("device is disconnected\n");
		dev->err = -ENODEV;
		dev->ops = NULL;
		goto fail;
	}

	if (!dev->ops) {
		pr_err("can't close bridge that was not open\n");
		goto fail;
	}

	dev_dbg(&dev->ifc->dev, "%s\n", __func__);
	dev->err = -ENODEV;
	dev->ops = NULL;

	pm_runtime_set_autosuspend_delay(&dev->udev->dev,
		dev->default_autosusp_delay);

fail:
	kref_put(&dev->kref, diag_bridge_delete);
	mutex_unlock(&dev->ifc_mutex);

	return;
}
EXPORT_SYMBOL(diag_bridge_close);

static void diag_bridge_read_cb(struct urb *urb)
{
	struct diag_bridge	*dev = urb->context;
	struct diag_bridge_ops	*cbs = dev->ops;

	dev_dbg(&dev->ifc->dev, "%s: status:%d actual:%d\n", __func__,
			urb->status, urb->actual_length);

	/* save error so that subsequent read/write returns ENODEV */
	if (urb->status == -EPROTO)
		dev->err = urb->status;

	if (cbs && cbs->read_complete_cb)
		cbs->read_complete_cb(cbs->ctxt,
			urb->transfer_buffer,
			urb->transfer_buffer_length,
			urb->status < 0 ? urb->status : urb->actual_length);

	dev->bytes_to_host += urb->actual_length;
	dev->pending_reads--;
	kref_put(&dev->kref, diag_bridge_delete);
}

int diag_bridge_read(int id, char *data, int size)
{
	struct urb		*urb = NULL;
	unsigned int		pipe;
	struct diag_bridge	*dev;
	int			ret;

	if (id < 0 || id >= MAX_DIAG_BRIDGE_DEVS) {
		pr_err("Invalid device ID\n");
		return -ENODEV;
	}

	pr_debug("reading %d bytes\n", size);

	dev = __dev[id];
	mutex_lock(&dev->ifc_mutex);
	if (!dev->ifc) {
		pr_err("device is disconnected\n");
		ret = -ENODEV;
		goto error;
	}

	if (!dev->ops) {
		pr_err("bridge is not open\n");
		ret = -ENODEV;
		goto error;
	}

	if (!size) {
		dev_err(&dev->ifc->dev, "invalid size:%d\n", size);
		dev->drop_count++;
		ret = -EINVAL;
		goto error;
	}

	/* if there was a previous unrecoverable error, just quit */
	if (dev->err) {
		pr_err("EPROTO error occurred, or device disconnected\n");
		ret = -ENODEV;
		goto error;
	}

	kref_get(&dev->kref);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		dev_err(&dev->ifc->dev, "unable to allocate urb\n");
		ret = -ENOMEM;
		goto put_error;
	}

	ret = usb_autopm_get_interface(dev->ifc);
	if (ret < 0 && ret != -EAGAIN && ret != -EACCES) {
		pr_err_ratelimited("read: autopm_get failed:%d\n", ret);
		goto free_error;
	}

	if (dev->use_int_in_pipe) {
		pipe = usb_rcvintpipe(dev->udev, dev->in_epAddr);
		usb_fill_int_urb(urb, dev->udev, pipe, data, size,
		diag_bridge_read_cb, dev, dev->period);
	} else {
		pipe = usb_rcvbulkpipe(dev->udev, dev->in_epAddr);
		usb_fill_bulk_urb(urb, dev->udev, pipe, data, size,
				diag_bridge_read_cb, dev);
	}

	usb_anchor_urb(urb, &dev->submitted);
	dev->pending_reads++;

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		pr_err_ratelimited("submitting urb failed err:%d\n", ret);
		dev->pending_reads--;
		usb_unanchor_urb(urb);
		usb_autopm_put_interface(dev->ifc);
		goto free_error;
	}

	usb_autopm_put_interface(dev->ifc);
	usb_free_urb(urb);
	mutex_unlock(&dev->ifc_mutex);
	return ret;

free_error:
	usb_free_urb(urb);
put_error:
	/* If URB submit successful, this is done in the completion handler */
	kref_put(&dev->kref, diag_bridge_delete);
error:
	mutex_unlock(&dev->ifc_mutex);
	return ret;
}
EXPORT_SYMBOL(diag_bridge_read);

static void diag_bridge_write_cb(struct urb *urb)
{
	struct diag_bridge	*dev = urb->context;
	struct diag_bridge_ops	*cbs = dev->ops;

	dev_dbg(&dev->ifc->dev, "%s\n", __func__);

	usb_autopm_put_interface_async(dev->ifc);

	/* save error so that subsequent read/write returns ENODEV */
	if (urb->status == -EPROTO)
		dev->err = urb->status;

	if (cbs && cbs->write_complete_cb)
		cbs->write_complete_cb(cbs->ctxt,
			urb->transfer_buffer,
			urb->transfer_buffer_length,
			urb->status < 0 ? urb->status : urb->actual_length);

	dev->bytes_to_mdm += urb->actual_length;
	dev->pending_writes--;
	kref_put(&dev->kref, diag_bridge_delete);
}

int diag_bridge_write(int id, char *data, int size)
{
	struct urb		*urb = NULL;
	unsigned int		pipe;
	struct diag_bridge	*dev;
	int			ret;

	if (id < 0 || id >= MAX_DIAG_BRIDGE_DEVS) {
		pr_err("Invalid device ID\n");
		return -ENODEV;
	}

	pr_debug("writing %d bytes\n", size);

	dev = __dev[id];
	mutex_lock(&dev->ifc_mutex);
	if (!dev->ifc) {
		pr_err("device is disconnected\n");
		ret = -ENODEV;
		goto error;
	}

	if (!dev->ops) {
		pr_err("bridge is not open\n");
		ret = -ENODEV;
		goto error;
	}

	if (!size) {
		dev_err(&dev->ifc->dev, "invalid size:%d\n", size);
		ret = -EINVAL;
		goto error;
	}

	/* if there was a previous unrecoverable error, just quit */
	if (dev->err) {
		pr_err("EPROTO error occurred, or device disconnected\n");
		ret = -ENODEV;
		goto error;
	}

	kref_get(&dev->kref);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		dev_err(&dev->ifc->dev, "unable to allocate urb\n");
		ret = -ENOMEM;
		goto put_error;
	}

	ret = usb_autopm_get_interface(dev->ifc);
	if (ret < 0 && ret != -EAGAIN && ret != -EACCES) {
		pr_err_ratelimited("write: autopm_get failed:%d\n", ret);
		goto free_error;
	}

	pipe = usb_sndbulkpipe(dev->udev, dev->out_epAddr);
	usb_fill_bulk_urb(urb, dev->udev, pipe, data, size,
				diag_bridge_write_cb, dev);
	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &dev->submitted);
	dev->pending_writes++;

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		pr_err_ratelimited("submitting urb failed err:%d\n", ret);
		dev->pending_writes--;
		usb_unanchor_urb(urb);
		usb_autopm_put_interface(dev->ifc);
		goto free_error;
	}

	usb_free_urb(urb);
	mutex_unlock(&dev->ifc_mutex);
	return ret;

free_error:
	usb_free_urb(urb);
put_error:
	/* If URB submit successful, this is done in the completion handler */
	kref_put(&dev->kref, diag_bridge_delete);
error:
	mutex_unlock(&dev->ifc_mutex);
	return ret;
}
EXPORT_SYMBOL(diag_bridge_write);

#if defined(CONFIG_DEBUG_FS)
#define DEBUG_BUF_SIZE	512
static ssize_t diag_read_stats(struct file *file, char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char			*buf;
	int			i, ret = 0;

	buf = kzalloc(sizeof(char) * DEBUG_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < MAX_DIAG_BRIDGE_DEVS; i++) {
		struct diag_bridge *dev = __dev[i];

		ret += scnprintf(buf, DEBUG_BUF_SIZE,
				"epin:%d, epout:%d\n"
				"bytes to host: %lu\n"
				"bytes to mdm: %lu\n"
				"pending reads: %u\n"
				"pending writes: %u\n"
				"drop count:%u\n"
				"last error: %d\n",
				dev->in_epAddr, dev->out_epAddr,
				dev->bytes_to_host, dev->bytes_to_mdm,
				dev->pending_reads, dev->pending_writes,
				dev->drop_count,
				dev->err);
	}

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, ret);
	kfree(buf);
	return ret;
}

static ssize_t diag_reset_stats(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	int i;

	for (i = 0; i < MAX_DIAG_BRIDGE_DEVS; i++) {
		struct diag_bridge *dev = __dev[i];

		dev->bytes_to_host = dev->bytes_to_mdm = 0;
		dev->pending_reads = dev->pending_writes = 0;
		dev->drop_count = 0;
	}

	return count;
}

const struct file_operations diag_stats_ops = {
	.read = diag_read_stats,
	.write = diag_reset_stats,
};

static struct dentry *dent;

static void diag_bridge_debugfs_init(void)
{
	struct dentry *dfile;

	dent = debugfs_create_dir("diag_bridge", 0);
	if (IS_ERR(dent))
		return;

	dfile = debugfs_create_file("status", 0444, dent, 0, &diag_stats_ops);
	if (!dfile || IS_ERR(dfile))
		debugfs_remove(dent);
}

static void diag_bridge_debugfs_cleanup(void)
{
	debugfs_remove_recursive(dent);
	dent = NULL;
}
#else
static inline void diag_bridge_debugfs_init(void) { }
static inline void diag_bridge_debugfs_cleanup(void) { }
#endif

static int
diag_bridge_probe(struct usb_interface *ifc, const struct usb_device_id *id)
{
	struct diag_bridge		*dev;
	struct usb_host_interface	*ifc_desc;
	struct usb_endpoint_descriptor	*ep_desc;
	struct usb_device		*udev;
	int				i, devid, ret = -ENOMEM;

	pr_debug("id:%lu\n", id->driver_info);

	udev = interface_to_usbdev(ifc);
	if (udev->actconfig->desc.bNumInterfaces == 1) {
		pr_err("Invalid configuration: Only one interface\n");
		return -EINVAL;
	}

	devid = id->driver_info & 0xFF;
	if (devid < 0 || devid >= MAX_DIAG_BRIDGE_DEVS) {
		pr_err("Invalid device ID\n");
		return -ENODEV;
	}

	dev = __dev[devid];
	if (dev->ifc) {
		pr_err("Port already in use\n");
		return -ENODEV;
	}

	mutex_lock(&dev->ifc_mutex);
	dev->id = devid;
	dev->udev = usb_get_dev(udev);
	dev->ifc = usb_get_intf(ifc);
	kref_get(&dev->kref);
	init_usb_anchor(&dev->submitted);

	ifc_desc = ifc->cur_altsetting;
	for (i = 0; i < ifc_desc->desc.bNumEndpoints; i++) {
		ep_desc = &ifc_desc->endpoint[i].desc;
		if (!dev->in_epAddr && (usb_endpoint_is_bulk_in(ep_desc) ||
			usb_endpoint_is_int_in(ep_desc))) {
			dev->in_epAddr = ep_desc->bEndpointAddress;
			if (usb_endpoint_is_int_in(ep_desc)) {
				dev->use_int_in_pipe = 1;
				dev->period = ep_desc->bInterval;
			}
		}
		if (!dev->out_epAddr && usb_endpoint_is_bulk_out(ep_desc))
			dev->out_epAddr = ep_desc->bEndpointAddress;
	}

	usb_set_intfdata(ifc, dev);
	if (!(dev->in_epAddr && dev->out_epAddr)) {
		pr_err("could not find bulk in and bulk out endpoints\n");
		ret = -ENODEV;
		goto error;
	}

	dev->pdev = platform_device_register_simple("diag_bridge", devid,
						    NULL, 0);
	if (IS_ERR(dev->pdev)) {
		pr_err("unable to allocate platform device\n");
		ret = PTR_ERR(dev->pdev);
		goto error;
	}

	dev_dbg(&dev->ifc->dev, "%s: complete\n", __func__);
	mutex_unlock(&dev->ifc_mutex);

	return 0;

error:
	kref_put(&dev->kref, diag_bridge_delete);
	mutex_unlock(&dev->ifc_mutex);

	return ret;
}

static void diag_bridge_disconnect(struct usb_interface *ifc)
{
	struct diag_bridge	*dev = usb_get_intfdata(ifc);

	dev_dbg(&dev->ifc->dev, "%s\n", __func__);

	mutex_lock(&dev->ifc_mutex);
	platform_device_unregister(dev->pdev);
	kref_put(&dev->kref, diag_bridge_delete);
	mutex_unlock(&dev->ifc_mutex);
}

static int diag_bridge_suspend(struct usb_interface *ifc, pm_message_t message)
{
	struct diag_bridge	*dev = usb_get_intfdata(ifc);
	struct diag_bridge_ops	*cbs = dev->ops;
	int ret = 0;

	if (cbs && cbs->suspend) {
		ret = cbs->suspend(cbs->ctxt);
		if (ret) {
			dev_dbg(&dev->ifc->dev,
				"%s: diag veto'd suspend\n", __func__);
			return ret;
		}

		usb_kill_anchored_urbs(&dev->submitted);
	}

	return ret;
}

static int diag_bridge_resume(struct usb_interface *ifc)
{
	struct diag_bridge	*dev = usb_get_intfdata(ifc);
	struct diag_bridge_ops	*cbs = dev->ops;

	if (cbs && cbs->resume)
		cbs->resume(cbs->ctxt);

	return 0;
}

#define DEV_ID(n)		(n)

static const struct usb_device_id diag_bridge_ids[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(0x5c6, 0x901F, 0),
	.driver_info =  DEV_ID(0), },
	{ USB_DEVICE_INTERFACE_NUMBER(0x5c6, 0x90F3, 0),
	.driver_info =	DEV_ID(0), },

	{} /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, diag_bridge_ids);

static struct usb_driver diag_bridge_driver = {
	.name =		"diag_bridge",
	.probe =	diag_bridge_probe,
	.disconnect =	diag_bridge_disconnect,
	.suspend =	diag_bridge_suspend,
	.resume =	diag_bridge_resume,
	.reset_resume =	diag_bridge_resume,
	.id_table =	diag_bridge_ids,
	.supports_autosuspend = 1,
};

static int __init diag_bridge_init(void)
{
	struct diag_bridge *dev;
	int ret, i;
	int num_instances = 0;

	for (i = 0; i < MAX_DIAG_BRIDGE_DEVS; i++) {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev) {
			ret = -ENOMEM;
			goto dev_free;
		}

		mutex_init(&dev->ifc_mutex);
		num_instances++;
		__dev[i] = dev;
	}

	ret = usb_register(&diag_bridge_driver);
	if (ret) {
		pr_err("unable to register diag driver\n");
		goto dev_free;
	}

	diag_bridge_debugfs_init();

	return 0;

dev_free:
	for (i = 0; i < num_instances; i++) {
		dev = __dev[i];
		mutex_destroy(&dev->ifc_mutex);
		kfree(dev);
		__dev[i] = NULL;
	}

	return ret;
}

static void __exit diag_bridge_exit(void)
{
	struct diag_bridge *dev;
	int i;

	diag_bridge_debugfs_cleanup();
	usb_deregister(&diag_bridge_driver);
	for (i = 0; i < MAX_DIAG_BRIDGE_DEVS; i++) {
		dev = __dev[i];
		mutex_destroy(&dev->ifc_mutex);
		kfree(dev);
		__dev[i] = NULL;
	}
}

module_init(diag_bridge_init);
module_exit(diag_bridge_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
