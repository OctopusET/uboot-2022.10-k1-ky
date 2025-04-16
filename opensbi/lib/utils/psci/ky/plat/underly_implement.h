#ifndef __UNDERLY_IMPLEMENT__H__
#define __UNDERLY_IMPLEMENT__H__

#include <sbi/sbi_types.h>

void ky_top_on(u_register_t mpidr);
void ky_top_off(u_register_t mpidr);
void ky_cluster_on(u_register_t mpidr);
void ky_cluster_off(u_register_t mpidr);
void ky_wakeup_cpu(u_register_t mpidr);
void ky_assert_cpu(u_register_t mpidr);
int ky_core_enter_c2(u_register_t mpidr);
int ky_cluster_enter_m2(u_register_t mpidr);
void ky_wait_core_enter_c2(u_register_t mpidr);
void ky_deassert_cpu(void);

#endif
