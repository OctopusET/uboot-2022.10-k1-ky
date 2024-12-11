// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Spacemit K1x Mobile Storage Host Controller
 *
 * Copyright (C) 2023 Spacemit Inc.
 */
#include <common.h>
#include <clk.h>
#include <dm.h>
#include <dm/pinctrl.h>
#include <fdtdec.h>
#include <linux/libfdt.h>
#include <linux/delay.h>
#include <malloc.h>
#include <sdhci.h>
#include <reset-uclass.h>
#include <power/regulator.h>
#include <mapmem.h>

DECLARE_GLOBAL_DATA_PTR;

/* SDH registers define */
#define SDHC_OP_EXT_REG			0x108
#define OVRRD_CLK_OEN			0x0800
#define FORCE_CLK_ON			0x1000

#define SDHC_LEGACY_CTRL_REG		0x10C
#define GEN_PAD_CLK_ON			0x0040

#define SDHC_MMC_CTRL_REG		0x114
#define MISC_INT_EN			0x0002
#define MISC_INT			0x0004
#define ENHANCE_STROBE_EN		0x0100
#define MMC_HS400			0x0200
#define MMC_HS200			0x0400
#define MMC_CARD_MODE			0x1000

#define SDHC_TX_CFG_REG			0x11C
#define TX_INT_CLK_SEL			0x40000000
#define TX_MUX_SEL			0x80000000

#define SDHC_PHY_CTRL_REG		0x160
#define PHY_FUNC_EN			0x0001
#define PHY_PLL_LOCK			0x0002
#define HOST_LEGACY_MODE		0x80000000

#define SDHC_PHY_FUNC_REG		0x164
#define PHY_TEST_EN			0x0080
#define HS200_USE_RFIFO			0x8000

#define SDHC_PHY_DLLCFG			0x168
#define DLL_PREDLY_NUM			0x04
#define DLL_FULLDLY_RANGE		0x10
#define DLL_VREG_CTRL			0x40
#define DLL_ENABLE			0x80000000
#define DLL_REFRESH_SWEN_SHIFT		0x1C
#define DLL_REFRESH_SW_SHIFT		0x1D

#define SDHC_PHY_DLLCFG1		0x16C
#define DLL_REG2_CTRL			0x0C
#define DLL_REG3_CTRL_MASK		0xFF
#define DLL_REG3_CTRL_SHIFT		0x10
#define DLL_REG2_CTRL_MASK		0xFF
#define DLL_REG2_CTRL_SHIFT		0x08
#define DLL_REG1_CTRL			0x92
#define DLL_REG1_CTRL_MASK		0xFF
#define DLL_REG1_CTRL_SHIFT		0x00

#define SDHC_PHY_DLLSTS			0x170
#define DLL_LOCK_STATE			0x01

#define SDHC_PHY_DLLSTS1		0x174
#define DLL_MASTER_DELAY_MASK		0xFF
#define DLL_MASTER_DELAY_SHIFT		0x10

#define SDHC_PHY_PADCFG_REG		0x178
#define RX_BIAS_CTRL_SHIFT		0x5
#define PHY_DRIVE_SEL_SHIFT		0x0
#define PHY_DRIVE_SEL_MASK		0x7
#define PHY_DRIVE_SEL_DEFAULT		0x4

#define MMC1_IO_V18EN			0x04
#define AKEY_ASFAR			0xBABA
#define AKEY_ASSAR			0xEB10

#define SDHC_RX_CFG_REG			0x118
#define RX_SDCLK_SEL0_MASK		0x03
#define RX_SDCLK_SEL0_SHIFT		0x00
#define RX_SDCLK_SEL0			0x02
#define RX_SDCLK_SEL1_MASK		0x03
#define RX_SDCLK_SEL1_SHIFT		0x02
#define RX_SDCLK_SEL1			0x01

#define SDHC_DLINE_CTRL_REG		0x130
#define DLINE_PU			0x01
#define RX_DLINE_CODE_MASK		0xFF
#define RX_DLINE_CODE_SHIFT		0x10
#define TX_DLINE_CODE_MASK		0xFF
#define TX_DLINE_CODE_SHIFT		0x18

#define SDHC_DLINE_CFG_REG		0x134
#define RX_DLINE_REG_MASK		0xFF
#define RX_DLINE_REG_SHIFT		0x00
#define RX_DLINE_GAIN_MASK		0x1
#define RX_DLINE_GAIN_SHIFT		0x8
#define RX_DLINE_GAIN			0x1
#define TX_DLINE_REG_MASK		0xFF
#define TX_DLINE_REG_SHIFT		0x10

#define SDHC_RX_TUNE_DELAY_MIN		0x0
#define SDHC_RX_TUNE_DELAY_MAX		0xFF
#define SDHC_RX_TUNE_DELAY_STEP		0x1

#define CANDIDATE_WIN_NUM 3
#define SELECT_DELAY_NUM 9
#define WINDOW_1ST 0
#define WINDOW_2ND 1
#define WINDOW_3RD 2

