#include <common.h>
#include <fdt_support.h>
#include <log.h>
#include <irq.h>
#include <dm.h>
#include <dm/lists.h>
#include <power-domain.h>
#include <power/regulator.h>
#include <asm/sbi.h>
#include <power/battery.h>
#include <asm/csr.h>
#include <asm/io.h>
#include <led.h>
#include <asm-generic/gpio.h>
#include <power/charger.h>
#include <power/spacemit/spacemit_pmic.h>

#define SBI_HSM_SUSP_TOP_BIT 0x92000000

#define PLIC_PMIC_PRIO_REG	0xe0000100
#define PLIC_PMIC_THRESH_REG	0xe0201000
#define PLIC_PMIC_EN_REG	0xe0002088
#define PLIC_PMIC_PENDING_REG	0xe0001008

#define RTC_CLK_SEL_EXTERNAL_OSC	0x8
#define RTC_EN				0x4
#define RTC_TICK_TYPE_1S		0x0
#define RTC_TICK_TYPE_1MIN		0x1
#define RTC_TICK_EN			0x40
#define RTC_CRYSTAL_EN			0x1
#define RTC_OUT_32K_EN			0x2
#define RTC_TICK_IRQ			0x20

#define WAKEUP_SOURCE_POWER_KEY_EVENT	0x0
#define WAKEUP_SOURCE_POWER_KEY_INTER	0x1
#define WAKEUP_SOURCE_RTC_WAKEUP_CTRL	0x2
#define WAKEUP_SOURCE_RTC_WAKEUP_EVENT	0x3
#define WAKEUP_SOURCE_RTC_WAKEUP_IRQ	0x4
#define SYS_SHUTDOWN			0x5
#define SYS_REBOOT_FLAG			0x6

#define SYS_SHUTDOWN_BIT		0x4

#define BATTERY_RED_LIGHT_INDEX		0x0
#define BATTERY_GREEN_LIGHT_INDEX	0x1
#define DISABLE_BATTERY_LED		0x1

#define MAX_GPIO_COUNT		3
#define MAX_REGULATOR_COUNT	7
#define MAX_CHARGER_COUNT	2

#define CHARGER_LIGHT_FLASHES_CNT	10

#define VTHRESHOLD0		3000000
#define VTHRESHOLD1		3500000
#define VTHRESHOLD2		4350000

struct pt_regs {
	unsigned long long sepc;
	unsigned long long ra;
	unsigned long long sp;
	unsigned long long gp;
	unsigned long long tp;
	unsigned long long t0;
	unsigned long long t1;
	unsigned long long t2;
	unsigned long long s0;
	unsigned long long s1;
	unsigned long long a0;
	unsigned long long a1;
	unsigned long long a2;
	unsigned long long a3;
	unsigned long long a4;
	unsigned long long a5;
	unsigned long long a6;
	unsigned long long a7;
	unsigned long long s2;
	unsigned long long s3;
	unsigned long long s4;
	unsigned long long s5;
	unsigned long long s6;
	unsigned long long s7;
	unsigned long long s8;
	unsigned long long s9;
	unsigned long long s10;
	unsigned long long s11;
	unsigned long long t3;
	unsigned long long t4;
	unsigned long long t5;
	unsigned long long t6;
	/* Supervisor */
	unsigned long long sstatus;
	unsigned long long sbadaddr;
	unsigned long long scause;
};

struct shutdown_charge {
	/* electricity meter */
	struct udevice *ele_meter;
	/* wakeup-source set
	 * 0: wk_event
	 * 1: wk_int
	 * 2: rtc_tick_ctrl
	 * 3: rtc_tick_event
	 * 4: rtc_tick_irq
	 * */
	struct udevice *wkup_set[MAX_REGULATOR_COUNT];
	const char **wkup_name;
	/* led charging indicator */
	struct gpio_desc led_indicators[MAX_GPIO_COUNT];
	int gpio_cnt;
	/* charget */
	struct udevice *charger[MAX_CHARGER_COUNT];
	const char **charger_name;
	int charger_cnt;
	/* power domain */
	struct power_domain pm_domain;
};

struct suspend_context {
	/* Saved and restored by low-level functions */
	struct pt_regs regs;
	/* Saved and restored by high-level functions */
	unsigned long scratch;
	unsigned long tvec;
	unsigned long ie;
} __aligned(64);

extern int __cpu_suspend_enter(void *context);
extern int __cpu_resume_enter(void *context);

