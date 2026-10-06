/*
 * This file is part of the LogicAnalyzer project.
 * LogicAnaylzer is based on libsigrok.
 *
 * Copyright (C) 2026 Q2H2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"
#include <math.h>

/* 逻辑分析仪采样率 - USB 3.0 (最高200MHz) */
static const uint64_t logic_samplerates_usb3[] = {
	SR_MHZ(200),	
	SR_MHZ(187.5),	
	SR_MHZ(175),	
	SR_MHZ(162.5),	
	SR_MHZ(156.25), 
	SR_MHZ(150),	
	SR_MHZ(137.5),	
	SR_MHZ(125),	
	SR_MHZ(112.5),	
	SR_MHZ(100),	
	SR_MHZ(93.75),	
	SR_MHZ(87.5),	
	SR_MHZ(81.5),	
	SR_MHZ(75.125), 
	SR_MHZ(75),		
	SR_MHZ(68.75),	
};

/* 逻辑分析仪USB 2.0 16通道 (最高20MHz) */
static const uint64_t logic_samplerates_usb2_16ch[] = {
	SR_MHZ(20),
};

/* 逻辑分析仪USB 2.0 8通道 (最高40MHz) */
static const uint64_t logic_samplerates_usb2_8ch[] = {
	SR_MHZ(40),
};

/* ADC采样率 最高20MHz */
static const uint64_t adc_samplerates[] = {
	SR_MHZ(20),	  
	SR_MHZ(16),	  
	SR_MHZ(13.33),
	SR_MHZ(11.43),
	SR_MHZ(10),
};

static uint8_t resp_buffer[PACKET_SIZE_IN];
static int resp_len = 0;

SR_PRIV int ch32h417_send_command(struct sr_dev_inst *sdi,
							   uint8_t cmd, uint8_t len, const uint8_t *data)
{
	struct dev_context *devc;
	uint8_t out_buffer[PACKET_SIZE_OUT] = {0};
	unsigned long io_length;

	devc = sdi->priv;

	if (len > (PACKET_SIZE_OUT - 2))
	{
		sr_err("Command data too long: %d > %d", len, PACKET_SIZE_OUT - 2);
		return SR_ERR_ARG;
	}

	out_buffer[0] = cmd;
	out_buffer[1] = len;
	if (len > 0 && data)
		memcpy(&out_buffer[2], data, len);

	io_length = PACKET_SIZE_OUT;

	if (!ch375_write_endpoint(devc->device_index, PIPE_CMD, out_buffer, &io_length))
	{
		sr_err("CH375WriteEndP failed for command 0x%02x", cmd);
		return SR_ERR;
	}

	sr_dbg("Sent command 0x%02x, len=%d, written=%lu bytes", cmd, len, io_length);

	return SR_OK;
}

SR_PRIV int ch32h417_receive_response(struct sr_dev_inst *sdi,
								   uint8_t expected_cmd, uint8_t *data, int *len)
{
	struct dev_context *devc;
	unsigned long io_length;

	devc = sdi->priv;

	io_length = PACKET_SIZE_IN;

	if (!ch375_read_endpoint(devc->device_index, PIPE_CMD, resp_buffer, &io_length))
	{
		sr_err("CH375ReadEndP failed for response");
		return SR_ERR;
	}

	resp_len = (int)io_length;

	if (resp_len < 2)
	{
		sr_err("Response too short: %d bytes", resp_len);
		return SR_ERR;
	}

	if (resp_buffer[0] != expected_cmd)
	{
		sr_err("Unexpected response: 0x%02x (expected 0x%02x)",
			   resp_buffer[0], expected_cmd);
		return SR_ERR;
	}

	uint8_t resp_data_len = resp_buffer[1];
	if (resp_data_len > 0 && resp_buffer[2] != 0x00)
	{
		sr_err("Command failed with status 0x%02x", resp_buffer[2]);
		return SR_ERR;
	}

	if (len)
		*len = resp_data_len;
	if (data && resp_data_len > 1)
		memcpy(data, &resp_buffer[3], resp_data_len - 1);

	sr_dbg("Received response 0x%02x, len=%d", resp_buffer[0], resp_data_len);

	return SR_OK;
}


SR_PRIV int ch32h417_set_logic_params(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t params[2];
	int ret;

	devc = sdi->priv;

	/* 采样位宽 */
	params[0] = devc->sample_width;

	/* 分频系数 */
	int div = 0;
	while (div < devc->num_samplerates &&
		   devc->samplerates[div] != devc->cur_samplerate)
	{
		div++;
	}
	if (div >= devc->num_samplerates)
	{
		sr_err("Invalid samplerate: %" PRIu64, devc->cur_samplerate);
		return SR_ERR_ARG;
	}
	params[1] = (uint8_t)div;

	sr_info("Setting logic params: width=0x%02x, div=%d", params[0], params[1]);

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_SET_LOGIC_PARAMS, 2, params);
	if (ret != SR_OK)
		return ret;

	return ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_SET_LOGIC_PARAMS, NULL, NULL);
}

