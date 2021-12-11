/* Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
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
#include "hab_grantable.h"

/*
 * use physical channel to send export parcel

 * local                      remote
 * send(export)        -->    IRQ store to export warehouse
 * wait(export ack)   <--     send(export ack)

 * the actual data consists the following 3 parts listed in order
 * 1. header (uint32_t) vcid|type|size
 * 2. export parcel (full struct)
 * 3. full contents in export->pdata
 */


static int hab_export_ack_find(struct uhab_context *ctx,
	struct hab_export_ack *expect_ack, struct virtual_channel *vchan)
{
	int ret = 0;
	struct hab_export_ack_recvd *ack_recvd, *tmp;

	spin_lock_bh(&ctx->expq_lock);

	list_for_each_entry_safe(ack_recvd, tmp, &ctx->exp_rxq, node) {
		if ((ack_recvd->ack.export_id == expect_ack->export_id &&
			ack_recvd->ack.vcid_local == expect_ack->vcid_local &&
			ack_recvd->ack.vcid_remote == expect_ack->vcid_remote)
			|| vchan->otherend_closed) {
			list_del(&ack_recvd->node);
			kfree(ack_recvd);
			ret = 1;
			break;
		}
		ack_recvd->age++;
		if (ack_recvd->age > Q_AGE_THRESHOLD) {
			list_del(&ack_recvd->node);
			kfree(ack_recvd);
		}
	}

	spin_unlock_bh(&ctx->expq_lock);

	return ret;
}

static int hab_export_ack_wait(struct uhab_context *ctx,
	struct hab_export_ack *expect_ack, struct virtual_channel *vchan)
{
	int ret;

	ret = wait_event_interruptible_timeout(ctx->exp_wq,
		hab_export_ack_find(ctx, expect_ack, vchan),
		HAB_HS_TIMEOUT);
	if (!ret || (ret == -ERESTARTSYS))
		ret = -EAGAIN;
	else if (vchan->otherend_closed)
		ret = -ENODEV;
	else if (ret > 0)
		ret = 0;
	return ret;
}

/*
 * Get id from free list first. if not available, new id is generated.
 * Once generated it will not be erased
 * assumptions: no handshake or memory map/unmap in this helper function
 */
struct export_desc_super *habmem_add_export(
		struct virtual_channel *vchan,
		int sizebytes,
		uint32_t flags)
{
	struct uhab_context *ctx = NULL;
	struct export_desc *exp = NULL;
	struct export_desc_super *exp_super = NULL;

	if (!vchan || !sizebytes)
		return NULL;

	exp_super = kzalloc(sizebytes, GFP_KERNEL);
	if (!exp_super)
		return NULL;

	exp = &exp_super->exp;
	idr_preload(GFP_KERNEL);
	spin_lock(&vchan->pchan->expid_lock);
	exp->export_id =
		idr_alloc(&vchan->pchan->expid_idr, exp, 1, 0, GFP_NOWAIT);
	spin_unlock(&vchan->pchan->expid_lock);
	idr_preload_end();

	exp->readonly = flags;
	exp->vchan = vchan;
	exp->vcid_local = vchan->id;
	exp->vcid_remote = vchan->otherend_id;
	exp->domid_local = vchan->pchan->vmid_local;
	exp->domid_remote = vchan->pchan->vmid_remote;
	exp->ctx = vchan->ctx;
	exp->pchan = vchan->pchan;

	ctx = vchan->ctx;
	write_lock(&ctx->exp_lock);
	ctx->export_total++;
	list_add_tail(&exp->node, &ctx->exp_whse);
	write_unlock(&ctx->exp_lock);

	return exp_super;
}

void habmem_remove_export(struct export_desc *exp)
{
	struct uhab_context *ctx = NULL;
	struct export_desc_super *exp_super =
			container_of(exp,
				struct export_desc_super,
				exp);

	if (!exp || !exp->ctx) {
		if (exp)
			pr_err("invalid info in exp %pK ctx %pK\n",
			   exp, exp->ctx);
		else
			pr_err("invalid exp\n");
		return;
	}

	ctx = exp->ctx;
	ctx->export_total--;
	exp->ctx = NULL;

	habmem_export_put(exp_super);
}

