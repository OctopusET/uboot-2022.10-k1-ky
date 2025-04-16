// SPDX-License-Identifier: GPL-2.0
/*
 * ky-hubpwr-regulator.c -
 * 		Regulator Wrapper for Ky x1 onboard usb hub
 *
 * Copyright (c) 2023 Ky Co., Ltd.
 *
 */
#include <common.h>
#include <clk.h>
#include <dm.h>
#include <generic-phy.h>
#include <dt-bindings/phy/phy.h>
#include <dm/device_compat.h>
#include <dm/lists.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <asm/gpio.h>
#include <power/regulator.h>

#define GPIOD_OUT_HIGH (GPIOD_IS_OUT | GPIOD_PULL_UP)
#define GPIOD_OUT_LOW (GPIOD_IS_OUT | GPIOD_PULL_DOWN)
#define GPIO_MAX_COUNT 4

struct ky_hub_regulator_plat {
	struct udevice *dev;
	bool is_on;
	struct gpio_desc vbus_gpios[GPIO_MAX_COUNT];
	struct gpio_desc hub_gpios[GPIO_MAX_COUNT];
	u8 vbus_gpio_cnt;
	u8 hub_gpio_cnt;
	bool hub_gpio_active_low;
	bool vbus_gpio_active_low;
	struct udevice *vbus_regulator;
	struct udevice *hub_regulator;
	u32 hub_inter_delay_ms;
	u32 vbus_delay_ms;
	u32 vbus_inter_delay_ms;
};

