/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
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
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/msm-bus.h>

#include <dt-bindings/clock/qcom,dispcc-sm8150.h>
#include <dt-bindings/msm/msm-bus-ids.h>

#include "common.h"
#include "clk-regmap-divider.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "clk-alpha-pll.h"
#include "vdd-level.h"

#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }

#define MSM_BUS_VECTOR(_src, _dst, _ab, _ib)	\
{						\
	.src = _src,				\
	.dst = _dst,				\
	.ab = _ab,				\
	.ib = _ib,				\
}

static DEFINE_VDD_REGULATORS(vdd_mm, VDD_MM_NUM, 1, vdd_corner);

static struct msm_bus_vectors clk_debugfs_vectors[] = {
	MSM_BUS_VECTOR(MSM_BUS_MASTER_AMPSS_M0,
			MSM_BUS_SLAVE_DISPLAY_CFG, 0, 0),
	MSM_BUS_VECTOR(MSM_BUS_MASTER_AMPSS_M0,
			MSM_BUS_SLAVE_DISPLAY_CFG, 0, 1),
};

static struct msm_bus_paths clk_debugfs_usecases[] = {
	{
		.num_paths = 1,
		.vectors = &clk_debugfs_vectors[0],
	},
	{
		.num_paths = 1,
		.vectors = &clk_debugfs_vectors[1],
	}
};

static struct msm_bus_scale_pdata clk_debugfs_scale_table = {
	.usecase = clk_debugfs_usecases,
	.num_usecases = ARRAY_SIZE(clk_debugfs_usecases),
	.name = "clk_dispcc_debugfs",
};

#define DISP_CC_MISC_CMD	0x8000

enum {
	P_BI_TCXO,
	P_CORE_BI_PLL_TEST_SE,
	P_DISP_CC_PLL0_OUT_MAIN,
	P_DISP_CC_PLL1_OUT_EVEN,
	P_DISP_CC_PLL1_OUT_MAIN,
	P_DP_PHY_PLL_LINK_CLK,
	P_DP_PHY_PLL_VCO_DIV_CLK,
	P_DPTX1_PHY_PLL_LINK_CLK,
	P_DPTX1_PHY_PLL_VCO_DIV_CLK,
	P_DPTX2_PHY_PLL_LINK_CLK,
	P_DPTX2_PHY_PLL_VCO_DIV_CLK,
	P_DSI0_PHY_PLL_OUT_BYTECLK,
	P_DSI0_PHY_PLL_OUT_DSICLK,
	P_DSI1_PHY_PLL_OUT_BYTECLK,
	P_DSI1_PHY_PLL_OUT_DSICLK,
	P_EDP_PHY_PLL_LINK_CLK,
	P_EDP_PHY_PLL_VCO_DIV_CLK,
};

