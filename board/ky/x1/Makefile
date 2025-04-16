# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (c) 2022-2023 Ky, Inc

obj-y	+= x1.o
obj-$(CONFIG_SPL_BUILD)	+= spl.o x1-i2c-eeprom.o x1-tlvinfo.o
obj-$(CONFIG_SPLASH_SOURCE) +=splash.o
obj-$(CONFIG_ENV_IS_IN_NFS) +=nfs_env.o