#define RX_TUNING_WINDOW_THRESHOLD 80
#define RX_TUNING_DLINE_REG 0x09
#define TX_TUNING_DLINE_REG 0x00
#define TX_TUNING_DELAYCODE 127

enum window_type {
	LEFT_WINDOW = 0,
	MIDDLE_WINDOW = 1,
	RIGHT_WINDOW = 2,
};

struct tuning_window {
	u8 type;
	u8 min_delay;
	u8 max_delay;
};

struct rx_tuning {
	u8 rx_dline_reg;
	u8 select_delay_num;
	/* 0: biggest window, 1: bigger, 2:  small */
	struct tuning_window windows[CANDIDATE_WIN_NUM];
	u8 select_delay[SELECT_DELAY_NUM];

	u8 window_limit;
};

struct spacemit_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
	struct reset_ctl_bulk resets;
	struct clk_bulk clks;

	u32 aib_mmc1_io_reg;
	u32 apbc_asfar_reg;
	u32 apbc_assar_reg;

	u8 tx_dline_reg;
	u8 tx_delaycode;
	struct rx_tuning rxtuning;
};

struct spacemit_sdhci_priv {
	struct sdhci_host host;
	u32 clk_src_freq;
	u32 phy_module;
};

struct spacemit_sdhci_driver_data {
	const struct sdhci_ops *ops;
	u32 flags;
};

static const u32 tuning_patten4[16] = {
	0x00ff0fff, 0xccc3ccff, 0xffcc3cc3, 0xeffefffe,
	0xddffdfff, 0xfbfffbff, 0xff7fffbf, 0xefbdf777,
	0xf0fff0ff, 0x3cccfc0f, 0xcfcc33cc, 0xeeffefff,
	0xfdfffdff, 0xffbfffdf, 0xfff7ffbb, 0xde7b7ff7,
};

static const u32 tuning_patten8[32] = {
	0xff00ffff, 0x0000ffff, 0xccccffff, 0xcccc33cc,
	0xcc3333cc, 0xffffcccc, 0xffffeeff, 0xffeeeeff,
	0xffddffff, 0xddddffff, 0xbbffffff, 0xbbffffff,
	0xffffffbb, 0xffffff77, 0x77ff7777, 0xffeeddbb,
	0x00ffffff, 0x00ffffff, 0xccffff00, 0xcc33cccc,
	0x3333cccc, 0xffcccccc, 0xffeeffff, 0xeeeeffff,
	0xddffffff, 0xddffffff, 0xffffffdd, 0xffffffbb,
	0xffffbbbb, 0xffff77ff, 0xff7777ff, 0xeeddbb77,
};

/*
 * refer to PMU_SDH0_CLK_RES_CTRL<0x054>, SDH0_CLK_SEL:0x0, SDH0_CLK_DIV:0x1
 * the default clock source is 204800000Hz [409.6MHz(pll1_d6_409p6Mhz)/2]
 *
 * in the start-up phase, use the 200KHz frequency
 */
#define SDHC_DEFAULT_MAX_CLOCK (204800000)
#define SDHC_MIN_CLOCK (200*1000)

static int is_emulator_platform(void)
{
#ifdef CONFIG_K1_X_BOARD_FPGA
	return 1;
#else
	return 0;
#endif
}

static int spacemit_reg[] = {
	0x100, 0x104, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x11c,
	0x120, 0x124, 0x128, 0x12c, 0x130, 0x134, 0x160, 0x164,
	0x168, 0x16c, 0x170, 0x174, 0x178, 0x17c, 0x180, 0x184,
	0x188, 0x18c, 0x190, 0x1f0, 0x1f4, 0xFFF,
};
static u8 cur_com_reg[960]; /* 8 line, 120  character  per line */
static u8 cur_pri_reg[960];

static void __maybe_unused dump_sdh_regs(struct sdhci_host *host, u8 is_emmc)
{
	int val;
	int offset;
	int i;
	int len;
	char *buf;

	buf = (char *)&cur_com_reg[0];
	len = 0;
	i = 0;
	for (offset = 0; offset < 0x70; offset += 4) {
		val = sdhci_readl(host, offset);
		if (i % 4 == 0)
			len += sprintf(buf + len, "\n");
		len += sprintf(buf + len, "\toffset:0x%03x 0x%08x\t", offset, val);
		i++;
	}

	if (i % 4 == 0)
		len += sprintf(buf + len, "\n");
	val = sdhci_readl(host, 0xe0);
	len += sprintf(buf + len, "\toffset:0x%03x 0x%08x\t", 0xe0, val);
	val = sdhci_readl(host, 0xfc);
	len += sprintf(buf + len, "\toffset:0x%03x 0x%08x\t\n", 0xfc, val);

	buf = (char *)&cur_pri_reg[0];
	len = 0;
	i = 0;
	do {
		if (!is_emmc && (spacemit_reg[i] > 0x134))
			break;
		val = sdhci_readl(host, spacemit_reg[i]);
		if (i % 4 == 0)
			len += sprintf(buf + len, "\n");
		len += sprintf(buf + len, "\toffset:0x%03x 0x%08x\t", spacemit_reg[i], val);
		i++;
	} while (spacemit_reg[i] != 0xFFF);
	len += sprintf(buf + len, "\n");

	pr_debug("%s", cur_com_reg);
	pr_debug("%s", cur_pri_reg);
}

