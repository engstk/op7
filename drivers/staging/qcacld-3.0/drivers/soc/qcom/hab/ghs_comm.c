/* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
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
#include "hab_ghs.h"

int physical_channel_read(struct physical_channel *pchan,
		void *payload,
		size_t read_size)
{
	struct ghs_vdev *dev  = (struct ghs_vdev *)pchan->hyp_data;

	/* size in header is only for payload excluding the header itself */
	if (dev->read_size < read_size + sizeof(struct hab_header)) {
		pr_warn("read %zd is less than requested %zd plus header %zd\n",
			dev->read_size, read_size, sizeof(struct hab_header));
		read_size = dev->read_size;
	}

	/* always skip the header */
	memcpy(payload, (unsigned char *)dev->read_data +
		sizeof(struct hab_header) + dev->read_offset, read_size);
	dev->read_offset += read_size;

	return read_size;
}

int physical_channel_send(struct physical_channel *pchan,
		struct hab_header *header,
		void *payload)
{
	size_t sizebytes = HAB_HEADER_GET_SIZE(*header);
	struct ghs_vdev *dev  = (struct ghs_vdev *)pchan->hyp_data;
	GIPC_Result result;
	uint8_t *msg;
	int irqs_disabled = irqs_disabled();

	hab_spin_lock(&dev->io_lock, irqs_disabled);

	result = hab_gipc_wait_to_send(dev->endpoint);
	if (result != GIPC_Success) {
		hab_spin_unlock(&dev->io_lock, irqs_disabled);
		pr_err("failed to wait to send %d\n", result);
		return -EBUSY;
	}

	result = GIPC_PrepareMessage(dev->endpoint, sizebytes+sizeof(*header),
		(void **)&msg);
	if (result == GIPC_Full) {
		hab_spin_unlock(&dev->io_lock, irqs_disabled);
		/* need to wait for space! */
		pr_err("failed to reserve send msg for %zd bytes\n",
			sizebytes+sizeof(*header));
		return -EBUSY;
	} else if (result != GIPC_Success) {
		hab_spin_unlock(&dev->io_lock, irqs_disabled);
		pr_err("failed to send due to error %d\n", result);
		return -ENOMEM;
	}

	if (HAB_HEADER_GET_TYPE(*header) == HAB_PAYLOAD_TYPE_PROFILE) {
		struct timeval tv;
		struct habmm_xing_vm_stat *pstat =
					(struct habmm_xing_vm_stat *)payload;

		do_gettimeofday(&tv);
		pstat->tx_sec = tv.tv_sec;
		pstat->tx_usec = tv.tv_usec;
	}

	memcpy(msg, header, sizeof(*header));

	if (sizebytes)
		memcpy(msg+sizeof(*header), payload, sizebytes);

	result = GIPC_IssueMessage(dev->endpoint, sizebytes+sizeof(*header),
		header->id_type_size);
	hab_spin_unlock(&dev->io_lock, irqs_disabled);
	if (result != GIPC_Success) {
		pr_err("send error %d, sz %zd, prot %x\n",
			result, sizebytes+sizeof(*header),
			   header->id_type_size);
		return -EAGAIN;
	}

	return 0;
}

void physical_channel_rx_dispatch_common(unsigned long physical_channel)
{
	struct hab_header header;
	struct physical_channel *pchan =
		(struct physical_channel *)physical_channel;
	struct ghs_vdev *dev = (struct ghs_vdev *)pchan->hyp_data;
	GIPC_Result result;
	int irqs_disabled = irqs_disabled();

	hab_spin_lock(&pchan->rxbuf_lock, irqs_disabled);
	while (1) {
		dev->read_size = 0;
		dev->read_offset = 0;
		result = GIPC_ReceiveMessage(dev->endpoint,
				dev->read_data,
				GIPC_RECV_BUFF_SIZE_BYTES,
				&dev->read_size,
				&header.id_type_size);

		if (result == GIPC_Success || dev->read_size > 0) {
			 /* handle corrupted msg? */
			hab_msg_recv(pchan, dev->read_data);
			continue;
		} else if (result == GIPC_Empty) {
			/* no more pending msg */
			break;
		}
		pr_err("recv unhandled result %d, size %zd\n",
			result, dev->read_size);
		break;
	}
	hab_spin_unlock(&pchan->rxbuf_lock, irqs_disabled);
}
