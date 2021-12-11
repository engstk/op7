/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "clk: %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,camcc-sm6150.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "clk-alpha-pll.h"
#include "vdd-level-sm6150.h"

#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_NUM, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_MX_NUM, 1, vdd_mx_corner);

enum {
	P_BI_TCXO,
	P_CAM_CC_PLL0_OUT_AUX,
	P_CAM_CC_PLL1_OUT_AUX,
	P_CAM_CC_PLL2_OUT_AUX2,
	P_CAM_CC_PLL2_OUT_EARLY,
	P_CAM_CC_PLL3_OUT_MAIN,
	P_CORE_BI_PLL_TEST_SE,
};

static const struct parent_map cam_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL1_OUT_AUX, 2 },
	{ P_CAM_CC_PLL0_OUT_AUX, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_0[] = {
	"bi_tcxo",
	"cam_cc_pll1_out_aux",
	"cam_cc_pll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map cam_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL2_OUT_EARLY, 4 },
	{ P_CAM_CC_PLL3_OUT_MAIN, 5 },
	{ P_CAM_CC_PLL0_OUT_AUX, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_1[] = {
	"bi_tcxo",
	"cam_cc_pll2_out_early",
	"cam_cc_pll3_out_main",
	"cam_cc_pll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map cam_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL1_OUT_AUX, 2 },
	{ P_CAM_CC_PLL2_OUT_EARLY, 4 },
	{ P_CAM_CC_PLL3_OUT_MAIN, 5 },
	{ P_CAM_CC_PLL0_OUT_AUX, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_2[] = {
	"bi_tcxo",
	"cam_cc_pll1_out_aux",
	"cam_cc_pll2_out_early",
	"cam_cc_pll3_out_main",
	"cam_cc_pll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map cam_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL2_OUT_AUX2, 1 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_3[] = {
	"bi_tcxo",
	"cam_cc_pll2_out_aux2",
	"core_bi_pll_test_se",
};

static const struct parent_map cam_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL3_OUT_MAIN, 5 },
	{ P_CAM_CC_PLL0_OUT_AUX, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_4[] = {
	"bi_tcxo",
	"cam_cc_pll3_out_main",
	"cam_cc_pll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map cam_cc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL0_OUT_AUX, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_5[] = {
	"bi_tcxo",
	"cam_cc_pll0_out_aux",
	"core_bi_pll_test_se",
};

static const struct parent_map cam_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_CAM_CC_PLL1_OUT_AUX, 2 },
	{ P_CAM_CC_PLL3_OUT_MAIN, 5 },
	{ P_CAM_CC_PLL0_OUT_AUX, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const cam_cc_parent_names_6[] = {
	"bi_tcxo",
	"cam_cc_pll1_out_aux",
	"cam_cc_pll3_out_main",
	"cam_cc_pll0_out_aux",
	"core_bi_pll_test_se",
};

static struct pll_vco cam_cc_pll2_vco[] = {
	{ 500000000, 1250000000, 0 },
};

static struct pll_vco cam_cc_pll_vco[] = {
	{ 1000000000, 2000000000, 0 },
	{ 500000000, 1000000000, 2 },
};

/* 600MHz configuration */
static struct alpha_pll_config cam_cc_pll0_config = {
	.l = 0x1F,
	.alpha_u = 0x40,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = 0x3 << 20,
	.aux_output_mask = BIT(1),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll cam_cc_pll0_out_aux = {
	.offset = 0x0,
	.vco_table = cam_cc_pll_vco,
	.num_vco = ARRAY_SIZE(cam_cc_pll_vco),
	.config = &cam_cc_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_pll0_out_aux",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 1000000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 808MHz configuration */
static struct alpha_pll_config cam_cc_pll1_config = {
	.l = 0x2A,
	.alpha_u = 0x15,
	.alpha = 0x55555555,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = 0x3 << 20,
	.aux_output_mask = BIT(1),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll cam_cc_pll1_out_aux = {
	.offset = 0x1000,
	.vco_table = cam_cc_pll_vco,
	.num_vco = ARRAY_SIZE(cam_cc_pll_vco),
	.config = &cam_cc_pll1_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_pll1_out_aux",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 1000000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 960MHz configuration */
static struct alpha_pll_config cam_cc_pll2_config = {
	.l = 0x32,
	.vco_val = 0x0 << 20,
	.vco_mask = 0x3 << 20,
	.early_output_mask = BIT(3),
	.aux2_output_mask = BIT(2),
	.post_div_val = 0x1 << 8,
	.post_div_mask = 0x3 << 8,
	.config_ctl_val = 0x04289,
	.test_ctl_val = 0x08000000,
	.test_ctl_mask = 0x08000000,
};

static struct clk_alpha_pll cam_cc_pll2_out_early = {
	.offset = 0x2000,
	.vco_table = cam_cc_pll2_vco,
	.num_vco = ARRAY_SIZE(cam_cc_pll2_vco),
	.config = &cam_cc_pll2_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_pll2_out_early",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_MX_NUM,
			.rate_max = (unsigned long[VDD_MX_NUM]) {
				[VDD_MX_LOW] = 1250000000},
		},
	},
};

static struct clk_alpha_pll_postdiv cam_cc_pll2_out_aux2 = {
	.offset = 0x2000,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_pll2_out_aux2",
		.parent_names = (const char *[]){ "cam_cc_pll2_out_early" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

/* 1080MHz configuration */
static struct alpha_pll_config cam_cc_pll3_config = {
	.l = 0x38,
	.alpha_u = 0x40,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x0 << 20,
	.vco_mask = 0x3 << 20,
	.main_output_mask = BIT(0),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll cam_cc_pll3_out_main = {
	.offset = 0x3000,
	.vco_table = cam_cc_pll_vco,
	.num_vco = ARRAY_SIZE(cam_cc_pll_vco),
	.config = &cam_cc_pll3_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_pll3_out_main",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_MX_NUM,
			.rate_max = (unsigned long[VDD_MX_NUM]) {
				[VDD_MX_MIN] = 1000000000,
				[VDD_MX_NOMINAL] = 2000000000},
		},
	},
};

static const struct freq_tbl ftbl_cam_cc_bps_clk_src[] = {
	F(200000000, P_CAM_CC_PLL0_OUT_AUX, 3, 0, 0),
	F(360000000, P_CAM_CC_PLL3_OUT_MAIN, 3, 0, 0),
	F(432000000, P_CAM_CC_PLL3_OUT_MAIN, 2.5, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_EARLY, 2, 0, 0),
	F(540000000, P_CAM_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_AUX, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_bps_clk_src = {
	.cmd_rcgr = 0x6010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_bps_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_bps_clk_src",
		.parent_names = cam_cc_parent_names_1,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000,
			[VDD_LOW] = 360000000,
			[VDD_LOW_L1] = 432000000,
			[VDD_NOMINAL] = 480000000,
			[VDD_NOMINAL_L1] = 540000000,
			[VDD_HIGH] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_cci_clk_src[] = {
	F(37500000, P_CAM_CC_PLL0_OUT_AUX, 16, 0, 0),
	F(50000000, P_CAM_CC_PLL0_OUT_AUX, 12, 0, 0),
	F(100000000, P_CAM_CC_PLL0_OUT_AUX, 6, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cci_clk_src = {
	.cmd_rcgr = 0xb0d8,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_5,
	.freq_tbl = ftbl_cam_cc_cci_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_cci_clk_src",
		.parent_names = cam_cc_parent_names_5,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 37500000,
			[VDD_LOW] = 50000000,
			[VDD_NOMINAL] = 100000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_cphy_rx_clk_src[] = {
	F(100000000, P_CAM_CC_PLL0_OUT_AUX, 6, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_AUX, 3, 0, 0),
	F(269333333, P_CAM_CC_PLL1_OUT_AUX, 3, 0, 0),
	F(320000000, P_CAM_CC_PLL2_OUT_EARLY, 3, 0, 0),
	F(384000000, P_CAM_CC_PLL2_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_cphy_rx_clk_src = {
	.cmd_rcgr = 0x9064,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_cphy_rx_clk_src",
		.parent_names = cam_cc_parent_names_2,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 269333333,
			[VDD_NOMINAL] = 320000000,
			[VDD_HIGH] = 384000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_csi0phytimer_clk_src[] = {
	F(100000000, P_CAM_CC_PLL0_OUT_AUX, 6, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_AUX, 3, 0, 0),
	F(269333333, P_CAM_CC_PLL1_OUT_AUX, 3, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_csi0phytimer_clk_src = {
	.cmd_rcgr = 0x5004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_csi0phytimer_clk_src",
		.parent_names = cam_cc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 269333333},
	},
};

static struct clk_rcg2 cam_cc_csi1phytimer_clk_src = {
	.cmd_rcgr = 0x5028,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_csi1phytimer_clk_src",
		.parent_names = cam_cc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 269333333},
	},
};

static struct clk_rcg2 cam_cc_csi2phytimer_clk_src = {
	.cmd_rcgr = 0x504c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_csi2phytimer_clk_src",
		.parent_names = cam_cc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 269333333},
	},
};

static const struct freq_tbl ftbl_cam_cc_fast_ahb_clk_src[] = {
	F(100000000, P_CAM_CC_PLL0_OUT_AUX, 6, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_AUX, 3, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_AUX, 2, 0, 0),
	F(404000000, P_CAM_CC_PLL1_OUT_AUX, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_fast_ahb_clk_src = {
	.cmd_rcgr = 0x603c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.freq_tbl = ftbl_cam_cc_fast_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_fast_ahb_clk_src",
		.parent_names = cam_cc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 404000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_icp_clk_src[] = {
	F(240000000, P_CAM_CC_PLL0_OUT_AUX, 2.5, 0, 0),
	F(360000000, P_CAM_CC_PLL3_OUT_MAIN, 3, 0, 0),
	F(432000000, P_CAM_CC_PLL3_OUT_MAIN, 2.5, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_EARLY, 2, 0, 0),
	F(540000000, P_CAM_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_AUX, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_icp_clk_src = {
	.cmd_rcgr = 0xb088,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_icp_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_icp_clk_src",
		.parent_names = cam_cc_parent_names_1,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 240000000,
			[VDD_LOW] = 360000000,
			[VDD_LOW_L1] = 432000000,
			[VDD_NOMINAL] = 480000000,
			[VDD_NOMINAL_L1] = 540000000,
			[VDD_HIGH] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_0_clk_src[] = {
	F(240000000, P_CAM_CC_PLL0_OUT_AUX, 2.5, 0, 0),
	F(360000000, P_CAM_CC_PLL3_OUT_MAIN, 3, 0, 0),
	F(432000000, P_CAM_CC_PLL3_OUT_MAIN, 2.5, 0, 0),
	F(540000000, P_CAM_CC_PLL3_OUT_MAIN, 2, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_AUX, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_0_clk_src = {
	.cmd_rcgr = 0x9010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_ife_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ife_0_clk_src",
		.parent_names = cam_cc_parent_names_4,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 240000000,
			[VDD_LOW] = 360000000,
			[VDD_LOW_L1] = 432000000,
			[VDD_NOMINAL] = 540000000,
			[VDD_HIGH] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_ife_0_csid_clk_src[] = {
	F(100000000, P_CAM_CC_PLL0_OUT_AUX, 6, 0, 0),
	F(200000000, P_CAM_CC_PLL0_OUT_AUX, 3, 0, 0),
	F(320000000, P_CAM_CC_PLL2_OUT_EARLY, 3, 0, 0),
	F(404000000, P_CAM_CC_PLL1_OUT_AUX, 2, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_EARLY, 2, 0, 0),
	F(540000000, P_CAM_CC_PLL3_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_ife_0_csid_clk_src = {
	.cmd_rcgr = 0x903c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_ife_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ife_0_csid_clk_src",
		.parent_names = cam_cc_parent_names_2,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 320000000,
			[VDD_NOMINAL] = 404000000,
			[VDD_NOMINAL_L1] = 480000000,
			[VDD_HIGH] = 540000000},
	},
};

static struct clk_rcg2 cam_cc_ife_1_clk_src = {
	.cmd_rcgr = 0xa010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_ife_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ife_1_clk_src",
		.parent_names = cam_cc_parent_names_4,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 240000000,
			[VDD_LOW] = 360000000,
			[VDD_LOW_L1] = 432000000,
			[VDD_NOMINAL] = 540000000,
			[VDD_HIGH] = 600000000},
	},
};

static struct clk_rcg2 cam_cc_ife_1_csid_clk_src = {
	.cmd_rcgr = 0xa034,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_ife_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ife_1_csid_clk_src",
		.parent_names = cam_cc_parent_names_2,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 320000000,
			[VDD_NOMINAL] = 404000000,
			[VDD_NOMINAL_L1] = 480000000,
			[VDD_HIGH] = 540000000},
	},
};

static struct clk_rcg2 cam_cc_ife_lite_clk_src = {
	.cmd_rcgr = 0xb004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_4,
	.freq_tbl = ftbl_cam_cc_ife_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ife_lite_clk_src",
		.parent_names = cam_cc_parent_names_4,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 240000000,
			[VDD_LOW] = 360000000,
			[VDD_LOW_L1] = 432000000,
			[VDD_NOMINAL] = 540000000,
			[VDD_HIGH] = 600000000},
	},
};

static struct clk_rcg2 cam_cc_ife_lite_csid_clk_src = {
	.cmd_rcgr = 0xb024,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_2,
	.freq_tbl = ftbl_cam_cc_ife_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ife_lite_csid_clk_src",
		.parent_names = cam_cc_parent_names_2,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 320000000,
			[VDD_NOMINAL] = 404000000,
			[VDD_NOMINAL_L1] = 480000000,
			[VDD_HIGH] = 540000000},
	},
};

static struct clk_rcg2 cam_cc_ipe_0_clk_src = {
	.cmd_rcgr = 0x7010,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_icp_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_ipe_0_clk_src",
		.parent_names = cam_cc_parent_names_1,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 240000000,
			[VDD_LOW] = 360000000,
			[VDD_LOW_L1] = 432000000,
			[VDD_NOMINAL] = 480000000,
			[VDD_NOMINAL_L1] = 540000000,
			[VDD_HIGH] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_jpeg_clk_src[] = {
	F(66666667, P_CAM_CC_PLL0_OUT_AUX, 9, 0, 0),
	F(133333333, P_CAM_CC_PLL0_OUT_AUX, 4.5, 0, 0),
	F(216000000, P_CAM_CC_PLL3_OUT_MAIN, 5, 0, 0),
	F(320000000, P_CAM_CC_PLL2_OUT_EARLY, 3, 0, 0),
	F(480000000, P_CAM_CC_PLL2_OUT_EARLY, 2, 0, 0),
	F(600000000, P_CAM_CC_PLL0_OUT_AUX, 1, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_jpeg_clk_src = {
	.cmd_rcgr = 0xb04c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_1,
	.freq_tbl = ftbl_cam_cc_jpeg_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_jpeg_clk_src",
		.parent_names = cam_cc_parent_names_1,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 66666667,
			[VDD_LOW] = 133333333,
			[VDD_LOW_L1] = 216000000,
			[VDD_NOMINAL] = 320000000,
			[VDD_NOMINAL_L1] = 480000000,
			[VDD_HIGH] = 600000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_lrme_clk_src[] = {
	F(200000000, P_CAM_CC_PLL0_OUT_AUX, 3, 0, 0),
	F(216000000, P_CAM_CC_PLL3_OUT_MAIN, 5, 0, 0),
	F(300000000, P_CAM_CC_PLL0_OUT_AUX, 2, 0, 0),
	F(404000000, P_CAM_CC_PLL1_OUT_AUX, 2, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_lrme_clk_src = {
	.cmd_rcgr = 0xb0f8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_6,
	.freq_tbl = ftbl_cam_cc_lrme_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_lrme_clk_src",
		.parent_names = cam_cc_parent_names_6,
		.num_parents = 5,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000,
			[VDD_LOW] = 216000000,
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 404000000},
	},
};

static const struct freq_tbl ftbl_cam_cc_mclk0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24000000, P_CAM_CC_PLL2_OUT_AUX2, 10, 1, 2),
	F(34285716, P_CAM_CC_PLL2_OUT_AUX2, 14, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_mclk0_clk_src = {
	.cmd_rcgr = 0x4004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_mclk0_clk_src",
		.parent_names = cam_cc_parent_names_3,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.flags = CLK_SET_RATE_PARENT,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 34285716},
	},
};

static struct clk_rcg2 cam_cc_mclk1_clk_src = {
	.cmd_rcgr = 0x4024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_mclk1_clk_src",
		.parent_names = cam_cc_parent_names_3,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.flags = CLK_SET_RATE_PARENT,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 34285716},
	},
};

static struct clk_rcg2 cam_cc_mclk2_clk_src = {
	.cmd_rcgr = 0x4044,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_mclk2_clk_src",
		.parent_names = cam_cc_parent_names_3,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.flags = CLK_SET_RATE_PARENT,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 34285716},
	},
};

static struct clk_rcg2 cam_cc_mclk3_clk_src = {
	.cmd_rcgr = 0x4064,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_3,
	.freq_tbl = ftbl_cam_cc_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_mclk3_clk_src",
		.parent_names = cam_cc_parent_names_3,
		.num_parents = 3,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.flags = CLK_SET_RATE_PARENT,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 34285716},
	},
};

static const struct freq_tbl ftbl_cam_cc_slow_ahb_clk_src[] = {
	F(80000000, P_CAM_CC_PLL0_OUT_AUX, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 cam_cc_slow_ahb_clk_src = {
	.cmd_rcgr = 0x6058,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = cam_cc_parent_map_0,
	.enable_safe_config = true,
	.freq_tbl = ftbl_cam_cc_slow_ahb_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "cam_cc_slow_ahb_clk_src",
		.parent_names = cam_cc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 80000000},
	},
};

static struct clk_branch cam_cc_bps_ahb_clk = {
	.halt_reg = 0x6070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6070,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_bps_ahb_clk",
			.parent_names = (const char *[]){
				"cam_cc_slow_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_bps_areg_clk = {
	.halt_reg = 0x6054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_bps_areg_clk",
			.parent_names = (const char *[]){
				"cam_cc_fast_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_bps_axi_clk = {
	.halt_reg = 0x6038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_bps_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_bps_clk = {
	.halt_reg = 0x6028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_bps_clk",
			.parent_names = (const char *[]){
				"cam_cc_bps_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_atb_clk = {
	.halt_reg = 0xb12c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb12c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_camnoc_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_camnoc_axi_clk = {
	.halt_reg = 0xb124,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb124,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_camnoc_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cci_clk = {
	.halt_reg = 0xb0f0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0f0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_cci_clk",
			.parent_names = (const char *[]){
				"cam_cc_cci_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_core_ahb_clk = {
	.halt_reg = 0xb144,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0xb144,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_core_ahb_clk",
			.parent_names = (const char *[]){
				"cam_cc_slow_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_cpas_ahb_clk = {
	.halt_reg = 0xb11c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb11c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_cpas_ahb_clk",
			.parent_names = (const char *[]){
				"cam_cc_slow_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi0phytimer_clk = {
	.halt_reg = 0x501c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x501c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_csi0phytimer_clk",
			.parent_names = (const char *[]){
				"cam_cc_csi0phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi1phytimer_clk = {
	.halt_reg = 0x5040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_csi1phytimer_clk",
			.parent_names = (const char *[]){
				"cam_cc_csi1phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csi2phytimer_clk = {
	.halt_reg = 0x5064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_csi2phytimer_clk",
			.parent_names = (const char *[]){
				"cam_cc_csi2phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy0_clk = {
	.halt_reg = 0x5020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_csiphy0_clk",
			.parent_names = (const char *[]){
				"cam_cc_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy1_clk = {
	.halt_reg = 0x5044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_csiphy1_clk",
			.parent_names = (const char *[]){
				"cam_cc_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_csiphy2_clk = {
	.halt_reg = 0x5068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_csiphy2_clk",
			.parent_names = (const char *[]){
				"cam_cc_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_apb_clk = {
	.halt_reg = 0xb084,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb084,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_icp_apb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_atb_clk = {
	.halt_reg = 0xb078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_icp_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_clk = {
	.halt_reg = 0xb0a0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0a0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_icp_clk",
			.parent_names = (const char *[]){
				"cam_cc_icp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_cti_clk = {
	.halt_reg = 0xb07c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb07c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_icp_cti_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_icp_ts_clk = {
	.halt_reg = 0xb080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_icp_ts_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_axi_clk = {
	.halt_reg = 0x9080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_clk = {
	.halt_reg = 0x9028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_0_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_cphy_rx_clk = {
	.halt_reg = 0x907c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x907c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_0_cphy_rx_clk",
			.parent_names = (const char *[]){
				"cam_cc_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_csid_clk = {
	.halt_reg = 0x9054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_0_csid_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_0_csid_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_0_dsp_clk = {
	.halt_reg = 0x9038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x9038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_0_dsp_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_axi_clk = {
	.halt_reg = 0xa058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_clk = {
	.halt_reg = 0xa028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_1_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_cphy_rx_clk = {
	.halt_reg = 0xa054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_1_cphy_rx_clk",
			.parent_names = (const char *[]){
				"cam_cc_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_csid_clk = {
	.halt_reg = 0xa04c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa04c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_1_csid_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_1_csid_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_1_dsp_clk = {
	.halt_reg = 0xa030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xa030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_1_dsp_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_clk = {
	.halt_reg = 0xb01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb01c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_lite_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_lite_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_cphy_rx_clk = {
	.halt_reg = 0xb044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_lite_cphy_rx_clk",
			.parent_names = (const char *[]){
				"cam_cc_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ife_lite_csid_clk = {
	.halt_reg = 0xb03c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb03c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ife_lite_csid_clk",
			.parent_names = (const char *[]){
				"cam_cc_ife_lite_csid_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_ahb_clk = {
	.halt_reg = 0x7040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ipe_0_ahb_clk",
			.parent_names = (const char *[]){
				"cam_cc_slow_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_areg_clk = {
	.halt_reg = 0x703c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x703c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ipe_0_areg_clk",
			.parent_names = (const char *[]){
				"cam_cc_fast_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_axi_clk = {
	.halt_reg = 0x7038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ipe_0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_ipe_0_clk = {
	.halt_reg = 0x7028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_ipe_0_clk",
			.parent_names = (const char *[]){
				"cam_cc_ipe_0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_jpeg_clk = {
	.halt_reg = 0xb064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_jpeg_clk",
			.parent_names = (const char *[]){
				"cam_cc_jpeg_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_lrme_clk = {
	.halt_reg = 0xb110,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb110,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_lrme_clk",
			.parent_names = (const char *[]){
				"cam_cc_lrme_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk0_clk = {
	.halt_reg = 0x401c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x401c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_mclk0_clk",
			.parent_names = (const char *[]){
				"cam_cc_mclk0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk1_clk = {
	.halt_reg = 0x403c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x403c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_mclk1_clk",
			.parent_names = (const char *[]){
				"cam_cc_mclk1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk2_clk = {
	.halt_reg = 0x405c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x405c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_mclk2_clk",
			.parent_names = (const char *[]){
				"cam_cc_mclk2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_mclk3_clk = {
	.halt_reg = 0x407c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x407c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_mclk3_clk",
			.parent_names = (const char *[]){
				"cam_cc_mclk3_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_soc_ahb_clk = {
	.halt_reg = 0xb140,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb140,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_soc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch cam_cc_sys_tmr_clk = {
	.halt_reg = 0xb0a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0xb0a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "cam_cc_sys_tmr_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *cam_cc_sm6150_clocks[] = {
	[CAM_CC_BPS_AHB_CLK] = &cam_cc_bps_ahb_clk.clkr,
	[CAM_CC_BPS_AREG_CLK] = &cam_cc_bps_areg_clk.clkr,
	[CAM_CC_BPS_AXI_CLK] = &cam_cc_bps_axi_clk.clkr,
	[CAM_CC_BPS_CLK] = &cam_cc_bps_clk.clkr,
	[CAM_CC_BPS_CLK_SRC] = &cam_cc_bps_clk_src.clkr,
	[CAM_CC_CAMNOC_ATB_CLK] = &cam_cc_camnoc_atb_clk.clkr,
	[CAM_CC_CAMNOC_AXI_CLK] = &cam_cc_camnoc_axi_clk.clkr,
	[CAM_CC_CCI_CLK] = &cam_cc_cci_clk.clkr,
	[CAM_CC_CCI_CLK_SRC] = &cam_cc_cci_clk_src.clkr,
	[CAM_CC_CORE_AHB_CLK] = &cam_cc_core_ahb_clk.clkr,
	[CAM_CC_CPAS_AHB_CLK] = &cam_cc_cpas_ahb_clk.clkr,
	[CAM_CC_CPHY_RX_CLK_SRC] = &cam_cc_cphy_rx_clk_src.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK] = &cam_cc_csi0phytimer_clk.clkr,
	[CAM_CC_CSI0PHYTIMER_CLK_SRC] = &cam_cc_csi0phytimer_clk_src.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK] = &cam_cc_csi1phytimer_clk.clkr,
	[CAM_CC_CSI1PHYTIMER_CLK_SRC] = &cam_cc_csi1phytimer_clk_src.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK] = &cam_cc_csi2phytimer_clk.clkr,
	[CAM_CC_CSI2PHYTIMER_CLK_SRC] = &cam_cc_csi2phytimer_clk_src.clkr,
	[CAM_CC_CSIPHY0_CLK] = &cam_cc_csiphy0_clk.clkr,
	[CAM_CC_CSIPHY1_CLK] = &cam_cc_csiphy1_clk.clkr,
	[CAM_CC_CSIPHY2_CLK] = &cam_cc_csiphy2_clk.clkr,
	[CAM_CC_FAST_AHB_CLK_SRC] = &cam_cc_fast_ahb_clk_src.clkr,
	[CAM_CC_ICP_APB_CLK] = &cam_cc_icp_apb_clk.clkr,
	[CAM_CC_ICP_ATB_CLK] = &cam_cc_icp_atb_clk.clkr,
	[CAM_CC_ICP_CLK] = &cam_cc_icp_clk.clkr,
	[CAM_CC_ICP_CLK_SRC] = &cam_cc_icp_clk_src.clkr,
	[CAM_CC_ICP_CTI_CLK] = &cam_cc_icp_cti_clk.clkr,
	[CAM_CC_ICP_TS_CLK] = &cam_cc_icp_ts_clk.clkr,
	[CAM_CC_IFE_0_AXI_CLK] = &cam_cc_ife_0_axi_clk.clkr,
	[CAM_CC_IFE_0_CLK] = &cam_cc_ife_0_clk.clkr,
	[CAM_CC_IFE_0_CLK_SRC] = &cam_cc_ife_0_clk_src.clkr,
	[CAM_CC_IFE_0_CPHY_RX_CLK] = &cam_cc_ife_0_cphy_rx_clk.clkr,
	[CAM_CC_IFE_0_CSID_CLK] = &cam_cc_ife_0_csid_clk.clkr,
	[CAM_CC_IFE_0_CSID_CLK_SRC] = &cam_cc_ife_0_csid_clk_src.clkr,
	[CAM_CC_IFE_0_DSP_CLK] = &cam_cc_ife_0_dsp_clk.clkr,
	[CAM_CC_IFE_1_AXI_CLK] = &cam_cc_ife_1_axi_clk.clkr,
	[CAM_CC_IFE_1_CLK] = &cam_cc_ife_1_clk.clkr,
	[CAM_CC_IFE_1_CLK_SRC] = &cam_cc_ife_1_clk_src.clkr,
	[CAM_CC_IFE_1_CPHY_RX_CLK] = &cam_cc_ife_1_cphy_rx_clk.clkr,
	[CAM_CC_IFE_1_CSID_CLK] = &cam_cc_ife_1_csid_clk.clkr,
	[CAM_CC_IFE_1_CSID_CLK_SRC] = &cam_cc_ife_1_csid_clk_src.clkr,
	[CAM_CC_IFE_1_DSP_CLK] = &cam_cc_ife_1_dsp_clk.clkr,
	[CAM_CC_IFE_LITE_CLK] = &cam_cc_ife_lite_clk.clkr,
	[CAM_CC_IFE_LITE_CLK_SRC] = &cam_cc_ife_lite_clk_src.clkr,
	[CAM_CC_IFE_LITE_CPHY_RX_CLK] = &cam_cc_ife_lite_cphy_rx_clk.clkr,
	[CAM_CC_IFE_LITE_CSID_CLK] = &cam_cc_ife_lite_csid_clk.clkr,
	[CAM_CC_IFE_LITE_CSID_CLK_SRC] = &cam_cc_ife_lite_csid_clk_src.clkr,
	[CAM_CC_IPE_0_AHB_CLK] = &cam_cc_ipe_0_ahb_clk.clkr,
	[CAM_CC_IPE_0_AREG_CLK] = &cam_cc_ipe_0_areg_clk.clkr,
	[CAM_CC_IPE_0_AXI_CLK] = &cam_cc_ipe_0_axi_clk.clkr,
	[CAM_CC_IPE_0_CLK] = &cam_cc_ipe_0_clk.clkr,
	[CAM_CC_IPE_0_CLK_SRC] = &cam_cc_ipe_0_clk_src.clkr,
	[CAM_CC_JPEG_CLK] = &cam_cc_jpeg_clk.clkr,
	[CAM_CC_JPEG_CLK_SRC] = &cam_cc_jpeg_clk_src.clkr,
	[CAM_CC_LRME_CLK] = &cam_cc_lrme_clk.clkr,
	[CAM_CC_LRME_CLK_SRC] = &cam_cc_lrme_clk_src.clkr,
	[CAM_CC_MCLK0_CLK] = &cam_cc_mclk0_clk.clkr,
	[CAM_CC_MCLK0_CLK_SRC] = &cam_cc_mclk0_clk_src.clkr,
	[CAM_CC_MCLK1_CLK] = &cam_cc_mclk1_clk.clkr,
	[CAM_CC_MCLK1_CLK_SRC] = &cam_cc_mclk1_clk_src.clkr,
	[CAM_CC_MCLK2_CLK] = &cam_cc_mclk2_clk.clkr,
	[CAM_CC_MCLK2_CLK_SRC] = &cam_cc_mclk2_clk_src.clkr,
	[CAM_CC_MCLK3_CLK] = &cam_cc_mclk3_clk.clkr,
	[CAM_CC_MCLK3_CLK_SRC] = &cam_cc_mclk3_clk_src.clkr,
	[CAM_CC_PLL0_OUT_AUX] = &cam_cc_pll0_out_aux.clkr,
	[CAM_CC_PLL1_OUT_AUX] = &cam_cc_pll1_out_aux.clkr,
	[CAM_CC_PLL2_OUT_EARLY] = &cam_cc_pll2_out_early.clkr,
	[CAM_CC_PLL2_OUT_AUX2] = &cam_cc_pll2_out_aux2.clkr,
	[CAM_CC_PLL3_OUT_MAIN] = &cam_cc_pll3_out_main.clkr,
	[CAM_CC_SLOW_AHB_CLK_SRC] = &cam_cc_slow_ahb_clk_src.clkr,
	[CAM_CC_SOC_AHB_CLK] = &cam_cc_soc_ahb_clk.clkr,
	[CAM_CC_SYS_TMR_CLK] = &cam_cc_sys_tmr_clk.clkr,
};

static const struct regmap_config cam_cc_sm6150_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0xd004,
	.fast_io	= true,
};

static const struct qcom_cc_desc cam_cc_sm6150_desc = {
	.config = &cam_cc_sm6150_regmap_config,
	.clks = cam_cc_sm6150_clocks,
	.num_clks = ARRAY_SIZE(cam_cc_sm6150_clocks),
};

static const struct of_device_id cam_cc_sm6150_match_table[] = {
	{ .compatible = "qcom,camcc-sm6150" },
	{ .compatible = "qcom,camcc-sa6155" },
	{ }
};
MODULE_DEVICE_TABLE(of, cam_cc_sm6150_match_table);

static void camcc_sm6150_fixup_sa6155(struct platform_device *pdev)
{
	vdd_cx.num_levels = VDD_NUM_SA6155;
	vdd_mx.num_levels = VDD_MX_NUM_SA6155;
	vdd_cx.cur_level = VDD_NUM_SA6155;
	vdd_mx.cur_level = VDD_MX_NUM_SA6155;
}

static int cam_cc_sm6150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret = 0;
	int is_sa6155;

	vdd_cx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_cx");
	if (IS_ERR(vdd_cx.regulator[0])) {
		if (!(PTR_ERR(vdd_cx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev, "Unable to get vdd_cx regulator\n");
		return PTR_ERR(vdd_cx.regulator[0]);
	}

	vdd_mx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_mx");
	if (IS_ERR(vdd_mx.regulator[0])) {
		if (!(PTR_ERR(vdd_mx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev, "Unable to get vdd_mx regulator\n");
		return PTR_ERR(vdd_mx.regulator[0]);
	}

	is_sa6155 = of_device_is_compatible(pdev->dev.of_node,
						"qcom,camcc-sa6155");
	if (is_sa6155)
		camcc_sm6150_fixup_sa6155(pdev);

	regmap = qcom_cc_map(pdev, &cam_cc_sm6150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the cam_cc registers\n");
		return PTR_ERR(regmap);
	}

	clk_alpha_pll_configure(&cam_cc_pll0_out_aux, regmap,
				cam_cc_pll0_out_aux.config);
	clk_alpha_pll_configure(&cam_cc_pll1_out_aux, regmap,
				cam_cc_pll1_out_aux.config);
	clk_alpha_pll_configure(&cam_cc_pll2_out_early, regmap,
				cam_cc_pll2_out_early.config);
	clk_alpha_pll_configure(&cam_cc_pll3_out_main, regmap,
				cam_cc_pll3_out_main.config);

	ret = qcom_cc_really_probe(pdev, &cam_cc_sm6150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register Camera CC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered Camera CC clocks\n");

	return ret;
}

static struct platform_driver cam_cc_sm6150_driver = {
	.probe = cam_cc_sm6150_probe,
	.driver = {
		.name = "cam_cc-sm6150",
		.of_match_table = cam_cc_sm6150_match_table,
	},
};

static int __init cam_cc_sm6150_init(void)
{
	return platform_driver_register(&cam_cc_sm6150_driver);
}
subsys_initcall(cam_cc_sm6150_init);

static void __exit cam_cc_sm6150_exit(void)
{
	platform_driver_unregister(&cam_cc_sm6150_driver);
}
module_exit(cam_cc_sm6150_exit);

MODULE_DESCRIPTION("QTI CAM_CC sm6150 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cam_cc-sm6150");