static void spacemit_mmc_phy_init(struct sdhci_host *host)
{
	struct udevice *dev = host->mmc->dev;
	struct spacemit_sdhci_priv *priv = dev_get_priv(dev);
	unsigned int value = 0;

	if (priv->phy_module) {
		/* mmc card mode */
		value = sdhci_readl(host, SDHC_MMC_CTRL_REG);
		value |= MMC_CARD_MODE;
		sdhci_writel(host, value, SDHC_MMC_CTRL_REG);

		if (is_emulator_platform()) {
			/* mmc phy bypass */
			value = sdhci_readl(host, SDHC_TX_CFG_REG);
			value |= TX_INT_CLK_SEL;
			sdhci_writel (host, value, SDHC_TX_CFG_REG);

			value = sdhci_readl(host, SDHC_PHY_CTRL_REG);
			value |= HOST_LEGACY_MODE;
			sdhci_writel (host, value, SDHC_PHY_CTRL_REG);

			value = sdhci_readl(host, SDHC_PHY_FUNC_REG);
			value |= PHY_TEST_EN;
			sdhci_writel (host, value, SDHC_PHY_FUNC_REG);
			pr_debug("%s: mmc phy bypass.\n", host->name);
		} else {
			/* use phy func mode */
			value = sdhci_readl(host, SDHC_PHY_CTRL_REG);
			value |= (PHY_FUNC_EN | PHY_PLL_LOCK);
			sdhci_writel(host, value, SDHC_PHY_CTRL_REG);

			value = sdhci_readl(host, SDHC_PHY_PADCFG_REG);
			value |= (1 << RX_BIAS_CTRL_SHIFT);
			sdhci_writel(host, value, SDHC_PHY_PADCFG_REG);
			pr_debug("%s: use mmc phy func.\n", host->name);
		}
	} else {
		pr_debug("%s: not support phy module.\n", host->name);
		value = sdhci_readl (host, SDHC_TX_CFG_REG);
		value |= TX_INT_CLK_SEL;
		sdhci_writel (host, value, SDHC_TX_CFG_REG);
	}

	value = sdhci_readl(host, SDHC_MMC_CTRL_REG);
	value &= ~ENHANCE_STROBE_EN;
	sdhci_writel(host, value, SDHC_MMC_CTRL_REG);
}

static void spacemit_sdhci_set_voltage(struct sdhci_host *host)
{
	if (IS_ENABLED(CONFIG_MMC_IO_VOLTAGE)) {
		struct mmc *mmc = (struct mmc *)host->mmc;
		u32 ctrl;

		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		switch (mmc->signal_voltage) {
		case MMC_SIGNAL_VOLTAGE_330:
#if CONFIG_IS_ENABLED(DM_REGULATOR)
			if (mmc->vqmmc_supply) {
				if (regulator_set_value(mmc->vqmmc_supply, 3300000)) {
					pr_err("failed to set vqmmc-voltage to 3.3V\n");
					return;
				}

				if (regulator_set_enable_if_allowed(mmc->vqmmc_supply, true)) {
					pr_err("failed to enable vqmmc-supply\n");
					return;
				}
			}
#endif
			if (IS_SD(mmc)) {
				ctrl &= ~SDHCI_CTRL_VDD_180;
				sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
			}

			/* Wait for 5ms */
			mdelay(5);

			/* 3.3V regulator output should be stable within 5 ms */
			if (IS_SD(mmc)) {
				if (ctrl & SDHCI_CTRL_VDD_180) {
					pr_err("3.3V regulator output did not become stable\n");
					return;
				}
			}

			break;
		case MMC_SIGNAL_VOLTAGE_180:
#if CONFIG_IS_ENABLED(DM_REGULATOR)
			if (mmc->vqmmc_supply) {
				if (regulator_set_value(mmc->vqmmc_supply, 1800000)) {
					pr_err("failed to set vqmmc-voltage to 1.8V\n");
					return;
				}

				if (regulator_set_enable_if_allowed(mmc->vqmmc_supply, true)) {
					pr_err("failed to enable vqmmc-supply\n");
					return;
				}
			}
#endif
			if (IS_SD(mmc)) {
				ctrl |= SDHCI_CTRL_VDD_180;
				sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
			}

			/* Wait for 5 ms */
			mdelay(5);

			/* 1.8V regulator output has to be stable within 5 ms */
			if (IS_SD(mmc)) {
				if (!(ctrl & SDHCI_CTRL_VDD_180)) {
					pr_err("1.8V regulator output did not become stable\n");
					return;
				}
			}

			break;
		default:
			/* No signal voltage switch required */
			return;
		}
	}
}

