// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, ImageCue
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

// These values are based upon data received from Ronbo
// They do not match the median values suggested for the chip
#define H_DISPLAY	800
#define HS_START	816
#define HS_END		832
#define H_TOTAL		880
#define V_DISPLAY	1280
#define VS_START	1288
#define VS_END		1292
#define V_TOTAL		1296
#define V_REFRESH	60
#define WIDTHMM		108
#define HEIGHTMM	172
#define PIX_CLOCK	68430	//(H_TOTAL*V_TOTAL*V_REFRESH)/1000

struct jd9365 {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct regulator	*power;
	struct gpio_desc	*reset;
};

/* Manufacturer Command Set pages (CMD2) */
struct cmd_set_entry {
	u8 cmd;
	u8 param;
};

/*
 * There is no description in the E-Touch docs about these commands.
 * We received them from E-Touch, so just use them as is.
 */
static const struct cmd_set_entry manufacturer_cmd_set[] = {
		{0xE0,0x00},
		{0xE1,0x93},
		{0xE2,0x65},
		{0xE3,0xF8},
		{0x80,0x03},
		{0x78,0x93},   //ID
		{0x79,0x66},   //ID
		{0x7A,0xFF},   //ID
		{0x7B,0xFF},   //ID
		{0xE0,0x01},   //page1
		{0x00,0x00},   //Vcom
		{0x01,0xB0},   //Vcom
		{0x03,0x00},   //Vcom
		{0x04,0xB0},   //Vcom
		{0x17,0x00},
		{0x18,0xB1},
		{0x19,0x01},
		{0x1A,0x00},
		{0x1B,0xB1},
		{0x1C,0x01},
		{0x1F,0x3E},
		{0x20,0x28},
		{0x21,0x28},
		{0x22,0x0E},
		{0x37,0x19},
		{0x38,0x05},
		{0x39,0x08},
		{0x3A,0x12},
		{0x3C,0x78},
		{0x3D,0xFF},
		{0x3E,0xFF},
		{0x3F,0xFF},
		{0x40,0x06},
		{0x41,0xA0},
		{0x43,0x08},
		{0x44,0x07},
		{0x45,0x40},
		{0x4B,0x04},
		{0x55,0x00},
		{0x56,0x01},
		{0x57,0x89},
		{0x58,0x0A},
		{0x59,0x0A},
		{0x5A,0x28},
		{0x5B,0x14},
		{0x5D,0x7C},
		{0x5E,0x66},
		{0x5F,0x57},
		{0x60,0x49},
		{0x61,0x46},
		{0x62,0x36},
		{0x63,0x3A},
		{0x64,0x23},
		{0x65,0x3C},
		{0x66,0x38},
		{0x67,0x37},
		{0x68,0x53},
		{0x69,0x40},
		{0x6A,0x47},
		{0x6B,0x38},
		{0x6C,0x31},
		{0x6D,0x22},
		{0x6E,0x11},
		{0x6F,0x00},
		{0x70,0x7C},
		{0x71,0x66},
		{0x72,0x57},
		{0x73,0x49},
		{0x74,0x46},
		{0x75,0x36},
		{0x76,0x3A},
		{0x77,0x23},
		{0x78,0x3C},
		{0x79,0x38},
		{0x7A,0x37},
		{0x7B,0x53},
		{0x7C,0x40},
		{0x7D,0x47},
		{0x7E,0x38},
		{0x7F,0x31},
		{0x80,0x22},
		{0x81,0x11},
		{0x82,0x00},
		{0xE0,0x02},
		{0x00,0x47},
		{0x01,0x47},
		{0x02,0x45},
		{0x03,0x45},
		{0x04,0x4B},
		{0x05,0x4B},
		{0x06,0x49},
		{0x07,0x49},
		{0x08,0x41},
		{0x09,0x1F},
		{0x0A,0x1F},
		{0x0B,0x1F},
		{0x0C,0x1F},
		{0x0D,0x1F},
		{0x0E,0x1F},
		{0x0F,0x43},
		{0x10,0x1F},
		{0x11,0x1F},
		{0x12,0x1F},
		{0x13,0x1F},
		{0x14,0x1F},
		{0x15,0x1F},
		{0x16,0x46},
		{0x17,0x46},
		{0x18,0x44},
		{0x19,0x44},
		{0x1A,0x4A},
		{0x1B,0x4A},
		{0x1C,0x48},
		{0x1D,0x48},
		{0x1E,0x40},
		{0x1F,0x1F},
		{0x20,0x1F},
		{0x21,0x1F},
		{0x22,0x1F},
		{0x23,0x1F},
		{0x24,0x1F},
		{0x25,0x42},
		{0x26,0x1F},
		{0x27,0x1F},
		{0x28,0x1F},
		{0x29,0x1F},
		{0x2A,0x1F},
		{0x2B,0x1F},
		{0x2C,0x11},
		{0x2D,0x0F},
		{0x2E,0x0D},
		{0x2F,0x0B},
		{0x30,0x09},
		{0x31,0x07},
		{0x32,0x05},
		{0x33,0x18},
		{0x34,0x17},
		{0x35,0x1F},
		{0x36,0x01},
		{0x37,0x1F},
		{0x38,0x1F},
		{0x39,0x1F},
		{0x3A,0x1F},
		{0x3B,0x1F},
		{0x3C,0x1F},
		{0x3D,0x1F},
		{0x3E,0x1F},
		{0x3F,0x13},
		{0x40,0x1F},
		{0x41,0x1F},
		{0x42,0x10},
		{0x43,0x0E},
		{0x44,0x0C},
		{0x45,0x0A},
		{0x46,0x08},
		{0x47,0x06},
		{0x48,0x04},
		{0x49,0x18},
		{0x4A,0x17},
		{0x4B,0x1F},
		{0x4C,0x00},
		{0x4D,0x1F},
		{0x4E,0x1F},
		{0x4F,0x1F},
		{0x50,0x1F},
		{0x51,0x1F},
		{0x52,0x1F},
		{0x53,0x1F},
		{0x54,0x1F},
		{0x55,0x12},
		{0x56,0x1F},
		{0x57,0x1F},
		{0x58,0x40},
		{0x5B,0x30},
		{0x5C,0x02},
		{0x5D,0x40},
		{0x5E,0x01},
		{0x5F,0x02},
		{0x63,0x62},
		{0x64,0x62},
		{0x67,0x74},
		{0x68,0x04},
		{0x69,0x62},
		{0x6A,0x66},
		{0x6B,0x08},
		{0x6C,0x00},
		{0x6D,0x00},
		{0x6E,0x00},
		{0x6F,0x08},
		{0xE0,0x03},
		{0x98,0x3E},
		{0xE0,0x04},
		{0x09,0x14},
		{0x2B,0x2B},
		{0x2E,0x44},
		{0xE0,0x00},
		{0xE6,0x02},
		{0xE7,0x06},
		{0xE0,0x03},
		{0x9B,0x04},
		{0xE0,0x00},
};

