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

#define HAB_DEVICE_CNSTR(__name__, __id__, __num__) { \
	.name = __name__,\
	.id = __id__,\
	.pchannels = LIST_HEAD_INIT(hab_devices[__num__].pchannels),\
	.pchan_lock = __SPIN_LOCK_UNLOCKED(hab_devices[__num__].pchan_lock),\
	.openq_list = LIST_HEAD_INIT(hab_devices[__num__].openq_list),\
	.openlock = __SPIN_LOCK_UNLOCKED(&hab_devices[__num__].openlock)\
	}

static const char hab_info_str[] = "Change: 17280941 Revision: #81";

/*
 * The following has to match habmm definitions, order does not matter if
 * hab config does not care either. When hab config is not present, the default
 * is as guest VM all pchans are pchan opener (FE)
 */
static struct hab_device hab_devices[] = {
	HAB_DEVICE_CNSTR(DEVICE_AUD1_NAME, MM_AUD_1, 0),
	HAB_DEVICE_CNSTR(DEVICE_AUD2_NAME, MM_AUD_2, 1),
	HAB_DEVICE_CNSTR(DEVICE_AUD3_NAME, MM_AUD_3, 2),
	HAB_DEVICE_CNSTR(DEVICE_AUD4_NAME, MM_AUD_4, 3),
	HAB_DEVICE_CNSTR(DEVICE_CAM1_NAME, MM_CAM_1, 4),
	HAB_DEVICE_CNSTR(DEVICE_CAM2_NAME, MM_CAM_2, 5),
	HAB_DEVICE_CNSTR(DEVICE_DISP1_NAME, MM_DISP_1, 6),
	HAB_DEVICE_CNSTR(DEVICE_DISP2_NAME, MM_DISP_2, 7),
	HAB_DEVICE_CNSTR(DEVICE_DISP3_NAME, MM_DISP_3, 8),
	HAB_DEVICE_CNSTR(DEVICE_DISP4_NAME, MM_DISP_4, 9),
	HAB_DEVICE_CNSTR(DEVICE_DISP5_NAME, MM_DISP_5, 10),
	HAB_DEVICE_CNSTR(DEVICE_GFX_NAME, MM_GFX, 11),
	HAB_DEVICE_CNSTR(DEVICE_VID_NAME, MM_VID, 12),
	HAB_DEVICE_CNSTR(DEVICE_VID2_NAME, MM_VID_2, 13),
	HAB_DEVICE_CNSTR(DEVICE_MISC_NAME, MM_MISC, 14),
	HAB_DEVICE_CNSTR(DEVICE_QCPE1_NAME, MM_QCPE_VM1, 15),
	HAB_DEVICE_CNSTR(DEVICE_CLK1_NAME, MM_CLK_VM1, 16),
	HAB_DEVICE_CNSTR(DEVICE_CLK2_NAME, MM_CLK_VM2, 17),
	HAB_DEVICE_CNSTR(DEVICE_FDE1_NAME, MM_FDE_1, 18),
	HAB_DEVICE_CNSTR(DEVICE_BUFFERQ1_NAME, MM_BUFFERQ_1, 19),
	HAB_DEVICE_CNSTR(DEVICE_DATA1_NAME, MM_DATA_NETWORK_1, 20),
	HAB_DEVICE_CNSTR(DEVICE_DATA2_NAME, MM_DATA_NETWORK_2, 21),
	HAB_DEVICE_CNSTR(DEVICE_HSI2S1_NAME, MM_HSI2S_1, 22),
};

struct hab_driver hab_driver = {
	.ndevices = ARRAY_SIZE(hab_devices),
	.devp = hab_devices,
	.uctx_list = LIST_HEAD_INIT(hab_driver.uctx_list),
	.drvlock = __SPIN_LOCK_UNLOCKED(hab_driver.drvlock),
	.imp_list = LIST_HEAD_INIT(hab_driver.imp_list),
	.imp_lock = __SPIN_LOCK_UNLOCKED(hab_driver.imp_lock),
};

struct uhab_context *hab_ctx_alloc(int kernel)
{
	struct uhab_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->closing = 0;
	INIT_LIST_HEAD(&ctx->vchannels);
	INIT_LIST_HEAD(&ctx->exp_whse);
	INIT_LIST_HEAD(&ctx->imp_whse);

	INIT_LIST_HEAD(&ctx->exp_rxq);
	init_waitqueue_head(&ctx->exp_wq);
	spin_lock_init(&ctx->expq_lock);

	spin_lock_init(&ctx->imp_lock);
	rwlock_init(&ctx->exp_lock);
	rwlock_init(&ctx->ctx_lock);

