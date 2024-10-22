#ifndef __SLEEP__H_
#define __SLEEP__H_

#define PT_EPC 0 /* offsetof(struct pt_regs, epc) */
#define PT_RA 8 /* offsetof(struct pt_regs, ra) */
#define PT_S0 64 /* offsetof(struct pt_regs, s0) */
#define PT_S1 72 /* offsetof(struct pt_regs, s1) */
#define PT_S2 144 /* offsetof(struct pt_regs, s2) */
#define PT_S3 152 /* offsetof(struct pt_regs, s3) */
#define PT_S4 160 /* offsetof(struct pt_regs, s4) */
#define PT_S5 168 /* offsetof(struct pt_regs, s5) */
#define PT_S6 176 /* offsetof(struct pt_regs, s6) */
#define PT_S7 184 /* offsetof(struct pt_regs, s7) */
#define PT_S8 192 /* offsetof(struct pt_regs, s8) */
#define PT_S9 200 /* offsetof(struct pt_regs, s9) */
#define PT_S10 208 /* offsetof(struct pt_regs, s10) */
#define PT_S11 216 /* offsetof(struct pt_regs, s11) */
#define PT_SP 16 /* offsetof(struct pt_regs, sp) */
#define PT_TP 32 /* offsetof(struct pt_regs, tp) */
#define PT_A0 80 /* offsetof(struct pt_regs, a0) */
#define PT_A1 88 /* offsetof(struct pt_regs, a1) */
#define PT_A2 96 /* offsetof(struct pt_regs, a2) */
#define PT_A3 104 /* offsetof(struct pt_regs, a3) */
#define PT_A4 112 /* offsetof(struct pt_regs, a4) */
#define PT_A5 120 /* offsetof(struct pt_regs, a5) */
#define PT_A6 128 /* offsetof(struct pt_regs, a6) */
#define PT_A7 136 /* offsetof(struct pt_regs, a7) */
#define PT_T0 40 /* offsetof(struct pt_regs, t0) */
#define PT_T1 48 /* offsetof(struct pt_regs, t1) */
#define PT_T2 56 /* offsetof(struct pt_regs, t2) */
#define PT_T3 224 /* offsetof(struct pt_regs, t3) */
#define PT_T4 232 /* offsetof(struct pt_regs, t4) */
#define PT_T5 240 /* offsetof(struct pt_regs, t5) */
#define PT_T6 248 /* offsetof(struct pt_regs, t6) */
#define PT_GP 24 /* offsetof(struct pt_regs, gp) */
#define PT_ORIG_A0 280 /* offsetof(struct pt_regs, orig_a0) */
#define PT_STATUS 256 /* offsetof(struct pt_regs, status) */
#define PT_BADADDR 264 /* offsetof(struct pt_regs, badaddr) */
#define PT_CAUSE 272 /* offsetof(struct pt_regs, cause) */
#define SUSPEND_CONTEXT_REGS 0 /* offsetof(struct suspend_context, regs) */

#endif