static int ky_hub_enable(struct ky_hub_regulator_plat *ky, bool on)
{
	unsigned i;
	int ret = 0;
	int active_val = ky->hub_gpio_active_low ? 0 : 1;

	if (!ky->hub_gpio_cnt && !ky->hub_regulator)
		return 0;
	dev_dbg(ky->dev, "do hub enable %s\n", on ? "on" : "off");
	if (ky->hub_regulator) {
		ret = regulator_set_enable(ky->hub_regulator, on);
		if (ret)
			return ret;
	}
	if (on) {
		for (i = 0; i < ky->hub_gpio_cnt; i++) {
			ret = dm_gpio_set_value(&ky->hub_gpios[i],
					active_val);
			if (ret)
				return ret;
			if (ky->hub_inter_delay_ms) {
				mdelay(ky->hub_inter_delay_ms);
			}
		}
	} else {
		for (i = ky->hub_gpio_cnt; i > 0; --i) {
			ret = dm_gpio_set_value(&ky->hub_gpios[i - 1],
					!active_val);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int ky_hub_vbus_enable(struct ky_hub_regulator_plat *ky,
					 bool on)
{
	unsigned i;
	int ret = 0;
	int active_val = ky->vbus_gpio_active_low ? 0 : 1;

	if (!ky->vbus_gpio_cnt && !ky->vbus_regulator)
		return 0;
	dev_dbg(ky->dev, "do hub vbus on %s\n", on ? "on" : "off");
	if (ky->vbus_regulator) {
		regulator_set_enable(ky->vbus_regulator, on);
		if (ret)
			return ret;
	}
	if (on) {
		for (i = 0; i < ky->vbus_gpio_cnt; i++) {
			ret = dm_gpio_set_value(&ky->vbus_gpios[i],
					active_val);
			if (ret)
				return ret;
			if (ky->vbus_inter_delay_ms) {
				mdelay(ky->vbus_inter_delay_ms);
			}
		}
	} else {
		for (i = ky->vbus_gpio_cnt; i > 0; --i) {
			ret = dm_gpio_set_value(&ky->vbus_gpios[i - 1],
					!active_val);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int ky_hub_configure(struct ky_hub_regulator_plat *ky, bool on)
{
	int ret = 0;
	dev_dbg(ky->dev, "do hub configure %s\n", on ? "on" : "off");
	if (on) {
		ret = ky_hub_enable(ky, true);
		if (ret)
			return ret;
		if (ky->vbus_delay_ms && ky->vbus_gpio_cnt) {
			mdelay(ky->vbus_delay_ms);
		}
		ret = ky_hub_vbus_enable(ky, true);
		if (ret)
			return ret;
	} else {
		ret = ky_hub_vbus_enable(ky, false);
		if (ret)
			return ret;
		ret = ky_hub_enable(ky, false);
		if (ret)
			return ret;
	}
	ky->is_on = on;
	return 0;
}

static int ky_hub_regulator_of_to_plat(struct udevice *dev)
{
	struct dm_regulator_uclass_plat *uc_pdata;
	struct ky_hub_regulator_plat *ky;
	int ret;

	ky = dev_get_plat(dev);
	uc_pdata = dev_get_uclass_plat(dev);
	if (!uc_pdata)
		return -ENXIO;

	/* Set type to fixed to support boot-on/off */
	uc_pdata->type = REGULATOR_TYPE_FIXED;

	ky->hub_inter_delay_ms = dev_read_u32_default(dev, "hub_inter_delay_ms", 0);
	ky->vbus_inter_delay_ms = dev_read_u32_default(dev, "vbus_inter_delay_ms", 0);
	ky->vbus_delay_ms = dev_read_u32_default(dev, "vbus_delay_ms", 10);

 	ky->hub_gpio_active_low = dev_read_bool(dev, "hub_gpio_active_low");
	ky->vbus_gpio_active_low = dev_read_bool(dev, "vbus_gpio_active_low");

	ret = gpio_request_list_by_name(dev, "hub-gpios", ky->hub_gpios,
			 ARRAY_SIZE(ky->hub_gpios),
			 ky->hub_gpio_active_low ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW);

	if (ret < 0) {
		dev_err(dev, "failed to retrieve hub-gpios from dts: %d\n", ret);
		return ret;
	}
	ky->hub_gpio_cnt = ret;
	ret = device_get_supply_regulator(dev, "hub-supply", &ky->hub_regulator);
	if (ret < 0 && ret != -ENOENT) {
		dev_err(dev, "failed to retrieve hub-supply from dts: %d\n", ret);
		return ret;
	}
	dev_dbg(dev, "got %d hub-supply, hubs: %p\n", ret, ky->hub_regulator);

	ret = gpio_request_list_by_name(dev, "vbus-gpios", ky->vbus_gpios,
			ARRAY_SIZE(ky->vbus_gpios),
			ky->vbus_gpio_active_low ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW);
	if (ret < 0) {
		dev_err(dev, "failed to retrieve hub-gpios from dts: %d\n", ret);
		return ret;
	}
	ky->vbus_gpio_cnt = ret;
	ret = device_get_supply_regulator(dev, "vbus-supply", &ky->vbus_regulator);
	if (ret < 0 && ret != -ENOENT) {
		dev_err(dev, "failed to retrieve vbus-supply from dts: %d\n", ret);
		return ret;
	}
	dev_dbg(dev, "got vbus-supply ret %d %p\n", ret, ky->vbus_regulator);
	dev_dbg(dev, "found hub gpios: %d vbus gpios: %d\n", ky->hub_gpio_cnt,
		ky->vbus_gpio_cnt);

	return 0;
}

static int ky_hub_regulator_get_enable(struct udevice *dev)
{
	struct ky_hub_regulator_plat *ky = dev_get_plat(dev);
	return ky->is_on;
}

static int ky_hub_regulator_set_enable(struct udevice *dev, bool enable)
{
	struct ky_hub_regulator_plat *ky = dev_get_plat(dev);
	return ky_hub_configure(ky, enable);
}

static const struct dm_regulator_ops ky_hub_regulator_ops = {
	.get_enable	= ky_hub_regulator_get_enable,
	.set_enable	= ky_hub_regulator_set_enable,
};

static const struct udevice_id ky_hub_regulator_ids[] = {
	{.compatible = "ky,usb-hub",},
	{ },
};

U_BOOT_DRIVER(ky_hub_regulator) = {
	.name = "gpio regulator",
	.id = UCLASS_REGULATOR,
	.ops = &ky_hub_regulator_ops,
	.of_match = ky_hub_regulator_ids,
	.of_to_plat = ky_hub_regulator_of_to_plat,
	.plat_auto	= sizeof(struct ky_hub_regulator_plat),
};