/*
 * 设置DAC阈值
 * 发送两字节DAC值：buf[0] = 高位, buf[1] = 低位
 * 值范围：0-1024，0对应约3.3V，1024对应约1.2V
 */
SR_PRIV int ch32h417_set_threshold_value(const struct sr_dev_inst *sdi, uint16_t value)
{
	uint8_t params[2];
	int ret;

	params[0] = (value >> 8) & 0xFF;  /* 高位 */
	params[1] = value & 0xFF;          /* 低位 */

	sr_info("Setting DAC threshold: 0x%04x (%d)", value, value);

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_SET_LOGIC_LEVEL, 2, params);
	if (ret != SR_OK)
		return ret;

	return ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_SET_LOGIC_LEVEL, NULL, NULL);
}

/*
 * 获取逻辑分析仪参数
 */
SR_PRIV int ch32h417_get_logic_params(const struct sr_dev_inst *sdi,
								   uint8_t *width, uint8_t *div)
{
	uint8_t data[2];
	int len;
	int ret;
	gint64 t1, t2;

	t1 = g_get_monotonic_time();
	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_GET_LOGIC_PARAMS, 0, NULL);
	t2 = g_get_monotonic_time();
	sr_dbg("ch32h417_get_logic_params: send_command took %" G_GINT64_FORMAT " us", t2 - t1);
	if (ret != SR_OK)
		return ret;

	t1 = g_get_monotonic_time();
	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_GET_LOGIC_PARAMS, data, &len);
	t2 = g_get_monotonic_time();
	sr_dbg("ch32h417_get_logic_params: receive_response took %" G_GINT64_FORMAT " us", t2 - t1);
	if (ret != SR_OK)
		return ret;

	if (width)
		*width = data[0];
	if (div)
		*div = data[1];

	sr_dbg("Got logic params: width=0x%02x, div=%d", data[0], data[1]);

	return SR_OK;
}

/*
 * 获取逻辑分析仪电压电平
 */
SR_PRIV int ch32h417_get_logic_level(const struct sr_dev_inst *sdi,
								  uint8_t *level)
{
	uint8_t data;
	int len;
	int ret;

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_GET_LOGIC_LEVEL, 0, NULL);
	if (ret != SR_OK)
		return ret;

	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_GET_LOGIC_LEVEL, &data, &len);
	if (ret != SR_OK)
		return ret;

	if (level)
		*level = data;

	sr_dbg("Got logic level: %d", data);

	return SR_OK;
}

/*
 * 设置ADC参数
 * Buf[0]: 0x8=8位ADC, 0x0a=10位ADC
 * Buf[1]: 分频系数 (0=20M, 1=10M, ..., 最多64分频)
 */
SR_PRIV int ch32h417_set_adc_params(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t params[2];
	int ret;

	devc = sdi->priv;

	params[0] = devc->adc_width;

	/* 计算分频系数 */
	int div = 0;
	while (div < ARRAY_SIZE(adc_samplerates) &&
		   adc_samplerates[div] != devc->cur_samplerate)
	{
		div++;
	}
	if (div >= ARRAY_SIZE(adc_samplerates))
	{
		sr_err("Invalid ADC samplerate: %" PRIu64, devc->cur_samplerate);
		return SR_ERR_ARG;
	}
	div += 3;
	params[1] = (uint8_t)div;

	sr_info("Setting ADC params: width=0x%02x, div=%d", params[0], params[1]);

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_SET_ADC_PARAMS, 2, params);
	if (ret != SR_OK)
		return ret;

	return ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_SET_ADC_PARAMS, NULL, NULL);
}

/*
 * 设置ADC通道
 * Buf[0]: 0=通道1, 1=通道2
 */
SR_PRIV int ch32h417_set_adc_channel(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t param;
	int ret;

	devc = sdi->priv;

	param = (uint8_t)devc->adc_channel;

	sr_info("Setting ADC channel: %d", param);

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_SET_ADC_CHANNEL, 1, &param);
	if (ret != SR_OK)
		return ret;

	return ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_SET_ADC_CHANNEL, NULL, NULL);
}

/*
 * 获取ADC参数
 */
SR_PRIV int ch32h417_get_adc_params(const struct sr_dev_inst *sdi,
								 uint8_t *width, uint8_t *div)
{
	uint8_t data[2];
	int len;
	int ret;

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_GET_ADC_PARAMS, 0, NULL);
	if (ret != SR_OK)
		return ret;

	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_GET_ADC_PARAMS, data, &len);
	if (ret != SR_OK)
		return ret;

	if (width)
		*width = data[0];
	if (div)
		*div = data[1];

	return SR_OK;
}

/*
 * 获取ADC通道
 */
SR_PRIV int ch32h417_get_adc_channel(const struct sr_dev_inst *sdi,
								  uint8_t *channel)
{
	uint8_t data;
	int len;
	int ret;

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_GET_ADC_CHANNEL, 0, NULL);
	if (ret != SR_OK)
		return ret;

	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_GET_ADC_CHANNEL, &data, &len);
	if (ret != SR_OK)
		return ret;

	if (channel)
		*channel = data;

	return SR_OK;
}