static void spacemit_set_aib_mmc1_io(struct sdhci_host *host, int vol)
{
	void __iomem *aib_mmc1_io;
	void __iomem *apbc_asfar;
	void __iomem *apbc_assar;
	u32 reg;
	struct mmc *mmc = host->mmc;
	struct spacemit_sdhci_plat *plat = dev_get_plat(mmc->dev);

	if (!plat->aib_mmc1_io_reg ||
		!plat->apbc_asfar_reg ||
		!plat->apbc_assar_reg)
		return;

	aib_mmc1_io = map_sysmem((uintptr_t)plat->aib_mmc1_io_reg, sizeof(uintptr_t));
	apbc_asfar = map_sysmem((uintptr_t)plat->apbc_asfar_reg, sizeof(uintptr_t));
	apbc_assar = map_sysmem((uintptr_t)plat->apbc_assar_reg, sizeof(uintptr_t));

	writel(AKEY_ASFAR, apbc_asfar);
	writel(AKEY_ASSAR, apbc_assar);
	reg = readl(aib_mmc1_io);

	switch (vol) {
	case MMC_SIGNAL_VOLTAGE_180:
		reg |= MMC1_IO_V18EN;
		break;
	default:
		reg &= ~MMC1_IO_V18EN;
		break;
	}
	writel(AKEY_ASFAR, apbc_asfar);
	writel(AKEY_ASSAR, apbc_assar);
	writel(reg, aib_mmc1_io);
}

static void spacemit_sdhci_set_clk_gate(struct sdhci_host *host, int auto_gate)
{
	unsigned int reg;

	reg = sdhci_readl(host, SDHC_OP_EXT_REG);
	if (auto_gate)
		reg &= ~(OVRRD_CLK_OEN | FORCE_CLK_ON);
	else
		reg |= (OVRRD_CLK_OEN | FORCE_CLK_ON);
	sdhci_writel(host, reg, SDHC_OP_EXT_REG);
}

static int spacemit_sdhci_wait_dat0(struct udevice *dev, int state,
			   int timeout_us)
{
	struct mmc *mmc = mmc_get_mmc_dev(dev);
	struct sdhci_host *host = mmc->priv;
	unsigned long timeout = timer_get_us() + timeout_us;
	u32 tmp;
	u32 cmd;

	// readx_poll_timeout is unsuitable because sdhci_readl accepts
	// two arguments
	do {
		tmp = sdhci_readl(host, SDHCI_PRESENT_STATE);
		if (!!(tmp & SDHCI_DATA_0_LVL_MASK) == !!state){
			if (IS_SD(mmc)) {
				cmd = SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND));
				if ((cmd == SD_CMD_SWITCH_UHS18V) && (mmc->signal_voltage == MMC_SIGNAL_VOLTAGE_180)) {
					/* recover the auto clock */
					spacemit_sdhci_set_clk_gate(host, 1);
				}
			}
			return 0;
		}
	} while (!timeout_us || !time_after(timer_get_us(), timeout));

	return -ETIMEDOUT;
}

static void spacemit_sdhci_set_control_reg(struct sdhci_host *host)
{
	struct mmc *mmc = host->mmc;
	u32 reg;
	u32 cmd;

	pr_debug("%s: select mode: %s, io voltage: %d\n", host->name,
			mmc_mode_name(mmc->selected_mode), mmc->signal_voltage);

	spacemit_sdhci_set_voltage(host);
	spacemit_set_aib_mmc1_io(host, mmc->signal_voltage);

	if (IS_SD(mmc)) {
		cmd = SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND));
		if ((cmd == SD_CMD_SWITCH_UHS18V) && (mmc->signal_voltage == MMC_SIGNAL_VOLTAGE_180)) {
			/* disable auto clock */
			spacemit_sdhci_set_clk_gate(host, 0);
		}
	}

	/* according to the SDHC_TX_CFG_REG(0x11c<bit>),
	 * set TX_INT_CLK_SEL to gurantee the hold time
	 * at default speed mode or HS/SDR12/SDR25/SDR50 mode.
	 */
	reg = sdhci_readl(host, SDHC_TX_CFG_REG);
	if ((mmc->selected_mode == MMC_LEGACY) ||
	    (mmc->selected_mode == MMC_HS) ||
	    (mmc->selected_mode == SD_HS) ||
	    (mmc->selected_mode == UHS_SDR12) ||
	    (mmc->selected_mode == UHS_SDR25) ||
	    (mmc->selected_mode == UHS_SDR50)) {
		reg |= TX_INT_CLK_SEL;
	} else {
		reg &= ~TX_INT_CLK_SEL;
	}
	sdhci_writel(host, reg, SDHC_TX_CFG_REG);

	/* set pinctrl state */
#ifdef CONFIG_PINCTRL
	if (mmc->clock >= 200000000)
		pinctrl_select_state(mmc->dev, "fast");
	else
		pinctrl_select_state(mmc->dev, "default");
