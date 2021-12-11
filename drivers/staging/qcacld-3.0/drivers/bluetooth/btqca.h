/*
 *  Bluetooth supports for Qualcomm Atheros ROME chips
 *
 *  Copyright (c) 2015 The Linux Foundation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define EDL_PATCH_CMD_OPCODE		(0xFC00)
#define EDL_NVM_ACCESS_OPCODE		(0xFC0B)
#define EDL_PATCH_CMD_LEN		(1)
#define EDL_PATCH_VER_REQ_CMD		(0x19)
#define EDL_PATCH_TLV_REQ_CMD		(0x1E)
#define EDL_NVM_ACCESS_SET_REQ_CMD	(0x01)
#define MAX_SIZE_PER_TLV_SEGMENT	(243)

#define EDL_CMD_REQ_RES_EVT		(0x00)
#define EDL_PATCH_VER_RES_EVT		(0x19)
#define EDL_APP_VER_RES_EVT		(0x02)
#define EDL_TVL_DNLD_RES_EVT		(0x04)
#define EDL_CMD_EXE_STATUS_EVT		(0x00)
#define EDL_SET_BAUDRATE_RSP_EVT	(0x92)
#define EDL_NVM_ACCESS_CODE_EVT		(0x0B)

#define EDL_TAG_ID_BD_ADDRESS		(2)
#define EDL_TAG_ID_HCI			(17)
#define EDL_TAG_ID_DEEP_SLEEP		(27)

#define QCA_BT_VER(s, p, b) (((u64)(s) << 32) | ((u64)(p & 0xffff) << 16) \
				| ((u64)(b & 0xffff)))

enum {
	ROME_SOC_ID_44 = 0x00000044,
};

enum {
	ROME_PROD_ID = 0x08,
};

enum {
	ROME_BUILD_VER_0302 = 0x0302,
};

enum {
	HST_SOC_ID_0200 = 0x400A0200,
};

enum {
	HST_PROD_ID = 0x10,
};

enum {
	HST_BUILD_VER_0200 = 0x0200,
};

enum {
	GNA_SOC_ID_0200 = 0x400B0200,
};

enum {
	GNA_PROD_ID = 0x12,
};

enum {
	GNA_BUILD_VER_0200 = 0x0200,
};

enum {
	ROME_VER_3_2 = QCA_BT_VER(ROME_SOC_ID_44, ROME_PROD_ID,
						ROME_BUILD_VER_0302),
	HST_VER_2_0  = QCA_BT_VER(HST_SOC_ID_0200, HST_PROD_ID,
						HST_BUILD_VER_0200),
	GNA_VER_2_0  = QCA_BT_VER(GNA_SOC_ID_0200, GNA_PROD_ID,
						GNA_BUILD_VER_0200),
};

enum qca_bardrate {
	QCA_BAUDRATE_115200 	= 0,
	QCA_BAUDRATE_57600,
	QCA_BAUDRATE_38400,
	QCA_BAUDRATE_19200,
	QCA_BAUDRATE_9600,
	QCA_BAUDRATE_230400,
	QCA_BAUDRATE_250000,
	QCA_BAUDRATE_460800,
	QCA_BAUDRATE_500000,
	QCA_BAUDRATE_720000,
	QCA_BAUDRATE_921600,
	QCA_BAUDRATE_1000000,
	QCA_BAUDRATE_1250000,
	QCA_BAUDRATE_2000000,
	QCA_BAUDRATE_3000000,
	QCA_BAUDRATE_4000000,
	QCA_BAUDRATE_1600000,
	QCA_BAUDRATE_3200000,
	QCA_BAUDRATE_3500000,
	QCA_BAUDRATE_AUTO 	= 0xFE,
	QCA_BAUDRATE_RESERVED
};

enum rome_tlv_type {
	TLV_TYPE_PATCH = 1,
	TLV_TYPE_NVM
};

struct rome_config {
	u8 type;
	char fwname[64];
	uint8_t user_baud_rate;
};

struct edl_event_hdr {
	__u8 cresp;
	__u8 rtype;
	__u8 data[0];
} __packed;

struct edl_hst_event_hdr {
	__u8 status;
	__u8 sbcode;
	__u8 len;
	__u8 data[0];
} __packed;

struct cmd_cpl_event {
	__u8 type;
	__u8 event;
	__u8 len;
	__u8 num;
	__le16 opcode;
	__u8 status;
	__u8 sbcode;
} __packed;

struct qca_version {
	__le32 product_id;
	__le16 patch_ver;
	__le16 rome_ver;
	__le32 soc_id;
} __packed;

struct tlv_seg_resp {
	__u8 result;
} __packed;

struct tlv_type_patch {
	__le32 total_size;
	__le32 data_length;
	__u8   format_version;
	__u8   signature;
	__le16 reserved1;
	__le16 product_id;
	__le16 rom_build;
	__le16 patch_version;
	__le16 reserved2;
	__le32 entry;
} __packed;

struct tlv_type_nvm {
	__le16 tag_id;
	__le16 tag_len;
	__le32 reserve1;
	__le32 reserve2;
	__u8   data[0];
} __packed;

struct tlv_type_hdr {
	__le32 type_len;
	__u8   data[0];
} __packed;

typedef int (*qca_enque_send_callback)(struct hci_dev *hdev,
						struct sk_buff *skb);

#if IS_ENABLED(CONFIG_BT_QCA)

int qca_set_bdaddr_rome(struct hci_dev *hdev, const bdaddr_t *bdaddr);
int qca_uart_setup_rome(struct hci_dev *hdev, uint8_t baudrate,
					qca_enque_send_callback callback);

#else

static inline int qca_set_bdaddr_rome(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return -EOPNOTSUPP;
}

static inline int qca_uart_setup_rome(struct hci_dev *hdev, int speed,
					qca_enque_send_callback callback)
{
	return -EOPNOTSUPP;
}

#endif
