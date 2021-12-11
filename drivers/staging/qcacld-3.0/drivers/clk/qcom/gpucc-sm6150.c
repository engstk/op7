/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gpucc-sm6150.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-sm6150.h"

#define CX_GMU_CBCR_SLEEP_MASK		0xf
#define CX_GMU_CBCR_SLEEP_SHIFT		4
#define CX_GMU_CBCR_WAKE_MASK		0xf
#define CX_GMU_CBCR_WAKE_SHIFT		8
#define GFX3D_CRC_SID_FSM_CTRL		0x1024
#define GFX3D_CRC_MND_CFG		0x1028

#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_NUM, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_MX_NUM, 1, vdd_mx_corner);

enum {
	P_BI_TCXO,
	P_CORE_BI_PLL_TEST_SE,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL0_2X_CLK,
	P_CRC_DIV_PLL0_OUT_AUX2,
	P_GPU_CC_PLL0_OUT_MAIN,
	P_GPU_CC_PLL1_OUT_AUX,
	P_CRC_DIV_PLL1_OUT_AUX2,
	P_GPU_CC_PLL1_OUT_MAIN,
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_OUT_MAIN, 1 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gpu_cc_parent_names_0[] = {
	"bi_tcxo",
	"gpu_cc_pll0_out_main",
	"gpu_cc_pll1_out_main",
	"gpll0_out_main",
	"gpll0_out_main_div",
	"core_bi_pll_test_se",
};

static const struct parent_map gpu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL0_2X_CLK, 1 },
	{ P_CRC_DIV_PLL0_OUT_AUX2, 2 },
	{ P_GPU_CC_PLL1_OUT_AUX, 3 },
	{ P_CRC_DIV_PLL1_OUT_AUX2, 4 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gpu_cc_parent_names_1[] = {
	"bi_tcxo",
	"gpu_cc_pll0_out_aux",
	"crc_div_pll0_out_aux2",
	"gpu_cc_pll1_out_aux",
	"crc_div_pll1_out_aux2",
	"gpll0_out_main",
	"core_bi_pll_test_se",
};

static struct pll_vco gpu_cc_pll_vco[] = {
	{ 1000000000, 2000000000, 0 },
	{ 500000000,  1000000000, 2 },
};

static struct pll_vco gpu_cc_pll0_vco[] = {
	{ 1000000000, 2000000000, 0 },
};

static struct pll_vco gpu_cc_pll1_vco[] = {
	{ 500000000,  1000000000, 2 },
};

/* 1020MHz configuration */
static struct alpha_pll_config gpu_pll0_config = {
	.l = 0x35,
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
	.alpha_u = 0x20,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x0 << 20,
	.vco_mask = 0x3 << 20,
	.aux2_output_mask = BIT(2),
};

/* 930MHz configuration */
static struct alpha_pll_config gpu_pll1_config = {
	.l = 0x30,
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi_val = 0x1,
	.test_ctl_hi_mask = 0x1,
	.alpha_u = 0x70,
	.alpha = 0x00,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = 0x3 << 20,
	.aux2_output_mask = BIT(2),
};

static struct clk_init_data gpu_cc_pll0_out_aux2_sa6155 = {
	.name = "gpu_cc_pll0_out_aux2",
	.parent_names = (const char *[]){ "bi_tcxo" },
	.num_parents = 1,
	.ops = &clk_alpha_pll_slew_ops,
	.vdd_class = &vdd_mx,
	.num_rate_max = VDD_MX_NUM,
	.rate_max = (unsigned long[VDD_MX_NUM]) {
		[VDD_MX_MIN] = 1000000000,
		[VDD_MX_NOMINAL] = 2000000000},
};

static struct clk_alpha_pll gpu_cc_pll0_out_aux2 = {
	.offset = 0x0,
	.vco_table = gpu_cc_pll_vco,
	.num_vco = ARRAY_SIZE(gpu_cc_pll_vco),
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.config = &gpu_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_pll0_out_aux2",
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

static struct clk_init_data gpu_cc_pll1_out_aux2_sa6155 = {
	.name = "gpu_cc_pll1_out_aux2",
	.parent_names = (const char *[]){ "bi_tcxo" },
	.num_parents = 1,
	.ops = &clk_alpha_pll_slew_ops,
	.vdd_class = &vdd_mx,
	.num_rate_max = VDD_MX_NUM,
	.rate_max = (unsigned long[VDD_MX_NUM]) {
		[VDD_MX_MIN] = 1000000000,
		[VDD_MX_NOMINAL] = 2000000000},
};

static struct clk_alpha_pll gpu_cc_pll1_out_aux2 = {
	.offset = 0x100,
	.vco_table = gpu_cc_pll_vco,
	.num_vco = ARRAY_SIZE(gpu_cc_pll_vco),
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.config = &gpu_pll1_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_pll1_out_aux2",
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

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gmu_clk_src",
		.parent_names = gpu_cc_parent_names_0,
		.num_parents = 6,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 200000000},
	},
};