SR_PRIV int ch32h417_set_adc_range(const struct sr_dev_inst *sdi,
	uint8_t channel, uint8_t range)
{
	uint8_t params[2] = {channel, range};
	int ret;

	if (channel > 1 || range > 3)
		return SR_ERR_ARG;
	ret = ch32h417_send_command((struct sr_dev_inst *)sdi,
		CMD_SET_ADC_RANGE, sizeof(params), params);
	if (ret != SR_OK)
		return ret;
	return ch32h417_receive_response((struct sr_dev_inst *)sdi,
		RESP_SET_ADC_RANGE, NULL, NULL);
}

SR_PRIV int ch32h417_get_adc_ranges(const struct sr_dev_inst *sdi,
	uint8_t ranges[2])
{
	int len = 0;
	int ret;

	if (!ranges)
		return SR_ERR_ARG;
	ret = ch32h417_send_command((struct sr_dev_inst *)sdi,
		CMD_GET_ADC_RANGE, 0, NULL);
	if (ret != SR_OK)
		return ret;
	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi,
		RESP_GET_ADC_RANGE, ranges, &len);
	if (ret != SR_OK)
		return ret;
	return (len >= 3) ? SR_OK : SR_ERR_DATA;
}

/*
 * 开始逻辑分析仪采集
 */
SR_PRIV int ch32h417_start_logic(const struct sr_dev_inst *sdi)
{
	sr_info("Starting logic acquisition");
	return ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_START_LOGIC, 0, NULL);
}

/*
 * 停止逻辑分析仪采集
 */
SR_PRIV int ch32h417_stop_logic(const struct sr_dev_inst *sdi)
{
	sr_info("Stopping logic acquisition");
	return ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_STOP_LOGIC, 0, NULL);
}

/*
 * 开始ADC采集
 */
SR_PRIV int ch32h417_start_adc(const struct sr_dev_inst *sdi)
{
	sr_info("Starting ADC acquisition");
	return ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_START_ADC, 0, NULL);
}

/*
 * 停止ADC采集
 */
SR_PRIV int ch32h417_stop_adc(const struct sr_dev_inst *sdi)
{
	sr_info("Stopping ADC acquisition");
	return ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_STOP_ADC, 0, NULL);
}

/*
 * 进入IAP模式（固件升级模式）
 * 命令: 0xAE + len(0)
 * 设备收到此命令后会进入IAP模式，可用于固件升级
 */
SR_PRIV int ch32h417_enter_iap(const struct sr_dev_inst *sdi)
{
	sr_info("Entering IAP mode for firmware upgrade");
	return ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_ENTER_IAP, 0, NULL);
}

/* 发送停止命令并发送SR_DF_END */
static void send_stop_command(const struct sr_dev_inst *sdi)
{
	int ret = -1;
	struct dev_context *devc = sdi->priv;

	/* 发送停止命令 */
	if (devc->mode == MODE_LOGIC_ANALYZER)
	{
		ret = ch32h417_stop_logic(sdi);
		if (ret != SR_OK)
		{
			sr_warn("Failed to send stop logic command on finish.");
		}
	}
	else
	{
		ret = ch32h417_stop_adc(sdi);
		if (ret != SR_OK)
		{
			sr_warn("Failed to send stop ADC command on finish.");
		}
	}
	
	/* 清除缓冲上传缓存 */
	unsigned long pipe_num = (devc->mode == MODE_LOGIC_ANALYZER) ?
									 PIPE_LOGIC_DATA : PIPE_ADC_DATA;
	if (!ch375_clear_buf_upload(devc->device_index, pipe_num)) {
		sr_warn("Failed to clear upload buffer pipe %lu for device at index %d", pipe_num, devc->device_index);
	}
}

/*
 * 创建设备上下文
 */
SR_PRIV struct dev_context *ch32h417_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->mode = MODE_LOGIC_ANALYZER;
	devc->cur_samplerate = logic_samplerates_usb3[0];
	devc->limit_samples = 0;
	devc->limit_frames = 1;
	devc->capture_ratio = 0;
	devc->sample_width = SAMPLE_WIDTH_16BIT;
	devc->num_channels = 16; /* 默认16通道 */
	devc->voltage_level = VOLTAGE_3_3V;
	devc->adc_width = ADC_WIDTH_8BIT;
	devc->adc_channel = ADC_CHANNEL_1;
	devc->adc_range[0] = devc->adc_range[1] = 0;
	for (int channel = 0; channel < 2; channel++)
		for (int range = 0; range < 4; range++)
			devc->adc_zero[channel][range] = 512;
	devc->fw_version = 0;
	devc->is_usb2 = FALSE;
	devc->samplerates = logic_samplerates_usb3;
	devc->num_samplerates = ARRAY_SIZE(logic_samplerates_usb3);

	/* CH375相关初始化 */
	devc->device_index = -1;
	devc->device_handle = NULL;
	devc->buffer_size = BUFFER_UPLOAD_SIZE;

	g_mutex_init(&devc->mutex);
	g_cond_init(&devc->cond);

	return devc;
}