	INIT_LIST_HEAD(&ctx->pending_open);
	kref_init(&ctx->refcount);
	ctx->import_ctx = habmem_imp_hyp_open();
	if (!ctx->import_ctx) {
		pr_err("habmem_imp_hyp_open failed\n");
		kfree(ctx);
		return NULL;
	}
	ctx->kernel = kernel;

	spin_lock_bh(&hab_driver.drvlock);
	list_add_tail(&ctx->node, &hab_driver.uctx_list);
	hab_driver.ctx_cnt++;
	ctx->lb_be = hab_driver.b_loopback_be; /* loopback only */
	hab_driver.b_loopback_be = ~hab_driver.b_loopback_be; /* loopback only*/
	spin_unlock_bh(&hab_driver.drvlock);
	pr_debug("ctx %pK live %d loopback be %d\n",
		ctx, hab_driver.ctx_cnt, ctx->lb_be);

	return ctx;
}

/* ctx can only be freed when all the vchan releases the refcnt */
void hab_ctx_free(struct kref *ref)
{
	struct uhab_context *ctx =
		container_of(ref, struct uhab_context, refcount);
	struct hab_export_ack_recvd *ack_recvd, *tmp;
	struct virtual_channel *vchan;
	struct physical_channel *pchan;
	int i;
	struct uhab_context *ctxdel, *ctxtmp;
	struct hab_open_node *node;
	struct export_desc *exp = NULL, *exp_tmp = NULL;

	/* garbage-collect exp/imp buffers */
	write_lock_bh(&ctx->exp_lock);
	list_for_each_entry_safe(exp, exp_tmp, &ctx->exp_whse, node) {
		list_del(&exp->node);
		pr_debug("potential leak exp %d vcid %X recovered\n",
				exp->export_id, exp->vcid_local);
		habmem_hyp_revoke(exp->payload, exp->payload_count);
		habmem_remove_export(exp);
	}
	write_unlock_bh(&ctx->exp_lock);

	spin_lock_bh(&ctx->imp_lock);
	list_for_each_entry_safe(exp, exp_tmp, &ctx->imp_whse, node) {
		list_del(&exp->node);
		ctx->import_total--;
		pr_debug("leaked imp %d vcid %X for ctx is collected total %d\n",
			exp->export_id, exp->vcid_local,
			ctx->import_total);
		habmm_imp_hyp_unmap(ctx->import_ctx, exp, ctx->kernel);
		kfree(exp);
	}
	spin_unlock_bh(&ctx->imp_lock);

	habmem_imp_hyp_close(ctx->import_ctx, ctx->kernel);

	list_for_each_entry_safe(ack_recvd, tmp, &ctx->exp_rxq, node) {
		list_del(&ack_recvd->node);
		kfree(ack_recvd);
	}

	/* walk vchan list to find the leakage */
	spin_lock_bh(&hab_driver.drvlock);
	hab_driver.ctx_cnt--;
	list_for_each_entry_safe(ctxdel, ctxtmp, &hab_driver.uctx_list, node) {
		if (ctxdel == ctx)
			list_del(&ctxdel->node);
	}
	spin_unlock_bh(&hab_driver.drvlock);
	pr_debug("live ctx %d refcnt %d kernel %d close %d owner %d\n",
			hab_driver.ctx_cnt, get_refcnt(ctx->refcount),
			ctx->kernel, ctx->closing, ctx->owner);

	/* check vchans in this ctx */
	write_lock_bh(&ctx->ctx_lock);
	list_for_each_entry(vchan, &ctx->vchannels, node) {
		pr_warn("leak vchan id %X cnt %X remote %d in ctx\n",
				vchan->id, get_refcnt(vchan->refcount),
				vchan->otherend_id);
	}
	write_unlock_bh(&ctx->ctx_lock);

	/* check pending open */
	if (ctx->pending_cnt)
		pr_warn("potential leak of pendin_open nodes %d\n",
			ctx->pending_cnt);

	write_lock_bh(&ctx->ctx_lock);
	list_for_each_entry(node, &ctx->pending_open, node) {
		pr_warn("leak pending open vcid %X type %d subid %d openid %d\n",
			node->request.xdata.vchan_id, node->request.type,
			node->request.xdata.sub_id,
			node->request.xdata.open_id);
	}
	write_unlock_bh(&ctx->ctx_lock);

	/* check vchans belong to this ctx in all hab/mmid devices */
	for (i = 0; i < hab_driver.ndevices; i++) {
		struct hab_device *habdev = &hab_driver.devp[i];

		spin_lock_bh(&habdev->pchan_lock);
		list_for_each_entry(pchan, &habdev->pchannels, node) {

			/* check vchan ctx owner */
			write_lock(&pchan->vchans_lock);
			list_for_each_entry(vchan, &pchan->vchannels, pnode) {
				if (vchan->ctx == ctx) {
					pr_warn("leak vcid %X cnt %d pchan %s local %d remote %d\n",
						vchan->id,
						get_refcnt(vchan->refcount),
						pchan->name, pchan->vmid_local,
						pchan->vmid_remote);
				}
			}
			write_unlock(&pchan->vchans_lock);
		}
		spin_unlock_bh(&habdev->pchan_lock);
	}
	kfree(ctx);
}