static struct suspend_context context = { 0 };

extern void flush_dcache_range(unsigned long start, unsigned long end);

static int sbi_suspend_finisher(unsigned long suspend_type,
                                unsigned long resume_addr,
                                unsigned long opaque)
{
	struct sbiret ret;

	flush_dcache_range((unsigned long)&context, (unsigned long)(&context) + sizeof(struct suspend_context));

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_SUSPEND,
			suspend_type, resume_addr, opaque, 0, 0, 0);

        return (ret.error) ? ret.error : 0;
}

static void suspend_save_csrs(struct suspend_context *context)
{
	context->scratch = csr_read(CSR_SSCRATCH);
	context->tvec = csr_read(CSR_STVEC);
	context->ie = csr_read(CSR_SIE);
}

static void suspend_restore_csrs(struct suspend_context *context)
{
	csr_write(CSR_SSCRATCH, context->scratch);
	csr_write(CSR_STVEC, context->tvec);
	csr_write(CSR_SIE, context->ie);
}

int cpu_suspend(unsigned long arg,
                int (*finish)(unsigned long arg,
                              unsigned long entry,
                              unsigned long context))
{
        int rc = 0;

        /* Finisher should be non-NULL */
        if (!finish)
                return -EINVAL;

        /* Save additional CSRs*/
        suspend_save_csrs(&context);

        /* Save context on stack */
        if (__cpu_suspend_enter(&context)) {
                /* Call the finisher */
                rc = finish(arg, (unsigned long)__cpu_resume_enter,
                            (ulong)&context);

                /*
                 * Should never reach here, unless the suspend finisher
                 * fails. Successful cpu_suspend() should return from
                 * __cpu_resume_entry()
                 */
                if (!rc)
                        rc = -EOPNOTSUPP;
        }

        /* Restore additional CSRs */
        suspend_restore_csrs(&context);

        return rc;
}