/*
 * 检测USB速度（通过设备描述符中的bcdUSB字段）
 * USB设备描述符结构：
 *   偏移0: bLength (描述符长度)
 *   偏移1: bDescriptorType (描述符类型)
 *   偏移2-3: bcdUSB (USB版本，BCD编码)
 *   ...
 * 返回: TRUE表示USB 2.0，FALSE表示USB 3.0
 */
SR_PRIV gboolean ch32h417_detect_usb_speed(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t descr[18];  /* USB设备描述符固定18字节 */
	unsigned long length;
	uint16_t bcd_usb;

	devc = sdi->priv;

	g_usleep(50000);

	length = sizeof(descr);
	if (!ch375_get_device_descr(devc->device_index, descr, &length)) {
		sr_warn("Failed to get device descriptor, assuming USB 3.0");
		return FALSE;  /* 默认USB 3.0 */
	}

	if (length < 4) {
		sr_warn("Device descriptor too short (%lu bytes), assuming USB 3.0", length);
		return FALSE;
	}

	/* bcdUSB字段位于偏移2-3，小端序 */
	bcd_usb = descr[2] | (descr[3] << 8);

	sr_info("Device descriptor: bcdUSB=0x%04x", bcd_usb);

	/* USB 2.0: bcdUSB = 0x0200
	 * USB 3.0: bcdUSB = 0x0300
	 */
	if (bcd_usb >= 0x0300) {
		sr_info("USB 3.0 device detected (bcdUSB=0x%04x)", bcd_usb);
		return FALSE;
	} else {
		sr_info("USB 2.0 device detected (bcdUSB=0x%04x)", bcd_usb);
		return TRUE;
	}
}

/*
 * 获取固件版本号
 * CH32H417无FPGA，只有固件版本
 * 命令: 0xAC + len(0)
 * 返回: 0xBC + len(2) + Status(0x00) + buf[0](固件版本) + buf[1](硬件版本)
 */
SR_PRIV int ch32h417_get_fw_version(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t data[2] = {0};
	int len;
	int ret;

	devc = sdi->priv;

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, CMD_GET_VERSION, 0, NULL);
	if (ret != SR_OK) {
		sr_err("Failed to send get version command");
		return ret;
	}

	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi, RESP_GET_VERSION, data, &len);
	if (ret != SR_OK) {
		sr_err("Failed to receive version response");
		return ret;
	}

	if (len >= 2) {
		devc->fw_version = (data[0] << 8) | data[1];
		sr_info("Firmware version: %d, Hardware version: %d", data[0], data[1]);
	} else {
		sr_warn("Version response too short: %d", len);
		devc->fw_version = 0;
	}

	sr_dbg("Firmware version: 0x%04x", devc->fw_version);

	return SR_OK;
}

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ch32h417_read_instrument_chunk(const struct sr_dev_inst *sdi,
	uint8_t cmd, uint32_t offset, uint8_t *data, size_t *data_len)
{
	uint8_t request[4];
	int response_len = 0;
	int ret;

	request[0] = (uint8_t)offset;
	request[1] = (uint8_t)(offset >> 8);
	request[2] = (uint8_t)(offset >> 16);
	request[3] = (uint8_t)(offset >> 24);

	ret = ch32h417_send_command((struct sr_dev_inst *)sdi, cmd,
		(cmd == CMD_GET_INSTRUMENT_STATE) ? 0 : sizeof(request), request);
	if (ret != SR_OK)
		return ret;

	ret = ch32h417_receive_response((struct sr_dev_inst *)sdi, cmd,
		data, &response_len);
	if (ret != SR_OK)
		return ret;
	if (response_len < 1 || response_len > 30)
		return SR_ERR_DATA;

	*data_len = (size_t)(response_len - 1); /* response length includes status */
	return SR_OK;
}

SR_PRIV int ch32h417_get_instrument_status(const struct sr_dev_inst *sdi,
	uint64_t *status)
{
	struct dev_context *devc;
	uint8_t data[29] = {0};
	size_t len = 0;
	uint32_t samples;
	int ret;

	if (!sdi || !status)
		return SR_ERR_ARG;
	devc = sdi->priv;

	g_mutex_lock(&devc->mutex);
	ret = ch32h417_read_instrument_chunk(sdi,
		CMD_GET_INSTRUMENT_STATE, 0, data, &len);
	g_mutex_unlock(&devc->mutex);
	if (ret != SR_OK)
		return ret;
	if (len < 8)
		return SR_ERR_DATA;

	samples = get_le32(data + 4);
	*status = (uint64_t)data[0] |
		((uint64_t)(data[1] != 0) << 8) |
		((uint64_t)(data[2] != 0) << 9) |
		((uint64_t)(data[3] != 0) << 10) |
		((uint64_t)samples << 32);
	return SR_OK;
}