#endif

	if ((mmc->selected_mode == MMC_HS_200) ||
	    (mmc->selected_mode == MMC_HS_400) ||
	    (mmc->selected_mode == MMC_HS_400_ES)) {
		reg = sdhci_readw(host, SDHC_MMC_CTRL_REG);
		reg |= (mmc->selected_mode == MMC_HS_200) ? MMC_HS200 : MMC_HS400;
		sdhci_writew(host, reg, SDHC_MMC_CTRL_REG);
	} else {
		reg = sdhci_readw(host, SDHC_MMC_CTRL_REG);
		reg &= ~(MMC_HS200 | MMC_HS400 | ENHANCE_STROBE_EN);
		sdhci_writew(host, reg, SDHC_MMC_CTRL_REG);
	}

	sdhci_set_uhs_timing(host);
	return;
}

static void spacemit_sw_rx_tuning_prepare(struct sdhci_host *host, u8 dline_reg)
{
	struct mmc *mmc = host->mmc;
	u32 reg;

	reg = sdhci_readl(host, SDHC_DLINE_CFG_REG);
	reg &= ~(RX_DLINE_REG_MASK << RX_DLINE_REG_SHIFT);
	reg |= dline_reg << RX_DLINE_REG_SHIFT;

	reg &= ~(RX_DLINE_GAIN_MASK << RX_DLINE_GAIN_SHIFT);
	if ((mmc->selected_mode == UHS_SDR50) && (reg & 0x40))
		reg |= RX_DLINE_GAIN << RX_DLINE_GAIN_SHIFT;

	sdhci_writel(host, reg, SDHC_DLINE_CFG_REG);

	reg = sdhci_readl(host, SDHC_DLINE_CTRL_REG);
	reg |= DLINE_PU;
	sdhci_writel(host, reg, SDHC_DLINE_CTRL_REG);
	udelay(5);

	reg = sdhci_readl(host, SDHC_RX_CFG_REG);
	reg &= ~(RX_SDCLK_SEL1_MASK << RX_SDCLK_SEL1_SHIFT);
	reg |= RX_SDCLK_SEL1 << RX_SDCLK_SEL1_SHIFT;
	sdhci_writel(host, reg, SDHC_RX_CFG_REG);

	if (mmc->selected_mode == MMC_HS_200) {
		reg = sdhci_readl(host, SDHC_PHY_FUNC_REG);
		reg |= HS200_USE_RFIFO;
		sdhci_writel(host, reg, SDHC_PHY_FUNC_REG);
	}
}

static void spacemit_sw_rx_set_delaycode(struct sdhci_host *host, u32 delay)
{
	u32 reg;

	reg = sdhci_readl(host, SDHC_DLINE_CTRL_REG);
	reg &= ~(RX_DLINE_CODE_MASK << RX_DLINE_CODE_SHIFT);
	reg |= (delay & RX_DLINE_CODE_MASK) << RX_DLINE_CODE_SHIFT;
	sdhci_writel(host, reg, SDHC_DLINE_CTRL_REG);
}

static void spacemit_sw_tx_tuning_prepare(struct sdhci_host *host)
{
	u32 reg;

	/* set TX_MUX_SEL */
	reg = sdhci_readl(host, SDHC_TX_CFG_REG);
	reg |= TX_MUX_SEL;
	sdhci_writel(host, reg, SDHC_TX_CFG_REG);

	reg = sdhci_readl(host, SDHC_DLINE_CTRL_REG);
	reg |= DLINE_PU;
	sdhci_writel(host, reg, SDHC_DLINE_CTRL_REG);
	udelay(5);
}

static void spacemit_sw_tx_set_dlinereg(struct sdhci_host *host, u8 dline_reg)
{
	u32 reg;

	reg = sdhci_readl(host, SDHC_DLINE_CFG_REG);
	reg &= ~(TX_DLINE_REG_MASK << TX_DLINE_REG_SHIFT);
	reg |= dline_reg << TX_DLINE_REG_SHIFT;
	sdhci_writel(host, reg, SDHC_DLINE_CFG_REG);
}

static void spacemit_sw_tx_set_delaycode(struct sdhci_host *host, u32 delay)
{
	u32 reg;

	reg = sdhci_readl(host, SDHC_DLINE_CTRL_REG);
	reg &= ~(TX_DLINE_CODE_MASK << TX_DLINE_CODE_SHIFT);
	reg |= (delay & TX_DLINE_CODE_MASK) << TX_DLINE_CODE_SHIFT;
	sdhci_writel(host, reg, SDHC_DLINE_CTRL_REG);
}

static void spacemit_sdhci_clear_set_irqs(struct sdhci_host *host, u32 clr, u32 set)
{
	u32 ier;

	ier = sdhci_readl(host, SDHCI_INT_ENABLE);
	ier &= ~clr;
	ier |= set;
	sdhci_writel(host, ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, ier, SDHCI_SIGNAL_ENABLE);
}

static int spacemit_tuning_patten_check(struct sdhci_host *host)
{
	struct mmc *mmc = host->mmc;
	u32 read_patten;
	unsigned int i;
	u32 *tuning_patten;
	int patten_len;
	int err = 0;

	if (mmc->bus_width == 8) {
		tuning_patten = (u32 *)tuning_patten8;
		patten_len = ARRAY_SIZE(tuning_patten8);
	} else {
		tuning_patten = (u32 *)tuning_patten4;
		patten_len = ARRAY_SIZE(tuning_patten4);
	}

	for (i = 0; i < patten_len; i++) {
		read_patten = sdhci_readl(host, SDHCI_BUFFER);
		if (read_patten != tuning_patten[i])
			err++;
	}

	return err;
}

