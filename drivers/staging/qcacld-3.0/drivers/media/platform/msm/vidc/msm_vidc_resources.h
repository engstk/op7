/* Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
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

#ifndef __MSM_VIDC_RESOURCES_H__
#define __MSM_VIDC_RESOURCES_H__

#include <linux/devfreq.h>
#include <linux/platform_device.h>
#include "msm_vidc.h"
#include <linux/soc/qcom/llcc-qcom.h>
#include "soc/qcom/cx_ipeak.h"

#define MAX_BUFFER_TYPES 32

struct dcvs_table {
	u32 load;
	u32 load_low;
	u32 load_high;
	u32 supported_codecs;
};

struct dcvs_limit {
	u32 min_mbpf;
	u32 fps;
};

struct reg_value_pair {
	u32 reg;
	u32 value;
};

struct reg_set {
	struct reg_value_pair *reg_tbl;
	int count;
};

struct addr_range {
	u32 start;
	u32 size;
};

struct addr_set {
	struct addr_range *addr_tbl;
	int count;
};

struct context_bank_info {
	struct list_head list;
	const char *name;
	u32 buffer_type;
	bool is_secure;
	struct addr_range addr_range;
	struct device *dev;
	struct dma_iommu_mapping *mapping;
};

struct buffer_usage_table {
	u32 buffer_type;
	u32 tz_usage;
};

struct buffer_usage_set {
	struct buffer_usage_table *buffer_usage_tbl;
	u32 count;
};

struct regulator_info {
	struct regulator *regulator;
	bool has_hw_power_collapse;
	char *name;
};

struct regulator_set {
	struct regulator_info *regulator_tbl;
	u32 count;
};

struct clock_info {
	const char *name;
	struct clk *clk;
	u32 count;
	bool has_scaling;
	bool has_mem_retention;
};

struct clock_set {
	struct clock_info *clock_tbl;
	u32 count;
};

struct bus_info {
	char *name;
	int master;
	int slave;
	unsigned int range[2];
	const char *governor;
	struct device *dev;
	struct devfreq_dev_profile devfreq_prof;
	struct devfreq *devfreq;
	struct msm_bus_client_handle *client;
	bool is_prfm_gov_used;
};

struct bus_set {
	struct bus_info *bus_tbl;
	u32 count;
};

struct reset_info {
	struct reset_control *rst;
	const char *name;
};

struct reset_set {
	struct reset_info *reset_tbl;
	u32 count;
};

struct allowed_clock_rates_table {
	u32 clock_rate;
};

struct clock_profile_entry {
	u32 codec_mask;
	u32 vpp_cycles;
	u32 vsp_cycles;
	u32 low_power_cycles;
};

struct clock_freq_table {
	struct clock_profile_entry *clk_prof_entries;
	u32 count;
};

struct subcache_info {
	const char *name;
	bool isactive;
	bool isset;
	struct llcc_slice_desc *subcache;
};

struct subcache_set {
	struct subcache_info *subcache_tbl;
	u32 count;
};

struct msm_vidc_mem_cdsp {
	struct device *dev;
};

struct msm_vidc_platform_resources {
	phys_addr_t firmware_base;
	phys_addr_t register_base;
	uint32_t register_size;
	uint32_t irq;
	uint32_t sku_version;
	struct allowed_clock_rates_table *allowed_clks_tbl;
	u32 allowed_clks_tbl_size;
	struct clock_freq_table clock_freq_tbl;
	struct dcvs_table *dcvs_tbl;
	uint32_t dcvs_tbl_size;
	struct dcvs_limit *dcvs_limit;
	bool sys_cache_present;
	bool sys_cache_res_set;
	struct subcache_set subcache_set;
	struct reg_set reg_set;
	struct addr_set qdss_addr_set;
	struct buffer_usage_set buffer_usage_set;
	uint32_t max_load;
	uint32_t max_hq_mbs_per_frame;
	uint32_t max_hq_mbs_per_sec;
	struct platform_device *pdev;
	struct regulator_set regulator_set;
	struct clock_set clock_set;
	struct bus_set bus_set;
	struct reset_set reset_set;
	bool use_non_secure_pil;
	bool sw_power_collapsible;
	bool slave_side_cp;
	struct list_head context_banks;
	bool thermal_mitigable;
	const char *fw_name;
	const char *hfi_version;
	bool never_unload_fw;
	bool debug_timeout;
	uint32_t pm_qos_latency_us;
	uint32_t max_inst_count;
	uint32_t max_secure_inst_count;
	int msm_vidc_hw_rsp_timeout;
	int msm_vidc_firmware_unload_delay;
	uint32_t msm_vidc_pwr_collapse_delay;
	bool domain_cvp;
	bool non_fatal_pagefaults;
	bool cache_pagetables;
	bool decode_batching;
	bool dcvs;
	struct msm_vidc_codec_data *codec_data;
	int codec_data_count;
	struct msm_vidc_csc_coeff *csc_coeff_data;
	struct msm_vidc_mem_cdsp mem_cdsp;
	uint32_t vpu_ver;
	uint32_t fw_cycles;
	uint32_t fw_vpp_cycles;
	uint32_t clk_freq_threshold;
	struct cx_ipeak_client *cx_ipeak_context;
	struct msm_vidc_ubwc_config *ubwc_config;
	uint32_t ubwc_config_length;
};


/**
 * The version 1 HFI strcuture for the UBWC configuration
 * @bMaxChannelsOverride : enable - 1 /disable - 0 max channel override
 * @bMalLengthOverride : enable - 1 /disable - 0 HBB override
 * @bHBBOverride : enable - 1 /disable - 0 mal length override
 * @nMaxChannels: Num DDR channels 4/8 channel,
 *                This is to control mircotilling mode.
 * @nMalLength : UBWC compression ratio granularity 32B/64B MAL
 * @nHighestBankBit : Valid range 13-19
 */

struct msm_vidc_ubwc_config_v1 {
	struct {
		u32 bMaxChannelsOverride : 1;
		u32 bMalLengthOverride : 1;
		u32 bHBBOverride : 1;
		u32 reserved1 : 29;
	} sOverrideBitInfo;

	u32 nMaxChannels;
	u32 nMalLength;
	u32 nHighestBankBit;
	u32 reserved2[2];
};

/**
 * The version 2 HFI strcuture for the UBWC configuration
 * @nSize : the size of the packet in bytes
 * @ePacketType: HFI_PROPERTY_SYS_UBWC_CONFIG
 * @v1 : The same UBWC config parameters as the version 1
 */

struct msm_vidc_ubwc_config {
	u32 nSize;
	u32 ePacketType;
	struct msm_vidc_ubwc_config_v1 v1;
};

static inline bool is_iommu_present(struct msm_vidc_platform_resources *res)
{
	return !list_empty(&res->context_banks);
}

#endif