SR_PRIV int ch32h417_get_instrument_capture(const struct sr_dev_inst *sdi,
	char **capture_hex)
{
	static const char digits[] = "0123456789abcdef";
	struct dev_context *devc;
	uint8_t header[INSTRUMENT_HEADER_SIZE];
	uint8_t chunk[29];
	uint8_t *capture = NULL;
	char *hex = NULL;
	size_t offset, chunk_len, total;
	uint32_t magic, bytes;
	int ret = SR_OK;

	if (!sdi || !capture_hex)
		return SR_ERR_ARG;
	*capture_hex = NULL;
	devc = sdi->priv;

	g_mutex_lock(&devc->mutex);
	for (offset = 0; offset < sizeof(header); offset += chunk_len) {
		ret = ch32h417_read_instrument_chunk(sdi,
			CMD_GET_INSTRUMENT_HEADER, (uint32_t)offset, chunk, &chunk_len);
		if (ret != SR_OK || chunk_len == 0 || chunk_len > sizeof(header) - offset) {
			ret = (ret == SR_OK) ? SR_ERR_DATA : ret;
			goto out;
		}
		memcpy(header + offset, chunk, chunk_len);
	}

	magic = get_le32(header);
	bytes = get_le32(header + 12);
	if (magic != 0x31414c44U || bytes == 0 || bytes > INSTRUMENT_MAX_DATA_SIZE) {
		ret = SR_ERR_DATA;
		goto out;
	}

	total = sizeof(header) + bytes;
	capture = g_try_malloc(total);
	if (!capture) {
		ret = SR_ERR_MALLOC;
		goto out;
	}
	memcpy(capture, header, sizeof(header));

	for (offset = 0; offset < bytes; offset += chunk_len) {
		ret = ch32h417_read_instrument_chunk(sdi,
			CMD_GET_INSTRUMENT_DATA, (uint32_t)offset, chunk, &chunk_len);
		if (ret != SR_OK || chunk_len == 0 || chunk_len > bytes - offset) {
			ret = (ret == SR_OK) ? SR_ERR_DATA : ret;
			goto out;
		}
		memcpy(capture + sizeof(header) + offset, chunk, chunk_len);
	}

	hex = g_try_malloc(total * 2 + 1);
	if (!hex) {
		ret = SR_ERR_MALLOC;
		goto out;
	}
	for (offset = 0; offset < total; offset++) {
		hex[offset * 2] = digits[capture[offset] >> 4];
		hex[offset * 2 + 1] = digits[capture[offset] & 0x0f];
	}
	hex[total * 2] = '\0';
	*capture_hex = hex;
	hex = NULL;

out:
	g_mutex_unlock(&devc->mutex);
	g_free(capture);
	g_free(hex);
	return ret;
}