static inline struct jd9365 *panel_to_jd9365(struct drm_panel *panel)
{
	return container_of(panel, struct jd9365, panel);
}

static int jd9365_panel_send_cmd_list(struct mipi_dsi_device *dsi)
{
	size_t i;
	size_t count = ARRAY_SIZE(manufacturer_cmd_set);
	int ret = 0;

	printk(KERN_NOTICE "BILL: DRIVER: sending init commands: count=%d\n", count);
	for (i = 0; i < count; i++) {
		const struct cmd_set_entry *entry = &manufacturer_cmd_set[i];
		u8 buffer[2] = { entry->cmd, entry->param };

		ret = mipi_dsi_dcs_write_buffer(dsi, buffer, sizeof(buffer));
		if (ret < 0)
		{
			printk(KERN_NOTICE "BILL: DRIVER: error sending init commands: i=%d error=%d\n", i, ret);
			return ret;
		}
	}
	printk(KERN_NOTICE "BILL: DRIVER: %d init commands sent.\n", i);
	return ret;
};

static int jd9365_prepare(struct drm_panel *panel)
{
	struct jd9365 *ctx = panel_to_jd9365(panel);
	unsigned int i;
	int ret;

	/* Power the panel */
	ret = regulator_enable(ctx->power);
	if (ret)
		return ret;
	msleep(250);

	gpiod_set_value(ctx->reset, 0); //ensure line is high
	msleep(250); //delay for a while

	printk(KERN_NOTICE "BILL: DRIVER: Successful exit from prepare\n");
	return 0;
}

