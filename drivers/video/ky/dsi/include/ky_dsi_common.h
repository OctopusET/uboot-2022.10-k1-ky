// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Ky Co., Ltd.
 *
 */

#ifndef _KY_DSI_COMMON_H_
#define _KY_DSI_COMMON_H_

#include <linux/types.h>
#include <common.h>
#include <stdio.h>

#define MAX_TX_CMD_COUNT 100
#define MAX_RX_DATA_COUNT 64

enum ky_mipi_burst_mode{
	DSI_BURST_MODE_NON_BURST_SYNC_PULSE = 0,
	DSI_BURST_MODE_NON_BURST_SYNC_EVENT = 1,
	DSI_BURST_MODE_BURST = 2,
	DSI_BURST_MODE_MAX
};

enum ky_mipi_input_data_mode{
	DSI_INPUT_DATA_RGB_MODE_565 = 0,
	DSI_INPUT_DATA_RGB_MODE_666PACKET = 1,
	DSI_INPUT_DATA_RGB_MODE_666UNPACKET = 2,
	DSI_INPUT_DATA_RGB_MODE_888 = 3,
	DSI_INPUT_DATA_RGB_MODE_MAX
};

enum ky_dsi_work_mode {
	KY_DSI_MODE_VIDEO,
	KY_DSI_MODE_CMD,
	KY_DSI_MODE_MAX
};

enum ky_dsi_cmd_type {
	KY_DSI_DCS_SWRITE = 0x5,
	KY_DSI_DCS_SWRITE1 = 0x15,
	KY_DSI_DCS_LWRITE = 0x39,
	KY_DSI_DCS_READ = 0x6,
	KY_DSI_GENERIC_LWRITE = 0x29,
	KY_DSI_GENERIC_READ1 = 0x14,
	KY_DSI_SET_MAX_PKT_SIZE = 0x37,
};

enum ky_dsi_tx_mode {
	KY_DSI_HS_MODE = 0,
	KY_DSI_LP_MODE = 1,
};

enum ky_dsi_rx_data_type {
	KY_DSI_ACK_ERR_RESP = 0x2,
	KY_DSI_EOTP = 0x8,
	KY_DSI_GEN_READ1_RESP = 0x11,
	KY_DSI_GEN_READ2_RESP = 0x12,
	KY_DSI_GEN_LREAD_RESP = 0x1A,
	KY_DSI_DCS_READ1_RESP = 0x21,
	KY_DSI_DCS_READ2_RESP = 0x22,
	KY_DSI_DCS_LREAD_RESP = 0x1C,
};

enum ky_dsi_polarity {
	KY_DSI_POLARITY_POS = 0,
	KY_DSI_POLARITY_NEG,
	KY_DSI_POLARITY_MAX
};

enum ky_dsi_te_mode {
	KY_DSI_TE_MODE_NO = 0,
	KY_DSI_TE_MODE_A,
	KY_DSI_TE_MODE_B,
	KY_DSI_TE_MODE_C,
	KY_DSI_TE_MODE_MAX,

};

struct ky_mipi_info {
	unsigned int height;
	unsigned int width;
	unsigned int hfp; /*pixel*/
	unsigned int hbp;
	unsigned int hsync;
	unsigned int vfp; /*line*/
	unsigned int vbp;
	unsigned int vsync;
	unsigned int fps;

	unsigned int work_mode; /*command_mode, video_mode*/
	unsigned int rgb_mode;
	unsigned int lane_number;
	unsigned int phy_bit_clock;
	unsigned int phy_esc_clock;

	unsigned int split_enable;
	unsigned int eotp_enable;

	/*for video mode*/
	unsigned int burst_mode;

	/*for cmd mode*/
	unsigned int te_enable;
	unsigned int vsync_pol;
	unsigned int te_pol;
	unsigned int te_mode;

	/*The following fields need not be set by panel*/
	unsigned int real_fps;
};

struct ky_dsi_cmd_desc {
	enum ky_dsi_cmd_type cmd_type;
	uint8_t  lp;		/*command tx through low power mode or hs mode */
	uint32_t delay;	/* time to delay */
	uint32_t length;	/* cmds length */
	uint8_t data[MAX_TX_CMD_COUNT];
};

struct ky_dsi_rx_buf {
	enum ky_dsi_rx_data_type data_type;
	uint32_t length; /* cmds length */
	uint8_t data[MAX_RX_DATA_COUNT];
};

/*API for mipi panel*/
int ky_mipi_open(int id, struct ky_mipi_info *mipi_info, bool ready);
int ky_mipi_close(int id);
int ky_mipi_write_cmds(int id, struct ky_dsi_cmd_desc *cmds, int count);
int ky_mipi_read_cmds(int id, struct ky_dsi_rx_buf *dbuf,
							struct ky_dsi_cmd_desc *cmds, int count);
int ky_mipi_ready_for_datatx(int id, struct ky_mipi_info *mipi_info);
int ky_mipi_close_datatx(int id);

/*API for dsi driver*/
int ky_dsi_register_device(void *device);

int ky_dsi_probe(void);
int lcd_mipi_probe(void);

int lcd_icnl9911c_init(void);
int lcd_icnl9951r_init(void);
int lcd_gx09inx101_init(void);
int lcd_jd9365dah3_init(void);
int lcd_lt8911ext_edp_1080p_init(void);

#endif /*_KY_DSI_COMMON_H_*/
