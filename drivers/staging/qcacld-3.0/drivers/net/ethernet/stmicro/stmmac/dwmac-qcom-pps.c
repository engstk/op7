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
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mii.h>
#include <linux/of_mdio.h>
#include <linux/slab.h>
#include <linux/ipc_logging.h>
#include <linux/poll.h>
#include <linux/debugfs.h>
#include "stmmac.h"
#include "stmmac_platform.h"
#include "stmmac_ptp.h"
#include "dwmac-qcom-ethqos.h"

extern struct qcom_ethqos *pethqos;

static bool avb_class_a_msg_wq_flag;
static bool avb_class_b_msg_wq_flag;

static DECLARE_WAIT_QUEUE_HEAD(avb_class_a_msg_wq);
static DECLARE_WAIT_QUEUE_HEAD(avb_class_b_msg_wq);

static int strlcmp(const char *s, const char *t, size_t n)
{
	while (n-- && *t != '\0') {
		if (*s != *t) {
			return ((unsigned char)*s - (unsigned char)*t);
			n = 0;
		} else {
			++s, ++t;
		}
	}
	return (unsigned char)*s;
}

static void align_target_time_reg(u32 ch, void __iomem *ioaddr,
				  struct pps_cfg *eth_pps_cfg,
				  unsigned int align_ns)
{
	unsigned int system_s, system_ns, temp_system_s;

	system_s = readl_relaxed(ioaddr + 0xb00 + PTP_STSR);
	system_ns = readl_relaxed(ioaddr + 0xb00 + PTP_STNSR);
	temp_system_s = readl_relaxed(ioaddr + 0xb00 + PTP_STSR);

	if (temp_system_s != system_s) { // second roll over
		system_s = readl_relaxed(ioaddr + 0xb00 + PTP_STSR);
		system_ns = readl_relaxed(ioaddr + 0xb00 + PTP_STNSR);
	}

	system_ns += PPS_START_DELAY;
	if (system_ns >= align_ns)
		system_s += 1;

	writel_relaxed(system_s, ioaddr +
		       MAC_PPSX_TARGET_TIME_SEC(eth_pps_cfg->ppsout_ch));

	writel_relaxed(align_ns, ioaddr +
		       MAC_PPSX_TARGET_TIME_NSEC(eth_pps_cfg->ppsout_ch));
}

static u32 pps_config_sub_second_increment(void __iomem *ioaddr,
					   u32 ptp_clock, int gmac4)
{
	u32 value = readl_relaxed(ioaddr + PTP_TCR);
	u64 data;
	u64 sns_inc = 0;
	u32 reg_value;
	u32 reg_value2;
	/* For GMAC3.x, 4.x versions, convert the ptp_clock to nano second
	 *	formula = (1/ptp_clock) * 1000000000
	 * where ptp_clock is 50MHz if fine method is used to update system
	 */
	if (value & PTP_TCR_TSCFUPDT) {
		data = div_u64((1000000000ULL), ptp_clock);
		sns_inc = 1000000000ull - (data * ptp_clock);
		sns_inc = div_u64((sns_inc * 256), ptp_clock);

	} else {
		data = div_u64((1000000000ULL), ptp_clock);
	}
	/* 0.465ns accuracy */
	if (!(value & PTP_TCR_TSCTRLSSR))
		data = div_u64((data * 1000), 465);

	data &= PTP_SSIR_SSINC_MASK;

	reg_value = data;
	if (gmac4)
		reg_value <<= GMAC4_PTP_SSIR_SSINC_SHIFT;

	sns_inc &= PTP_SSIR_SNSINC_MASK;
	reg_value2 = sns_inc;
	if (gmac4)
		reg_value2 <<= GMAC4_PTP_SSIR_SNSINC_SHIFT;
	writel_relaxed(reg_value + reg_value2, ioaddr + PTP_SSIR);
	return data;
}

int ppsout_stop(struct stmmac_priv *priv, struct pps_cfg *eth_pps_cfg)
{
	u32 val;
	void __iomem *ioaddr = priv->ioaddr;

	val = readl_relaxed(ioaddr + MAC_PPS_CONTROL);
	val |= PPSCMDX(eth_pps_cfg->ppsout_ch, 0x5);
	val |= TRGTMODSELX(eth_pps_cfg->ppsout_ch, 0x3);
	val |= PPSEN0;
	writel_relaxed(val, ioaddr + MAC_PPS_CONTROL);
	return 0;
}