/*
 * caller needs to call vchan_put() afterwards. this is used to refcnt
 * the local ioctl access based on ctx
 */
struct virtual_channel *hab_get_vchan_fromvcid(int32_t vcid,
		struct uhab_context *ctx, int ignore_remote)
{
	struct virtual_channel *vchan;

	read_lock(&ctx->ctx_lock);
	list_for_each_entry(vchan, &ctx->vchannels, node) {
		if (vcid == vchan->id) {
			if ((ignore_remote ? 0 : vchan->otherend_closed) ||
				vchan->closed ||
				!kref_get_unless_zero(&vchan->refcount)) {
				pr_debug("failed to inc vcid %x remote %x session %d refcnt %d close_flg remote %d local %d\n",
					vchan->id, vchan->otherend_id,
					vchan->session_id,
					get_refcnt(vchan->refcount),
					vchan->otherend_closed, vchan->closed);
				vchan = NULL;
			}
			read_unlock(&ctx->ctx_lock);
			return vchan;
		}
	}
	read_unlock(&ctx->ctx_lock);
	return NULL;
}

struct hab_device *find_hab_device(unsigned int mm_id)
{
	int i;

	for (i = 0; i < hab_driver.ndevices; i++) {
		if (hab_driver.devp[i].id == HAB_MMID_GET_MAJOR(mm_id))
			return &hab_driver.devp[i];
	}

	pr_err("%s: id=%d\n", __func__, mm_id);
	return NULL;
}
/*
 *   open handshake in FE and BE

 *   frontend            backend
 *  send(INIT)          wait(INIT)
 *  wait(INIT_ACK)      send(INIT_ACK)
 *  send(INIT_DONE)     wait(INIT_DONE)

 */
struct virtual_channel *frontend_open(struct uhab_context *ctx,
		unsigned int mm_id,
		int dom_id)
{
	int ret, ret2, open_id = 0;
	struct physical_channel *pchan = NULL;
	struct hab_device *dev;
	struct virtual_channel *vchan = NULL;
	static atomic_t open_id_counter = ATOMIC_INIT(0);
	struct hab_open_request request;
	struct hab_open_request *recv_request;
	int sub_id = HAB_MMID_GET_MINOR(mm_id);
	struct hab_open_node pending_open = { { 0 } };

	dev = find_hab_device(mm_id);
	if (dev == NULL) {
		pr_err("HAB device %d is not initialized\n", mm_id);
		ret = -EINVAL;
		goto err;
	}

	/* guest can find its own id */
	pchan = hab_pchan_find_domid(dev, dom_id);
	if (!pchan) {
		pr_err("hab_pchan_find_domid failed: dom_id=%d\n", dom_id);
		ret = -EINVAL;
		goto err;
	}

	open_id = atomic_inc_return(&open_id_counter);
	vchan = hab_vchan_alloc(ctx, pchan, open_id);
	if (!vchan) {
		pr_err("vchan alloc failed\n");
		ret = -ENOMEM;
		goto err;
	}

	/* Send Init sequence */
	hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT, pchan,
		vchan->id, sub_id, open_id);
	request.xdata.ver_fe = HAB_API_VER;
	ret = hab_open_request_send(&request);
	if (ret) {
		pr_err("hab_open_request_send failed: %d\n", ret);
		goto err;
	}

	pending_open.request = request;

	/* during wait app could be terminated */
	hab_open_pending_enter(ctx, pchan, &pending_open);

	/* Wait for Init-Ack sequence */
	hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_ACK, pchan,
		0, sub_id, open_id);
	/* wait forever */
	ret = hab_open_listen(ctx, dev, &request, &recv_request, 0);
	if (!ret && recv_request && ((recv_request->xdata.ver_fe & 0xFFFF0000)
		!= (recv_request->xdata.ver_be & 0xFFFF0000))) {
		/* version check */
		pr_err("hab major version mismatch fe %X be %X on mmid %d\n",
			recv_request->xdata.ver_fe,
			recv_request->xdata.ver_be, mm_id);

		hab_open_pending_exit(ctx, pchan, &pending_open);
		ret = -EPROTO;
		goto err;
	} else if (ret || !recv_request) {
		pr_err("hab_open_listen failed: %d, send cancel vcid %x subid %d openid %d\n",
			ret, vchan->id,
			sub_id, open_id);
		/* send cancel to BE due to FE's local close */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_CANCEL,
					pchan, vchan->id, sub_id, open_id);
		request.xdata.ver_fe = HAB_API_VER;
		ret2 = hab_open_request_send(&request);
		if (ret2)
			pr_err("send init_cancel failed %d on vcid %x\n", ret2,
				   vchan->id);
		hab_open_pending_exit(ctx, pchan, &pending_open);

		if (ret != -EINTR)
			ret = -EINVAL;
		goto err;
	}

	/* remove pending open locally after good pairing */
	hab_open_pending_exit(ctx, pchan, &pending_open);

	pr_debug("hab version match fe %X be %X on mmid %d\n",
		recv_request->xdata.ver_fe, recv_request->xdata.ver_be,
		mm_id);

	vchan->otherend_id = recv_request->xdata.vchan_id;
	hab_open_request_free(recv_request);

	/* Send Ack sequence */
	hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_DONE, pchan,
		0, sub_id, open_id);
	request.xdata.ver_fe = HAB_API_VER;
	ret = hab_open_request_send(&request);
	if (ret) {
		pr_err("failed to send init-done vcid %x remote %x openid %d\n",
		   vchan->id, vchan->otherend_id, vchan->session_id);
		goto err;
	}

	hab_pchan_put(pchan);

	return vchan;
