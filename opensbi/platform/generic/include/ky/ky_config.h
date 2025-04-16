#ifndef __KY_CONFIG_H__
#define __KY_CONFIG_H__

#if defined(CONFIG_PLATFORM_KY_K1PRO)
#include "./k1pro/core_common.h"

#if defined(CONFIG_PLATFORM_KY_K1PRO_FPGA)
#include "./k1pro/k1pro_fpga.h"
#elif defined(CONFIG_PLATFORM_KY_K1PRO_QEMU)
#include "./k1pro/k1pro_qemu.h"
#elif defined(CONFIG_PLATFORM_KY_K1PRO_SIM)
#include "./k1pro/k1pro_sim.h"
#elif defined(CONFIG_PLATFORM_KY_K1PRO_VERIFY)
#include "./k1pro/k1pro_verify.h"
#endif

#endif

#if defined(CONFIG_PLATFORM_KY_X1)
#include "./x1/core_common.h"

#if defined(CONFIG_PLATFORM_KY_X1_FPGA)
#include "./x1/x1_fpga.h"
#elif defined(CONFIG_PLATFORM_KY_X1_EVB)
#include "./x1/x1_evb.h"
#endif

#endif

#endif /* __KY_CONFIG_H__ */