static int acquisition_check_cb(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;

	(void)fd;
	(void)revents;

	g_mutex_lock(&devc->mutex);
	gboolean should_stop = devc->acq_aborted;
	g_mutex_unlock(&devc->mutex);

	if (should_stop) {
		sr_dbg("acquisition_check_cb: thread requested stop");
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

/* 发送逻辑分析仪数据 */
static void la_send_data_proc(struct sr_dev_inst *sdi,
			  uint8_t *data, size_t length, size_t unitsize)
{
	const struct sr_datafeed_logic logic = {
		.length = length,
		.unitsize = unitsize,
		.data = data};

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic};

	sr_session_send(sdi, &packet);
}

/* 发送ADC数据 */
static void adc_send_data_proc(struct sr_dev_inst *sdi,
							   uint8_t *data, size_t length, size_t sample_width)
{
	struct dev_context *devc;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float *float_data;
	size_t num_samples;
	size_t i;
	uint16_t raw_sample;
	uint16_t code10;
	float voltage;
	static const float feedback_ohms[4] = {6980.0f, 28000.0f, 140000.0f, 280000.0f};

	devc = sdi->priv;
	const unsigned int channel = (unsigned int)devc->adc_channel;
	if (channel >= 2U) {
		sr_err("Invalid ADC channel index: %u", channel);
		return;
	}
	const unsigned int range = devc->adc_range[channel] & 3U;

	/* The firmware streams one ADC channel at a time. Never publish a packet
	 * with zero or multiple channels: consumers use this list to calculate the
	 * interleaved buffer size and would otherwise read past float_data. */
	if (g_slist_length(devc->enabled_analog_channels) != 1) {
		sr_err("Refusing malformed ADC packet: expected one channel, got %u",
			(unsigned int)g_slist_length(devc->enabled_analog_channels));
		return;
	}

	/* 计算样本数量：8位ADC每样本1字节，10位ADC每样本2字节 */
	size_t sample_bytes = (devc->adc_width == ADC_WIDTH_10BIT) ? 2 : 1;
	num_samples = length / sample_bytes;

	/* 将原始ADC数据转换为float电压值 */
	float_data = g_try_malloc(sizeof(float) * num_samples);
	if (!float_data) {
		sr_err("Failed to allocate float buffer for ADC data");
		return;
	}

	for (i = 0; i < num_samples; i++) {
		/* 根据ADC位宽读取数据 */
		if (devc->adc_width == ADC_WIDTH_10BIT) {
			/* 10bit ADC：读取16位值 */
			raw_sample = *((uint16_t*)(data + i * 2));
			code10 = raw_sample & 0x3ff;
		} else {
			/* 8bit ADC：读取8位值 */
			code10 = (uint16_t)(((uint32_t)data[i] * 1023U + 127U) / 255U);
		}
		/* New inverting AFE: Vout=VCM-Vin*Rf/99.8k. */
		voltage = ((float)((int)devc->adc_zero[channel][range] - (int)code10) *
			3.3f * 99800.0f) / (1023.0f * feedback_ohms[range]);

		float_data[i] = voltage;
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
	analog.meaning->channels = devc->enabled_analog_channels;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = 0;
	analog.num_samples = num_samples;
	analog.data = float_data;

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_ANALOG,
		.payload = &analog};

	sr_session_send(sdi, &packet);

	g_free(float_data);
}

/* 数据读取线程 */
static gpointer data_read_thread(gpointer arg)
{
	struct sr_dev_inst *sdi = arg;
	struct dev_context *devc = sdi->priv;
	unsigned long pipe_num;
	unsigned long transfer_count, total_data_len;
	unsigned long length;
	uint8_t *buffer;
	size_t unitsize;
	unsigned int num_samples;
	int trigger_offset, pre_trigger_samples;
	int no_data_count = 0;
	const int no_data_timeout_ms = 500; /* 500ms无数据超时 */

	pipe_num = (devc->mode == MODE_LOGIC_ANALYZER) ? PIPE_LOGIC_DATA : PIPE_ADC_DATA;

	if (devc->mode == MODE_LOGIC_ANALYZER)
		unitsize = (devc->num_channels == 16) ? 2 : 1;
	else
		unitsize = (devc->adc_width == ADC_WIDTH_10BIT) ? 2 : 1;

	buffer = g_try_malloc(devc->buffer_size);
	if (!buffer) {
		sr_err("Failed to allocate read buffer");
		return NULL;
	}

	sr_dbg("Data read thread started, pipe=%lu, buffer_size=%zu",
		   pipe_num, devc->buffer_size);

	while (!devc->acq_aborted) {
		/* 查询缓冲数据 */
		if (!ch375_query_buf_upload_ex(devc->device_index, pipe_num,
									   &transfer_count, &total_data_len)) {
			/* 查询失败，短暂等待后重试 */
			g_usleep(1000);
			continue;
		}

		if (transfer_count == 0) {
			/* 没有数据，检查是否设备已停止 */
			no_data_count++;
			if (no_data_count >= no_data_timeout_ms) {
				/* 超时无数据，设备可能已停止采集 */
				sr_info("No data for %d ms, device may have stopped", no_data_timeout_ms);
				g_free(buffer);
				/* 设置标志表示线程正在退出，定时器回调会处理停止 */
				g_mutex_lock(&devc->mutex);
				devc->acq_aborted = TRUE;
				g_mutex_unlock(&devc->mutex);
				sr_dbg("data_read_thread EXIT (timeout)");
				return NULL;
			}
			g_usleep(1000);
			continue;
		}

		/* 有数据，重置计数器 */
		no_data_count = 0;

		/* 读取数据 */
		length = devc->buffer_size;
		if (!ch375_read_endpoint(devc->device_index, pipe_num, buffer, &length)) {
			sr_warn("Failed to read data from pipe %lu", pipe_num);
			g_usleep(1000);
			continue;
		}

		if (length == 0) {
			g_usleep(1000);
			continue;
		}

		sr_dbg("Read %lu bytes from pipe %lu", length, pipe_num);

		num_samples = length / unitsize;

		/* 触发处理 */
		g_mutex_lock(&devc->mutex);
		if (devc->trigger_fired) {
			/* 触发后发送数据 */
			if (!devc->limit_samples || devc->sent_samples < devc->limit_samples) {
				if (devc->limit_samples &&
					devc->sent_samples + num_samples > devc->limit_samples)
					num_samples = devc->limit_samples - devc->sent_samples;
				devc->send_data_proc(sdi, buffer,
								num_samples * unitsize, unitsize);
				devc->sent_samples += num_samples;
			}
		} else if (devc->stl) {
			/* 检测触发 */
			devc->stl->unitsize = unitsize;
			trigger_offset = soft_trigger_logic_check(devc->stl,
													  buffer, length,
													  &pre_trigger_samples);

			if (trigger_offset > -1) {
				/* 发送预触发数据 */
				devc->send_data_proc(sdi, buffer,
									trigger_offset * unitsize, unitsize);
				std_session_send_df_trigger(sdi);
				devc->trigger_fired = TRUE;
				devc->sent_samples = pre_trigger_samples;

				/* 发送触发后数据 */
				num_samples = (length / unitsize) - trigger_offset;
				if (devc->limit_samples &&
					devc->sent_samples + num_samples > devc->limit_samples)
					num_samples = devc->limit_samples - devc->sent_samples;

				if (num_samples > 0) {
					devc->send_data_proc(sdi,
										 	buffer + trigger_offset * unitsize,
										 	num_samples * unitsize, unitsize);
					devc->sent_samples += num_samples;
				}
			}
		}
		g_mutex_unlock(&devc->mutex);

		/* 检查采样限制 */
		if (devc->limit_samples && devc->sent_samples >= devc->limit_samples) {
			devc->num_frames++;
			devc->sent_samples = 0;
			devc->trigger_fired = FALSE;

			if (devc->stl)
				devc->stl->cur_stage = 0;

			if (devc->limit_frames && devc->num_frames >= devc->limit_frames) {
				g_mutex_lock(&devc->mutex);
				devc->acq_aborted = TRUE;
				g_mutex_unlock(&devc->mutex);
				g_free(buffer);
				sr_dbg("data_read_thread EXIT (frame limit)");
				return NULL;	
			}
		}
	}

	g_free(buffer);
	sr_dbg("Data read thread exiting");

	return NULL;
}

/* 结束采集 */
static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	/* 发送停止命令和结束包 */
	send_stop_command(sdi);

	/* 移除定时器回调 */
	sr_session_source_remove(sdi->session, -1);

	/* 停止读取线程 */
	g_mutex_lock(&devc->mutex);
	devc->acq_aborted = TRUE;
	g_mutex_unlock(&devc->mutex);

	/* 等待线程结束 */
	if (devc->read_thread) {
		g_thread_join(devc->read_thread);
		devc->read_thread = NULL;
	}
	
	std_session_send_df_end(sdi);

	sr_dbg("devc->read_thread EXIT");

	if (devc->stl)
	{
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}

	if (devc->logic_buffer)
	{
		g_free(devc->logic_buffer);
		devc->logic_buffer = NULL;
	}

	if (devc->analog_buffer)
	{
		g_free(devc->analog_buffer);
		devc->analog_buffer = NULL;
	}
	sr_dbg("finish_acquisition function done");
}