static irqreturn_t ethqos_pps_avb_class_a(int irq, void *dev_id)
{
	struct stmmac_priv *priv =
			(struct stmmac_priv *)dev_id;

	struct qcom_ethqos *ethqos = priv->plat->bsp_priv;

	ethqos->avb_class_a_intr_cnt++;
	avb_class_a_msg_wq_flag = 1;
	wake_up_interruptible(&avb_class_a_msg_wq);

	return IRQ_HANDLED;
}

static irqreturn_t ethqos_pps_avb_class_b(int irq, void *dev_id)
{
	struct stmmac_priv *priv =
				(struct stmmac_priv *)dev_id;

	struct qcom_ethqos *ethqos = priv->plat->bsp_priv;

	ethqos->avb_class_b_intr_cnt++;
	avb_class_b_msg_wq_flag = 1;
	wake_up_interruptible(&avb_class_b_msg_wq);
	return IRQ_HANDLED;
}

static void ethqos_register_pps_isr(struct stmmac_priv *priv, int ch)
{
	int ret;
	struct qcom_ethqos *ethqos = priv->plat->bsp_priv;

	if (ch == DWC_ETH_QOS_PPS_CH_2) {
		ret = request_irq(ethqos->pps_class_a_irq,
				  ethqos_pps_avb_class_a,
				  IRQF_TRIGGER_RISING, "stmmac_pps", priv);
		if (ret)
			ETHQOSERR("pps_avb_class_a_irq Failed ret=%d\n", ret);
		else
			ETHQOSDBG("pps_avb_class_a_irq pass\n");

	} else if (ch == DWC_ETH_QOS_PPS_CH_3) {
		ret = request_irq(ethqos->pps_class_b_irq,
				  ethqos_pps_avb_class_b,
				  IRQF_TRIGGER_RISING, "stmmac_pps", priv);
		if (ret)
			ETHQOSERR("pps_avb_class_b_irq Failed ret=%d\n", ret);
		else
			ETHQOSDBG("pps_avb_class_b_irq pass\n");
	}
}

static void ethqos_unregister_pps_isr(struct stmmac_priv *priv, int ch)
{
	struct qcom_ethqos *ethqos = priv->plat->bsp_priv;

	if (ch == DWC_ETH_QOS_PPS_CH_2) {
		free_irq(ethqos->pps_class_a_irq, priv);
	} else if (ch == DWC_ETH_QOS_PPS_CH_3) {
		free_irq(ethqos->pps_class_b_irq, priv);
	}
}

