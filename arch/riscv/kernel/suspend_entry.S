/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/sleep.h>

#define LREG			ld
#define SREG			sd

	.text
	.altmacro
	.option norelax

ENTRY(__cpu_suspend_enter)
	/* Save registers (except A0 and T0-T6) */
	SREG	ra, (SUSPEND_CONTEXT_REGS + PT_RA)(a0)
	SREG	sp, (SUSPEND_CONTEXT_REGS + PT_SP)(a0)
	SREG	gp, (SUSPEND_CONTEXT_REGS + PT_GP)(a0)
	SREG	tp, (SUSPEND_CONTEXT_REGS + PT_TP)(a0)
	SREG	s0, (SUSPEND_CONTEXT_REGS + PT_S0)(a0)
	SREG	s1, (SUSPEND_CONTEXT_REGS + PT_S1)(a0)
	SREG	a1, (SUSPEND_CONTEXT_REGS + PT_A1)(a0)
	SREG	a2, (SUSPEND_CONTEXT_REGS + PT_A2)(a0)
	SREG	a3, (SUSPEND_CONTEXT_REGS + PT_A3)(a0)
	SREG	a4, (SUSPEND_CONTEXT_REGS + PT_A4)(a0)
	SREG	a5, (SUSPEND_CONTEXT_REGS + PT_A5)(a0)
	SREG	a6, (SUSPEND_CONTEXT_REGS + PT_A6)(a0)
	SREG	a7, (SUSPEND_CONTEXT_REGS + PT_A7)(a0)
	SREG	s2, (SUSPEND_CONTEXT_REGS + PT_S2)(a0)
	SREG	s3, (SUSPEND_CONTEXT_REGS + PT_S3)(a0)
	SREG	s4, (SUSPEND_CONTEXT_REGS + PT_S4)(a0)
	SREG	s5, (SUSPEND_CONTEXT_REGS + PT_S5)(a0)
	SREG	s6, (SUSPEND_CONTEXT_REGS + PT_S6)(a0)
	SREG	s7, (SUSPEND_CONTEXT_REGS + PT_S7)(a0)
	SREG	s8, (SUSPEND_CONTEXT_REGS + PT_S8)(a0)
	SREG	s9, (SUSPEND_CONTEXT_REGS + PT_S9)(a0)
	SREG	s10, (SUSPEND_CONTEXT_REGS + PT_S10)(a0)
	SREG	s11, (SUSPEND_CONTEXT_REGS + PT_S11)(a0)

	/* Save CSRs */
	csrr	t0, CSR_SEPC
	SREG	t0, (SUSPEND_CONTEXT_REGS + PT_EPC)(a0)
	csrr	t0, CSR_SSTATUS
	SREG	t0, (SUSPEND_CONTEXT_REGS + PT_STATUS)(a0)
	csrr	t0, CSR_STVAL
	SREG	t0, (SUSPEND_CONTEXT_REGS + PT_BADADDR)(a0)
	csrr	t0, CSR_SCAUSE
	SREG	t0, (SUSPEND_CONTEXT_REGS + PT_CAUSE)(a0)

	/* Return non-zero value */
	li	a0, 1

	/* Return to C code */
	ret
END(__cpu_suspend_enter)

ENTRY(__cpu_resume_enter)
	/* Load the global pointer */
#	.option push
#	.option norelax
#		la gp, gd
#	.option pop

	/* Make A0 point to suspend context */
	add	a0, a1, zero

	/* Restore CSRs */
	LREG	t0, (SUSPEND_CONTEXT_REGS + PT_EPC)(a0)
	csrw	CSR_SEPC, t0
	LREG	t0, (SUSPEND_CONTEXT_REGS + PT_STATUS)(a0)
	csrw	CSR_SSTATUS, t0
	LREG	t0, (SUSPEND_CONTEXT_REGS + PT_BADADDR)(a0)
	csrw	CSR_STVAL, t0
	LREG	t0, (SUSPEND_CONTEXT_REGS + PT_CAUSE)(a0)
	csrw	CSR_SCAUSE, t0

	/* Restore registers (except A0 and T0-T6) */
	LREG	ra, (SUSPEND_CONTEXT_REGS + PT_RA)(a0)
	LREG	sp, (SUSPEND_CONTEXT_REGS + PT_SP)(a0)
	LREG	gp, (SUSPEND_CONTEXT_REGS + PT_GP)(a0)
	LREG	tp, (SUSPEND_CONTEXT_REGS + PT_TP)(a0)
	LREG	s0, (SUSPEND_CONTEXT_REGS + PT_S0)(a0)
	LREG	s1, (SUSPEND_CONTEXT_REGS + PT_S1)(a0)
	LREG	a1, (SUSPEND_CONTEXT_REGS + PT_A1)(a0)
	LREG	a2, (SUSPEND_CONTEXT_REGS + PT_A2)(a0)
	LREG	a3, (SUSPEND_CONTEXT_REGS + PT_A3)(a0)
	LREG	a4, (SUSPEND_CONTEXT_REGS + PT_A4)(a0)
	LREG	a5, (SUSPEND_CONTEXT_REGS + PT_A5)(a0)
	LREG	a6, (SUSPEND_CONTEXT_REGS + PT_A6)(a0)
	LREG	a7, (SUSPEND_CONTEXT_REGS + PT_A7)(a0)
	LREG	s2, (SUSPEND_CONTEXT_REGS + PT_S2)(a0)
	LREG	s3, (SUSPEND_CONTEXT_REGS + PT_S3)(a0)
	LREG	s4, (SUSPEND_CONTEXT_REGS + PT_S4)(a0)
	LREG	s5, (SUSPEND_CONTEXT_REGS + PT_S5)(a0)
	LREG	s6, (SUSPEND_CONTEXT_REGS + PT_S6)(a0)
	LREG	s7, (SUSPEND_CONTEXT_REGS + PT_S7)(a0)
	LREG	s8, (SUSPEND_CONTEXT_REGS + PT_S8)(a0)
	LREG	s9, (SUSPEND_CONTEXT_REGS + PT_S9)(a0)
	LREG	s10, (SUSPEND_CONTEXT_REGS + PT_S10)(a0)
	LREG	s11, (SUSPEND_CONTEXT_REGS + PT_S11)(a0)

	/* Return zero value */
	add	a0, zero, zero

	/* Return to C code */
	ret
END(__cpu_resume_enter)
