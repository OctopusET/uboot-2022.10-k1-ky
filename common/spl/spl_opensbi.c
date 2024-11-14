// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Fraunhofer AISEC,
 * Lukas Auer <lukas.auer@aisec.fraunhofer.de>
 *
 * Based on common/spl/spl_atf.c
 */
#include <common.h>
#include <cpu_func.h>
#include <errno.h>
#include <hang.h>
#include <image.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <asm/cache.h>
#include <opensbi.h>
#include <linux/libfdt.h>

DECLARE_GLOBAL_DATA_PTR;

struct fw_dynamic_info opensbi_info;

static int spl_opensbi_find_uboot_node(void *blob, int *uboot_node)
{
	int fit_images_node, node;
	const char *fit_os;

	fit_images_node = fdt_path_offset(blob, "/fit-images");
	if (fit_images_node < 0)
		return -ENODEV;

	fdt_for_each_subnode(node, blob, fit_images_node) {
		fit_os = fdt_getprop(blob, node, FIT_OS_PROP, NULL);
		if (!fit_os)
			continue;

		if (genimg_get_os_id(fit_os) == IH_OS_U_BOOT) {
			*uboot_node = node;
			return 0;
		}
	}

	return -ENODEV;
}

#ifdef CONFIG_SPL_BOOTING_NON_AI_CORE_SUPPORT

void _wakeup_non_ai_core(struct spl_image_info *spl_image)
{
	unsigned int val;
	unsigned long start, end, size = 2048;

	if (readl((unsigned int *)K1X_PMU_CORE_STATUS_REGISTER) &
			(1 << K1X_NON_AI_CORE4_C2_STATUS_BIT)) {

		/* flush the cache of spl_image */
		start = (unsigned long)spl_image;
		start = round_down(start, CONFIG_RISCV_CBOM_BLOCK_SIZE);

		end = start + size;

		while (start < end) {
			cbo_flush(start);
			start += CONFIG_RISCV_CBOM_BLOCK_SIZE;
		}

		/* flush the cache of opensbi_info */
		start = (unsigned long)&opensbi_info;
		start = round_down(start, CONFIG_RISCV_CBOM_BLOCK_SIZE);

		end = start + size;

		while (start < end) {
			cbo_flush(start);
			start += CONFIG_RISCV_CBOM_BLOCK_SIZE;
		}

		/* flush the ddr traning info */
		start = (unsigned long)DDR_TRAINING_INFO_BUFF;
		size = sizeof(struct ddr_training_info_t);

		start = round_down(start, CONFIG_RISCV_CBOM_BLOCK_SIZE);

		end = start + size;

		while (start < end) {
			cbo_flush(start);
			start += CONFIG_RISCV_CBOM_BLOCK_SIZE;
		}

		/* flush the cache of dtb */
		asm volatile ("csrwi 0x7c2, 0x3");

		/* wakeup core 4 & let core0 enter wfi */
		writel((u64)spl_image, (void __iomem *)CLUSTER0_RVBADDR_LO_ADDR);

		/* wakeup core 4 */
		writel(1 << K1X_LUANCH_AI_CORE_NUMBER, (void __iomem *)CLUSTER0_CPU_RESET_REGISTER);

		/* assert core0 */
		val = readl((void __iomem *)PMU_CAP_CORE0_IDLE_CFG);
		val |= CPU_PWR_DOWN_VALUE;
		writel(val, (void __iomem *)PMU_CAP_CORE0_IDLE_CFG);

		/* core0 enter wfi */
		while (1)
			asm volatile ("wfi");
	}
}

void _non_ai_entry(struct spl_image_info *spl_image)
{
	void (*opensbi_entry)(ulong hartid, ulong dtb, ulong info);

	opensbi_info.boot_hart = /* gd->arch.boot_hart */ K1X_LUANCH_AI_CORE_NUMBER;

	/* core4 enter opensbi */
	opensbi_entry = (void (*)(ulong, ulong, ulong))spl_image->entry_point;

	opensbi_entry(/* gd->arch.boot_hart */ K1X_LUANCH_AI_CORE_NUMBER,
			(ulong)spl_image->fdt_addr,
		      (ulong)&opensbi_info);
}

#endif

void spl_invoke_opensbi(struct spl_image_info *spl_image)
{
	int ret, uboot_node;
	ulong uboot_entry;
	void (*opensbi_entry)(ulong hartid, ulong dtb, ulong info);

	if (!spl_image->fdt_addr) {
		pr_err("No device tree specified in SPL image\n");
		hang();
	}

	/* Find U-Boot image in /fit-images */
	ret = spl_opensbi_find_uboot_node(spl_image->fdt_addr, &uboot_node);
	if (ret) {
		debug("Can't find U-Boot node, %d\n", ret);
#ifdef CONFIG_SYS_LOAD_IMAGE_SEC_PARTITION
		debug("had defined another file to load, maybe the uboot node set in it\n");
#else
		hang();
#endif
	}

	/* Get U-Boot entry point */
	ret = fit_image_get_entry(spl_image->fdt_addr, uboot_node, &uboot_entry);
	if (ret)
		ret = fit_image_get_load(spl_image->fdt_addr, uboot_node, &uboot_entry);

#ifdef CONFIG_SYS_LOAD_IMAGE_SEC_PARTITION
	/*if load other image, uboot_entry maybe not true, set to TEXT_BASE directory*/
	uboot_entry = CONFIG_SYS_TEXT_BASE;
#endif
	/* Prepare opensbi_info object */
	opensbi_info.magic = FW_DYNAMIC_INFO_MAGIC_VALUE;
	opensbi_info.version = FW_DYNAMIC_INFO_VERSION;
	opensbi_info.next_addr = uboot_entry;
	opensbi_info.next_mode = FW_DYNAMIC_INFO_NEXT_MODE_S;
	opensbi_info.options = CONFIG_SPL_OPENSBI_SCRATCH_OPTIONS;
	opensbi_info.boot_hart = gd->arch.boot_hart;

	opensbi_entry = (void (*)(ulong, ulong, ulong))spl_image->entry_point;

	invalidate_icache_all();

#ifdef CONFIG_SPL_SMP
	/*
	 * Start OpenSBI on all secondary harts and wait for acknowledgment.
	 *
	 * OpenSBI first relocates itself to its link address. This is done by
	 * the main hart. To make sure no hart is still running U-Boot SPL
	 * during relocation, we wait for all secondary harts to acknowledge
	 * the call-function request before entering OpenSBI on the main hart.
	 * Otherwise, code corruption can occur if the link address ranges of
	 * U-Boot SPL and OpenSBI overlap.
	 */
	ret = smp_call_function((ulong)spl_image->entry_point,
				(ulong)spl_image->fdt_addr,
				(ulong)&opensbi_info, 1);
	if (ret)
		hang();
#endif

#ifdef CONFIG_SPL_BOOTING_NON_AI_CORE_SUPPORT
	_wakeup_non_ai_core(spl_image);
#else
	opensbi_entry(gd->arch.boot_hart, (ulong)spl_image->fdt_addr,
		      (ulong)&opensbi_info);
#endif
}