int ppsout_config(struct stmmac_priv *priv, struct pps_cfg *eth_pps_cfg)
{
	int interval, width;
	u32 sub_second_inc;
	void __iomem *ioaddr = priv->ioaddr;
	u32 val, align_ns = 0;
	u64 temp;

	if (!eth_pps_cfg->ppsout_start) {
		ppsout_stop(priv, eth_pps_cfg);
		if (eth_pps_cfg->ppsout_ch == DWC_ETH_QOS_PPS_CH_2 ||
		    eth_pps_cfg->ppsout_ch == DWC_ETH_QOS_PPS_CH_3)
			ethqos_unregister_pps_isr(priv, eth_pps_cfg->ppsout_ch);
		return 0;
	}

	val = readl_relaxed(ioaddr + MAC_PPS_CONTROL);

	sub_second_inc = pps_config_sub_second_increment
			 (priv->ptpaddr, eth_pps_cfg->ptpclk_freq,
			  priv->plat->has_gmac4);

	temp = (u64)((u64)eth_pps_cfg->ptpclk_freq << 32);
	priv->default_addend = div_u64(temp, priv->plat->clk_ptp_rate);
	priv->hw->ptp->config_addend(priv->ptpaddr, priv->default_addend);

	val &= ~PPSX_MASK(eth_pps_cfg->ppsout_ch);

	val |= PPSCMDX(eth_pps_cfg->ppsout_ch, 0x2);
	val |= TRGTMODSELX(eth_pps_cfg->ppsout_ch, 0x2);
	val |= PPSEN0;

	if (eth_pps_cfg->ppsout_ch == DWC_ETH_QOS_PPS_CH_2 ||
	    eth_pps_cfg->ppsout_ch == DWC_ETH_QOS_PPS_CH_3)
		ethqos_register_pps_isr(priv, eth_pps_cfg->ppsout_ch);

	writel_relaxed(0, ioaddr +
		       MAC_PPSX_TARGET_TIME_SEC(eth_pps_cfg->ppsout_ch));

	writel_relaxed(0, ioaddr +
		       MAC_PPSX_TARGET_TIME_NSEC(eth_pps_cfg->ppsout_ch));

	interval = ((eth_pps_cfg->ptpclk_freq + eth_pps_cfg->ppsout_freq / 2)
		   / eth_pps_cfg->ppsout_freq);

	width = ((interval * eth_pps_cfg->ppsout_duty) + 50) / 100 - 1;
	if (width >= interval)
		width = interval - 1;
	if (width < 0)
		width = 0;

	if (eth_pps_cfg->ppsout_align == 1) {
		align_ns = eth_pps_cfg->ppsout_align_ns;
		if (align_ns < PPS_ADJUST_NS)
			align_ns += (ONE_NS - PPS_ADJUST_NS);
		else
			align_ns -= PPS_ADJUST_NS;
		align_target_time_reg(eth_pps_cfg->ppsout_ch,
				      priv->ioaddr, eth_pps_cfg, align_ns);
	}

	writel_relaxed(interval, ioaddr +
		       MAC_PPSX_INTERVAL(eth_pps_cfg->ppsout_ch));

	writel_relaxed(width, ioaddr + MAC_PPSX_WIDTH(eth_pps_cfg->ppsout_ch));

	writel_relaxed(val, ioaddr + MAC_PPS_CONTROL);

	return 0;
}

int ethqos_init_pps(struct stmmac_priv *priv)
{
	u32 value;
	struct pps_cfg eth_pps_cfg = {0};

	priv->ptpaddr = priv->ioaddr + PTP_GMAC4_OFFSET;
	value = (PTP_TCR_TSENA | PTP_TCR_TSCFUPDT | PTP_TCR_TSUPDT);
	priv->hw->ptp->config_hw_tstamping(priv->ptpaddr, value);
	priv->hw->ptp->init_systime(priv->ptpaddr, 0, 0);
	priv->hw->ptp->adjust_systime(priv->ptpaddr, 0, 0, 0, 1);

	/*Configuaring PPS0 PPS output frequency to default 19.2 Mhz*/
	eth_pps_cfg.ppsout_ch = 0;
	eth_pps_cfg.ptpclk_freq = priv->plat->clk_ptp_req_rate;
	eth_pps_cfg.ppsout_freq = PPS_19_2_FREQ;
	eth_pps_cfg.ppsout_start = 1;
	eth_pps_cfg.ppsout_duty = 50;

	ppsout_config(priv, &eth_pps_cfg);
	return 0;
}

static ssize_t pps_fops_read(struct file *filp, char __user *buf,
			     size_t count, loff_t *f_pos)
{
	unsigned int len = 0, buf_len = 5000;
	char *temp_buf;
	ssize_t ret_cnt = 0;
	struct pps_info *info;

	info = filp->private_data;

	if (info->channel_no == AVB_CLASS_A_CHANNEL_NUM) {
		avb_class_a_msg_wq_flag = 0;
		temp_buf = kzalloc(buf_len, GFP_KERNEL);
		if (!temp_buf)
			return -ENOMEM;

		if (pethqos)
			len = scnprintf(temp_buf, buf_len,
					"%ld\n", pethqos->avb_class_a_intr_cnt);
		else
			len = scnprintf(temp_buf, buf_len, "0\n");

		ret_cnt = simple_read_from_buffer(buf, count, f_pos,
						  temp_buf, len);
		kfree(temp_buf);
		if (pethqos)
			ETHQOSERR("poll pps2intr info=%d sent by kernel\n",
				  pethqos->avb_class_a_intr_cnt);
	} else if (info->channel_no == AVB_CLASS_B_CHANNEL_NUM) {
		avb_class_b_msg_wq_flag = 0;
		temp_buf = kzalloc(buf_len, GFP_KERNEL);
		if (!temp_buf)
			return -ENOMEM;

		if (pethqos)
			len = scnprintf(temp_buf, buf_len,
					"%ld\n", pethqos->avb_class_b_intr_cnt);
		else
			len = scnprintf(temp_buf, buf_len, "0\n");

		ret_cnt = simple_read_from_buffer
			  (buf, count, f_pos, temp_buf, len);
		kfree(temp_buf);

	} else {
		ETHQOSERR("invalid channel %d\n", info->channel_no);
	}
	return ret_cnt;
}

