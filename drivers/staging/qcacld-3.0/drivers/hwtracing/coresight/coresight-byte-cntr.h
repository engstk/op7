#ifndef _CORESIGHT_BYTE_CNTR_H
#define _CORESIGHT_BYTE_CNTR_H
#include <linux/cdev.h>
#include <linux/amba/bus.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/msm_mhi_dev.h>

struct byte_cntr {
	struct cdev		dev;
	struct class		*driver_class;
	bool			enable;
	bool			read_active;
	bool			pcie_chan_opened;
	uint32_t		byte_cntr_value;
	uint32_t		block_size;
	int			byte_cntr_irq;
	atomic_t		irq_cnt;
	wait_queue_head_t	wq;
	wait_queue_head_t	pcie_wait_wq;
	struct mutex		byte_cntr_lock;
	struct coresight_csr		*csr;
	u32			pcie_out_chan;
	struct mhi_dev_client	*out_handle;
	struct work_struct	pcie_open_work;
	struct work_struct	pcie_write_work;
	struct workqueue_struct	*pcie_wq;
	void (*event_notifier)(struct mhi_dev_client_cb_reason *cb);
	loff_t		offset;
};

extern void tmc_etr_byte_cntr_start(struct byte_cntr *byte_cntr_data);
extern void tmc_etr_byte_cntr_stop(struct byte_cntr *byte_cntr_data);
extern int etr_register_pcie_channel(struct byte_cntr *byte_cntr_data);
extern int etr_pcie_start(struct byte_cntr *byte_cntr_data);
extern void etr_pcie_stop(struct byte_cntr *byte_cntr_data);

#endif