static int shutdown_charge_probe(struct udevice *dev)
{
	int ret, status;
	unsigned int pwr_key_status = 0, reboot_flag;
	unsigned long long plugin_count = 0, plugout_count = 0, plugin_and_pwr_key_flag = 0;
	unsigned int uV, prio, thresh;
	struct shutdown_charge *priv = dev_get_priv(dev);

	ret = uclass_get_device_by_phandle(UCLASS_BATTERY, dev,
				"electricity-meter", &priv->ele_meter);
	if (ret) {
		printf("%s:%d, get electrycity meter failed\n", __func__, __LINE__);
		return ret;
	}

	ret = power_domain_get(dev, &priv->pm_domain);
	if (ret) {
		printf("get pm domain failed\n");
		return ret;
	}

	ret = dev_read_string_list(dev, "wk-name", &priv->wkup_name);
	if (ret < 0) {
		printf("get wk source name failed\n");
		return ret;
	}

	for (int i = 0; i < dev_read_string_count(dev, "wk-name"); ++i) {
		ret = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev, priv->wkup_name[i],
				&priv->wkup_set[i]);
		if (ret) {
			printf("get wk source: %s faild\n", priv->wkup_name[i]);
			return ret;
		}
	}

	priv->charger_cnt = dev_read_string_count(dev, "charger-name");
	if (priv->charger_cnt < 0) {
		printf("get charger count failed\n");
		return ret;
	}

	ret = dev_read_string_list(dev, "charger-name", &priv->charger_name);
	if (ret < 0) {
		printf("get charger name failed\n");
		return ret;
	}

	for (int i = 0; i < priv->charger_cnt; ++i) {
		ret = uclass_get_device_by_phandle(UCLASS_CHARGER, dev, priv->charger_name[i],
				&priv->charger[i]);
		if (ret) {
			printf("get charger: %s faild\n", priv->charger_name[i]);
			return ret;
		}
	}

	priv->gpio_cnt = gpio_request_list_by_name(dev, "charge-light", priv->led_indicators,
				MAX_GPIO_COUNT,
				GPIOD_IS_OUT);
	if (priv->gpio_cnt < 0) {
		printf("get charge-light failed\n");
		return priv->gpio_cnt;
	}

	/* disable the battery led */
	for (int i = 0; i < priv->gpio_cnt; ++i) {
		dm_gpio_set_value(&priv->led_indicators[i], DISABLE_BATTERY_LED);
	}

	pwr_key_status = regulator_get_value(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_EVENT]);

	ret = battery_get_voltage(priv->ele_meter, &uV);
	if (ret) {
		printf("%s:%d, get battery volatge failed\n", __func__, __LINE__);
		return ret;
	}

	status = 0;
	for (int i = 0; i < priv->charger_cnt; ++i) {
		status |= charger_get_status(priv->charger[i]);
	}

	/* reboot ??? */
	reboot_flag = regulator_get_value(priv->wkup_set[SYS_REBOOT_FLAG]);
	/* clear the reboot flag */
	reboot_flag &= ~(1 << SYS_REBOOT_FLAG_BIT);
	regulator_set_value_force(priv->wkup_set[SYS_REBOOT_FLAG], reboot_flag);

	if ((reboot_flag & SYS_REBOOT_FLAG_BIT) && (uV >= VTHRESHOLD1)) {
		goto out;
	}

	/* power-up system */
	if ((pwr_key_status & PWRKEY_RISING_EVENT) && !status && (uV >= VTHRESHOLD1)) {
		goto out;
	}

	/* clear the power key pending */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_EVENT],
					PWRKEY_RISING_EVENT |
					PWRKEY_FAILING_EVENT |
					PWRKEY_LONG_PRESS_EVENT |
					PWRKEY_SHORT_PRESS_EVENT);

	/* enable the rtc base function */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL],
					RTC_CLK_SEL_EXTERNAL_OSC |
					RTC_EN |
					RTC_OUT_32K_EN |
					RTC_CRYSTAL_EN);
	/* clear the rtc tick event */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_EVENT], 0xff);

	/* set the priority */
	prio = readl((void __iomem *)PLIC_PMIC_PRIO_REG);
	writel(8, (void __iomem *)PLIC_PMIC_PRIO_REG);
	/* set the priority */
	thresh = readl((void __iomem *)PLIC_PMIC_THRESH_REG);
	writel(7, (void __iomem *)PLIC_PMIC_THRESH_REG);
	/* enable the IE: the interrupt number of pmic is 64 */
	writel(1, (void __iomem *)PLIC_PMIC_EN_REG);
	/* clear pending first */
	writel(0, (void __iomem *)PLIC_PMIC_PENDING_REG);

	while (1) {
		ret = battery_get_voltage(priv->ele_meter, &uV);
		if (ret) {
			printf("%s:%d, get battery volatge failed\n", __func__, __LINE__);
			return ret;
		}

		status = 0;
		for (int i = 0; i < priv->charger_cnt; ++i) {
			status |= charger_get_status(priv->charger[i]);
		}

		/* dp is not plugged */
		if (!status) {

			++plugout_count;
			plugin_count = 0;
			plugin_and_pwr_key_flag = 0;

			/* let the system power-down */
			if (plugout_count == CHARGER_LIGHT_FLASHES_CNT) {
				regulator_set_value_force(priv->wkup_set[SYS_SHUTDOWN], SYS_SHUTDOWN);
			}

			if (uV >= VTHRESHOLD2) {
				dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], DISABLE_BATTERY_LED);
				dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], !DISABLE_BATTERY_LED);
			} else {
				dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], !!(plugout_count % 2));
				dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], DISABLE_BATTERY_LED);
			}

			/* enable rtc tick modeule & 1s  */
			ret = regulator_get_value(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL]);
			ret |= (RTC_TICK_EN | RTC_EN);
			regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL], ret);

			/* enable rtc tick irq */
			regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_IRQ], RTC_TICK_IRQ);

			if (uV >= VTHRESHOLD0 && uV < VTHRESHOLD1) {
				/* the screen lights up to indicate low battery level */
			}
	
		} else {
			/* the dp is plugged, we are charging now, so we can let the system enter low power mode */
			plugout_count = 0;
			++plugin_count;

			/* enable rtc tick modeule & 1s  */
			ret = regulator_get_value(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL]);
			ret |= (RTC_TICK_EN | RTC_EN);
			regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL], ret);

			/* enable rtc tick irq */
			regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_IRQ], RTC_TICK_IRQ);

			if (uV < VTHRESHOLD2) {
				/* have the power-key event */
				if (pwr_key_status & PWRKEY_FAILING_EVENT || plugin_and_pwr_key_flag == 1) {

					/* bring up the system */
					if (uV >= VTHRESHOLD1) {
						dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], !DISABLE_BATTERY_LED);
						dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], DISABLE_BATTERY_LED);
						plugin_and_pwr_key_flag = 0;
						goto out;
					}

					if (plugin_count < CHARGER_LIGHT_FLASHES_CNT) {

						plugin_and_pwr_key_flag = 1;
						dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], !!(plugin_count % 2));
						dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], DISABLE_BATTERY_LED);

						if (uV >= VTHRESHOLD0) {

							/* screen displays chargin ircron and battery level */
						}

					} else {
						plugin_and_pwr_key_flag = 0;
						dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], !DISABLE_BATTERY_LED);
						dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], DISABLE_BATTERY_LED);
					}
				} else {

					plugin_and_pwr_key_flag = 0;

					/* the led is alway on */
					dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], !DISABLE_BATTERY_LED);
					dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], DISABLE_BATTERY_LED);

					if (uV >= VTHRESHOLD0) {
						if (plugin_count < CHARGER_LIGHT_FLASHES_CNT) {

							/* screen displays chargin ircron and battery level */

						}
					}
				}
			} else {
				/* the led is alway on */
				dm_gpio_set_value(&priv->led_indicators[BATTERY_RED_LIGHT_INDEX], DISABLE_BATTERY_LED);
				dm_gpio_set_value(&priv->led_indicators[BATTERY_GREEN_LIGHT_INDEX], !DISABLE_BATTERY_LED);

				/* bringup the system */
				if (pwr_key_status & PWRKEY_FAILING_EVENT)
					goto out;
			}
		}

		/* enable the power-key */
		regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_INTER], PWRKEY_FAILING_EVENT);

		power_domain_on(&priv->pm_domain);

		cpu_suspend(SBI_HSM_SUSP_TOP_BIT, sbi_suspend_finisher);

		power_domain_off(&priv->pm_domain);

		/* if we have the power-key event, so we reset the plugin counter */
		pwr_key_status = regulator_get_value(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_EVENT]);
		if (pwr_key_status & PWRKEY_FAILING_EVENT) {
			plugin_count = 0;
			plugin_and_pwr_key_flag = 0;
			plugout_count = 0;
			printf("%s:%d, pwr_key_status:%x\n", __func__, __LINE__, pwr_key_status);
		}

		/* clean the event pending */
		regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_EVENT],
						PWRKEY_RISING_EVENT |
						PWRKEY_FAILING_EVENT |
						PWRKEY_LONG_PRESS_EVENT |
						PWRKEY_SHORT_PRESS_EVENT);
		/* disable the irq of power-key */
		regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_INTER], 0);

		/* disable rtc tick first */
		ret = regulator_get_value(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL]);
		ret &= ~(RTC_TICK_EN | RTC_EN);
		regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL], ret);

		/* clear the rtc tick event */
		regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_EVENT], 0xff);

		/* disable rtc tick irq */
		regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_IRQ], 0);

		/* clear pending */
		writel(0, (void __iomem *)PLIC_PMIC_PENDING_REG);
	}