static unsigned int pps_fops_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct pps_info *info;

	info = file->private_data;
	if (info->channel_no == AVB_CLASS_A_CHANNEL_NUM) {
		ETHQOSERR("avb_class_a_fops_poll wait\n");

		poll_wait(file, &avb_class_a_msg_wq, wait);

		if (avb_class_a_msg_wq_flag == 1) {
			//Sending read mask
			mask |= POLLIN | POLLRDNORM;
		}
	} else if (info->channel_no == AVB_CLASS_B_CHANNEL_NUM) {
		poll_wait(file, &avb_class_b_msg_wq, wait);

		if (avb_class_b_msg_wq_flag == 1) {
			//Sending read mask
			mask |= POLLIN | POLLRDNORM;
		}
	} else {
		ETHQOSERR("invalid channel %d\n", info->channel_no);
	}
	return mask;
}

static int pps_open(struct inode *inode, struct file *file)
{
	struct pps_info *info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!strlcmp(file->f_path.dentry->d_iname,
		     AVB_CLASS_A_POLL_DEV_NODE,
		     strlen(AVB_CLASS_A_POLL_DEV_NODE))) {
		ETHQOSERR("pps open file name =%s\n",
			  file->f_path.dentry->d_iname);
		info->channel_no = AVB_CLASS_A_CHANNEL_NUM;
	} else if (!strlcmp(file->f_path.dentry->d_iname,
			    AVB_CLASS_B_POLL_DEV_NODE,
			    strlen(AVB_CLASS_B_POLL_DEV_NODE))) {
		ETHQOSERR("pps open file name =%s\n",
			  file->f_path.dentry->d_iname);
		info->channel_no = AVB_CLASS_B_CHANNEL_NUM;
	} else {
		ETHQOSERR("stsrncmp failed for %s\n",
			  file->f_path.dentry->d_iname);
	}
	file->private_data = info;
	return 0;
}

static int pps_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations pps_fops = {
	.owner = THIS_MODULE,
	.open = pps_open,
	.release = pps_release,
	.read = pps_fops_read,
	.poll = pps_fops_poll,
};

int create_pps_interrupt_device_node(dev_t *pps_dev_t,
				     struct cdev **pps_cdev,
				     struct class **pps_class,
				     char *pps_dev_node_name)
{
	int ret;

	ret = alloc_chrdev_region(pps_dev_t, 0, 1,
				  pps_dev_node_name);
	if (ret) {
		ETHQOSERR("alloc_chrdev_region error for node %s\n",
			  pps_dev_node_name);
		goto alloc_chrdev1_region_fail;
	}

	*pps_cdev = cdev_alloc();
	if (!*pps_cdev) {
		ret = -ENOMEM;
		ETHQOSERR("failed to alloc cdev\n");
		goto fail_alloc_cdev;
	}
	cdev_init(*pps_cdev, &pps_fops);

	ret = cdev_add(*pps_cdev, *pps_dev_t, 1);
	if (ret < 0) {
		ETHQOSERR(":cdev_add err=%d\n", -ret);
		goto cdev1_add_fail;
	}

	*pps_class = class_create(THIS_MODULE, pps_dev_node_name);
	if (!*pps_class) {
		ret = -ENODEV;
		ETHQOSERR("failed to create class\n");
		goto fail_create_class;
	}

	if (!device_create(*pps_class, NULL,
			   *pps_dev_t, NULL, pps_dev_node_name)) {
		ret = -EINVAL;
		ETHQOSERR("failed to create device_create\n");
		goto fail_create_device;
	}

	return 0;

fail_create_device:
	class_destroy(*pps_class);
fail_create_class:
	cdev_del(*pps_cdev);
cdev1_add_fail:
fail_alloc_cdev:
	unregister_chrdev_region(*pps_dev_t, 1);
alloc_chrdev1_region_fail:
		return ret;
}