static int jd9365_enable(struct drm_panel *panel)
{
	struct jd9365 *ctx = panel_to_jd9365(panel);
	int ret;
	/* do a reset */
	gpiod_set_value(ctx->reset, 1);
	msleep(20);

	gpiod_set_value(ctx->reset, 0);
	msleep(150);
	printk(KERN_NOTICE "BILL: DRIVER: Reset pulse sent - just before command list\n");

	ret = jd9365_panel_send_cmd_list(ctx->dsi);
	if (ret < 0)
	{
		printk(KERN_NOTICE "BILL: DRIVER: ERROR sending command list\n");
		return ret;
	}

	msleep(10);

	ret = mipi_dsi_dcs_exit_sleep_mode(ctx->dsi);
	if (ret)
	{
		printk(KERN_NOTICE "BILL: DRIVER: ERROR disable sleep mode\n");
		return ret;
	}

	msleep(130);

	ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
	if (ret)
	{
		printk(KERN_NOTICE "BILL: DRIVER: ERROR turning on display\n");
		return ret;
	}

	msleep(10);

	ret = mipi_dsi_dcs_set_tear_on(ctx->dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret)
	{
		printk(KERN_NOTICE "BILL: DRIVER: ERROR turning on tear mode\n");
		return ret;
	}

		return ret;
	msleep(10);

	printk(KERN_NOTICE "BILL: DRIVER: Successful exit from enable\n");
	return 0;
}

static int jd9365_disable(struct drm_panel *panel)
{
	struct jd9365 *ctx = panel_to_jd9365(panel);

	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int jd9365_unprepare(struct drm_panel *panel)
{
	struct jd9365 *ctx = panel_to_jd9365(panel);

	mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	regulator_disable(ctx->power);
	gpiod_set_value(ctx->reset, 1); //puts reset line in low state!!!
	printk(KERN_NOTICE "BILL: DRIVER: Successful exit from unprepare - reset line low!!!\n");
	return 0;
}

static const struct drm_display_mode bananapi_default_mode = {
		.clock		= 62000,
		.vrefresh	= 60,

		.hdisplay	= 800,
		.hsync_start	= 800 + 16,
		.hsync_end	= 800 + 16 + 16,
		.htotal		= 800 + 16 + 16 + 48,

		.vdisplay	= 1280,
		.vsync_start	= 1280 + 8,
		.vsync_end	= 1280 + 8 + 4,
		.vtotal		= 1280 + 8 + 4 + 4,
};

static int jd9365_get_modes(struct drm_panel *panel,struct drm_connector *connector)
{
	struct jd9365 *ctx = panel_to_jd9365(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &bananapi_default_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			bananapi_default_mode.hdisplay,
			bananapi_default_mode.vdisplay,
			bananapi_default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = WIDTHMM;
	connector->display_info.height_mm = HEIGHTMM;

	return 1;
}

static const struct drm_panel_funcs jd9365_funcs = {
	.prepare	= jd9365_prepare,
	.unprepare	= jd9365_unprepare,
	.enable		= jd9365_enable,
	.disable	= jd9365_disable,
	.get_modes	= jd9365_get_modes,
};

static int jd9366_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct jd9365 *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel, &dsi->dev, &jd9365_funcs, DRM_MODE_CONNECTOR_DSI);

	ctx->power = devm_regulator_get(&dsi->dev, "power");
	printk(KERN_NOTICE "BILL: JD9365 DRIVER: just called devm_regulator_get\n");
	if (IS_ERR(ctx->power)) {
		dev_err(&dsi->dev, "Couldn't get our power regulator\n");
		return PTR_ERR(ctx->power);
	}

	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset);
	}

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	return mipi_dsi_attach(dsi);
}

static int jd9366_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct jd9365 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id jd9366_of_match[] = {
	{ .compatible = "ronbo,jd9366" },
	{ }
};
MODULE_DEVICE_TABLE(of, jd9366_of_match);

static struct mipi_dsi_driver jd9366_dsi_driver = {
	.probe		= jd9366_dsi_probe,
	.remove		= jd9366_dsi_remove,
	.driver = {
		.name		= "jd9366-dsi",
		.of_match_table	= jd9366_of_match,
	},
};
module_mipi_dsi_driver(jd9366_dsi_driver);

MODULE_AUTHOR("Bill Hewlett <bill@imagecuellc.com>");
MODULE_DESCRIPTION("JD9366 Controller Driver");
MODULE_LICENSE("GPL v2");