static const struct parent_map disp_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_DP_PHY_PLL_LINK_CLK, 1 },
	{ P_DP_PHY_PLL_VCO_DIV_CLK, 2 },
	{ P_DPTX1_PHY_PLL_LINK_CLK, 3 },
	{ P_DPTX1_PHY_PLL_VCO_DIV_CLK, 4 },
	{ P_DPTX2_PHY_PLL_LINK_CLK, 5 },
	{ P_DPTX2_PHY_PLL_VCO_DIV_CLK, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_0[] = {
	"bi_tcxo",
	"dp_link_clk_divsel_ten",
	"dp_vco_divided_clk_src_mux",
	"dptx1_phy_pll_link_clk",
	"dptx1_phy_pll_vco_div_clk",
	"dptx2_phy_pll_link_clk",
	"dptx2_phy_pll_vco_div_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_1[] = {
	"bi_tcxo",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_BYTECLK, 1 },
	{ P_DSI1_PHY_PLL_OUT_BYTECLK, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_2[] = {
	"bi_tcxo",
	"dsi0_phy_pll_out_byteclk",
	"dsi1_phy_pll_out_byteclk",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_DISP_CC_PLL1_OUT_MAIN, 4 },
	{ P_DISP_CC_PLL1_OUT_EVEN, 5 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_3[] = {
	"bi_tcxo",
	"disp_cc_pll1",
	"disp_cc_pll1_out_even",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_EDP_PHY_PLL_LINK_CLK, 1 },
	{ P_EDP_PHY_PLL_VCO_DIV_CLK, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_4[] = {
	"bi_tcxo",
	"edp_phy_pll_link_clk",
	"edp_phy_pll_vco_div_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_4_sdmshrikev2[] = {
	{ P_BI_TCXO, 0 },
	{ P_EDP_PHY_PLL_LINK_CLK, 1 },
	{ P_EDP_PHY_PLL_VCO_DIV_CLK, 2 },
	{ P_DP_PHY_PLL_VCO_DIV_CLK, 3 },
	{ P_DPTX1_PHY_PLL_VCO_DIV_CLK, 4 },
	{ P_DPTX2_PHY_PLL_VCO_DIV_CLK, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_4_sdmshrikev2[] = {
	"bi_tcxo",
	"edp_phy_pll_link_clk",
	"edp_phy_pll_vco_div_clk",
	"dp_phy_pll_vco_div_clk",
	"dptx1_phy_pll_vco_div_clk",
	"dptx2_phy_pll_vco_div_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_DISP_CC_PLL0_OUT_MAIN, 1 },
	{ P_DISP_CC_PLL1_OUT_MAIN, 4 },
	{ P_DISP_CC_PLL1_OUT_EVEN, 5 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_5[] = {
	"bi_tcxo",
	"disp_cc_pll0",
	"disp_cc_pll1",
	"disp_cc_pll1_out_even",
	"core_bi_pll_test_se",
};

static const struct parent_map disp_cc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_DSI0_PHY_PLL_OUT_DSICLK, 1 },
	{ P_DSI1_PHY_PLL_OUT_DSICLK, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const disp_cc_parent_names_6[] = {
	"bi_tcxo",
	"dsi0_phy_pll_out_dsiclk",
	"dsi1_phy_pll_out_dsiclk",
	"core_bi_pll_test_se",
};

static struct pll_vco trion_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static struct alpha_pll_config disp_cc_pll0_config = {
	.l = 0x47,
	.alpha = 0xE000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000002,
	.test_ctl_hi1_val = 0x00000000,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct alpha_pll_config disp_cc_pll0_config_sm8150_v2 = {
	.l = 0x47,
	.alpha = 0xE000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll disp_cc_pll0 = {
	.offset = 0x0,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.type = TRION_PLL,
	.config = &disp_cc_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_pll0",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_trion_pll_ops,
			.vdd_class = &vdd_mm,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static struct alpha_pll_config disp_cc_pll1_config = {
	.l = 0x1F,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000002,
	.test_ctl_hi1_val = 0x00000000,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct alpha_pll_config disp_cc_pll1_config_sm8150_v2 = {
	.l = 0x1F,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll disp_cc_pll1 = {
	.offset = 0x1000,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.type = TRION_PLL,
	.config = &disp_cc_pll1_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_pll1",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_trion_pll_ops,
			.vdd_class = &vdd_mm,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_DISP_CC_PLL1_OUT_MAIN, 16, 0, 0),
	F(75000000, P_DISP_CC_PLL1_OUT_MAIN, 8, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_ahb_clk_src = {
	.cmd_rcgr = 0x22bc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_3,
	.freq_tbl = ftbl_disp_cc_mdss_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_ahb_clk_src",
		.parent_names = disp_cc_parent_names_3,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOW] = 37500000,
			[VDD_NOMINAL] = 75000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_byte0_clk_src = {
	.cmd_rcgr = 0x2110,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_byte0_clk_src",
		.parent_names = disp_cc_parent_names_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_byte2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 187500000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 358000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_byte1_clk_src = {
	.cmd_rcgr = 0x212c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_byte1_clk_src",
		.parent_names = disp_cc_parent_names_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_byte2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 187500000,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 358000000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_dp_aux1_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dp_aux1_clk_src = {
	.cmd_rcgr = 0x2240,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_aux1_clk_src",
		.parent_names = disp_cc_parent_names_1,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_aux_clk_src = {
	.cmd_rcgr = 0x21dc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_aux_clk_src",
		.parent_names = disp_cc_parent_names_1,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_dp_crypto1_clk_src[] = {
	F( 108000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 180000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 360000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 540000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	{ }
};

static const struct freq_tbl
ftbl_disp_cc_mdss_dp_crypto1_clk_src_sdmshrikev2[] = {
	F( 108000, P_DPTX1_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 180000, P_DPTX1_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 360000, P_DPTX1_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 540000, P_DPTX1_PHY_PLL_LINK_CLK,   3,   0,   0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dp_crypto1_clk_src = {
	.cmd_rcgr = 0x2228,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_crypto1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_crypto1_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_GET_RATE_NOCACHE,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 12800,
			[VDD_LOWER] = 180000,
			[VDD_LOW_L1] = 360000,
			[VDD_NOMINAL] = 540000},
	},
};

static const struct freq_tbl
ftbl_disp_cc_mdss_dp_crypto_clk_src_sdmshrikev2[] = {
	F( 108000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 180000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 360000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	F( 540000, P_DP_PHY_PLL_LINK_CLK,   3,   0,   0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dp_crypto_clk_src = {
	.cmd_rcgr = 0x2194,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_crypto1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_crypto_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_GET_RATE_NOCACHE,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 12800,
			[VDD_LOWER] = 180000,
			[VDD_LOW_L1] = 360000,
			[VDD_NOMINAL] = 540000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_dp_link1_clk_src[] = {
	F( 162000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 270000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 540000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 810000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	{ }
};

static const struct freq_tbl
ftbl_disp_cc_mdss_dp_link1_clk_src_sdmshrikev2[] = {
	F( 19200, P_BI_TCXO, 1, 0, 0),
	F( 162000, P_DPTX1_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 270000, P_DPTX1_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 540000, P_DPTX1_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 810000, P_DPTX1_PHY_PLL_LINK_CLK,   1,   0,   0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dp_link1_clk_src = {
	.cmd_rcgr = 0x220c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_link1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_link1_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200,
			[VDD_LOWER] = 270000,
			[VDD_LOW_L1] = 540000,
			[VDD_NOMINAL] = 810000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_dp_link_clk_src_sdmshrikev2[] = {
	F( 19200, P_BI_TCXO, 1, 0, 0),
	F( 162000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 270000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 540000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 810000, P_DP_PHY_PLL_LINK_CLK,   1,   0,   0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_dp_link_clk_src = {
	.cmd_rcgr = 0x2178,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.freq_tbl = ftbl_disp_cc_mdss_dp_link1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_link_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200,
			[VDD_LOWER] = 270000,
			[VDD_LOW_L1] = 540000,
			[VDD_NOMINAL] = 810000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_pixel1_clk_src = {
	.cmd_rcgr = 0x21c4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_pixel1_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_dp_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200,
			[VDD_LOWER] = 337500,
			[VDD_LOW_L1] = 675000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_pixel2_clk_src = {
	.cmd_rcgr = 0x21f4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_pixel2_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_dp_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200,
			[VDD_LOWER] = 337500,
			[VDD_LOW_L1] = 675000},
	},
};

static struct clk_rcg2 disp_cc_mdss_dp_pixel_clk_src = {
	.cmd_rcgr = 0x21ac,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_0,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_dp_pixel_clk_src",
		.parent_names = disp_cc_parent_names_0,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_dp_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200,
			[VDD_LOWER] = 337500,
			[VDD_LOW_L1] = 675000},
	},
};

static struct clk_rcg2 disp_cc_mdss_edp_aux_clk_src = {
	.cmd_rcgr = 0x228c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_aux_clk_src",
		.parent_names = disp_cc_parent_names_1,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static struct clk_rcg2 disp_cc_mdss_edp_gtc_clk_src = {
	.cmd_rcgr = 0x22a4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_3,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_gtc_clk_src",
		.parent_names = disp_cc_parent_names_3,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_edp_link_clk_src[] = {
	F( 19200000, P_BI_TCXO, 1, 0, 0),
	F( 270000000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 594000000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 810000000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	{ }
};

static const struct freq_tbl
ftbl_disp_cc_mdss_edp_link_clk_src_sdmshrikev2[] = {
	F( 19200, P_BI_TCXO, 1, 0, 0),
	F( 162000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 216000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 243000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 270000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 324000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 432000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 540000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 594000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	F( 810000, P_EDP_PHY_PLL_LINK_CLK,   1,   0,   0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_edp_link_clk_src = {
	.cmd_rcgr = 0x2270,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_4,
	.freq_tbl = ftbl_disp_cc_mdss_edp_link_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_link_clk_src",
		.parent_names = disp_cc_parent_names_4,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 270000000,
			[VDD_LOW_L1] = 594000000,
			[VDD_NOMINAL] = 810000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_edp_pixel_clk_src = {
	.cmd_rcgr = 0x2258,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_4,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_edp_pixel_clk_src",
		.parent_names = disp_cc_parent_names_4,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_dp_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 337500000,
			[VDD_LOW_L1] = 675000000},
	},
};

static struct clk_init_data disp_cc_mdss_edp_pixel_clk_src_sdmshrikev2 = {
	.name = "disp_cc_mdss_edp_pixel_clk_src",
	.parent_names = disp_cc_parent_names_4_sdmshrikev2,
	.num_parents = 7,
	.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
	.ops = &clk_dp_ops,
	.vdd_class = &vdd_mm,
	.num_rate_max = VDD_NUM,
	.rate_max = (unsigned long[VDD_NUM]) {
		[VDD_MIN] = 19200000,
		[VDD_LOWER] = 337500000,
		[VDD_LOW_L1] = 675000000},
};

static struct clk_rcg2 disp_cc_mdss_esc0_clk_src = {
	.cmd_rcgr = 0x2148,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_esc0_clk_src",
		.parent_names = disp_cc_parent_names_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static struct clk_rcg2 disp_cc_mdss_esc1_clk_src = {
	.cmd_rcgr = 0x2160,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_2,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_esc1_clk_src",
		.parent_names = disp_cc_parent_names_2,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_mdp_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(85714286, P_DISP_CC_PLL1_OUT_MAIN, 7, 0, 0),
	F(100000000, P_DISP_CC_PLL1_OUT_MAIN, 6, 0, 0),
	F(150000000, P_DISP_CC_PLL1_OUT_MAIN, 4, 0, 0),
	F(171428571, P_DISP_CC_PLL1_OUT_MAIN, 3.5, 0, 0),
	F(200000000, P_DISP_CC_PLL1_OUT_MAIN, 3, 0, 0),
	F(300000000, P_DISP_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(345000000, P_DISP_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(460000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_mdp_clk_src = {
	.cmd_rcgr = 0x20c8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_5,
	.freq_tbl = ftbl_disp_cc_mdss_mdp_clk_src,
	.enable_safe_config = true,
	.flags = HW_CLK_CTRL_MODE,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_mdp_clk_src",
		.parent_names = disp_cc_parent_names_5,
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 171428571,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 345000000,
			[VDD_NOMINAL] = 460000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_pclk0_clk_src = {
	.cmd_rcgr = 0x2098,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_6,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_pclk0_clk_src",
		.parent_names = disp_cc_parent_names_6,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_pixel_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 300000000,
			[VDD_LOW] = 480000000,
			[VDD_LOW_L1] = 625000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_pclk1_clk_src = {
	.cmd_rcgr = 0x20b0,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_6,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_pclk1_clk_src",
		.parent_names = disp_cc_parent_names_6,
		.num_parents = 4,
		.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
		.ops = &clk_pixel_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 300000000,
			[VDD_LOW] = 480000000,
			[VDD_LOW_L1] = 625000000},
	},
};

static const struct freq_tbl ftbl_disp_cc_mdss_rot_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(171428571, P_DISP_CC_PLL1_OUT_MAIN, 3.5, 0, 0),
	F(300000000, P_DISP_CC_PLL1_OUT_MAIN, 2, 0, 0),
	F(345000000, P_DISP_CC_PLL0_OUT_MAIN, 4, 0, 0),
	F(460000000, P_DISP_CC_PLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 disp_cc_mdss_rot_clk_src = {
	.cmd_rcgr = 0x20e0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_5,
	.freq_tbl = ftbl_disp_cc_mdss_rot_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_rot_clk_src",
		.parent_names = disp_cc_parent_names_5,
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000,
			[VDD_LOWER] = 171428571,
			[VDD_LOW] = 300000000,
			[VDD_LOW_L1] = 345000000,
			[VDD_NOMINAL] = 460000000},
	},
};

static struct clk_rcg2 disp_cc_mdss_vsync_clk_src = {
	.cmd_rcgr = 0x20f8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = disp_cc_parent_map_1,
	.freq_tbl = ftbl_disp_cc_mdss_dp_aux1_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "disp_cc_mdss_vsync_clk_src",
		.parent_names = disp_cc_parent_names_1,
		.num_parents = 2,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_mm,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_MIN] = 19200000},
	},
};

static struct clk_branch disp_cc_mdss_ahb_clk = {
	.halt_reg = 0x2080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_ahb_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte0_clk = {
	.halt_reg = 0x2028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte0_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_byte0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_div disp_cc_mdss_byte0_div_clk_src = {
	.reg = 0x2128,
	.shift = 0,
	.width = 2,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte0_div_clk_src",
			.parent_names = (const char *[]){
				"disp_cc_mdss_byte0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_GET_RATE_NOCACHE,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte0_intf_clk = {
	.halt_reg = 0x202c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x202c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte0_intf_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_byte0_div_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte1_clk = {
	.halt_reg = 0x2030,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_byte1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap_div disp_cc_mdss_byte1_div_clk_src = {
	.reg = 0x2144,
	.shift = 0,
	.width = 2,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte1_div_clk_src",
			.parent_names = (const char *[]){
				"disp_cc_mdss_byte1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_GET_RATE_NOCACHE,
			.ops = &clk_regmap_div_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_byte1_intf_clk = {
	.halt_reg = 0x2034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_byte1_intf_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_byte1_div_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_aux1_clk = {
	.halt_reg = 0x2068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_aux1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_aux1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_aux_clk = {
	.halt_reg = 0x2054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_aux_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_aux_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_crypto1_clk = {
	.halt_reg = 0x2064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_crypto1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_crypto1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_crypto_clk = {
	.halt_reg = 0x2048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_crypto_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_crypto_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_link1_clk = {
	.halt_reg = 0x205c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x205c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_link1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_link1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_link1_intf_clk = {
	.halt_reg = 0x2060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_link1_intf_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_link1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_link_clk = {
	.halt_reg = 0x2040,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_link_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_link_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_link_intf_clk = {
	.halt_reg = 0x2044,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2044,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_link_intf_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_link_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_pixel1_clk = {
	.halt_reg = 0x2050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_pixel1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_pixel1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_pixel2_clk = {
	.halt_reg = 0x2058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_pixel2_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_pixel2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_dp_pixel_clk = {
	.halt_reg = 0x204c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x204c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_dp_pixel_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_dp_pixel_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_aux_clk = {
	.halt_reg = 0x2078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_aux_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_edp_aux_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_gtc_clk = {
	.halt_reg = 0x207c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x207c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_gtc_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_edp_gtc_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_link_clk = {
	.halt_reg = 0x2070,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2070,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_link_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_edp_link_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_link_intf_clk = {
	.halt_reg = 0x2074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2074,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_link_intf_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_edp_link_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_edp_pixel_clk = {
	.halt_reg = 0x206c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x206c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_edp_pixel_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_edp_pixel_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_esc0_clk = {
	.halt_reg = 0x2038,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2038,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_esc0_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_esc0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_esc1_clk = {
	.halt_reg = 0x203c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x203c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_esc1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_esc1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_clk = {
	.halt_reg = 0x200c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x200c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_mdp_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_mdp_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_mdp_lut_clk = {
	.halt_reg = 0x201c,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x201c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_mdp_lut_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_mdp_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_non_gdsc_ahb_clk = {
	.halt_reg = 0x4004,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x4004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_non_gdsc_ahb_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_pclk0_clk = {
	.halt_reg = 0x2004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_pclk0_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_pclk0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_pclk1_clk = {
	.halt_reg = 0x2008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_pclk1_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_pclk1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rot_clk = {
	.halt_reg = 0x2014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_rot_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_rot_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rscc_ahb_clk = {
	.halt_reg = 0x400c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x400c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_rscc_ahb_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_rscc_vsync_clk = {
	.halt_reg = 0x4008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_rscc_vsync_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_vsync_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_mdss_vsync_clk = {
	.halt_reg = 0x2024,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_mdss_vsync_clk",
			.parent_names = (const char *[]){
				"disp_cc_mdss_vsync_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch disp_cc_xo_clk = {
	.halt_reg = 0x605c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x605c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "disp_cc_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *disp_cc_sm8150_clocks[] = {
	[DISP_CC_MDSS_AHB_CLK] = &disp_cc_mdss_ahb_clk.clkr,
	[DISP_CC_MDSS_AHB_CLK_SRC] = &disp_cc_mdss_ahb_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_CLK] = &disp_cc_mdss_byte0_clk.clkr,
	[DISP_CC_MDSS_BYTE0_CLK_SRC] = &disp_cc_mdss_byte0_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_DIV_CLK_SRC] = &disp_cc_mdss_byte0_div_clk_src.clkr,
	[DISP_CC_MDSS_BYTE0_INTF_CLK] = &disp_cc_mdss_byte0_intf_clk.clkr,
	[DISP_CC_MDSS_BYTE1_CLK] = &disp_cc_mdss_byte1_clk.clkr,
	[DISP_CC_MDSS_BYTE1_CLK_SRC] = &disp_cc_mdss_byte1_clk_src.clkr,
	[DISP_CC_MDSS_BYTE1_DIV_CLK_SRC] = &disp_cc_mdss_byte1_div_clk_src.clkr,
	[DISP_CC_MDSS_BYTE1_INTF_CLK] = &disp_cc_mdss_byte1_intf_clk.clkr,
	[DISP_CC_MDSS_DP_AUX1_CLK] = &disp_cc_mdss_dp_aux1_clk.clkr,
	[DISP_CC_MDSS_DP_AUX1_CLK_SRC] = &disp_cc_mdss_dp_aux1_clk_src.clkr,
	[DISP_CC_MDSS_DP_AUX_CLK] = &disp_cc_mdss_dp_aux_clk.clkr,
	[DISP_CC_MDSS_DP_AUX_CLK_SRC] = &disp_cc_mdss_dp_aux_clk_src.clkr,
	[DISP_CC_MDSS_DP_CRYPTO1_CLK] = &disp_cc_mdss_dp_crypto1_clk.clkr,
	[DISP_CC_MDSS_DP_CRYPTO1_CLK_SRC] =
		&disp_cc_mdss_dp_crypto1_clk_src.clkr,
	[DISP_CC_MDSS_DP_CRYPTO_CLK] = &disp_cc_mdss_dp_crypto_clk.clkr,
	[DISP_CC_MDSS_DP_CRYPTO_CLK_SRC] = &disp_cc_mdss_dp_crypto_clk_src.clkr,
	[DISP_CC_MDSS_DP_LINK1_CLK] = &disp_cc_mdss_dp_link1_clk.clkr,
	[DISP_CC_MDSS_DP_LINK1_CLK_SRC] = &disp_cc_mdss_dp_link1_clk_src.clkr,
	[DISP_CC_MDSS_DP_LINK1_INTF_CLK] = &disp_cc_mdss_dp_link1_intf_clk.clkr,
	[DISP_CC_MDSS_DP_LINK_CLK] = &disp_cc_mdss_dp_link_clk.clkr,
	[DISP_CC_MDSS_DP_LINK_CLK_SRC] = &disp_cc_mdss_dp_link_clk_src.clkr,
	[DISP_CC_MDSS_DP_LINK_INTF_CLK] = &disp_cc_mdss_dp_link_intf_clk.clkr,
	[DISP_CC_MDSS_DP_PIXEL1_CLK] = &disp_cc_mdss_dp_pixel1_clk.clkr,
	[DISP_CC_MDSS_DP_PIXEL1_CLK_SRC] = &disp_cc_mdss_dp_pixel1_clk_src.clkr,
	[DISP_CC_MDSS_DP_PIXEL2_CLK] = &disp_cc_mdss_dp_pixel2_clk.clkr,
	[DISP_CC_MDSS_DP_PIXEL2_CLK_SRC] = &disp_cc_mdss_dp_pixel2_clk_src.clkr,
	[DISP_CC_MDSS_DP_PIXEL_CLK] = &disp_cc_mdss_dp_pixel_clk.clkr,
	[DISP_CC_MDSS_DP_PIXEL_CLK_SRC] = &disp_cc_mdss_dp_pixel_clk_src.clkr,
	[DISP_CC_MDSS_EDP_AUX_CLK] = &disp_cc_mdss_edp_aux_clk.clkr,
	[DISP_CC_MDSS_EDP_AUX_CLK_SRC] = &disp_cc_mdss_edp_aux_clk_src.clkr,
	[DISP_CC_MDSS_EDP_GTC_CLK] = &disp_cc_mdss_edp_gtc_clk.clkr,
	[DISP_CC_MDSS_EDP_GTC_CLK_SRC] = &disp_cc_mdss_edp_gtc_clk_src.clkr,
	[DISP_CC_MDSS_EDP_LINK_CLK] = &disp_cc_mdss_edp_link_clk.clkr,
	[DISP_CC_MDSS_EDP_LINK_CLK_SRC] = &disp_cc_mdss_edp_link_clk_src.clkr,
	[DISP_CC_MDSS_EDP_LINK_INTF_CLK] = &disp_cc_mdss_edp_link_intf_clk.clkr,
	[DISP_CC_MDSS_EDP_PIXEL_CLK] = &disp_cc_mdss_edp_pixel_clk.clkr,
	[DISP_CC_MDSS_EDP_PIXEL_CLK_SRC] = &disp_cc_mdss_edp_pixel_clk_src.clkr,
	[DISP_CC_MDSS_ESC0_CLK] = &disp_cc_mdss_esc0_clk.clkr,
	[DISP_CC_MDSS_ESC0_CLK_SRC] = &disp_cc_mdss_esc0_clk_src.clkr,
	[DISP_CC_MDSS_ESC1_CLK] = &disp_cc_mdss_esc1_clk.clkr,
	[DISP_CC_MDSS_ESC1_CLK_SRC] = &disp_cc_mdss_esc1_clk_src.clkr,
	[DISP_CC_MDSS_MDP_CLK] = &disp_cc_mdss_mdp_clk.clkr,
	[DISP_CC_MDSS_MDP_CLK_SRC] = &disp_cc_mdss_mdp_clk_src.clkr,
	[DISP_CC_MDSS_MDP_LUT_CLK] = &disp_cc_mdss_mdp_lut_clk.clkr,
	[DISP_CC_MDSS_NON_GDSC_AHB_CLK] = &disp_cc_mdss_non_gdsc_ahb_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK] = &disp_cc_mdss_pclk0_clk.clkr,
	[DISP_CC_MDSS_PCLK0_CLK_SRC] = &disp_cc_mdss_pclk0_clk_src.clkr,
	[DISP_CC_MDSS_PCLK1_CLK] = &disp_cc_mdss_pclk1_clk.clkr,
	[DISP_CC_MDSS_PCLK1_CLK_SRC] = &disp_cc_mdss_pclk1_clk_src.clkr,
	[DISP_CC_MDSS_ROT_CLK] = &disp_cc_mdss_rot_clk.clkr,
	[DISP_CC_MDSS_ROT_CLK_SRC] = &disp_cc_mdss_rot_clk_src.clkr,
	[DISP_CC_MDSS_RSCC_AHB_CLK] = &disp_cc_mdss_rscc_ahb_clk.clkr,
	[DISP_CC_MDSS_RSCC_VSYNC_CLK] = &disp_cc_mdss_rscc_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK] = &disp_cc_mdss_vsync_clk.clkr,
	[DISP_CC_MDSS_VSYNC_CLK_SRC] = &disp_cc_mdss_vsync_clk_src.clkr,
	[DISP_CC_PLL0] = &disp_cc_pll0.clkr,
	[DISP_CC_PLL1] = &disp_cc_pll1.clkr,
	[DISP_CC_XO_CLK] = &disp_cc_xo_clk.clkr,
};

static const struct qcom_reset_map disp_cc_sm8150_resets[] = {
	[DISP_CC_MDSS_CORE_BCR] = { 0x2000 },
	[DISP_CC_MDSS_RSCC_BCR] = { 0x4000 },
	[DISP_CC_MDSS_SPDM_BCR] = { 0x6000 },
};

static const struct regmap_config disp_cc_sm8150_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x10000,
	.fast_io	= true,
};

static const struct qcom_cc_desc disp_cc_sm8150_desc = {
	.config = &disp_cc_sm8150_regmap_config,
	.clks = disp_cc_sm8150_clocks,
	.num_clks = ARRAY_SIZE(disp_cc_sm8150_clocks),
	.resets = disp_cc_sm8150_resets,
	.num_resets = ARRAY_SIZE(disp_cc_sm8150_resets),
};

static const struct of_device_id disp_cc_sm8150_match_table[] = {
	{ .compatible = "qcom,dispcc-sm8150" },
	{ .compatible = "qcom,dispcc-sm8150-v2" },
	{ .compatible = "qcom,dispcc-sdmshrike-v2" },
	{ }
};
MODULE_DEVICE_TABLE(of, disp_cc_sm8150_match_table);

static void disp_cc_sm8150_fixup_sm8150v2(struct regmap *regmap)
{
	disp_cc_pll0.config = &disp_cc_pll0_config_sm8150_v2;
	disp_cc_pll1.config = &disp_cc_pll1_config_sm8150_v2;

	disp_cc_mdss_dp_pixel1_clk_src.clkr.hw.init->rate_max[VDD_LOW_L1] =
		337500;
	disp_cc_mdss_dp_pixel1_clk_src.clkr.hw.init->rate_max[VDD_NOMINAL] =
		675000;
	disp_cc_mdss_dp_pixel2_clk_src.clkr.hw.init->rate_max[VDD_LOW_L1] =
		337500;
	disp_cc_mdss_dp_pixel2_clk_src.clkr.hw.init->rate_max[VDD_NOMINAL] =
		675000;
	disp_cc_mdss_dp_pixel_clk_src.clkr.hw.init->rate_max[VDD_LOW_L1] =
		337500;
	disp_cc_mdss_dp_pixel_clk_src.clkr.hw.init->rate_max[VDD_NOMINAL] =
		675000;
	disp_cc_mdss_edp_link_clk_src.clkr.hw.init->rate_max[VDD_LOW] =
		594000000;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init->rate_max[VDD_LOW_L1] =
		337500000;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init->rate_max[VDD_NOMINAL] =
		675000000;
}

static void disp_cc_sm8150_fixup_sdmshrikev2(struct regmap *regmap)
{
	disp_cc_sm8150_fixup_sm8150v2(regmap);

	disp_cc_mdss_dp_crypto_clk_src.freq_tbl =
		ftbl_disp_cc_mdss_dp_crypto_clk_src_sdmshrikev2;
	disp_cc_mdss_dp_crypto1_clk_src.freq_tbl =
		ftbl_disp_cc_mdss_dp_crypto1_clk_src_sdmshrikev2;
	disp_cc_mdss_dp_link_clk_src.freq_tbl =
		ftbl_disp_cc_mdss_dp_link_clk_src_sdmshrikev2;
	disp_cc_mdss_dp_link1_clk_src.freq_tbl =
		ftbl_disp_cc_mdss_dp_link1_clk_src_sdmshrikev2;

	disp_cc_mdss_edp_link_clk_src.freq_tbl =
		ftbl_disp_cc_mdss_edp_link_clk_src_sdmshrikev2;
	disp_cc_mdss_edp_link_clk_src.clkr.hw.init->rate_max[VDD_MIN] =
		19200;
	disp_cc_mdss_edp_link_clk_src.clkr.hw.init->rate_max[VDD_LOWER] =
		270000;
	disp_cc_mdss_edp_link_clk_src.clkr.hw.init->rate_max[VDD_LOW_L1] =
		594000;
	disp_cc_mdss_edp_link_clk_src.clkr.hw.init->rate_max[VDD_NOMINAL] =
		810000;

	disp_cc_mdss_edp_pixel_clk_src.parent_map =
		disp_cc_parent_map_4_sdmshrikev2;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init =
		&disp_cc_mdss_edp_pixel_clk_src_sdmshrikev2;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init->rate_max[VDD_MIN] =
		19200;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init->rate_max[VDD_LOWER] =
		337500;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init->rate_max[VDD_LOW_L1] =
		371500;
	disp_cc_mdss_edp_pixel_clk_src.clkr.hw.init->rate_max[VDD_NOMINAL] =
		675000;
}

static int disp_cc_sm8150_fixup(struct platform_device *pdev,
	struct regmap *regmap)
{
	const char *compat = NULL;
	int compatlen = 0;

	compat = of_get_property(pdev->dev.of_node, "compatible", &compatlen);
	if (!compat || (compatlen <= 0))
		return -EINVAL;

	if (!strcmp(compat, "qcom,dispcc-sm8150-v2"))
		disp_cc_sm8150_fixup_sm8150v2(regmap);
	else if (!strcmp(compat, "qcom,dispcc-sdmshrike-v2"))
		disp_cc_sm8150_fixup_sdmshrikev2(regmap);

	return 0;
}

static int disp_cc_sm8150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct clk *clk;
	int ret = 0;
	int i;
	unsigned int dispcc_bus_id;

	regmap = qcom_cc_map(pdev, &disp_cc_sm8150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the display CC registers\n");
		return PTR_ERR(regmap);
	}

	clk = devm_clk_get(&pdev->dev, "cfg_ahb_clk");
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Unable to get ahb clock handle\n");
		return PTR_ERR(clk);
	}
	devm_clk_put(&pdev->dev, clk);

	vdd_mm.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_mm");
	if (IS_ERR(vdd_mm.regulator[0])) {
		if (PTR_ERR(vdd_mm.regulator[0]) != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"Unable to get vdd_mm regulator\n");
		return PTR_ERR(vdd_mm.regulator[0]);
	}

	dispcc_bus_id = msm_bus_scale_register_client(&clk_debugfs_scale_table);
	if (!dispcc_bus_id) {
		dev_err(&pdev->dev, "Unable to register for bw voting\n");
		return -EPROBE_DEFER;
	}
	for (i = 0; i < ARRAY_SIZE(disp_cc_sm8150_clocks); i++)
		if (disp_cc_sm8150_clocks[i])
			*(unsigned int *)(void *)
			&disp_cc_sm8150_clocks[i]->hw.init->bus_cl_id =
							dispcc_bus_id;

	ret = disp_cc_sm8150_fixup(pdev, regmap);
	if (ret)
		return ret;

	clk_trion_pll_configure(&disp_cc_pll0, regmap, disp_cc_pll0.config);
	clk_trion_pll_configure(&disp_cc_pll1, regmap, disp_cc_pll1.config);

	/* Enable clock gating for DSI and MDP clocks */
	regmap_update_bits(regmap, DISP_CC_MISC_CMD, 0x10, 0x10);

	ret = qcom_cc_really_probe(pdev, &disp_cc_sm8150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register Display CC clocks\n");
		return ret;
	}

	dev_info(&pdev->dev, "Registered Display CC clocks\n");
	return ret;
}

static struct platform_driver disp_cc_sm8150_driver = {
	.probe		= disp_cc_sm8150_probe,
	.driver		= {
		.name	= "disp_cc-sm8150",
		.of_match_table = disp_cc_sm8150_match_table,
	},
};

static int __init disp_cc_sm8150_init(void)
{
	return platform_driver_register(&disp_cc_sm8150_driver);
}
subsys_initcall(disp_cc_sm8150_init);

static void __exit disp_cc_sm8150_exit(void)
{
	platform_driver_unregister(&disp_cc_sm8150_driver);
}
module_exit(disp_cc_sm8150_exit);

MODULE_DESCRIPTION("QTI DISP_CC SM8150 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:disp_cc-sm8150");