static int spacemit_send_tuning(struct sdhci_host *host, u32 opcode, int *cmd_error)
{
	struct mmc *mmc = host->mmc;
	struct mmc_cmd cmd;
	int size, blk_size, err;
	size = sizeof(tuning_patten4);

	cmd.cmdidx = opcode;
	cmd.cmdarg = 0;
	cmd.resp_type = MMC_RSP_R1;

	blk_size = SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, 64);
	sdhci_writew(host, blk_size, SDHCI_BLOCK_SIZE);
	sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);

	err = mmc_send_cmd(mmc, &cmd, NULL);
	if (err){
		pr_warn("%s: tuning send cmd err: %d\n", host->name, err);
		return err;
	}
	return 0;
}

static int spacemit_send_tuning_cmd(struct sdhci_host *host, u32 opcode)
{
	int err = 0;

	err = spacemit_send_tuning(host, opcode, NULL);
	if (err) {
		pr_warn("%s: send tuning err:%d\n", host->name, err);
		return err;
	}

	err = spacemit_tuning_patten_check(host);
	return err;
}

static int spacemit_sw_rx_select_window(struct sdhci_host *host, u32 opcode)
{
	int min;
	int max;
	u16 ctrl;
	u32 ier;
	int err = 0;
	int i, j, len;
	struct tuning_window tmp;
	struct mmc *mmc = host->mmc;
	struct spacemit_sdhci_plat *pdata = dev_get_plat(mmc->dev);
	struct rx_tuning *rxtuning = &pdata->rxtuning;

	/* change to pio mode during the tuning stage */
	ier = sdhci_readl(host, SDHCI_INT_ENABLE);
	spacemit_sdhci_clear_set_irqs(host, ier, SDHCI_INT_DATA_AVAIL);

	min = SDHC_RX_TUNE_DELAY_MIN;
	do {
		/* find the mininum delay first which can pass tuning */
		while (min < SDHC_RX_TUNE_DELAY_MAX) {
			spacemit_sw_rx_set_delaycode(host, min);
			err = spacemit_send_tuning_cmd(host, opcode);
			if (!err)
				break;
			ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			ctrl &= ~(SDHCI_CTRL_TUNED_CLK | SDHCI_CTRL_EXEC_TUNING);
			sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
			min += SDHC_RX_TUNE_DELAY_STEP;
		}

		/* find the maxinum delay which can not pass tuning */
		max = min + SDHC_RX_TUNE_DELAY_STEP;
		while (max < SDHC_RX_TUNE_DELAY_MAX) {
			spacemit_sw_rx_set_delaycode(host, max);
			err = spacemit_send_tuning_cmd(host, opcode);
			if (err) {
				ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
				ctrl &= ~(SDHCI_CTRL_TUNED_CLK | SDHCI_CTRL_EXEC_TUNING);
				sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
				break;
			}
			max += SDHC_RX_TUNE_DELAY_STEP;
		}

		pr_info("%s: pass window [%d %d) \n", host->name, min, max);
		/* store the top 3 window */
		if ((max - min) >= rxtuning->window_limit) {
			tmp.max_delay = max;
			tmp.min_delay = min;
			tmp.type = MIDDLE_WINDOW;
			for (i = 0; i < CANDIDATE_WIN_NUM; i++) {
				len = rxtuning->windows[i].max_delay - rxtuning->windows[i].min_delay;
				if ((tmp.max_delay - tmp.min_delay) > len) {
					for (j = CANDIDATE_WIN_NUM - 1; j > i; j--) {
						rxtuning->windows[j] = rxtuning->windows[j-1];
					}
					rxtuning->windows[i] = tmp;
					break;
				}
			}
		}
		min = max + SDHC_RX_TUNE_DELAY_STEP;
	} while (min < SDHC_RX_TUNE_DELAY_MAX);

	spacemit_sdhci_clear_set_irqs(host, SDHCI_INT_DATA_AVAIL, ier);
	return 0;
}

static int spacemit_sw_rx_select_delay(struct sdhci_host *host)
{
	int i;
	int win_len, min, max, mid;
	struct tuning_window *window;

	struct mmc *mmc = host->mmc;
	struct spacemit_sdhci_plat *pdata = dev_get_plat(mmc->dev);
	struct rx_tuning *tuning = &pdata->rxtuning;

	for (i = 0; i < CANDIDATE_WIN_NUM; i++) {
		window = &tuning->windows[i];
		min = window->min_delay;
		max = window->max_delay;
		mid = (min + max - 1) / 2;
		win_len = max - min;
		if (win_len < tuning->window_limit)
			continue;

		if (window->type == LEFT_WINDOW) {
			tuning->select_delay[tuning->select_delay_num++] = min + win_len / 3;
			tuning->select_delay[tuning->select_delay_num++] = min + win_len / 2;
		} else if (window->type == RIGHT_WINDOW) {
			tuning->select_delay[tuning->select_delay_num++] = max - win_len / 4;
			tuning->select_delay[tuning->select_delay_num++] = min - win_len / 3;
		} else {
			tuning->select_delay[tuning->select_delay_num++] = mid;
			tuning->select_delay[tuning->select_delay_num++] = mid + win_len / 4;
			tuning->select_delay[tuning->select_delay_num++] = mid - win_len / 4;
		}
	}

	return tuning->select_delay_num;
}