/*
 * 开始采集
 */
SR_PRIV int ch32h417_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	unsigned long pipe_num;
	int ret;

	devc = sdi->priv;
	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->num_frames = 0;

	/* 启动缓冲缓冲上传 */
	if (!ch375_set_buf_upload_ex(devc->device_index, 0, PIPE_LOGIC_DATA, devc->buffer_size)) {
		sr_warn("Failed to set upload buffer pipe %d for device at index %d", PIPE_LOGIC_DATA, devc->device_index);
	}
	if (!ch375_set_buf_upload_ex(devc->device_index, 0, PIPE_ADC_DATA, devc->buffer_size)) {
		sr_warn("Failed to set upload buffer pipe %d for device at index %d", PIPE_ADC_DATA, devc->device_index);
	}

	if (!ch375_set_buf_upload_ex(devc->device_index, 1, PIPE_LOGIC_DATA, devc->buffer_size)) {
		sr_warn("Failed to set upload buffer pipe %d for device at index %d", PIPE_LOGIC_DATA, devc->device_index);
	}
	if (!ch375_set_buf_upload_ex(devc->device_index, 1, PIPE_ADC_DATA, devc->buffer_size)) {
		sr_warn("Failed to set upload buffer pipe %d for device at index %d", PIPE_ADC_DATA, devc->device_index);
	}

	/* 配置通道 */
	if (devc->mode == MODE_LOGIC_ANALYZER)
	{
		pipe_num = PIPE_LOGIC_DATA;
		/* 先发送停止命令，确保设备处于空闲状态 */
		ret = ch32h417_stop_logic(sdi);
		if (ret != SR_OK)
		{
			sr_warn("Pre-stop logic command failed, continuing anyway...");
		}

		/* 设置逻辑分析仪参数 */
		ret = ch32h417_set_logic_params(sdi);
		if (ret != SR_OK)
		{
			sr_err("Failed to set logic params.");
			return ret;
		}

		/* 测试验证命令 */
		{
			uint8_t width, div, level;
			int test_failed = 0;

			/* 测试 A4: 获取逻辑分析仪参数 */
			ret = ch32h417_get_logic_params(sdi, &width, &div);
			if (ret == SR_OK) {
				sr_info("Verify A4: width=0x%02x, div=%d (expect width=0x%02x)",
					width, div, devc->sample_width);
				if (width != devc->sample_width) {
					sr_warn("A4 verification: width mismatch!");
					test_failed = 1;
				}
			} else {
				sr_err("A4 command failed!");
				test_failed = 1;
			}
		}

		/* 开始逻辑分析仪采集 */
		ret = ch32h417_start_logic(sdi);
		if (ret != SR_OK)
		{
			sr_err("Failed to start logic acquisition.");
			return ret;
		}
	}
	else
	{
		pipe_num = PIPE_ADC_DATA;

		sr_dbg("ADC mode: initializing enabled_analog_channels");
		sr_dbg("  sdi->channels = %p", sdi->channels);

		if (devc->enabled_analog_channels)
			g_slist_free(devc->enabled_analog_channels);
		devc->enabled_analog_channels = NULL;

		int analog_count = 0;
		int analog_index = 0;
		for (GSList *l = sdi->channels; l; l = l->next) {
			struct sr_channel *ch = l->data;
			sr_dbg("  channel %p: type=%d, enabled=%d", ch, ch->type, ch->enabled);
			if (ch->type == SR_CHANNEL_ANALOG) {
				/* A stale restored session may have both A0 and A1 enabled. The
				 * hardware still returns only devc->adc_channel, so normalize the
				 * libsigrok channel state at this final safety boundary. */
				ch->enabled = (analog_index == (int)devc->adc_channel);
				if (ch->enabled) {
					devc->enabled_analog_channels = g_slist_append(
						devc->enabled_analog_channels, ch);
					analog_count++;
				}
				analog_index++;
			}
		}
		sr_dbg("  enabled_analog_channels count = %d", analog_count);
		if (analog_count != 1) {
			sr_err("Invalid ADC channel %u (available analog channels: %d)",
				(unsigned int)devc->adc_channel, analog_index);
			return SR_ERR_ARG;
		}

		ret = ch32h417_stop_adc(sdi);
		if (ret != SR_OK)
		{
			sr_warn("Pre-stop ADC command failed, continuing anyway...");
		}
		/* STOP has no acknowledgement. Give the firmware acquisition task time
		 * to leave the active state before commands that it rejects while busy. */
		g_usleep(20000);

		ret = ch32h417_set_adc_params(sdi);
		if (ret != SR_OK)
		{
			sr_err("Failed to set ADC params.");
			return ret;
		}

		ret = ch32h417_set_adc_channel(sdi);
		if (ret != SR_OK)
		{
			sr_err("Failed to set ADC channel.");
			return ret;
		}

		for (int attempt = 0; attempt < 3; attempt++) {
			ret = ch32h417_set_adc_range(sdi, (uint8_t)devc->adc_channel,
				devc->adc_range[devc->adc_channel]);
			if (ret == SR_OK)
				break;
			sr_warn("ADC range command failed (attempt %d/3), stopping and retrying",
				attempt + 1);
			ch32h417_stop_adc(sdi);
			g_usleep(20000);
		}
		if (ret != SR_OK)
		{
			sr_err("Failed to set ADC front-end range.");
			return ret;
		}
		g_usleep(2000);

		/* 开始ADC采集 */
		ret = ch32h417_start_adc(sdi);
		if (ret != SR_OK)
		{
			sr_err("Failed to start ADC acquisition.");
			return ret;
		}

		/* ADC chunks are converted and forwarded incrementally by
		 * adc_send_data_proc(); allocating limit_samples floats here was unused
		 * and could reserve hundreds of megabytes for a 10 s capture. */
		devc->analog_buffer = NULL;
	}

	/* 设置触发 */
	if ((trigger = sr_session_trigger_get(sdi->session)))
	{
		int pre_trigger_samples = 0;
		if (devc->limit_samples > 0)
			pre_trigger_samples = (devc->capture_ratio * devc->limit_samples) / 100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	}
	else
	{
		devc->trigger_fired = TRUE;
	}

	if (devc->mode == MODE_LOGIC_ANALYZER)
	{
		devc->send_data_proc = la_send_data_proc;
	}
	else
	{
		devc->send_data_proc = adc_send_data_proc;
	}

	g_mutex_lock(&devc->mutex);
	devc->acq_aborted = TRUE;
	g_mutex_unlock(&devc->mutex);

	if (devc->read_thread) {
		g_thread_join(devc->read_thread);
		devc->read_thread = NULL;
	}

	g_mutex_lock(&devc->mutex);
	devc->acq_aborted = FALSE;
	g_mutex_unlock(&devc->mutex);

	if (!ch375_clear_buf_upload(devc->device_index, pipe_num)) {
		sr_warn("Failed to clear upload buffer pipe %lu for device at index %d", pipe_num, devc->device_index);
	}

	devc->read_thread = g_thread_new("ch32h417-read", data_read_thread, (void *)sdi);
	if (!devc->read_thread) {
		sr_err("Failed to create read thread");
		if (!ch375_clear_buf_upload(devc->device_index, pipe_num)) {
			sr_warn("Failed to clear upload buffer pipe %lu for device at index %d", pipe_num, devc->device_index);
		}
		send_stop_command(sdi);
		return SR_ERR;
	}

	std_session_send_df_header(sdi);

	/* 添加定时器回调，用于检查采集状态并在主线程中停止采集 */
	sr_session_source_add(sdi->session, -1, 0, 5,
		acquisition_check_cb, (void *)sdi);

	return SR_OK;
}

/*
 * 停止采集
 */
SR_PRIV int ch32h417_acquisition_stop(const struct sr_dev_inst *sdi)
{
	finish_acquisition((struct sr_dev_inst *)sdi);
	return SR_OK;
}
