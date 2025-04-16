// SPDX-License-Identifier: GPL-2.0
/*
 * phy-ky-x1-combphy.c - Ky x1 combo PHY for USB3 and PCIE
 *
 * Copyright (c) 2023 Ky Co., Ltd.
 *
 */
#include <common.h>
#include <reset.h>
#include <clk.h>
#include <dm.h>
#include <generic-phy.h>
#include <dt-bindings/phy/phy.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>


#define KY_COMBPHY_WAIT_TIMEOUT 1000
#define KY_COMBPHY_MODE_SEL BIT(3)

// Registers for USB3 PHY
#define KY_COMBPHY_USB_REG1 0x68
#define KY_COMBPHY_USB_REG1_VAL 0x0
#define KY_COMBPHY_USB_REG2 (0x12 << 2)
#define KY_COMBPHY_USB_REG2_VAL 0x603a2276
#define KY_COMBPHY_USB_REG3 (0x02 << 2)
#define KY_COMBPHY_USB_REG3_VAL 0x97c
#define KY_COMBPHY_USB_REG4 (0x06 << 2)
#define KY_COMBPHY_USB_REG4_VAL 0x0
#define KY_COMBPHY_USB_PLL_REG 0x8
#define KY_COMBPHY_USB_PLL_MASK 0x1
#define KY_COMBPHY_USB_PLL_VAL 0x1

struct ky_combphy_priv {
	struct udevice *dev;
	struct reset_ctl_bulk phy_rst;
	void __iomem *phy_sel;
	void __iomem *puphy_base;
	struct phy *phy;
	u8 type;
};

static inline void ky_reg_updatel(void __iomem *reg, u32 offset, u32 mask, u32 val)
{
	u32 tmp;
	tmp = readl(reg + offset);
	tmp = (tmp & ~(mask)) | val;
	writel(tmp, reg + offset);
}

static int ky_combphy_wait_ready(struct ky_combphy_priv *priv,
					   u32 offset, u32 mask, u32 val)
{
	int timeout = KY_COMBPHY_WAIT_TIMEOUT;
	while (((readl(priv->puphy_base + offset) & mask) != val) && --timeout)
		;
	if (!timeout) {
		return -ETIMEDOUT;
	}
	dev_dbg(priv->dev, "phy init timeout remain: %d", timeout);
	return 0;
}

static int ky_combphy_set_mode(struct ky_combphy_priv *priv)
{
	u8 mode = priv->type;
	if (mode == PHY_TYPE_USB3) {
		ky_reg_updatel(priv->phy_sel, 0, 0,
					 KY_COMBPHY_MODE_SEL);
	} else {
		dev_err(priv->dev, "PHY type %x not supported\n", mode);
		return -EINVAL;
	}
	return 0;
}

static int ky_combphy_init_usb(struct ky_combphy_priv *priv)
{
	int ret;
	void __iomem *base = priv->puphy_base;
	dev_info(priv->dev, "USB3 PHY init.\n");

	writel(KY_COMBPHY_USB_REG1_VAL, base + KY_COMBPHY_USB_REG1);
	writel(KY_COMBPHY_USB_REG2_VAL, base + KY_COMBPHY_USB_REG2);
	writel(KY_COMBPHY_USB_REG3_VAL, base + KY_COMBPHY_USB_REG3);
	writel(KY_COMBPHY_USB_REG4_VAL, base + KY_COMBPHY_USB_REG4);

	ret = ky_combphy_wait_ready(priv, KY_COMBPHY_USB_PLL_REG,
					  KY_COMBPHY_USB_PLL_MASK,
					  KY_COMBPHY_USB_PLL_VAL);

	if (ret)
		dev_err(priv->dev, "USB3 PHY init timeout!\n");

	return ret;
}


static int ky_combphy_init(struct phy *phy)
{
	struct ky_combphy_priv *priv = dev_get_priv(phy->dev);
	int ret;

	ret = ky_combphy_set_mode(priv);

	if (ret) {
		dev_err(priv->dev, "failed to set mode for PHY type %x\n",
			priv->type);
		goto out;
	}

	ret = reset_deassert_bulk(&priv->phy_rst);
	if (ret) {
		dev_err(priv->dev, "failed to deassert rst\n");
		goto out;
	}

	switch (priv->type) {
	case PHY_TYPE_USB3:
		ret = ky_combphy_init_usb(priv);
		break;
	default:
		dev_err(priv->dev, "PHY type %x not supported\n", priv->type);
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto err_rst;

	return 0;

err_rst:
	reset_assert_bulk(&priv->phy_rst);
out:
	return ret;
}

static int ky_combphy_exit(struct phy *phy)
{
	struct ky_combphy_priv *priv =  dev_get_priv(phy->dev);

	reset_assert_bulk(&priv->phy_rst);

	return 0;
}

static int ky_combphy_xlate(struct phy *phy, struct ofnode_phandle_args *args)
{
	struct ky_combphy_priv *priv =  dev_get_priv(phy->dev);

	if (args->args_count != 1) {
		dev_err(phy->dev, "invalid number of arguments\n");
		return -EINVAL;
	}

	if (priv->type != PHY_NONE && priv->type != args->args[0])
		dev_warn(phy->dev, "PHY type %d is selected to override %d\n",
			 args->args[0], priv->type);

	priv->type = args->args[0];

	return 0;
}

static int ky_combphy_probe(struct udevice *dev)
{
	struct ky_combphy_priv *priv = dev_get_priv(dev);
	int ret;

	if (!priv)
		return -ENOMEM;

	priv->puphy_base = (void*)dev_read_addr_name(dev, "puphy");
	if (IS_ERR(priv->puphy_base)) {
		ret = PTR_ERR(priv->puphy_base);
		return ret;
	}

	priv->phy_sel = (void*)dev_read_addr_name(dev, "phy_sel");
	if (IS_ERR(priv->phy_sel)) {
		ret = PTR_ERR(priv->phy_sel);
		return ret;
	}

	priv->dev = dev;
	priv->type = PHY_NONE;

	ret = reset_get_bulk(dev, &priv->phy_rst);
	if (ret && ret != -ENOENT) {
		dev_err(dev, "Failed to get resets (err=%d)\n", ret);
		return ret;
	}

	ret = reset_assert_bulk(&priv->phy_rst);
	if (ret) {
		dev_err(dev, "failed to reset phy\n");
		return ret;
	}

	return 0;
}

static const struct udevice_id ky_combphy_ids[] = {
	{.compatible = "ky,x1-combphy",},
	{ /* sentinel */ }
};

static struct phy_ops ky_combphy_ops = {
	.init = ky_combphy_init,
	.exit = ky_combphy_exit,
	.of_xlate = ky_combphy_xlate,
};

U_BOOT_DRIVER(x1_combphy) = {
	.name	= "x1_combphy",
	.id	= UCLASS_PHY,
	.of_match = ky_combphy_ids,
	.ops = &ky_combphy_ops,
	.probe = ky_combphy_probe,
	.priv_auto	= sizeof(struct ky_combphy_priv),
};