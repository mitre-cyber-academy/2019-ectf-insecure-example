/*
 * (C) Copyright 2016 Digilent Inc.
 *
 * Configuration for Zynq Development Board - ARTY Z7 ECTF
 * See zynq-common.h for Zynq common configs
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __CONFIG_ZYNQ_ECTF_H
#define __CONFIG_ZYNQ_ECTF_H

#define CONFIG_BOOT_RETRY_TIME 0
#define CONFIG_RESET_TO_RETRY

#define CONFIG_ZYNQ_I2C0
/*
#define CONFIG_ZYNQ_I2C1
#define CONFIG_SYS_I2C_EEPROM_ADDR_LEN	1
#define CONFIG_CMD_EEPROM
#define CONFIG_ZYNQ_GEM_EEPROM_ADDR	0x50
#define CONFIG_ZYNQ_GEM_I2C_MAC_OFFSET	0xFA
#define CONFIG_DISPLAY
#define CONFIG_I2C_EDID
*/

/* GEM MAC address offset */
#define CONFIG_ZYNQ_GEM_SPI_MAC_OFFSET	0x20

/* Define ARTY-Z PS Clock Frequency to 50MHz */
#define CONFIG_ZYNQ_PS_CLK_FREQ	50000000UL

#include <configs/zynq-common.h>
#ifdef CONFIG_BOOTCOMMAND
#undef CONFIG_BOOTCOMMAND
#endif

/*
#define CONFIG_BOOTCOMMAND \
		"if fatload mmc 0 0x3000000 image.aes && " \
		"aes_gcm 0x2fffff0 16 0x3000000; then " \
		"bootm 0x3000000; else mw 0x2fffff0 0x00000000 4; reset; fi" \
*/
/* Extra U-Boot Env settings */
#ifdef CONFIG_EXTRA_ENV_SETTINGS
#undef CONFIG_EXTRA_ENV_SETTINGS
#endif

#endif /* __CONFIG_ZYNQ_ECTF_H */