err:
	if (vchan)
		hab_vchan_put(vchan);
	if (pchan)
		hab_pchan_put(pchan);

	return ERR_PTR(ret);
}

struct virtual_channel *backend_listen(struct uhab_context *ctx,
		unsigned int mm_id, int timeout)
{
	int ret, ret2;
	int open_id, ver_fe;
	int sub_id = HAB_MMID_GET_MINOR(mm_id);
	struct physical_channel *pchan = NULL;
	struct hab_device *dev;
	struct virtual_channel *vchan = NULL;
	struct hab_open_request request;
	struct hab_open_request *recv_request;
	uint32_t otherend_vchan_id;
	struct hab_open_node pending_open = { { 0 } };

	dev = find_hab_device(mm_id);
	if (dev == NULL) {
		pr_err("failed to find dev based on id %d\n", mm_id);
		ret = -EINVAL;
		goto err;
	}

	while (1) {
		/* Wait for Init sequence */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT,
			NULL, 0, sub_id, 0);
		/* cancel should not happen at this moment */
		ret = hab_open_listen(ctx, dev, &request, &recv_request,
				timeout);
		if (ret || !recv_request) {
			if (!ret && !recv_request)
				ret = -EINVAL;
			if (-EAGAIN == ret) {
				ret = -ETIMEDOUT;
			} else {
				/* device is closed */
				pr_err("open request wait failed ctx closing %d\n",
						ctx->closing);
			}
			goto err;
		} else if (!ret && recv_request &&
				   ((recv_request->xdata.ver_fe & 0xFFFF0000) !=
					(HAB_API_VER & 0xFFFF0000))) {
			int ret2;
			/* version check */
			pr_err("version mismatch fe %X be %X on mmid %d\n",
			   recv_request->xdata.ver_fe, HAB_API_VER, mm_id);
			hab_open_request_init(&request,
				HAB_PAYLOAD_TYPE_INIT_ACK,
				NULL, 0, sub_id, recv_request->xdata.open_id);
			request.xdata.ver_be = HAB_API_VER;
			/* reply to allow FE to bail out */
			ret2 = hab_open_request_send(&request);
			if (ret2)
				pr_err("send FE version mismatch failed mmid %d sub %d\n",
					   mm_id, sub_id);
			ret = -EPROTO;
			goto err;
		}

		/* guest id from guest */
		otherend_vchan_id = recv_request->xdata.vchan_id;
		open_id = recv_request->xdata.open_id;
		ver_fe = recv_request->xdata.ver_fe;
		pchan = recv_request->pchan;
		hab_pchan_get(pchan);
		hab_open_request_free(recv_request);
		recv_request = NULL;

		vchan = hab_vchan_alloc(ctx, pchan, open_id);
		if (!vchan) {
			ret = -ENOMEM;
			goto err;
		}

		vchan->otherend_id = otherend_vchan_id;

		/* Send Init-Ack sequence */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_ACK,
				pchan, vchan->id, sub_id, open_id);
		request.xdata.ver_fe = ver_fe; /* carry over */
		request.xdata.ver_be = HAB_API_VER;
		ret = hab_open_request_send(&request);
		if (ret)
			goto err;

		pending_open.request = request;
		/* wait only after init-ack is sent */
		hab_open_pending_enter(ctx, pchan, &pending_open);

		/* Wait for Ack sequence */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_DONE,
				pchan, 0, sub_id, open_id);
		ret = hab_open_listen(ctx, dev, &request, &recv_request,
			HAB_HS_TIMEOUT);
		hab_open_pending_exit(ctx, pchan, &pending_open);
		if (ret && recv_request &&
			recv_request->type == HAB_PAYLOAD_TYPE_INIT_CANCEL) {
			pr_err("listen cancelled vcid %x subid %d openid %d ret %d\n",
				request.xdata.vchan_id, request.xdata.sub_id,
				request.xdata.open_id, ret);

			/* FE cancels this session.
			 * So BE has to cancel its too
			 */
			hab_open_request_init(&request,
					HAB_PAYLOAD_TYPE_INIT_CANCEL, pchan,
					vchan->id, sub_id, open_id);
			ret2 = hab_open_request_send(&request);
			if (ret2)
				pr_err("send init_ack failed %d on vcid %x\n",
					ret2, vchan->id);
			hab_open_pending_exit(ctx, pchan, &pending_open);

			ret = -ENODEV; /* open request cancelled remotely */
			break;
		} else if (ret != -EAGAIN) {
			hab_open_pending_exit(ctx, pchan, &pending_open);
			break; /* received something. good case! */
		}

		/* stay in the loop retry */
		pr_warn("retry open ret %d vcid %X remote %X sub %d open %d\n",
			ret, vchan->id, vchan->otherend_id, sub_id, open_id);

		/* retry path starting here. free previous vchan */
		hab_open_request_init(&request, HAB_PAYLOAD_TYPE_INIT_CANCEL,
					pchan, vchan->id, sub_id, open_id);
		request.xdata.ver_fe = ver_fe;
		request.xdata.ver_be = HAB_API_VER;
		ret2 = hab_open_request_send(&request);
		if (ret2)
			pr_err("send init_ack failed %d on vcid %x\n", ret2,
				   vchan->id);
		hab_open_pending_exit(ctx, pchan, &pending_open);

		hab_vchan_put(vchan);
		vchan = NULL;
		hab_pchan_put(pchan);
		pchan = NULL;
	}

	if (ret || !recv_request) {
		pr_err("backend mmid %d listen error %d\n", mm_id, ret);
		ret = -EINVAL;
		goto err;
	}

	hab_open_request_free(recv_request);
	hab_pchan_put(pchan);
	return vchan;