static void habmem_export_destroy(struct kref *refcount)
{
	struct physical_channel *pchan = NULL;
	struct export_desc_super *exp_super =
			container_of(
				refcount,
				struct export_desc_super,
				refcount);
	struct export_desc *exp = NULL;

	if (!exp_super) {
		pr_err("invalid exp_super\n");
		return;
	}

	exp = &exp_super->exp;
	if (!exp || !exp->pchan) {
		if (exp)
			pr_err("invalid info in exp %pK pchan %pK\n",
			   exp, exp->pchan);
		else
			pr_err("invalid exp\n");
		return;
	}

	pchan = exp->pchan;

	spin_lock(&pchan->expid_lock);
	idr_remove(&pchan->expid_idr, exp->export_id);
	spin_unlock(&pchan->expid_lock);

	habmem_exp_release(exp_super);
	kfree(exp_super);
}

/*
 * store the parcel to the warehouse, then send the parcel to remote side
 * both exporter composed export descriptor and the grantrefids are sent
 * as one msg to the importer side
 */
static int habmem_export_vchan(struct uhab_context *ctx,
		struct virtual_channel *vchan,
		int payload_size,
		uint32_t flags,
		uint32_t export_id)
{
	int ret;
	struct export_desc *exp;
	uint32_t sizebytes = sizeof(*exp) + payload_size;
	struct hab_export_ack expected_ack = {0};
	struct hab_header header = HAB_HEADER_INITIALIZER;

	exp = idr_find(&vchan->pchan->expid_idr, export_id);
	if (!exp) {
		pr_err("export vchan failed: exp_id %d, pchan %s\n",
				export_id, vchan->pchan->name);
		return -EINVAL;
	}

	HAB_HEADER_SET_SIZE(header, sizebytes);
	HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_EXPORT);
	HAB_HEADER_SET_ID(header, vchan->otherend_id);
	HAB_HEADER_SET_SESSION_ID(header, vchan->session_id);
	ret = physical_channel_send(vchan->pchan, &header, exp);

	if (ret != 0) {
		pr_err("failed to export payload to the remote %d\n", ret);
		return ret;
	}

	expected_ack.export_id = exp->export_id;
	expected_ack.vcid_local = exp->vcid_local;
	expected_ack.vcid_remote = exp->vcid_remote;
	ret = hab_export_ack_wait(ctx, &expected_ack, vchan);
	if (ret != 0) {
		pr_err("failed to receive remote export ack %d on vc %x\n",
				ret, vchan->id);
		return ret;
	}

	return ret;
}

void habmem_export_get(struct export_desc_super *exp_super)
{
	kref_get(&exp_super->refcount);
}

int habmem_export_put(struct export_desc_super *exp_super)
{
	return kref_put(&exp_super->refcount, habmem_export_destroy);
}

int hab_mem_export(struct uhab_context *ctx,
		struct hab_export *param,
		int kernel)
{
	int ret = 0;
	unsigned int payload_size = 0;
	uint32_t export_id = 0;
	struct virtual_channel *vchan;
	int page_count;
	int compressed = 0;

	if (!ctx || !param || !param->buffer || !param->sizebytes
		|| ((param->sizebytes % PAGE_SIZE) != 0))
		return -EINVAL;

	vchan = hab_get_vchan_fromvcid(param->vcid, ctx, 0);
	if (!vchan || !vchan->pchan) {
		ret = -ENODEV;
		goto err;
	}

	page_count = param->sizebytes/PAGE_SIZE;
	if (kernel) {
		ret = habmem_hyp_grant(vchan,
			(unsigned long)param->buffer,
			page_count,
			param->flags,
			vchan->pchan->dom_id,
			&compressed,
			&payload_size,
			&export_id);
	} else {
		ret = habmem_hyp_grant_user(vchan,
			(unsigned long)param->buffer,
			page_count,
			param->flags,
			vchan->pchan->dom_id,
			&compressed,
			&payload_size,
			&export_id);
	}
	if (ret < 0) {
		pr_err("habmem_hyp_grant vc %x failed size=%d ret=%d\n",
			   param->vcid, payload_size, ret);
		goto err;
	}

	ret = habmem_export_vchan(ctx,
		vchan,
		payload_size,
		param->flags,
		export_id);

	param->exportid = export_id;
err:
	if (vchan)
		hab_vchan_put(vchan);
	return ret;
}

