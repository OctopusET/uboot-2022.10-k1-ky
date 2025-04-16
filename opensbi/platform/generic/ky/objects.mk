#
# SPDX-License-Identifier: BSD-2-Clause
#

carray-platform_override_modules-$(CONFIG_PLATFORM_KY_K1PRO)$(CONFIG_PLATFORM_KY_X1) += ky_k1
platform-objs-$(CONFIG_PLATFORM_KY_K1PRO)$(CONFIG_PLATFORM_KY_X1) += ky/ky_k1.o
firmware-its-$(CONFIG_PLATFORM_KY_K1PRO)$(CONFIG_PLATFORM_KY_X1) += ky/fw_dynamic.its
