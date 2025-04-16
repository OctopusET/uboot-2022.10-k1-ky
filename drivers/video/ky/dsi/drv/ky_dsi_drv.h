// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Ky Co., Ltd.
 *
 */

#ifndef _KY_DSI_H_
#define _KY_DSI_H_

#include <string.h>
#include "../include/ky_dsi_common.h"
#include "ky_dphy.h"

enum ky_dsi_event_id {
	KY_DSI_EVENT_ERROR,
	KY_DSI_EVENT_MAX,
};

enum ky_dsi_status {
	DSI_STATUS_UNINIT = 0,
	DSI_STATUS_OPENED = 1,
	DSI_STATUS_INIT = 2,
	DSI_STATUS_MAX
};

struct ky_dsi_device;

struct ky_dsi_driver_ctx {
	int (*dsi_open)(struct ky_dsi_device* device_ctx, struct ky_mipi_info *mipi_info, bool ready);
	int (*dsi_close)(struct ky_dsi_device* device_ctx);
	int (*dsi_write_cmds)(struct ky_dsi_device* device_ctx, struct ky_dsi_cmd_desc *cmds, int count);
	int (*dsi_read_cmds)(struct ky_dsi_device* device_ctx, struct ky_dsi_rx_buf *dbuf,
								struct ky_dsi_cmd_desc *cmds, int count);
	int (*dsi_ready_for_datatx)(struct ky_dsi_device* device_ctx, struct ky_mipi_info *mipi_info);
	int (*dsi_close_datatx)(struct ky_dsi_device* device_ctx);
};

struct ky_dsi_advanced_setting {
	uint32_t lpm_frame_en; /*return to LP mode every frame*/
	uint32_t last_line_turn;
	uint32_t hex_slot_en;
	uint32_t hsa_pkt_en;
	uint32_t hse_pkt_en;
	uint32_t hbp_pkt_en; /*bit:18*/
	uint32_t hfp_pkt_en; /*bit:20*/
	uint32_t hex_pkt_en;
	uint32_t hlp_pkt_en; /*bit:22*/
	uint32_t auto_dly_dis;
	uint32_t timing_check_dis;
	uint32_t hact_wc_en;
	uint32_t auto_wc_dis;
	uint32_t vsync_rst_en;
};

struct ky_dsi_device {
	uint32_t id; /*dsi id*/

	unsigned long esc_clk_rate, bit_clk_rate;

	struct ky_dsi_driver_ctx *driver_ctx;

	struct ky_dphy_ctx dphy_config;
	struct ky_dsi_advanced_setting adv_setting;
	int status;
};

#endif /*_KY_DSI_H_*/