int hab_mem_unexport(struct uhab_context *ctx,
		struct hab_unexport *param,
		int kernel)
{
	int ret = 0, found = 0;
	struct export_desc *exp = NULL, *tmp = NULL;
	struct virtual_channel *vchan;

	if (!ctx || !param)
		return -EINVAL;

	/* refcnt on the access */
	vchan = hab_get_vchan_fromvcid(param->vcid, ctx, 1);
	if (!vchan || !vchan->pchan) {
		ret = -ENODEV;
		goto err_novchan;
	}

	write_lock(&ctx->exp_lock);
	list_for_each_entry_safe(exp, tmp, &ctx->exp_whse, node) {
		if (param->exportid == exp->export_id &&
			vchan->pchan == exp->pchan) {
			list_del(&exp->node);
			found = 1;
			break;
		}
	}
	write_unlock(&ctx->exp_lock);

	if (!found) {
		ret = -EINVAL;
		goto err_novchan;
	}

	ret = habmem_hyp_revoke(exp->payload, exp->payload_count);
	if (ret) {
		pr_err("Error found in revoke grant with ret %d", ret);
		goto err_novchan;
	}
	habmem_remove_export(exp);

err_novchan:
	if (vchan)
		hab_vchan_put(vchan);

	return ret;
}

int hab_mem_import(struct uhab_context *ctx,
		struct hab_import *param,
		int kernel)
{
	int ret = 0, found = 0;
	struct export_desc *exp = NULL;
	struct virtual_channel *vchan;

	if (!ctx || !param)
		return -EINVAL;

	vchan = hab_get_vchan_fromvcid(param->vcid, ctx, 0);
	if (!vchan || !vchan->pchan) {
		ret = -ENODEV;
		goto err_imp;
	}

	spin_lock_bh(&ctx->imp_lock);
	list_for_each_entry(exp, &ctx->imp_whse, node) {
		if ((exp->export_id == param->exportid) &&
			(exp->pchan == vchan->pchan)) {
			found = 1;
			break;
		}
	}
	spin_unlock_bh(&ctx->imp_lock);

	if ((exp->payload_count << PAGE_SHIFT) != param->sizebytes) {
		pr_err("input size %d don't match buffer size %d\n",
			param->sizebytes, exp->payload_count << PAGE_SHIFT);
		ret = -EINVAL;
		goto err_imp;
	}

	if (!found) {
		pr_err("Fail to get export descriptor from export id %d\n",
			param->exportid);
		ret = -ENODEV;
		goto err_imp;
	}

	ret = habmem_imp_hyp_map(ctx->import_ctx, param, exp, kernel);

	if (ret) {
		pr_err("Import fail ret:%d pcnt:%d rem:%d 1st_ref:0x%X\n",
			ret, exp->payload_count,
			exp->domid_local, *((uint32_t *)exp->payload));
		goto err_imp;
	}

	exp->import_index = param->index;
	exp->kva = kernel ? (void *)param->kva : NULL;

err_imp:
	if (vchan)
		hab_vchan_put(vchan);

	return ret;
}

int hab_mem_unimport(struct uhab_context *ctx,
		struct hab_unimport *param,
		int kernel)
{
	int ret = 0, found = 0;
	struct export_desc *exp = NULL, *exp_tmp;
	struct virtual_channel *vchan;

	if (!ctx || !param)
		return -EINVAL;

	vchan = hab_get_vchan_fromvcid(param->vcid, ctx, 1);
	if (!vchan || !vchan->pchan) {
		if (vchan)
			hab_vchan_put(vchan);
		return -ENODEV;
	}

	spin_lock_bh(&ctx->imp_lock);
	list_for_each_entry_safe(exp, exp_tmp, &ctx->imp_whse, node) {
		if (exp->export_id == param->exportid &&
			exp->pchan == vchan->pchan) {
			/* same pchan is expected here */
			list_del(&exp->node);
			ctx->import_total--;
			found = 1;
			break;
		}
	}
	spin_unlock_bh(&ctx->imp_lock);

	if (!found)
		ret = -EINVAL;
	else {
		ret = habmm_imp_hyp_unmap(ctx->import_ctx, exp, kernel);
		if (ret) {
			pr_err("unmap fail id:%d pcnt:%d vcid:%d\n",
			exp->export_id, exp->payload_count, exp->vcid_remote);
		}
		param->kva = (uint64_t)exp->kva;
		kfree(exp);
	}

	if (vchan)
		hab_vchan_put(vchan);

	return ret;
}