static struct clk_fixed_factor crc_div_pll0_out_aux2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "crc_div_pll0_out_aux2",
		.parent_names = (const char *[]){ "gpu_cc_pll0_out_aux2" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static struct clk_fixed_factor crc_div_pll1_out_aux2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "crc_div_pll1_out_aux2",
		.parent_names = (const char *[]){ "gpu_cc_pll1_out_aux2" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct freq_tbl ftbl_gpu_cc_gx_gfx3d_clk_src[] = {
	F(290000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(350000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(435000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(500000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(550000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(650000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(700000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(745000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(845000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(895000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gpu_cc_gx_gfx3d_clk_src_sa6155[] = {
	F(290000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(350000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(435000000, P_CRC_DIV_PLL1_OUT_AUX2, 1, 0, 0),
	F(500000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(550000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(650000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(700000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(745000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	F(845000000, P_CRC_DIV_PLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_1,
	.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src,
	.flags = FORCE_ENABLE_RCG,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gx_gfx3d_clk_src",
		.parent_names = gpu_cc_parent_names_1,
		.num_parents = 7,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 290000000,
			[VDD_LOW] = 435000000,
			[VDD_LOW_L1] = 550000000,
			[VDD_NOMINAL] = 700000000,
			[VDD_NOMINAL_L1] = 745000000,
			[VDD_HIGH] = 845000000,
			[VDD_HIGH_L1] = 895000000},
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_crc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_apb_clk = {
	.halt_reg = 0x1088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1088,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_apb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_clk = {
	.halt_reg = 0x10a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_gfx3d_clk",
			.parent_names = (const char *[]){
				"gpu_cc_gx_gfx3d_clk",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gfx3d_slv_clk = {
	.halt_reg = 0x10a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10a8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_gfx3d_slv_clk",
			.parent_names = (const char *[]){
				"gpu_cc_gx_gfx3d_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_gmu_clk",
			.parent_names = (const char *[]){
				"gpu_cc_gmu_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_snoc_dvm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gfx3d_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_gx_gfx3d_clk",
			.parent_names = (const char *[]){
				"gpu_cc_gx_gfx3d_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_gx_gmu_clk = {
	.halt_reg = 0x1064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_gx_gmu_clk",
			.parent_names = (const char *[]){
				"gpu_cc_gmu_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_sleep_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_ahb_clk = {
	.halt_reg = 0x1078,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_hlos1_vote_gpu_smmu_clk = {
	.halt_reg = 0x5000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x5000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_hlos1_vote_gpu_smmu_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

struct clk_hw *gpu_cc_sm6150_hws[] = {
	[CRC_DIV_PLL0_OUT_AUX2] = &crc_div_pll0_out_aux2.hw,
	[CRC_DIV_PLL1_OUT_AUX2] = &crc_div_pll1_out_aux2.hw,
};

static struct clk_regmap *gpu_cc_sm6150_clocks[] = {
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_APB_CLK] = &gpu_cc_cx_apb_clk.clkr,
	[GPU_CC_CX_GFX3D_CLK] = &gpu_cc_cx_gfx3d_clk.clkr,
	[GPU_CC_CX_GFX3D_SLV_CLK] = &gpu_cc_cx_gfx3d_slv_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpu_cc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpu_cc_cxo_aon_clk.clkr,
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_PLL0_OUT_AUX2] = &gpu_cc_pll0_out_aux2.clkr,
	[GPU_CC_PLL1_OUT_AUX2] = &gpu_cc_pll1_out_aux2.clkr,
	[GPU_CC_SLEEP_CLK] = &gpu_cc_sleep_clk.clkr,
	[GPU_CC_GX_GMU_CLK] = &gpu_cc_gx_gmu_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK] = &gpu_cc_gx_gfx3d_clk.clkr,
	[GPU_CC_GX_GFX3D_CLK_SRC] = &gpu_cc_gx_gfx3d_clk_src.clkr,
	[GPU_CC_AHB_CLK] = &gpu_cc_ahb_clk.clkr,
	[GPU_CC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpu_cc_hlos1_vote_gpu_smmu_clk.clkr,
};

static const struct regmap_config gpu_cc_sm6150_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x7008,
	.fast_io	= true,
};

static const struct qcom_cc_desc gpu_cc_sm6150_desc = {
	.config = &gpu_cc_sm6150_regmap_config,
	.clks = gpu_cc_sm6150_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sm6150_clocks),
	.hwclks = gpu_cc_sm6150_hws,
	.num_hwclks = ARRAY_SIZE(gpu_cc_sm6150_hws),
};

static struct clk_regmap *gpu_cc_sm6150_critical_clocks[] = {
	&gpu_cc_ahb_clk.clkr,
};

static const struct qcom_cc_critical_desc gpu_cc_sm6150_critical_desc = {
	.clks = gpu_cc_sm6150_critical_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sm6150_critical_clocks),
};

static const struct of_device_id gpu_cc_sm6150_match_table[] = {
	{ .compatible = "qcom,gpucc-sm6150" },
	{ .compatible = "qcom,gpucc-sa6155" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_sm6150_match_table);

static void gpu_cc_sm6150_configure(struct regmap *regmap)
{
	unsigned int value, mask;

	/* Recommended WAKEUP/SLEEP settings for the gpu_cc_cx_gmu_clk */
	mask = CX_GMU_CBCR_WAKE_MASK << CX_GMU_CBCR_WAKE_SHIFT;
	mask |= CX_GMU_CBCR_SLEEP_MASK << CX_GMU_CBCR_SLEEP_SHIFT;
	value = 0xf << CX_GMU_CBCR_WAKE_SHIFT | 0xf << CX_GMU_CBCR_SLEEP_SHIFT;
	regmap_update_bits(regmap, gpu_cc_cx_gmu_clk.clkr.enable_reg,
							mask, value);

	/* After POR, Clock Ramp Controller(CRC) will be in bypass mode.
	 * Software needs to do the following operation to enable the CRC
	 * for GFX3D clock and divide the input clock by div by 2.
	 */
	regmap_update_bits(regmap, GFX3D_CRC_MND_CFG, 0x00015011, 0x00015011);
	regmap_update_bits(regmap,
			GFX3D_CRC_SID_FSM_CTRL, 0x00800000, 0x00800000);
}

static int gpu_cc_sm6150_resume(struct device *dev)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	gpu_cc_sm6150_configure(regmap);

	return qcom_cc_enable_critical_clks(&gpu_cc_sm6150_critical_desc);
}

static const struct dev_pm_ops gpu_cc_sm6150_pm_ops = {
	.restore_early = gpu_cc_sm6150_resume,
};

static void gpucc_sm6150_fixup_sa6155(struct platform_device *pdev)
{
	vdd_cx.num_levels = VDD_NUM_SA6155;
	vdd_cx.cur_level = VDD_NUM_SA6155;
	vdd_mx.num_levels = VDD_MX_NUM_SA6155;
	vdd_mx.cur_level = VDD_MX_NUM_SA6155;
	gpu_cc_gx_gfx3d_clk_src.clkr.hw.init->rate_max[VDD_HIGH_L1] = 0;
	gpu_cc_gx_gfx3d_clk_src.freq_tbl = ftbl_gpu_cc_gx_gfx3d_clk_src_sa6155;

	gpu_cc_pll0_out_aux2.vco_table = gpu_cc_pll0_vco;
	gpu_cc_pll0_out_aux2.num_vco = ARRAY_SIZE(gpu_cc_pll0_vco);
	gpu_cc_pll0_out_aux2.clkr.hw.init = &gpu_cc_pll0_out_aux2_sa6155;
	gpu_cc_pll1_out_aux2.vco_table = gpu_cc_pll1_vco;
	gpu_cc_pll1_out_aux2.num_vco = ARRAY_SIZE(gpu_cc_pll1_vco);
	gpu_cc_pll1_out_aux2.clkr.hw.init = &gpu_cc_pll1_out_aux2_sa6155;
	pdev->dev.driver->pm =  &gpu_cc_sm6150_pm_ops;
}

static int gpu_cc_sm6150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;
	int is_sa6155;

	/* Get CX voltage regulator for CX and GMU clocks. */
	vdd_cx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_cx");
	if (IS_ERR(vdd_cx.regulator[0])) {
		if (!(PTR_ERR(vdd_cx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_cx regulator\n");
		return PTR_ERR(vdd_cx.regulator[0]);
	}

	/* Get MX voltage regulator for GPU PLL graphic clock. */
	vdd_mx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_mx");
	if (IS_ERR(vdd_mx.regulator[0])) {
		if (!(PTR_ERR(vdd_mx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_mx regulator\n");
		return PTR_ERR(vdd_mx.regulator[0]);
	}

	is_sa6155 = of_device_is_compatible(pdev->dev.of_node,
						"qcom,gpucc-sa6155");
	if (is_sa6155)
		gpucc_sm6150_fixup_sa6155(pdev);

	regmap = qcom_cc_map(pdev, &gpu_cc_sm6150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the gpu_cc registers\n");
		return PTR_ERR(regmap);
	}

	clk_alpha_pll_configure(&gpu_cc_pll0_out_aux2, regmap,
					gpu_cc_pll0_out_aux2.config);
	clk_alpha_pll_configure(&gpu_cc_pll1_out_aux2, regmap,
					gpu_cc_pll1_out_aux2.config);

	ret = qcom_cc_really_probe(pdev, &gpu_cc_sm6150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPU CC clocks\n");
		return ret;
	}

	gpu_cc_sm6150_configure(regmap);

	if (is_sa6155)
		dev_set_drvdata(&pdev->dev, regmap);

	dev_info(&pdev->dev, "Registered GPU CC clocks\n");

	return ret;
}

static struct platform_driver gpu_cc_sm6150_driver = {
	.probe		= gpu_cc_sm6150_probe,
	.driver		= {
		.name	= "gpu_cc-sm6150",
		.of_match_table = gpu_cc_sm6150_match_table,
	},
};

static int __init gpu_cc_sm6150_init(void)
{
	return platform_driver_register(&gpu_cc_sm6150_driver);
}
subsys_initcall(gpu_cc_sm6150_init);

static void __exit gpu_cc_sm6150_exit(void)
{
	platform_driver_unregister(&gpu_cc_sm6150_driver);
}
module_exit(gpu_cc_sm6150_exit);

MODULE_DESCRIPTION("QTI GPU_CC SM6150 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gpu_cc-sm6150");