err:
	if (ret != -ETIMEDOUT)
		pr_err("listen on mmid %d failed\n", mm_id);
	if (vchan)
		hab_vchan_put(vchan);
	if (pchan)
		hab_pchan_put(pchan);
	return ERR_PTR(ret);
}

long hab_vchan_send(struct uhab_context *ctx,
		int vcid,
		size_t sizebytes,
		void *data,
		unsigned int flags)
{
	struct virtual_channel *vchan;
	int ret;
	struct hab_header header = HAB_HEADER_INITIALIZER;
	int nonblocking_flag = flags & HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING;

	if (sizebytes > HAB_HEADER_SIZE_MASK) {
		pr_err("Message too large, %lu bytes, max is %d\n",
			sizebytes, HAB_HEADER_SIZE_MASK);
		return -EINVAL;
	}

	vchan = hab_get_vchan_fromvcid(vcid, ctx, 0);
	if (!vchan || vchan->otherend_closed) {
		ret = -ENODEV;
		goto err;
	}

	HAB_HEADER_SET_SIZE(header, sizebytes);
	if (flags & HABMM_SOCKET_SEND_FLAGS_XING_VM_STAT) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_PROFILE);
		if (sizebytes < sizeof(struct habmm_xing_vm_stat)) {
			pr_err("wrong profiling buffer size %zd, expect %zd\n",
				sizebytes,
				sizeof(struct habmm_xing_vm_stat));
			return -EINVAL;
		}
	} else if (flags & HABMM_SOCKET_XVM_SCHE_TEST) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_MSG);
	} else if (flags & HABMM_SOCKET_XVM_SCHE_TEST_ACK) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_MSG_ACK);
	} else if (flags & HABMM_SOCKET_XVM_SCHE_RESULT_REQ) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_RESULT_REQ);
	} else if (flags & HABMM_SOCKET_XVM_SCHE_RESULT_RSP) {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_SCHE_RESULT_RSP);
	} else {
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_MSG);
	}
	HAB_HEADER_SET_ID(header, vchan->otherend_id);
	HAB_HEADER_SET_SESSION_ID(header, vchan->session_id);

	while (1) {
		ret = physical_channel_send(vchan->pchan, &header, data);

		if (vchan->otherend_closed || nonblocking_flag ||
			ret != -EAGAIN)
			break;

		schedule();
	}