static int spacemit_sdhci_execute_tuning(struct mmc *mmc, u8 opcode)
{
	int ret;
	struct sdhci_host *host = mmc->priv;
	struct spacemit_sdhci_plat *pdata = dev_get_plat(mmc->dev);
	struct spacemit_sdhci_priv *priv = dev_get_priv(mmc->dev);
	struct rx_tuning *rxtuning = &pdata->rxtuning;

	/*
	 * Tuning is required for SDR50/SDR104 mode
	 */
	if (!IS_SD(host->mmc) ||
		!((mmc->selected_mode == UHS_SDR50) ||
		  (mmc->selected_mode == UHS_SDR104)))
		return 0;

	/* TX tuning config */
	if (!priv->phy_module) {
		spacemit_sw_tx_set_dlinereg(host, pdata->tx_dline_reg);
		spacemit_sw_tx_set_delaycode(host, pdata->tx_delaycode);
		pr_info("%s: set tx_delaycode: %d\n", host->name, pdata->tx_delaycode);
		spacemit_sw_tx_tuning_prepare(host);
	}

	rxtuning->select_delay_num = 0;
	memset(rxtuning->windows, 0, sizeof(rxtuning->windows));
	memset(rxtuning->select_delay, 0xFF, sizeof(rxtuning->select_delay));

	spacemit_sw_rx_tuning_prepare(host, rxtuning->rx_dline_reg);
	ret = spacemit_sw_rx_select_window(host, opcode);
	if (ret) {
		pr_warn("%s: abort tuning, err:%d\n", host->name, ret);
		return ret;
	}

	if (!spacemit_sw_rx_select_delay(host)) {
		pr_warn("%s: fail to get delaycode\n", host->name);
		return -EIO;
	}

	spacemit_sw_rx_set_delaycode(host, rxtuning->select_delay[0]);
	pr_info("%s: tuning done, use the firstly delay_code:%d\n",
		host->name, rxtuning->select_delay[0]);
	return 0;
}

#if CONFIG_IS_ENABLED(MMC_HS400_ES_SUPPORT)
static int spacemit_sdhci_phy_dll_init(struct sdhci_host *host)
{
	u32 reg;
	int i;

	/* config dll_reg1 & dll_reg2 */
	reg = sdhci_readl(host, SDHC_PHY_DLLCFG);
	reg |= (DLL_PREDLY_NUM | DLL_FULLDLY_RANGE | DLL_VREG_CTRL);
	sdhci_writel(host, reg, SDHC_PHY_DLLCFG);

	reg = sdhci_readl(host, SDHC_PHY_DLLCFG1);
	reg |= (DLL_REG1_CTRL & DLL_REG1_CTRL_MASK);
	sdhci_writel(host, reg, SDHC_PHY_DLLCFG1);

	/* dll enable */
	reg = sdhci_readl(host, SDHC_PHY_DLLCFG);
	reg |= DLL_ENABLE;
	sdhci_writel(host, reg, SDHC_PHY_DLLCFG);

	/* wait dll lock */
	i = 0;
	while (i++ < 100) {
		if (sdhci_readl(host, SDHC_PHY_DLLSTS) & DLL_LOCK_STATE)
			break;
		udelay(10);
	}
	if (i == 100) {
		pr_err("%s: phy dll lock timeout\n", host->name);
		return 1;
	}

	return 0;
}

static int spacemit_sdhci_hs400_enhanced_strobe(struct sdhci_host *host)
{
	u32 reg;

	reg = sdhci_readl(host, SDHC_MMC_CTRL_REG);
	reg |= ENHANCE_STROBE_EN;
	sdhci_writel(host, reg, SDHC_MMC_CTRL_REG);

	return spacemit_sdhci_phy_dll_init(host);
}
#endif

