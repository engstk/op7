/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
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

struct physical_channel *
hab_pchan_alloc(struct hab_device *habdev, int otherend_id)
{
	struct physical_channel *pchan = kzalloc(sizeof(*pchan), GFP_KERNEL);

	if (!pchan)
		return NULL;

	idr_init(&pchan->vchan_idr);
	spin_lock_init(&pchan->vid_lock);
	idr_init(&pchan->expid_idr);
	spin_lock_init(&pchan->expid_lock);
	kref_init(&pchan->refcount);

	pchan->habdev = habdev;
	pchan->dom_id = otherend_id;
	pchan->closed = 1;
	pchan->hyp_data = NULL;

	INIT_LIST_HEAD(&pchan->vchannels);
	rwlock_init(&pchan->vchans_lock);
	spin_lock_init(&pchan->rxbuf_lock);

	spin_lock_bh(&habdev->pchan_lock);
	list_add_tail(&pchan->node, &habdev->pchannels);
	habdev->pchan_cnt++;
	spin_unlock_bh(&habdev->pchan_lock);

	return pchan;
}

static void hab_pchan_free(struct kref *ref)
{
	struct physical_channel *pchan =
		container_of(ref, struct physical_channel, refcount);
	struct virtual_channel *vchan;

	pr_debug("pchan %s refcnt %d\n", pchan->name,
			get_refcnt(pchan->refcount));

	spin_lock_bh(&pchan->habdev->pchan_lock);
	list_del(&pchan->node);
	pchan->habdev->pchan_cnt--;
	spin_unlock_bh(&pchan->habdev->pchan_lock);

	/* check vchan leaking */
	read_lock(&pchan->vchans_lock);
	list_for_each_entry(vchan, &pchan->vchannels, pnode) {
		/* no logging on the owner. it might have been gone */
		pr_warn("leaking vchan id %X remote %X refcnt %d\n",
				vchan->id, vchan->otherend_id,
				get_refcnt(vchan->refcount));
	}
	read_unlock(&pchan->vchans_lock);

	kfree(pchan->hyp_data);
	kfree(pchan);
}

struct physical_channel *
hab_pchan_find_domid(struct hab_device *dev, int dom_id)
{
	struct physical_channel *pchan;

	spin_lock_bh(&dev->pchan_lock);
	list_for_each_entry(pchan, &dev->pchannels, node)
		if (pchan->dom_id == dom_id || dom_id == HABCFG_VMID_DONT_CARE)
			break;

	if (pchan->dom_id != dom_id && dom_id != HABCFG_VMID_DONT_CARE) {
		pr_err("dom_id mismatch requested %d, existing %d\n",
			dom_id, pchan->dom_id);
		pchan = NULL;
	}

	if (pchan && !kref_get_unless_zero(&pchan->refcount))
		pchan = NULL;

	spin_unlock_bh(&dev->pchan_lock);

	return pchan;
}

void hab_pchan_get(struct physical_channel *pchan)
{
	if (pchan)
		kref_get(&pchan->refcount);
}

void hab_pchan_put(struct physical_channel *pchan)
{
	if (pchan)
		kref_put(&pchan->refcount, hab_pchan_free);
}