err:
	if (vchan)
		hab_vchan_put(vchan);

	return ret;
}

int hab_vchan_recv(struct uhab_context *ctx,
				struct hab_message **message,
				int vcid,
				int *rsize,
				unsigned int flags)
{
	struct virtual_channel *vchan;
	int ret = 0;
	int nonblocking_flag = flags & HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING;

	vchan = hab_get_vchan_fromvcid(vcid, ctx, 1);
	if (!vchan) {
		pr_err("vcid %X vchan 0x%pK ctx %pK\n", vcid, vchan, ctx);
		*message = NULL;
		return -ENODEV;
	}

	if (nonblocking_flag) {
		/*
		 * Try to pull data from the ring in this context instead of
		 * IRQ handler. Any available messages will be copied and queued
		 * internally, then fetched by hab_msg_dequeue()
		 */
		physical_channel_rx_dispatch((unsigned long) vchan->pchan);
	}

	ret = hab_msg_dequeue(vchan, message, rsize, flags);
	if (!(*message)) {
		if (nonblocking_flag)
			ret = -EAGAIN;
		else if (vchan->otherend_closed)
			ret = -ENODEV;
		else if (ret == -ERESTARTSYS)
			ret = -EINTR;
	}

	hab_vchan_put(vchan);
	return ret;
}

bool hab_is_loopback(void)
{
	return hab_driver.b_loopback;
}

int hab_vchan_open(struct uhab_context *ctx,
		unsigned int mmid,
		int32_t *vcid,
		int32_t timeout,
		uint32_t flags)
{
	struct virtual_channel *vchan = NULL;
	struct hab_device *dev;

	pr_debug("Open mmid=%d, loopback mode=%d, loopback be ctx %d\n",
		mmid, hab_driver.b_loopback, ctx->lb_be);

	if (!vcid)
		return -EINVAL;

	if (hab_is_loopback()) {
		if (ctx->lb_be)
			vchan = backend_listen(ctx, mmid, timeout);
		else
			vchan = frontend_open(ctx, mmid, LOOPBACK_DOM);
	} else {
		dev = find_hab_device(mmid);

		if (dev) {
			struct physical_channel *pchan =
				hab_pchan_find_domid(dev,
					HABCFG_VMID_DONT_CARE);
			if (pchan) {
				if (pchan->is_be)
					vchan = backend_listen(ctx, mmid,
							timeout);
				else
					vchan = frontend_open(ctx, mmid,
							HABCFG_VMID_DONT_CARE);
			} else {
				pr_err("open on nonexistent pchan (mmid %x)",
					mmid);
				return -ENODEV;
			}
		} else {
			pr_err("failed to find device, mmid %d\n", mmid);
			return -ENODEV;
		}
	}

	if (IS_ERR(vchan)) {
		if (-ETIMEDOUT != PTR_ERR(vchan) && -EAGAIN != PTR_ERR(vchan))
			pr_err("vchan open failed mmid=%d\n", mmid);
		return PTR_ERR(vchan);
	}

	pr_debug("vchan id %x remote id %x session %d\n", vchan->id,
			vchan->otherend_id, vchan->session_id);

	write_lock(&ctx->ctx_lock);
	list_add_tail(&vchan->node, &ctx->vchannels);
	ctx->vcnt++;
	write_unlock(&ctx->ctx_lock);

	*vcid = vchan->id;

	return 0;
}

void hab_send_close_msg(struct virtual_channel *vchan)
{
	struct hab_header header = {0};

	if (vchan && !vchan->otherend_closed) {
		HAB_HEADER_SET_SIZE(header, 0);
		HAB_HEADER_SET_TYPE(header, HAB_PAYLOAD_TYPE_CLOSE);
		HAB_HEADER_SET_ID(header, vchan->otherend_id);
		HAB_HEADER_SET_SESSION_ID(header, vchan->session_id);
		physical_channel_send(vchan->pchan, &header, NULL);
	}
}