static int spacemit_sdhci_probe(struct udevice *dev)
{
	struct spacemit_sdhci_driver_data *drv_data =
			(struct spacemit_sdhci_driver_data *)dev_get_driver_data(dev);
	struct spacemit_sdhci_plat *plat = dev_get_plat(dev);
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct spacemit_sdhci_priv *priv = dev_get_priv(dev);
	struct sdhci_host *host = &priv->host;
	struct dm_mmc_ops *mmc_driver_ops = (struct dm_mmc_ops *)dev->driver->ops;
	struct clk clk;
	int ret = 0;

	host->mmc = &plat->mmc;
	host->mmc->priv = host;
	host->mmc->dev = dev;
	upriv->mmc = host->mmc;

	ret = clk_get_bulk(dev, &plat->clks);
	if (ret) {
		pr_err("Can't get clk: %d\n", ret);
		return ret;
	}

	ret = clk_enable_bulk(&plat->clks);
	if (ret) {
		pr_err("Failed to enable clk: %d\n", ret);
		return ret;
	}

	ret = reset_get_bulk(dev, &plat->resets);
	if (ret) {
		pr_err("Can't get reset: %d\n", ret);
		return ret;
	}

	ret = reset_deassert_bulk(&plat->resets);
	if (ret) {
		pr_err("Failed to reset: %d\n", ret);
		return ret;
	}

	ret = clk_get_by_index(dev, 0, &clk);
	if (ret) {
		pr_err("Can't get io clk: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(&clk, priv->clk_src_freq);
	if (ret) {
		pr_err("Failed to set io clk: %d\n", ret);
		return ret;
	}

	/* Set quirks */
#if defined(CONFIG_SPL_BUILD)
	host->quirks = SDHCI_QUIRK_WAIT_SEND_CMD;
#else
	host->quirks = SDHCI_QUIRK_WAIT_SEND_CMD | SDHCI_QUIRK_32BIT_DMA_ADDR;
#endif
	host->host_caps = MMC_MODE_HS | MMC_MODE_HS_52MHz;
	host->max_clk = priv->clk_src_freq;

	plat->cfg.f_max = priv->clk_src_freq;
	plat->cfg.f_min = SDHC_MIN_CLOCK;
	host->ops = drv_data->ops;

	mmc_driver_ops->wait_dat0 = spacemit_sdhci_wait_dat0;

	ret = sdhci_setup_cfg(&plat->cfg, host, priv->clk_src_freq, SDHC_MIN_CLOCK);
	if (ret)
		return ret;

	ret = sdhci_probe(dev);
	if (ret)
		return ret;

	/* emmc phy bypass if need */
	spacemit_mmc_phy_init(host);

	pr_info("%s: probe done.\n", host->name);
	return ret;
}

static int spacemit_sdhci_of_to_plat(struct udevice *dev)
{
	struct spacemit_sdhci_plat *plat = dev_get_plat(dev);
	struct spacemit_sdhci_priv *priv = dev_get_priv(dev);
	struct sdhci_host *host = &priv->host;
	int ret = 0;

	host->name = dev->name;
	host->ioaddr = (void *)devfdt_get_addr(dev);
	priv->phy_module = dev_read_u32_default(dev, "sdh-phy-module", 0);
	priv->clk_src_freq = dev_read_u32_default(dev, "clk-src-freq", SDHC_DEFAULT_MAX_CLOCK);

	plat->aib_mmc1_io_reg = dev_read_u32_default(dev, "spacemit,aib_mmc1_io_reg", 0);
	plat->apbc_asfar_reg = dev_read_u32_default(dev, "spacemit,apbc_asfar_reg", 0);
	plat->apbc_assar_reg = dev_read_u32_default(dev, "spacemit,apbc_assar_reg", 0);

	/* read rx tuning dline_reg */
	plat->rxtuning.rx_dline_reg = dev_read_u32_default(dev, "spacemit,rx_dline_reg", RX_TUNING_DLINE_REG);
	/* read rx tuning window limit */
	plat->rxtuning.window_limit = dev_read_u32_default(dev, "spacemit,rx_tuning_limit", RX_TUNING_WINDOW_THRESHOLD);

	/* tx tuning dline_reg */
	plat->tx_dline_reg = dev_read_u32_default(dev, "spacemit,tx_dline_reg", TX_TUNING_DLINE_REG);
	/* tx tuning delaycode */
	plat->tx_delaycode = dev_read_u32_default(dev, "spacemit,tx_delaycode", TX_TUNING_DELAYCODE);

	ret = mmc_of_parse(dev, &plat->cfg);

	return ret;
}

static int spacemit_sdhci_bind(struct udevice *dev)
{
	struct spacemit_sdhci_plat *plat = dev_get_plat(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

const struct sdhci_ops spacemit_sdhci_ops = {
	.set_control_reg = spacemit_sdhci_set_control_reg,
	.platform_execute_tuning = spacemit_sdhci_execute_tuning,
#if CONFIG_IS_ENABLED(MMC_HS400_ES_SUPPORT)
	.set_enhanced_strobe = spacemit_sdhci_hs400_enhanced_strobe,
#endif
};

const struct spacemit_sdhci_driver_data spacemit_sdhci_drv_data = {
	.ops = &spacemit_sdhci_ops,
};

static const struct udevice_id spacemit_sdhci_ids[] = {
	{ .compatible = "spacemit,k1-x-sdhci",
	  .data = (ulong)&spacemit_sdhci_drv_data,
	},
	{ }
};

U_BOOT_DRIVER(spacemit_sdhci_drv) = {
	.name		= "spacemit_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= spacemit_sdhci_ids,
	.of_to_plat = spacemit_sdhci_of_to_plat,
	.ops		= &sdhci_ops,
	.bind		= spacemit_sdhci_bind,
	.probe		= spacemit_sdhci_probe,
	.priv_auto = sizeof(struct spacemit_sdhci_priv),
	.plat_auto = sizeof(struct spacemit_sdhci_plat),
};