out:
	/* disable PLIC */
	writel(0, (void __iomem *)PLIC_PMIC_PENDING_REG);
	writel(thresh, (void __iomem *)PLIC_PMIC_THRESH_REG);
	writel(prio, (void __iomem *)PLIC_PMIC_PRIO_REG);
	writel(0, (void __iomem *)PLIC_PMIC_EN_REG);

	/* disable the event of power-key & clean the event pending */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_INTER], 0);

	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_POWER_KEY_EVENT],
					PWRKEY_RISING_EVENT |
					PWRKEY_FAILING_EVENT |
					PWRKEY_LONG_PRESS_EVENT |
					PWRKEY_SHORT_PRESS_EVENT);

	/* disable rtc tick first */
	ret = regulator_get_value(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL]);
	ret &= ~(RTC_TICK_EN | RTC_EN);
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL], ret);
	/* clear the rtc tick event */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_EVENT], 0xff);
	/* disable rtc module */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_CTRL], 0);
	/* disable rtc tick irq */
	regulator_set_value_force(priv->wkup_set[WAKEUP_SOURCE_RTC_WAKEUP_IRQ], 0);

	return 0;
}

static const struct udevice_id shutdown_charging_ids[] = {
	{ .compatible = "k1,shutdown-charging" },
	{}
};

U_BOOT_DRIVER(shutdown_charge) = {
        .name = "shutdown-charge",
        .of_match = shutdown_charging_ids,
        .id = UCLASS_MISC,
        .probe = shutdown_charge_probe,
        .priv_auto = sizeof(struct shutdown_charge),
};