int hab_vchan_close(struct uhab_context *ctx, int32_t vcid)
{
	struct virtual_channel *vchan = NULL, *tmp = NULL;
	int vchan_found = 0;
	int ret = 0;

	if (!ctx)
		return -EINVAL;

	write_lock(&ctx->ctx_lock);
	list_for_each_entry_safe(vchan, tmp, &ctx->vchannels, node) {
		if (vchan->id == vcid) {
			/* local close starts */
			vchan->closed = 1;

			/* vchan is not in this ctx anymore */
			list_del(&vchan->node);
			ctx->vcnt--;

			pr_debug("vcid %x remote %x session %d refcnt %d\n",
				vchan->id, vchan->otherend_id,
				vchan->session_id, get_refcnt(vchan->refcount));

			write_unlock(&ctx->ctx_lock);
			/* unblocking blocked in-calls */
			hab_vchan_stop_notify(vchan);
			hab_vchan_put(vchan); /* there is a lock inside */
			write_lock(&ctx->ctx_lock);
			vchan_found = 1;
			break;
		}
	}
	write_unlock(&ctx->ctx_lock);

	if (!vchan_found)
		ret = -ENODEV;

	return ret;
}

/*
 * To name the pchan - the pchan has two ends, either FE or BE locally.
 * if is_be is true, then this is listener for BE. pchane name use remote
 * FF's vmid from the table.
 * if is_be is false, then local is FE as opener. pchan name use local FE's
 * vmid (self)
 */
static int hab_initialize_pchan_entry(struct hab_device *mmid_device,
				int vmid_local, int vmid_remote, int is_be)
{
	char pchan_name[MAX_VMID_NAME_SIZE];
	struct physical_channel *pchan = NULL;
	int ret;
	int vmid = is_be ? vmid_remote : vmid_local; /* used for naming only */

	if (!mmid_device) {
		pr_err("habdev %pK, vmid local %d, remote %d, is be %d\n",
				mmid_device, vmid_local, vmid_remote, is_be);
		return -EINVAL;
	}

	snprintf(pchan_name, MAX_VMID_NAME_SIZE, "vm%d-", vmid);
	strlcat(pchan_name, mmid_device->name, MAX_VMID_NAME_SIZE);

	ret = habhyp_commdev_alloc((void **)&pchan, is_be, pchan_name,
					vmid_remote, mmid_device);
	if (ret) {
		pr_err("failed %d to allocate pchan %s, vmid local %d, remote %d, is_be %d, total %d\n",
				ret, pchan_name, vmid_local, vmid_remote,
				is_be, mmid_device->pchan_cnt);
	} else {
		/* local/remote id setting should be kept in lower level */
		pchan->vmid_local = vmid_local;
		pchan->vmid_remote = vmid_remote;
		pr_debug("pchan %s mmid %s local %d remote %d role %d\n",
				pchan_name, mmid_device->name,
				pchan->vmid_local, pchan->vmid_remote,
				pchan->dom_id);
	}

	return ret;
}

/*
 * generate pchan list based on hab settings table.
 * return status 0: success, otherwise failure
 */
