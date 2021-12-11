/*******************************************************************************
  Copyright (C) 2013  Vayavya Labs Pvt Ltd

  This implements all the API for managing HW timestamp & PTP.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/io.h>
#include <linux/delay.h>
#include "common.h"
#include "stmmac_ptp.h"

#define PTP_LIMIT 100000

static void stmmac_config_hw_tstamping(void __iomem *ioaddr, u32 data)
{
	writel(data, ioaddr + PTP_TCR);
}

static u32 stmmac_config_sub_second_increment(void __iomem *ioaddr,
					      u32 ptp_clock, int gmac4)
{
	u32 value = readl(ioaddr + PTP_TCR);
	u64 ss_inc = 0, sns_inc = 0, ptpclock = 0;
	u32 reg_value;

	/* For GMAC3.x, 4.x versions, convert the ptp_clock to nano second
	 *	formula = (1/ptp_clock) * 1000000000
	 * where ptp_clock is 50MHz if fine method is used to update system
	 */
	if (value & PTP_TCR_TSCFUPDT)
		ptpclock = (u64)ptp_clock;
	else
		ptpclock = (u64)ptp_clock;

	ss_inc = div_u64((1 * 1000000000ULL), ptpclock);
	sns_inc = 1000000000ULL - (ss_inc * ptpclock); //take remainder

	//sns_inc needs to be multiplied by 2^8, per spec.
	sns_inc = div_u64((sns_inc * 256), ptpclock);

	/* 0.465ns accuracy */
	if (!(value & PTP_TCR_TSCTRLSSR))
		ss_inc = div_u64((ss_inc * 1000), 465);

	ss_inc &= PTP_SSIR_SSINC_MASK;
	sns_inc &= PTP_SSIR_SNSINC_MASK;

	reg_value = ss_inc;

	if (gmac4)
		reg_value <<= GMAC4_PTP_SSIR_SSINC_SHIFT;

	reg_value |= (sns_inc << GMAC4_PTP_SSIR_SNSINC_SHIFT);

	writel(reg_value, ioaddr + PTP_SSIR);

	return reg_value;
}

static int stmmac_init_systime(void __iomem *ioaddr, u32 sec, u32 nsec)
{
	int limit;
	u32 value;

	/* wait for previous(if any) time initialization to complete. */
	limit = PTP_LIMIT;
	while (limit--) {
		if (!(readl_relaxed(ioaddr + PTP_TCR) &  PTP_TCR_TSINIT))
			break;
		usleep_range(1000, 1500);
	}
	if (limit < 0)
		return -EBUSY;

	writel(sec, ioaddr + PTP_STSUR);
	writel(nsec, ioaddr + PTP_STNSUR);
	/* issue command to initialize the system time value */
	value = readl(ioaddr + PTP_TCR);
	value |= PTP_TCR_TSINIT;
	writel(value, ioaddr + PTP_TCR);

	/* wait for present system time initialize to complete */
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + PTP_TCR) & PTP_TCR_TSINIT))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static int stmmac_config_addend(void __iomem *ioaddr, u32 addend)
{
	u32 value;
	int limit;

	writel(addend, ioaddr + PTP_TAR);
	/* issue command to update the addend value */
	value = readl(ioaddr + PTP_TCR);
	value |= PTP_TCR_TSADDREG;
	writel(value, ioaddr + PTP_TCR);

	/* wait for present addend update to complete */
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + PTP_TCR) & PTP_TCR_TSADDREG))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static int stmmac_adjust_systime(void __iomem *ioaddr, u32 sec, u32 nsec,
				 int add_sub, int gmac4)
{
	u32 value;
	int limit;

	/* wait for previous(if any) time adjust/update to complete. */
	limit = PTP_LIMIT;
	while (limit--) {
		if (!(readl_relaxed(ioaddr + PTP_TCR) & PTP_TCR_TSUPDT))
			break;
		usleep_range(1000, 1500);
	}
	if (limit < 0)
		return -EBUSY;

	if (add_sub) {
		/* If the new sec value needs to be subtracted with
		 * the system time, then MAC_STSUR reg should be
		 * programmed with (2^32 – <new_sec_value>)
		 */
		if (gmac4)
			sec = -sec;

		value = readl(ioaddr + PTP_TCR);
		if (value & PTP_TCR_TSCTRLSSR)
			nsec = (PTP_DIGITAL_ROLLOVER_MODE - nsec);
		else
			nsec = (PTP_BINARY_ROLLOVER_MODE - nsec);
	}

	writel(sec, ioaddr + PTP_STSUR);
	value = (add_sub << PTP_STNSUR_ADDSUB_SHIFT) | nsec;
	writel(value, ioaddr + PTP_STNSUR);

	/* issue command to initialize the system time value */
	value = readl(ioaddr + PTP_TCR);
	value |= PTP_TCR_TSUPDT;
	writel(value, ioaddr + PTP_TCR);

	/* wait for present system time adjust/update to complete */
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + PTP_TCR) & PTP_TCR_TSUPDT))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static u64 stmmac_get_systime(void __iomem *ioaddr)
{
	u64 ns;

	/* Get the TSSS value */
	ns = readl(ioaddr + PTP_STNSR);
	/* Get the TSS and convert sec time value to nanosecond */
	ns += readl(ioaddr + PTP_STSR) * 1000000000ULL;

	return ns;
}

const struct stmmac_hwtimestamp stmmac_ptp = {
	.config_hw_tstamping = stmmac_config_hw_tstamping,
	.init_systime = stmmac_init_systime,
	.config_sub_second_increment = stmmac_config_sub_second_increment,
	.config_addend = stmmac_config_addend,
	.adjust_systime = stmmac_adjust_systime,
	.get_systime = stmmac_get_systime,
};