static int hab_generate_pchan(struct local_vmid *settings, int i, int j)
{
	int k, ret = 0;

	pr_debug("%d as mmid %d in vmid %d\n",
			HABCFG_GET_MMID(settings, i, j), j, i);

	switch (HABCFG_GET_MMID(settings, i, j)) {
	case MM_AUD_START/100:
		for (k = MM_AUD_START + 1; k < MM_AUD_END; k++) {
			/*
			 * if this local pchan end is BE, then use
			 * remote FE's vmid. If local end is FE, then
			 * use self vmid
			 */
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_CAM_START/100:
		for (k = MM_CAM_START + 1; k < MM_CAM_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_DISP_START/100:
		for (k = MM_DISP_START + 1; k < MM_DISP_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_GFX_START/100:
		for (k = MM_GFX_START + 1; k < MM_GFX_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_VID_START/100:
		for (k = MM_VID_START + 1; k < MM_VID_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_MISC_START/100:
		for (k = MM_MISC_START + 1; k < MM_MISC_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_QCPE_START/100:
		for (k = MM_QCPE_START + 1; k < MM_QCPE_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;

	case MM_CLK_START/100:
		for (k = MM_CLK_START + 1; k < MM_CLK_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;
	case MM_FDE_START/100:
		for (k = MM_FDE_START + 1; k < MM_FDE_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;
	case MM_BUFFERQ_START/100:
		for (k = MM_BUFFERQ_START + 1; k < MM_BUFFERQ_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;
	case MM_DATA_START/100:
		for (k = MM_DATA_START + 1; k < MM_DATA_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;
	case MM_HSI2S_START/100:
		for (k = MM_HSI2S_START + 1; k < MM_HSI2S_END; k++) {
			ret += hab_initialize_pchan_entry(
					find_hab_device(k),
					settings->self,
					HABCFG_GET_VMID(settings, i),
					HABCFG_GET_BE(settings, i, j));
		}
		break;
	default:
		pr_err("failed to find mmid %d, i %d, j %d\n",
			HABCFG_GET_MMID(settings, i, j), i, j);

		break;
	}
	return ret;
}

/*
 * generate pchan list based on hab settings table.
 * return status 0: success, otherwise failure
 */
static int hab_generate_pchan_list(struct local_vmid *settings)
{
	int i, j, ret = 0;

	/* scan by valid VMs, then mmid */
	pr_debug("self vmid is %d\n", settings->self);
	for (i = 0; i < HABCFG_VMID_MAX; i++) {
		if (HABCFG_GET_VMID(settings, i) != HABCFG_VMID_INVALID &&
			HABCFG_GET_VMID(settings, i) != settings->self) {
			pr_debug("create pchans for vm %d\n", i);

			for (j = 1; j <= HABCFG_MMID_AREA_MAX; j++) {
				if (HABCFG_GET_MMID(settings, i, j)
						!= HABCFG_VMID_INVALID)
					ret = hab_generate_pchan(settings,
								i, j);
			}
		}
	}
	return ret;
}

/*
 * This function checks hypervisor plug-in readiness, read in hab configs,
 * and configure pchans
 */
#ifdef HABMM_HC_VMID
#define DEFAULT_GVMID 3
#else
#define DEFAULT_GVMID 2
#endif

int do_hab_parse(void)
{
	int result;
	int i;
	struct hab_device *device;

	/* single GVM is 2, multigvm is 2 or 3. GHS LV-GVM 2, LA-GVM 3 */
	int default_gvmid = DEFAULT_GVMID;

	pr_debug("hab parse starts for %s\n", hab_info_str);

	/* first check if hypervisor plug-in is ready */
	result = hab_hypervisor_register();
	if (result) {
		pr_err("register HYP plug-in failed, ret %d\n", result);
		return result;
	}

	/*
	 * Initialize open Q before first pchan starts.
	 * Each is for one pchan list
	 */
	for (i = 0; i < hab_driver.ndevices; i++) {
		device = &hab_driver.devp[i];
		init_waitqueue_head(&device->openq);
	}

	/* read in hab config and create pchans*/
	memset(&hab_driver.settings, HABCFG_VMID_INVALID,
				sizeof(hab_driver.settings));
	result = hab_parse(&hab_driver.settings);
	if (result) {
		pr_err("hab config open failed, prepare default gvm %d settings\n",
			   default_gvmid);
		fill_default_gvm_settings(&hab_driver.settings, default_gvmid,
						MM_AUD_START, MM_ID_MAX);
	}

	/* now generate hab pchan list */
	result  = hab_generate_pchan_list(&hab_driver.settings);
	if (result) {
		pr_err("generate pchan list failed, ret %d\n", result);
	} else {
		int pchan_total = 0;

		for (i = 0; i < hab_driver.ndevices; i++) {
			device = &hab_driver.devp[i];
			pchan_total += device->pchan_cnt;
		}
		pr_debug("ret %d, total %d pchans added, ndevices %d\n",
				 result, pchan_total, hab_driver.ndevices);
	}

	return result;
}

void hab_hypervisor_unregister_common(void)
{
	int status, i;
	struct uhab_context *ctx;
	struct virtual_channel *vchan;

	for (i = 0; i < hab_driver.ndevices; i++) {
		struct hab_device *habdev = &hab_driver.devp[i];
		struct physical_channel *pchan, *pchan_tmp;

		list_for_each_entry_safe(pchan, pchan_tmp,
				&habdev->pchannels, node) {
			status = habhyp_commdev_dealloc(pchan);
			if (status) {
				pr_err("failed to free pchan %pK, i %d, ret %d\n",
					pchan, i, status);
			}
		}
	}

	/* detect leaking uctx */
	spin_lock_bh(&hab_driver.drvlock);
	list_for_each_entry(ctx, &hab_driver.uctx_list, node) {
		pr_warn("leaking ctx owner %d refcnt %d kernel %d\n",
			ctx->owner, get_refcnt(ctx->refcount), ctx->kernel);
		/* further check vchan leak */
		read_lock(&ctx->ctx_lock);
		list_for_each_entry(vchan, &ctx->vchannels, node) {
			pr_warn("leaking vchan id %X remote %X refcnt %d\n",
					vchan->id, vchan->otherend_id,
					get_refcnt(vchan->refcount));
		}
		read_unlock(&ctx->ctx_lock);
	}
	spin_unlock_bh(&hab_driver.drvlock);
}
