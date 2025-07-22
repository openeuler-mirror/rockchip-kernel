// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/motorcomm.c
 *
 * Driver for Motorcomm PHYs
 *
 * Author: yinghong.zhang<yinghong.zhang@motor-comm.com>
 *
 * Copyright (c) 2024 Motorcomm, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Support Motorcomm Phys:
 * Giga phys: yt8511, yt8521, yt8531, yt8543, yt8614, yt8618
 * 100/10M Phys: yt8510, yt8512, yt8522
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

/* for wol feature */
#include <linux/netdevice.h>

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#define YTPHY_LINUX_VERSION "2.2.47853"

#define PHY_LINK_STATE "phylink_state"
#define PHYREG_DUMP "dump"
#define DRV_STRENGTH "drive_strength"
#define PHYDRV_VER "phydrv_ver"
#define TEMPLATE "template"
#define LOOPBACK "loopback"
#define CHECKER "checker"
#define GENERATOR "generator"
#define PHYREG "phyreg"
#define MODULE_NAME "yt"
#define CSD "csd"
#define SNR "snr"
#define DELIMITER " \t\n"

static struct dentry *root_dir;

#define MOTORCOMM_DEBUG_READ_ENTRY(name)					\
	static int yt_##name##_open(struct inode *inode, struct file *file)	\
	{									\
		return single_open(file, yt_##name##_show, inode->i_private);	\
	}									\
										\
	static const struct file_operations yt_##name##_fops = {		\
		.owner		= THIS_MODULE,					\
		.open		= yt_##name##_open,				\
		.read		= seq_read,					\
		.write		= NULL,						\
	}

#define MOTORCOMM_DEBUG_WRITE_ENTRY(name)					\
	static int yt_##name##_open(struct inode *inode, struct file *file)	\
	{									\
		file->private_data = inode->i_private;				\
		return 0;							\
	}									\
										\
	static const struct file_operations yt_##name##_fops = {		\
		.owner		= THIS_MODULE,					\
		.open		= yt_##name##_open,				\
		.read		= NULL,						\
		.write		= yt_##name##_cb,				\
	}

/**************** configuration section begin ************/
/* if system depends on ethernet packet to restore from sleep,
 * please define this macro to 1 otherwise, define it to 0.
 */
#define SYS_WAKEUP_BASED_ON_ETH_PKT	1

/* to enable system WOL feature of phy, please define this macro to 1
 * otherwise, define it to 0.
 */
#define YTPHY_WOL_FEATURE_ENABLE	0

/* some GMAC need clock input from PHY, for eg., 125M,
 * please enable this macro
 * by degault, it is set to 0
 * NOTE: this macro will need macro SYS_WAKEUP_BASED_ON_ETH_PKT to set to 1
 */
#define GMAC_CLOCK_INPUT_NEEDED		0

/* for YT8531 package A xtal init config */
#define YTPHY8531A_XTAL_INIT		(0)

/**** configuration section end ************/

/* no need to change below */
#define MOTORCOMM_PHY_ID_MASK		0xffffffff

#define PHY_ID_YT8510			0x00000109
#define PHY_ID_YT8511			0x0000010a
#define PHY_ID_YT8512			0x00000128
#define PHY_ID_YT8522			0x4f51e928
#define PHY_ID_YT8521			0x0000011a
#define PHY_ID_YT8531S			0x4f51e91a
#define PHY_ID_YT8531			0x4f51e91b
/* YT8543 phy driver disable(default) */
/* #define YTPHY_YT8543_ENABLE */
#ifdef YTPHY_YT8543_ENABLE
#define PHY_ID_YT8543			0x0008011b
#endif
#define PHY_ID_YT8614			0x4f51e899
#define PHY_ID_YT8614Q			0x4f51e8a9
#define PHY_ID_YT8618			0x4f51e889
#define PHY_ID_YT8821			0x4f51ea19
#define PHY_ID_YT8111			0x4f51e8e9
#define PHY_ID_YT8628			0x4f51e8c8
#define PHY_ID_YT8824			0x4f51e8b8

#define REG_PHY_SPEC_STATUS		0x11
#define REG_DEBUG_ADDR_OFFSET		0x1e
#define REG_DEBUG_DATA			0x1f
#define REG_MII_MMD_CTRL		0x0D
#define REG_MII_MMD_DATA		0x0E

#define YT8512_EXTREG_LED0		0x40c0
#define YT8512_EXTREG_LED1		0x40c3

#define YT8512_EXTREG_SLEEP_CONTROL1	0x2027
#define YT8512_EXTENDED_COMBO_CONTROL1	0x4000
#define YT8512_10BT_DEBUG_LPBKS		0x200A

#define YT_SOFTWARE_RESET		0x8000

#define YT8512_LED0_ACT_BLK_IND		0x1000
#define YT8512_LED0_DIS_LED_AN_TRY	0x0001
#define YT8512_LED0_BT_BLK_EN		0x0002
#define YT8512_LED0_HT_BLK_EN		0x0004
#define YT8512_LED0_COL_BLK_EN		0x0008
#define YT8512_LED0_BT_ON_EN		0x0010
#define YT8512_LED1_BT_ON_EN		0x0010
#define YT8512_LED1_TXACT_BLK_EN	0x0100
#define YT8512_LED1_RXACT_BLK_EN	0x0200
#define YT8512_EN_SLEEP_SW_BIT		15

#define YT8522_TX_CLK_DELAY		0x4210
#define YT8522_ANAGLOG_IF_CTRL		0x4008
#define YT8522_DAC_CTRL			0x2057
#define YT8522_INTERPOLATOR_FILTER_1	0x14
#define YT8522_INTERPOLATOR_FILTER_2	0x15
#define YT8522_EXTENDED_COMBO_CTRL_1	0x4000
#define YT8522_TX_DELAY_CONTROL		0x19
#define YT8522_EXTENDED_PAD_CONTROL	0x4001

#define YT8521_EXTREG_SLEEP_CONTROL1	0x27
#define YT8521_EN_SLEEP_SW_BIT		15

#define YTXXXX_SPEED_MODE		0xc000
#define YTXXXX_DUPLEX			0x2000
#define YTXXXX_SPEED_MODE_BIT		14
#define YTXXXX_DUPLEX_BIT		13
#define YTXXXX_AUTO_NEGOTIATION_BIT	12
#define YTXXXX_ASYMMETRIC_PAUSE_BIT	11
#define YTXXXX_PAUSE_BIT		10
#define YTXXXX_LINK_STATUS_BIT		10

#define YT8821_SDS_ASYMMETRIC_PAUSE_BIT	8
#define YT8821_SDS_PAUSE_BIT		7

/* based on yt8521 wol feature config register */
#define YTPHY_UTP_INTR_REG		0x12
#define YTPHY_UTP_INTR_STATUS_REG	0x13
#define YTPHY_INTR_LINK_STATUS		(BIT(11) | BIT(10))
/* WOL Feature Event Interrupt Enable */
#define YTPHY_WOL_FEATURE_INTR		BIT(6)

/* Magic Packet MAC address registers */
#define YTPHY_WOL_FEATURE_MACADDR2_4_MAGIC_PACKET	0xa007
#define YTPHY_WOL_FEATURE_MACADDR1_4_MAGIC_PACKET	0xa008
#define YTPHY_WOL_FEATURE_MACADDR0_4_MAGIC_PACKET	0xa009

#define YTPHY_WOL_FEATURE_REG_CFG	0xa00a
#define YTPHY_WOL_FEATURE_TYPE_CFG	BIT(0)
#define YTPHY_WOL_FEATURE_ENABLE_CFG	BIT(3)
#define YTPHY_WOL_FEATURE_INTR_SEL_CFG	BIT(6)
#define YTPHY_WOL_FEATURE_WIDTH1_CFG	BIT(1)
#define YTPHY_WOL_FEATURE_WIDTH2_CFG	BIT(2)

#define YTPHY_REG_SPACE_UTP		0
#define YTPHY_REG_SPACE_FIBER		2

#define YTPHY_REG_SMI_MUX		0xa000
#define YT8614_REG_SPACE_UTP		0
#define YT8614_REG_SPACE_QSGMII		2
#define YT8614_REG_SPACE_SGMII		3

#define YT8628_PIN_MUX_CFG_REG		0xA017
#define YT8628_INT_PIN_MUX		BIT(11)
#define YT8628_CHIP_MODE_REG		0xa008
#define YT8628_CHIP_MODE		(BIT(7) | BIT(6))
#define YT8628_CHIP_MODE_OFFSET		6
#define YT8628_CHIP_MODE_BASE_ADDR (BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define YT8628_REG_SPACE_QSGMII_OUSGMII 1

#define YT8824_REG_SPACE_UTP		0
#define YT8824_REG_SPACE_SERDES		1

enum ytphy_wol_feature_trigger_type_e {
	YTPHY_WOL_FEATURE_PULSE_TRIGGER,
	YTPHY_WOL_FEATURE_LEVEL_TRIGGER,
	YTPHY_WOL_FEATURE_TRIGGER_TYPE_MAX
};

enum ytphy_wol_feature_pulse_width_e {
	YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_336MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_84MS_PULSE_WIDTH,
	YTPHY_WOL_FEATURE_PULSE_WIDTH_MAX
};

struct ytphy_wol_feature_cfg {
	bool enable;
	int type;
	int width;
};

#if (YTPHY_WOL_FEATURE_ENABLE)
#undef SYS_WAKEUP_BASED_ON_ETH_PKT
#define SYS_WAKEUP_BASED_ON_ETH_PKT	1
#endif

struct yt8xxx_priv {
	u8 polling_mode;
	u8 chip_mode;
	u8 phy_base_addr;
	u8 top_phy_addr;
	u16 data;
};

/* polling mode */
#define YT_PHY_MODE_FIBER 		1	/* fiber mode only */
#define YT_PHY_MODE_UTP			2	/* utp mode only */
#define YT_PHY_MODE_POLL		(YT_PHY_MODE_FIBER | YT_PHY_MODE_UTP)

static int ytxxxx_soft_reset(struct phy_device *phydev);
static int yt861x_soft_reset_paged(struct phy_device *phydev, int reg_space);
static int yt861x_aneg_done_paged(struct phy_device *phydev, int reg_space);

static int yt8821_phyreg_dump(struct phy_device *phydev, char* reg_space);
static int yt8821_csd(struct phy_device *phydev);
static int yt8821_snr(struct phy_device *phydev);
static int yt8531_phyreg_dump(struct phy_device *phydev, char* reg_space);
static int yt8531S_phyreg_dump(struct phy_device *phydev, char* reg_space);
static int yt8531_drv_strength(struct phy_device *phydev);
static int yt8531_csd(struct phy_device *phydev);
static int yt8531_snr(struct phy_device *phydev);
static int yt8531_template(struct phy_device *phydev, char* speed, char* mode,
			   char *tone);
static int yt8821_template(struct phy_device *phydev, char* speed, char* mode,
			   char* tone);
static int yt8531_checker(struct phy_device *phydev, char* mode);
static int yt8531_generator(struct phy_device *phydev, char* mode);
static int yt8531_loopback(struct phy_device *phydev, char* type, char* mode,
			   char* speed);

struct ytphy_features_dbg_map {
	u32 phyid;
	int (*phyreg_dump)(struct phy_device *phydev, char* reg_space);
	int (*drive_strength)(struct phy_device *phydev);
	int (*csd)(struct phy_device *phydev);
	int (*snr)(struct phy_device *phydev);
	int (*template)(struct phy_device *phydev, char* speed, char* mode,
			char *tone);
	int (*checker)(struct phy_device *phydev, char* mode);
	int (*generator)(struct phy_device *phydev, char* mode);
	int (*loopback)(struct phy_device *phydev, char* type, char* mode,
			char* speed);
};

static const struct ytphy_features_dbg_map features_dbg[] = {
	{
		.phyid = PHY_ID_YT8821,
		.phyreg_dump = yt8821_phyreg_dump,
		.drive_strength = NULL,
		.csd = yt8821_csd,
		.snr = yt8821_snr,
		.template = yt8821_template,
		.checker = yt8531_checker,
		.generator = yt8531_generator,
		.loopback = yt8531_loopback,
	}, {
		.phyid = PHY_ID_YT8531,
		.phyreg_dump = yt8531_phyreg_dump,
		.drive_strength = yt8531_drv_strength,
		.csd = yt8531_csd,
		.snr = yt8531_snr,
		.template = yt8531_template,
		.checker = yt8531_checker,
		.generator = yt8531_generator,
		.loopback = yt8531_loopback,
	}, {
		.phyid = PHY_ID_YT8531S,
		.phyreg_dump = yt8531S_phyreg_dump,
		.drive_strength = yt8531_drv_strength,
		.csd = yt8531_csd,
		.snr = yt8531_snr,
		.template = yt8531_template,
		.checker = yt8531_checker,
		.generator = yt8531_generator,
		.loopback = yt8531_loopback,
	},
};

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
static int ytphy_config_init(struct phy_device *phydev)
{
	int val;

	val = phy_read(phydev, 3);

	return 0;
}
#endif


#if (KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE)
static inline void phy_lock_mdio_bus(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	mutex_lock(&phydev->bus->mdio_lock);
#else
	mutex_lock(&phydev->mdio.bus->mdio_lock);
#endif
}

static inline void phy_unlock_mdio_bus(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	mutex_unlock(&phydev->bus->mdio_lock);
#else
	mutex_unlock(&phydev->mdio.bus->mdio_lock);
#endif
}
#endif

#if (KERNEL_VERSION(4, 16, 0) > LINUX_VERSION_CODE)
static inline int __phy_read(struct phy_device *phydev, u32 regnum)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct mii_bus *bus = phydev->bus;
	int addr = phydev->addr;
	return bus->read(bus, phydev->addr, regnum);
#else
	struct mii_bus *bus = phydev->mdio.bus;
	int addr = phydev->mdio.addr;
#endif
	return bus->read(bus, addr, regnum);
}

static inline int __phy_write(struct phy_device *phydev, u32 regnum, u16 val)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct mii_bus *bus = phydev->bus;
	int addr = phydev->addr;
#else
	struct mii_bus *bus = phydev->mdio.bus;
	int addr = phydev->mdio.addr;
#endif
	return bus->write(bus, addr, regnum, val);
}
#endif

static inline int __phy_top_read(struct phy_device *phydev, u32 regnum)
{
	struct yt8xxx_priv *priv = phydev->priv;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct mii_bus *bus = phydev->bus;
#else
	struct mii_bus *bus = phydev->mdio.bus;
#endif
	return bus->read(bus, priv->top_phy_addr, regnum);
}

static inline int __phy_top_write(struct phy_device *phydev, u32 regnum,
				  u16 val)
{
	struct yt8xxx_priv *priv = phydev->priv;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct mii_bus *bus = phydev->bus;
#else
	struct mii_bus *bus = phydev->mdio.bus;
#endif
	return bus->write(bus, priv->top_phy_addr, regnum, val);
}

static int __ytphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;

	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	return __phy_read(phydev, REG_DEBUG_DATA);
}

static int __ytphy_top_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;

	ret = __phy_top_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	return __phy_top_read(phydev, REG_DEBUG_DATA);
}

static int ytphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		goto err_handle;

	ret = __phy_read(phydev, REG_DEBUG_DATA);
	if (ret < 0)
		goto err_handle;

err_handle:
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		goto err_handle;

	ret = __phy_write(phydev, REG_DEBUG_DATA, val);
	if (ret < 0)
		goto err_handle;

err_handle:
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int __ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	return 0;
}

static int __ytphy_top_write_ext(struct phy_device *phydev, u32 regnum,
				 u16 val)
{
	int ret;

	ret = __phy_top_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	ret = __phy_top_write(phydev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	return 0;
}

__attribute__((unused)) static int ytphy_read_mmd(struct phy_device* phydev,
						  u16 device, u16 reg)
{
	int val;

	phy_lock_mdio_bus(phydev);

	__phy_write(phydev, REG_MII_MMD_CTRL, device);
	__phy_write(phydev, REG_MII_MMD_DATA, reg);
	__phy_write(phydev, REG_MII_MMD_CTRL, device | 0x4000);
	val = __phy_read(phydev, REG_MII_MMD_DATA);
	if (val < 0) {
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
		dev_err(&phydev->dev, "error read mmd device(%u) reg (%u)\n",
			device, reg);
#else
		dev_err(&phydev->mdio.dev,
			"error read mmd device(%u) reg (%u)\n", device, reg);
#endif

		goto err_handle;
	}

err_handle:
	phy_unlock_mdio_bus(phydev);

	return val;
}

__attribute__((unused)) static int ytphy_write_mmd(struct phy_device* phydev,
						   u16 device, u16 reg,
						   u16 value)
{
	int ret = 0;

	phy_lock_mdio_bus(phydev);

	__phy_write(phydev, REG_MII_MMD_CTRL, device);
	__phy_write(phydev, REG_MII_MMD_DATA, reg);
	__phy_write(phydev, REG_MII_MMD_CTRL, device | 0x4000);
	__phy_write(phydev, REG_MII_MMD_DATA, value);

	phy_unlock_mdio_bus(phydev);

	return ret;
}

__attribute__((unused)) static int __ytphy_read_mmd(struct phy_device* phydev,
						    u16 device, u16 reg)
{
	int val;

	__phy_write(phydev, REG_MII_MMD_CTRL, device);
	__phy_write(phydev, REG_MII_MMD_DATA, reg);
	__phy_write(phydev, REG_MII_MMD_CTRL, device | 0x4000);
	val = __phy_read(phydev, REG_MII_MMD_DATA);

	return val;
}

__attribute__((unused)) static int __ytphy_write_mmd(struct phy_device* phydev,
						     u16 device, u16 reg,
						     u16 value)
{
	__phy_write(phydev, REG_MII_MMD_CTRL, device);
	__phy_write(phydev, REG_MII_MMD_DATA, reg);
	__phy_write(phydev, REG_MII_MMD_CTRL, device | 0x4000);
	__phy_write(phydev, REG_MII_MMD_DATA, value);

	return 0;
}

static int __ytphy_soft_reset(struct phy_device *phydev)
{
	int ret = 0, val = 0;

	val = __phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;

	ret = __phy_write(phydev, MII_BMCR, val | BMCR_RESET);
	if (ret < 0)
		return ret;

	return ret;
}

static int ytphy_soft_reset(struct phy_device *phydev)
{
	int ret = 0, val = 0;

	val = phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;

	ret = phy_write(phydev, MII_BMCR, val | BMCR_RESET);
	if (ret < 0)
		return ret;

	return ret;
}

#if (YTPHY8531A_XTAL_INIT)
static int yt8531a_xtal_init(struct phy_device *phydev)
{
	int ret = 0;
	int val = 0;
	bool state = false;

	msleep(50);

	do {
		ret = ytphy_write_ext(phydev, 0xa012, 0x88);
		if (ret < 0)
			return ret;

		msleep(100);

		val = ytphy_read_ext(phydev, 0xa012);
		if (val < 0)
			return val;

		usleep_range(10000, 20000);
	} while (val != 0x88);

	ret = ytphy_write_ext(phydev, 0xa012, 0xc8);
	if (ret < 0)
		return ret;

	return ret;
}
#endif

static int yt8512_led_init(struct phy_device *phydev)
{
	int mask;
	int ret;
	int val;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_LED0);
	if (val < 0)
		return val;

	val |= YT8512_LED0_ACT_BLK_IND;

	mask = YT8512_LED0_DIS_LED_AN_TRY | YT8512_LED0_BT_BLK_EN |
		YT8512_LED0_HT_BLK_EN | YT8512_LED0_COL_BLK_EN |
		YT8512_LED0_BT_ON_EN;
	val &= ~mask;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_LED0, val);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_LED1);
	if (val < 0)
		return val;

	val |= YT8512_LED1_BT_ON_EN;

	mask = YT8512_LED1_TXACT_BLK_EN | YT8512_LED1_RXACT_BLK_EN;
	val &= ~mask;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_LED1, val);

	return ret;
}

static int yt8512_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct yt8xxx_priv *priv;
	int chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext(phydev, YT8512_EXTENDED_COMBO_CONTROL1);

	priv->chip_mode = (chip_config & (BIT(1) | BIT(0)));

	return 0;
}

static int yt8512_config_init(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret;
	int val;

	val = ytphy_read_ext(phydev, YT8512_10BT_DEBUG_LPBKS);
	if (val < 0)
		return val;

	val &= ~BIT(10);
	ret = ytphy_write_ext(phydev, YT8512_10BT_DEBUG_LPBKS, val);
	if (ret < 0)
		return ret;

	if (!(priv->chip_mode)) {	/* MII mode */
		val &= ~BIT(15);
		ret = ytphy_write_ext(phydev, YT8512_10BT_DEBUG_LPBKS, val);
		if (ret < 0)
			return ret;
	}

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	ret = yt8512_led_init(phydev);

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8512_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	ytphy_soft_reset(phydev);

	return ret;
}

static int yt8512_read_status(struct phy_device *phydev)
{
	int speed, speed_mode, duplex;
	int val;

	genphy_read_status(phydev);

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	duplex = (val & YTXXXX_DUPLEX) >> YTXXXX_DUPLEX_BIT;
	speed_mode = (val & YTXXXX_SPEED_MODE) >> YTXXXX_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
	case 3:
	default:
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
		speed = -1;
#else
		speed = SPEED_UNKNOWN;
#endif
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

static int yt8522_read_status(struct phy_device *phydev)
{
	int speed, speed_mode, duplex;
	int val;

	genphy_read_status(phydev);

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	if ((val & BIT(10)) >> YTXXXX_LINK_STATUS_BIT) {
		duplex = (val & BIT(13)) >> YTXXXX_DUPLEX_BIT;
		speed_mode = (val & (BIT(15) | BIT(14))) >>
				YTXXXX_SPEED_MODE_BIT;
		switch (speed_mode) {
		case 0:
			speed = SPEED_10;
			break;
		case 1:
			speed = SPEED_100;
			break;
		case 2:
		case 3:
		default:
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
			speed = -1;
#else
			speed = SPEED_UNKNOWN;
#endif
			break;
		}

		phydev->link = 1;
		phydev->speed = speed;
		phydev->duplex = duplex;

		return 0;
	}

	phydev->link = 0;

	return 0;
}

static int yt8522_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct yt8xxx_priv *priv;
	int chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext(phydev, YT8522_EXTENDED_COMBO_CTRL_1);

	priv->chip_mode = (chip_config & (BIT(1) | BIT(0)));

	return 0;
}

static int yt8522_config_init(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret;
	int val;

	val = ytphy_read_ext(phydev, YT8522_EXTENDED_COMBO_CTRL_1);
	if (val < 0)
		return val;

	/* RMII2 mode */
	if (0x2 == (priv->chip_mode)) {
		val |= BIT(4);
		ret = ytphy_write_ext(phydev, YT8522_EXTENDED_COMBO_CTRL_1,
				      val);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, YT8522_TX_DELAY_CONTROL, 0x9f);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, YT8522_EXTENDED_PAD_CONTROL,
				      0x81d4);
		if (ret < 0)
			return ret;
	} else if (0x3 == (priv->chip_mode)) {	/* RMII1 mode */
		val |= BIT(4);
		ret = ytphy_write_ext(phydev, YT8522_EXTENDED_COMBO_CTRL_1,
				      val);
		if (ret < 0)
			return ret;
	}

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, YT8522_TX_CLK_DELAY, 0);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, YT8522_ANAGLOG_IF_CTRL, 0xbf2a);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, YT8522_DAC_CTRL, 0x297f);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, YT8522_INTERPOLATOR_FILTER_1, 0x1FE);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, YT8522_INTERPOLATOR_FILTER_2, 0x1FE);
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8512_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	ytphy_soft_reset(phydev);

	return 0;
}

static int yt8521_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct yt8xxx_priv *priv;
	int chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext(phydev, 0xa001);

	priv->chip_mode = chip_config & 0x7;
	switch (priv->chip_mode) {
	case 1:		/* fiber<>rgmii */
	case 4:
	case 5:
		priv->polling_mode = YT_PHY_MODE_FIBER;
		break;
	case 2:		/* utp/fiber<>rgmii */
	case 6:
	case 7:
		priv->polling_mode = YT_PHY_MODE_POLL;
		break;
	case 3:		/* utp<>sgmii */
	case 0:		/* utp<>rgmii */
	default:
		priv->polling_mode = YT_PHY_MODE_UTP;
		break;
	}

	return 0;
}

#if GMAC_CLOCK_INPUT_NEEDED
static int ytphy_mii_rd_ext(struct mii_bus *bus, int phy_id, u32 regnum)
{
	int ret;
	int val;

	ret = bus->write(bus, phy_id, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	val = bus->read(bus, phy_id, REG_DEBUG_DATA);

	return val;
}

static int ytphy_mii_wr_ext(struct mii_bus *bus, int phy_id, u32 regnum,
			    u16 val)
{
	int ret;

	ret = bus->write(bus, phy_id, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	ret = bus->write(bus, phy_id, REG_DEBUG_DATA, val);

	return ret;
}

static int yt8511_config_dis_txdelay(struct mii_bus *bus, int phy_id)
{
	int ret;
	int val;

	/* disable auto sleep */
	val = ytphy_mii_rd_ext(bus, phy_id, 0x27);
	if (val < 0)
		return val;

	val &= (~BIT(15));

	ret = ytphy_mii_wr_ext(bus, phy_id, 0x27, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_mii_rd_ext(bus, phy_id, 0xc);
	if (val < 0)
		return val;

	/* ext reg 0xc b[7:4]
	 * Tx Delay time = 150ps * N - 250ps
	 */
	val &= ~(0xf << 4);
	ret = ytphy_mii_wr_ext(bus, phy_id, 0xc, val);

	return ret;
}

static int yt8511_config_out_125m(struct mii_bus *bus, int phy_id)
{
	int ret;
	int val;

	/* disable auto sleep */
	val = ytphy_mii_rd_ext(bus, phy_id, 0x27);
	if (val < 0)
		return val;

	val &= (~BIT(15));

	ret = ytphy_mii_wr_ext(bus, phy_id, 0x27, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_mii_rd_ext(bus, phy_id, 0xc);
	if (val < 0)
		return val;

	/* ext reg 0xc.b[2:1]
	 * 00-----25M from pll;
	 * 01---- 25M from xtl;(default)
	 * 10-----62.5M from pll;
	 * 11----125M from pll(here set to this value)
	 */
	val |= (3 << 1);
	ret = ytphy_mii_wr_ext(bus, phy_id, 0xc, val);

#ifdef YT_8511_INIT_TO_MASTER
	/* for customer, please enable it based on demand.
	 * configure to master
	 */
	/* master/slave config reg*/
	val = bus->read(bus, phy_id, 0x9);
	/* to be manual config and force to be master */
	val |= (0x3<<11);
	/* take effect until phy soft reset */
	ret = bus->write(bus, phy_id, 0x9, val);
	if (ret < 0)
		return ret;
#endif

	return ret;
}

static int yt8511_config_init(struct phy_device *phydev)
{
	int ret;

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev, "%s done, phy addr: %d\n",
		    __func__, phydev->addr);
#else
	netdev_info(phydev->attached_dev, "%s done, phy addr: %d\n",
		    __func__, phydev->mdio.addr);
#endif

	ytphy_soft_reset(phydev);

	return ret;
}
#endif /* GMAC_CLOCK_INPUT_NEEDED */

#if (YTPHY_WOL_FEATURE_ENABLE)
static int ytphy_switch_reg_space(struct phy_device *phydev, int space)
{
	int ret;

	if (space == YTPHY_REG_SPACE_UTP)
		ret = ytphy_write_ext(phydev, 0xa000, 0);
	else
		ret = ytphy_write_ext(phydev, 0xa000, 2);

	return ret;
}

static int ytphy_wol_feature_enable_cfg(struct phy_device *phydev,
					struct ytphy_wol_feature_cfg wol_cfg)
{
	int ret = 0;
	int val = 0;

	val = ytphy_read_ext(phydev, YTPHY_WOL_FEATURE_REG_CFG);
	if (val < 0)
		return val;

	if (wol_cfg.enable) {
		val |= YTPHY_WOL_FEATURE_ENABLE_CFG;

		if (wol_cfg.type == YTPHY_WOL_FEATURE_LEVEL_TRIGGER) {
			val &= ~YTPHY_WOL_FEATURE_TYPE_CFG;
			val &= ~YTPHY_WOL_FEATURE_INTR_SEL_CFG;
		} else if (wol_cfg.type == YTPHY_WOL_FEATURE_PULSE_TRIGGER) {
			val |= YTPHY_WOL_FEATURE_TYPE_CFG;
			val |= YTPHY_WOL_FEATURE_INTR_SEL_CFG;

			if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_84MS_PULSE_WIDTH) {
				val &= ~YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val &= ~YTPHY_WOL_FEATURE_WIDTH2_CFG;
			} else if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH) {
				val |= YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val &= ~YTPHY_WOL_FEATURE_WIDTH2_CFG;
			} else if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_336MS_PULSE_WIDTH) {
				val &= ~YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val |= YTPHY_WOL_FEATURE_WIDTH2_CFG;
			} else if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH) {
				val |= YTPHY_WOL_FEATURE_WIDTH1_CFG;
				val |= YTPHY_WOL_FEATURE_WIDTH2_CFG;
			}
		}
	} else {
		val &= ~YTPHY_WOL_FEATURE_ENABLE_CFG;
		val &= ~YTPHY_WOL_FEATURE_INTR_SEL_CFG;
	}

	ret = ytphy_write_ext(phydev, YTPHY_WOL_FEATURE_REG_CFG, val);
	if (ret < 0)
		return ret;

	return 0;
}

static int __yt8824_wol_feature_enable_cfg(struct phy_device *phydev,
					   struct ytphy_wol_feature_cfg wol_cfg)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int port;
	int ret;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	port = phydev->addr - priv->phy_base_addr;
#else
	port = phydev->mdio.addr - priv->phy_base_addr;
#endif
	if (wol_cfg.enable) {
		/* Write_bits top_ext_0xa307[4,1,0] 3'b111
		 * bit0 1'b1 u0_wol_en
		 * bit1 1'b1 u1_wol_en
		 * bit4 1'b0(default) latch level output; 1'b1 pulse output
		 * bit6:5 pulse width
		 * (default 2'b01 168ms pulse width; 2'b00 84ms, 2'b10 336ms)
		 */
		ret = __ytphy_read_ext(phydev, 0xa307);
		if (ret < 0)
			return ret;
		
		if (port == 0 || port == 2)
			ret |= BIT(0);
		else if (port == 1 || port == 3)
			ret |= BIT(1);
		
		ret = __ytphy_write_ext(phydev, 0xa307, ret);
		if (ret < 0)
			return ret;

		/* config interrupt mask */
		ret = __ytphy_top_read_ext(phydev, 0xa019);
		if (ret < 0)
			return ret;
		
		if (port == 0)
			ret |= BIT(0);
		else if (port == 1)
			ret |= BIT(1);
		else if (port == 2)
			ret |= BIT(2);
		else if (port == 3)
			ret |= BIT(3);
		ret = __ytphy_top_write_ext(phydev, 0xa019, ret);
		if (ret < 0)
			return ret;

		/* dpad_intn_wol_od_en bit7 1'b0 default */
		ret = __ytphy_top_read_ext(phydev, 0xa005);
		if (ret < 0)
			return ret;
		
		ret |= BIT(7);
		ret = __ytphy_top_write_ext(phydev, 0xa005, ret);
		if (ret < 0)
			return ret;
		
		/* INTN_WOL bit5 1'b0 default disable */
		ret = __ytphy_top_read_ext(phydev, 0xa01a);
		if (ret < 0)
			return ret;
		
		ret |= BIT(5);
		ret = __ytphy_top_write_ext(phydev, 0xa01a, ret);
		if (ret < 0)
			return ret;

		ret = __ytphy_read_ext(phydev, 0xa307);
		if (ret < 0)
			return ret;
		
		if (wol_cfg.type == YTPHY_WOL_FEATURE_LEVEL_TRIGGER) {
			ret &= ~BIT(4);
		} else if (wol_cfg.type == YTPHY_WOL_FEATURE_PULSE_TRIGGER) {
			ret |= BIT(4);

			if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_84MS_PULSE_WIDTH) {
				ret &= ~BIT(6);
				ret &= ~BIT(5);
			} else if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH) {
				ret &= ~BIT(6);
				ret |= BIT(5);
			} else if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_336MS_PULSE_WIDTH) {
				ret |= BIT(6);
				ret &= ~BIT(5);
			} else if (wol_cfg.width ==
				YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH) {
				ret |= BIT(6);
				ret |= BIT(5);
			}

			/* config wol interrupt type
			 * top 0xa019 bit4 1'b0 level output; 1'b1 pulse output.
			 * if bit4 1'b1 then config pulse width
			 * top 0xa019 bit15:9 for wol_pulse_lth
			 * top 0xa019 bit8:7 for timer_tick_sel
			 * the pulse width below for example:
			 * 10ms pulse width:
			 * wol_pulse_lth = 0x7a, timer_tick_sel = 0x2
			 * 1ms pulse width:
			 * wol_pulse_lth = 0x64, timer_tick_sel = 0x1
			 * 100us pulse width:
			 * wol_pulse_lth = 0x4e, timer_tick_sel = 0x0
			 * 10us pulse width:
			 * wol_pulse_lth = 0x09, timer_tick_sel = 0x0
			 */
		}

		ret = __ytphy_write_ext(phydev, 0xa307, ret);
		if (ret < 0)
			return ret;
	} else {
		ret = __ytphy_read_ext(phydev, 0xa307);
		if (ret < 0)
			return ret;
		
		if (port == 0 || port == 2)
			ret &= ~BIT(0);
		else if (port == 1 || port == 3)
			ret &= ~BIT(1);
		
		ret = __ytphy_write_ext(phydev, 0xa307, ret);
		if (ret < 0)
			return ret;
		
		/* dpad_intn_wol_od_en bit7 1'b0 default disable */
		ret = __ytphy_top_read_ext(phydev, 0xa005);
		if (ret < 0)
			return ret;
		
		ret &= ~BIT(7);
		ret = __ytphy_top_write_ext(phydev, 0xa005, ret);
		if (ret < 0)
			return ret;
		
		/* INTN_WOL bit5 1'b0 default disable */
		ret = __ytphy_top_read_ext(phydev, 0xa01a);
		if (ret < 0)
			return ret;
		
		ret &= ~BIT(5);
		ret = __ytphy_top_write_ext(phydev, 0xa01a, ret);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void ytphy_wol_feature_get(struct phy_device *phydev,
				  struct ethtool_wolinfo *wol)
{
	int val = 0;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	val = ytphy_read_ext(phydev, YTPHY_WOL_FEATURE_REG_CFG);
	if (val < 0)
		return;

	if (val & YTPHY_WOL_FEATURE_ENABLE_CFG)
		wol->wolopts |= WAKE_MAGIC;
}

static int ytphy_wol_feature_set(struct phy_device *phydev,
				 struct ethtool_wolinfo *wol)
{
	struct net_device *p_attached_dev = phydev->attached_dev;
	struct ytphy_wol_feature_cfg wol_cfg;
	int ret, curr_reg_space, val;

	memset(&wol_cfg, 0, sizeof(struct ytphy_wol_feature_cfg));
	curr_reg_space = ytphy_read_ext(phydev, 0xa000);
	if (curr_reg_space < 0)
		return curr_reg_space;

	/* Switch to phy UTP page */
	ret = ytphy_switch_reg_space(phydev, YTPHY_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	if (wol->wolopts & WAKE_MAGIC) {
		/* Enable the WOL feature interrupt */
		val = phy_read(phydev, YTPHY_UTP_INTR_REG);
		val |= YTPHY_WOL_FEATURE_INTR;
		ret = phy_write(phydev, YTPHY_UTP_INTR_REG, val);
		if (ret < 0)
			return ret;

		/* Set the WOL feature config */
		wol_cfg.enable = true;
		wol_cfg.type = YTPHY_WOL_FEATURE_PULSE_TRIGGER;
		wol_cfg.width = YTPHY_WOL_FEATURE_672MS_PULSE_WIDTH;
		ret = ytphy_wol_feature_enable_cfg(phydev, wol_cfg);
		if (ret < 0)
			return ret;

		/* Store the device address for the magic packet */
		ret = ytphy_write_ext(phydev,
				      YTPHY_WOL_FEATURE_MACADDR2_4_MAGIC_PACKET,
				      ((p_attached_dev->dev_addr[0] << 8) |
				      p_attached_dev->dev_addr[1]));
		if (ret < 0)
			return ret;
		ret = ytphy_write_ext(phydev,
				      YTPHY_WOL_FEATURE_MACADDR1_4_MAGIC_PACKET,
				      ((p_attached_dev->dev_addr[2] << 8) |
				      p_attached_dev->dev_addr[3]));
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev,
				      YTPHY_WOL_FEATURE_MACADDR0_4_MAGIC_PACKET,
				      ((p_attached_dev->dev_addr[4] << 8) |
				      p_attached_dev->dev_addr[5]));
		if (ret < 0)
			return ret;
	} else {
		wol_cfg.enable = false;
		wol_cfg.type = YTPHY_WOL_FEATURE_TRIGGER_TYPE_MAX;
		wol_cfg.width = YTPHY_WOL_FEATURE_PULSE_WIDTH_MAX;
		ret = ytphy_wol_feature_enable_cfg(phydev, wol_cfg);
		if (ret < 0)
			return ret;
	}

	/* Recover to previous register space page */
	ret = ytphy_switch_reg_space(phydev, curr_reg_space);
	if (ret < 0)
		return ret;

	return 0;
}

static int yt8824_wol_feature_get(struct phy_device *phydev,
				  struct ethtool_wolinfo *wol)
{
	int ret = 0, oldpage;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	oldpage = ytphy_top_select_page(phydev, YT8824_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	/* INTN_WOL bit5 1'b0 default disable */
	ret = __ytphy_top_read_ext(phydev, 0xa01a);
	if (ret < 0)
		goto err_restore_page;

	if (ret & BIT(5))
		wol->wolopts |= WAKE_MAGIC;

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8824_wol_feature_set(struct phy_device *phydev,
				  struct ethtool_wolinfo *wol)
{
	struct net_device *p_attached_dev = phydev->attached_dev;
	struct yt8xxx_priv *priv = phydev->priv;
	struct ytphy_wol_feature_cfg wol_cfg;
	int ret = 0, oldpage;
	int port;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	port = phydev->addr - priv->phy_base_addr;
#else
	port = phydev->mdio.addr - priv->phy_base_addr;
#endif

	memset(&wol_cfg, 0, sizeof(struct ytphy_wol_feature_cfg));

	oldpage = ytphy_top_select_page(phydev, YT8824_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	if (wol->wolopts & WAKE_MAGIC) {
		/* sample: pulse(low active) trigger */
		if (port == 0 || port == 2) {
			/* port0 and port2 mac addr config */
			ret =
			__ytphy_write_ext(phydev, 0xa301,
					  ((p_attached_dev->dev_addr[0] << 8) |
					  p_attached_dev->dev_addr[1]));
			if (ret < 0)
				goto err_restore_page;
			ret =
			__ytphy_write_ext(phydev, 0xa302,
					  ((p_attached_dev->dev_addr[2] << 8) |
					  p_attached_dev->dev_addr[3]));
			if (ret < 0)
				goto err_restore_page;
			ret =
			__ytphy_write_ext(phydev, 0xa303,
					  ((p_attached_dev->dev_addr[4] << 8) |
					  p_attached_dev->dev_addr[5]));
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 1 || port == 3) {
			/* port1 and port3 mac addr config */
			ret =
			__ytphy_write_ext(phydev, 0xa304,
					  ((p_attached_dev->dev_addr[0] << 8) |
					  p_attached_dev->dev_addr[1]));
			if (ret < 0)
				goto err_restore_page;
			ret =
			__ytphy_write_ext(phydev, 0xa305,
					  ((p_attached_dev->dev_addr[2] << 8) |
					  p_attached_dev->dev_addr[3]));
			if (ret < 0)
				goto err_restore_page;
			ret =
			__ytphy_write_ext(phydev, 0xa306,
					  ((p_attached_dev->dev_addr[4] << 8) |
					  p_attached_dev->dev_addr[5]));
			if (ret < 0)
				goto err_restore_page;
		}

		/* config interrupt polarity(defautl 1'b0 low active) */
		ret = __ytphy_top_read_ext(phydev, 0xa000);
		if (ret < 0)
			goto err_restore_page;

		ret &= ~BIT(5);
		ret = __ytphy_top_write_ext(phydev, 0xa000, ret);
		if (ret < 0)
			goto err_restore_page;

		/* Set the WOL feature config */
		wol_cfg.enable = true;
		wol_cfg.type = YTPHY_WOL_FEATURE_PULSE_TRIGGER;
		wol_cfg.width = YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH;
		ret = __yt8824_wol_feature_enable_cfg(phydev, wol_cfg);
		if (ret < 0)
			goto err_restore_page;

		/* clear wol interrupt state */
		ret = __ytphy_top_read_ext(phydev, 0xa01a);
		if (ret < 0)
			goto err_restore_page;
	} else {
		/* Set the WOL feature config */
		wol_cfg.enable = false;
		wol_cfg.type = YTPHY_WOL_FEATURE_PULSE_TRIGGER;
		wol_cfg.width = YTPHY_WOL_FEATURE_168MS_PULSE_WIDTH;
		ret = __yt8824_wol_feature_enable_cfg(phydev, wol_cfg);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}
#endif /*(YTPHY_WOL_FEATURE_ENABLE)*/

static int yt8521_config_init(struct phy_device *phydev)
{
	int ret;
	int val;

	struct yt8xxx_priv *priv = phydev->priv;

#if (YTPHY_WOL_FEATURE_ENABLE)
	struct ethtool_wolinfo wol;

	/* set phy wol enable */
	memset(&wol, 0x0, sizeof(struct ethtool_wolinfo));
	wol.wolopts |= WAKE_MAGIC;
	ytphy_wol_feature_set(phydev, &wol);
#endif

	ytphy_write_ext(phydev, 0xa000, 0);
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_read_ext(phydev, 0xc);
	if (val < 0)
		return val;
	val &= ~(1 << 12);
	ret = ytphy_write_ext(phydev, 0xc, val);
	if (ret < 0)
		return ret;

	ytxxxx_soft_reset(phydev);

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode = %d, polling mode = %d\n",
		    __func__, phydev->addr,
		    priv->chip_mode, priv->polling_mode);
#else
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode = %d, polling mode = %d\n",
		    __func__, phydev->mdio.addr,
		    priv->chip_mode, priv->polling_mode);
#endif

	return ret;
}

/* for fiber mode, there is no 10M speed mode and
 * this function is for this purpose.
 */
static int ytxxxx_adjust_status(struct phy_device *phydev, int val, int is_utp)
{
	int speed_mode, duplex;
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
	int speed = -1;
#else
	int speed = SPEED_UNKNOWN;
#endif

	if (is_utp)
		duplex = (val & YTXXXX_DUPLEX) >> YTXXXX_DUPLEX_BIT;
	else
		duplex = 1;
	speed_mode = (val & YTXXXX_SPEED_MODE) >> YTXXXX_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		if (is_utp)
			speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
		speed = SPEED_1000;
		break;
	case 3:
		break;
	default:
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
		speed = -1;
#else
		speed = SPEED_UNKNOWN;
#endif
	break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

/* for fiber mode, when speed is 100M, there is no definition for
 * autonegotiation, and this function handles this case and return
 * 1 per linux kernel's polling.
 */
static int yt8521_aneg_done(struct phy_device *phydev)
{
	int link_fiber = 0, link_utp = 0;

	/* reading Fiber */
	ytphy_write_ext(phydev, 0xa000, 2);
	link_fiber = !!(phy_read(phydev, REG_PHY_SPEC_STATUS) &
			(BIT(YTXXXX_LINK_STATUS_BIT)));

	/* reading UTP */
	ytphy_write_ext(phydev, 0xa000, 0);
	if (!link_fiber)
		link_utp = !!(phy_read(phydev, REG_PHY_SPEC_STATUS) &
				(BIT(YTXXXX_LINK_STATUS_BIT)));

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s, phy addr: %d, link_fiber: %d, link_utp: %d\n",
		    __func__, phydev->addr, link_fiber, link_utp);
#else
	netdev_info(phydev->attached_dev,
		    "%s, phy addr: %d, link_fiber: %d, link_utp: %d\n",
		    __func__, phydev->mdio.addr, link_fiber, link_utp);
#endif

	return !!(link_fiber | link_utp);
}

static int yt8521_read_status(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int link_utp = 0, link_fiber = 0;
	int yt8521_fiber_latch_val;
	int yt8521_fiber_curr_val;
	int val_utp, val_fiber;
	int link;
	int ret;
	int lpa;

	phydev->pause = 0;
	phydev->asym_pause = 0;

	if(priv->polling_mode != YT_PHY_MODE_FIBER) {
		/* reading UTP */
		ret = ytphy_write_ext(phydev, 0xa000, 0);
		if (ret < 0)
			return ret;

		val_utp = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val_utp < 0)
			return val_utp;

		link = val_utp & (BIT(YTXXXX_LINK_STATUS_BIT));
		if (link) {
			link_utp = 1;
			ytxxxx_adjust_status(phydev, val_utp, 1);
		} else {
			link_utp = 0;
		}

		if (link_utp) {
			lpa = phy_read(phydev, MII_LPA);
			if (ret < 0)
				return ret;

			phydev->pause = !!(lpa & BIT(10));
			phydev->asym_pause = !!(lpa & BIT(11));
		}
	}

	if (priv->polling_mode != YT_PHY_MODE_UTP) {
		/* reading Fiber */
		ret = ytphy_write_ext(phydev, 0xa000, 2);
		if (ret < 0) {
			ytphy_write_ext(phydev, 0xa000, 0);
			return ret;
		}

		val_fiber = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val_fiber < 0) {
			ytphy_write_ext(phydev, 0xa000, 0);
			return val_fiber;
		}

		/* for fiber, from 1000m to 100m, there is not link down
		 * from 0x11, and check reg 1 to identify such case
		 * this is important for Linux kernel for that, missing linkdown
		 * event will cause problem.
		 */
		yt8521_fiber_latch_val = phy_read(phydev, MII_BMSR);
		yt8521_fiber_curr_val = phy_read(phydev, MII_BMSR);
		link = val_fiber & (BIT(YTXXXX_LINK_STATUS_BIT));
		if (link && yt8521_fiber_latch_val != yt8521_fiber_curr_val) {
			link = 0;
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)            
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, phydev->addr,
				    yt8521_fiber_latch_val,
				    yt8521_fiber_curr_val);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, phydev->mdio.addr,
				    yt8521_fiber_latch_val,
				    yt8521_fiber_curr_val);
#endif
		}

		if (link) {
			link_fiber = 1;
			ytxxxx_adjust_status(phydev, val_fiber, 0);
		} else {
			link_fiber = 0;
		}

		if (link_fiber) {
			lpa = phy_read(phydev, MII_LPA);
			if (ret < 0) {
				ytphy_write_ext(phydev, 0xa000, 0);
				return ret;
			}

			phydev->pause = !!(lpa & BIT(7));
			phydev->asym_pause = !!(lpa & BIT(8));
		}
	}

	if (link_utp || link_fiber) {
		if (phydev->link == 0)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media: %s, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->addr,
				    (link_utp && link_fiber) ?
				    "UNKNOWN MEDIA" :
				    (link_utp ? "UTP" : "Fiber"),
				    link_utp ? (unsigned int)val_utp :
				    (unsigned int)val_fiber);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media: %s, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->mdio.addr,
				    (link_utp && link_fiber) ? 
				    "UNKNOWN MEDIA" :
				    (link_utp ? "UTP" : "Fiber"),
				    link_utp ? (unsigned int)val_utp :
				    (unsigned int)val_fiber);
#endif
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->addr);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->mdio.addr);
#endif
		phydev->link = 0;
	}

	if (priv->polling_mode != YT_PHY_MODE_FIBER) {
		if (link_fiber)
			ytphy_write_ext(phydev, 0xa000, 2);
		else
			ytphy_write_ext(phydev, 0xa000, 0);
	}

	return 0;
}

static int yt8521_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
	mutex_lock(&phydev->lock);
#else
	/* no need lock in 4.19 */
#endif
	struct yt8xxx_priv *priv = phydev->priv;

	if (priv->polling_mode != YT_PHY_MODE_FIBER) {
		ytphy_write_ext(phydev, 0xa000, 0);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);
	}

	if (priv->polling_mode != YT_PHY_MODE_UTP) {
		ytphy_write_ext(phydev, 0xa000, 2);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);
	}

	ytphy_write_ext(phydev, 0xa000, 0);

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif	/*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8521_resume(struct phy_device *phydev)
{
	int value, ret;

	/* disable auto sleep */
	value = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (value < 0)
		return value;

	value &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, value);
	if (ret < 0)
		return ret;

#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
	mutex_lock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
	struct yt8xxx_priv *priv = phydev->priv;

	if (priv->polling_mode != YT_PHY_MODE_FIBER) {
		ytphy_write_ext(phydev, 0xa000, 0);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);
	}

	if (priv->polling_mode != YT_PHY_MODE_UTP) {
		ytphy_write_ext(phydev, 0xa000, 2);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

		ytphy_write_ext(phydev, 0xa000, 0);
	}

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
	mutex_unlock(&phydev->lock);
#else
	/* no need lock/unlock in 4.19 */
#endif
#endif	/*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8531S_config_init(struct phy_device *phydev)
{
	int ret = 0, val = 0;

#if (YTPHY8531A_XTAL_INIT)
	ret = yt8531a_xtal_init(phydev);
	if (ret < 0)
		return ret;
#endif
	ret = ytphy_write_ext(phydev, 0xa023, 0x4031);
	if (ret < 0)
		return ret;

	ytphy_write_ext(phydev, 0xa000, 0x0);
	val = ytphy_read_ext(phydev, 0xf);

	if(0x31 != val && 0x32 != val) {
		ret = ytphy_write_ext(phydev, 0xa071, 0x9007);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0x52, 0x231d);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0x51, 0x04a9);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0x57, 0x274c);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0xa006, 0x10d);
		if (ret < 0)
			return ret;

		if(0x500 == val) {
			val = ytphy_read_ext(phydev, 0xa001);
			if((0x30 == (val & 0x30)) || (0x20 == (val & 0x30))) {
				ret = ytphy_write_ext(phydev, 0xa010, 0xabff);
				if (ret < 0)
					return ret;
			}
		}
	}

	ret = yt8521_config_init(phydev);
	if (ret < 0)
		return ret;

	ytphy_write_ext(phydev, 0xa000, 0x0);

	return ret;
}

static int yt8531_config_init(struct phy_device *phydev)
{
	int ret = 0, val = 0;

#if (YTPHY8531A_XTAL_INIT)
	ret = yt8531a_xtal_init(phydev);
	if (ret < 0)
		return ret;
#endif

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, 0xf);
	if(0x31 != val && 0x32 != val) {
		ret = ytphy_write_ext(phydev, 0x52, 0x231d);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0x51, 0x04a9);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0x57, 0x274c);
		if (ret < 0)
			return ret;

		if(0x500 == val) {
			val = ytphy_read_ext(phydev, 0xa001);
			if((0x30 == (val & 0x30)) || (0x20 == (val & 0x30))) {
				ret = ytphy_write_ext(phydev, 0xa010, 0xabff);
				if (ret < 0)
					return ret;
			}
		}
	}

	ytphy_soft_reset(phydev);

	return 0;
}

#if (KERNEL_VERSION(5, 0, 21) < LINUX_VERSION_CODE)
static void ytphy_link_change_notify(struct phy_device *phydev)
{
	int adv;

	adv = phy_read(phydev, MII_ADVERTISE);
	if (adv < 0)
		return;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
			 phydev->advertising, (adv & ADVERTISE_10HALF));
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
			 phydev->advertising, (adv & ADVERTISE_10FULL));
	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
			 phydev->advertising, (adv & ADVERTISE_100HALF));
	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
			 phydev->advertising, (adv & ADVERTISE_100FULL));

	adv = phy_read(phydev, MII_CTRL1000);
	if (adv < 0)
		return;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
			 phydev->advertising, (adv & ADVERTISE_1000HALF));
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			 phydev->advertising, (adv & ADVERTISE_1000FULL));
}
#endif

#ifdef YTPHY_YT8543_ENABLE
static int yt8543_config_init(struct phy_device *phydev)
{
	int ret = 0, val = 0;

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x403c, 0x286);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xdc, 0x855c);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xdd, 0x6040);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x40e, 0xf00);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x40f, 0xf00);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x411, 0x5030);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x1f, 0x110a);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x20, 0xc06);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x40d, 0x1f);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, 0xa088);
	if (val < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa088,
			      ((val & 0xfff0) | BIT(4)) |
			      (((ytphy_read_ext(phydev, 0xa015) & 0x3c)) >> 2));
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa183, 0x1918);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa184, 0x1818);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa186, 0x2018);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa189, 0x3894);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa187, 0x3838);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa18b, 0x1918);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa18c, 0x1818);
	if (ret < 0)
		return ret;

	ytphy_soft_reset(phydev);

	return 0;
}

static int yt8543_read_status(struct phy_device *phydev)
{
	int link_utp = 0;
	int link;
	int val;

	genphy_read_status(phydev);

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	link = val & (BIT(YTXXXX_LINK_STATUS_BIT));
	if (link) {
		link_utp = 1;
		ytxxxx_adjust_status(phydev, val, 1);
	} else {
		link_utp = 0;
	}

	if (link_utp) {
		if (phydev->link == 0)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media: UTP, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->addr, (unsigned int)val);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media: UTP, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->mdio.addr,
				    (unsigned int)val);
#endif
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->addr);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->mdio.addr);
#endif

		phydev->link = 0;
	}

	return 0;
}
#endif

static int yt8614_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct yt8xxx_priv *priv;
	int chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext(phydev, 0xa007);

	priv->chip_mode = chip_config & 0xf;
	switch (priv->chip_mode) {
	case 8:		/* 4'b1000, Fiber x4 + Copper x4 */
	case 12:	/* 4'b1100, QSGMII x1 + Combo x4 mode; */
	case 13:	/* 4'b1101, QSGMII x1 + Combo x4 mode; */
		priv->polling_mode = (YT_PHY_MODE_FIBER | YT_PHY_MODE_UTP);
		break;
	case 14:	/* 4'b1110, QSGMII x1 + SGMII(MAC) x4 mode; */
	case 11:	/* 4'b1011, QSGMII x1 + Fiber x4 mode; */
		priv->polling_mode = YT_PHY_MODE_FIBER; 
		break;
	case 9:		/* 4'b1001, Reserved. */
	case 10:	/* 4'b1010, QSGMII x1 + Copper x4 mode */
	case 15:	/* 4'b1111, SGMII(PHY) x4 + Copper x4 mode */
	default:
		priv->polling_mode = YT_PHY_MODE_UTP; 
		break;
	}

	return 0;
}

static int yt8614Q_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct yt8xxx_priv *priv;
	int chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext(phydev, 0xa007);

	priv->chip_mode = chip_config & 0xf;
	switch (priv->chip_mode) {
	case 0x1:	/* 4'b0001, QSGMII to 1000BASE-X or 100BASE-FX x 4 */
		priv->polling_mode = YT_PHY_MODE_FIBER; 
		break;
	default:
		break;
	}

	return 0;
}

static int yt8628_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct yt8xxx_priv *priv;
	int chip_config;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext(phydev, YT8628_CHIP_MODE_REG);
	if (chip_config < 0)
		return chip_config;

	priv->chip_mode = (chip_config & YT8628_CHIP_MODE) >>
				YT8628_CHIP_MODE_OFFSET;

	priv->phy_base_addr = chip_config & YT8628_CHIP_MODE_BASE_ADDR;

	priv->data = (~ytphy_read_ext(phydev, 0xa032)) & 0x003f;

	return 0;
}

/* config YT8824 base addr according to Power On Strapping in dts
 * eg,
 * ethernet-phy@4 {
 * 	...
 * 	motorcomm,base-address = <1>;
 * 	...
 * };
 * or
 * #define PHY_BASE_ADDR (1)
 */
#define PHY_BASE_ADDR (1)
static int yt8824_probe(struct phy_device *phydev)
{
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	struct device *dev = &phydev->dev;
#else
	struct device *dev = &phydev->mdio.dev;
#endif
	struct device_node *of_node = dev->of_node;
	struct yt8xxx_priv *priv;
	u8 phy_base_addr;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	if(!of_property_read_u8(of_node, "motorcomm,base-address",
				&phy_base_addr))
		priv->phy_base_addr = phy_base_addr;
	else
		priv->phy_base_addr = PHY_BASE_ADDR;

	priv->top_phy_addr = priv->phy_base_addr + 4;

	return 0;
}

static int yt8618_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0) 
		return ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	return 0;
}

static int yt8628_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = yt861x_soft_reset_paged(phydev, YT8628_REG_SPACE_QSGMII_OUSGMII);
	if (ret < 0) 
		return ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	return 0;
}

static int __ytphy_write_page(struct phy_device *phydev, int page)
{
	return __ytphy_write_ext(phydev, YTPHY_REG_SMI_MUX, page);
}

static int __ytphy_read_page(struct phy_device *phydev)
{
	return __ytphy_read_ext(phydev, YTPHY_REG_SMI_MUX);
}

static int __ytphy_top_write_page(struct phy_device *phydev, int page)
{
	return __ytphy_top_write_ext(phydev, YTPHY_REG_SMI_MUX, page);
}

static int __ytphy_top_read_page(struct phy_device *phydev)
{
	return __ytphy_top_read_ext(phydev, YTPHY_REG_SMI_MUX);
}

static int ytphy_save_page(struct phy_device *phydev)
{
	/* mutex_lock(&phydev->mdio.bus->mdio_lock); */
	phy_lock_mdio_bus(phydev);
	return __ytphy_read_page(phydev);
}

static int ytphy_top_save_page(struct phy_device *phydev)
{
	/* mutex_lock(&phydev->mdio.bus->mdio_lock); */
	phy_lock_mdio_bus(phydev);
	return __ytphy_top_read_page(phydev);
}

static int ytphy_select_page(struct phy_device *phydev, int page)
{
	int ret, oldpage;

	oldpage = ret = ytphy_save_page(phydev);
	if (ret < 0)
		return ret;

	if (oldpage != page) {
		ret = __ytphy_write_page(phydev, page);
		if (ret < 0)
			return ret;
	}

	return oldpage;
}

static int ytphy_restore_page(struct phy_device *phydev, int oldpage, int ret)
{
	int r;

	if (oldpage >= 0) {
		r = __ytphy_write_page(phydev, oldpage);

		/* Propagate the operation return code if the page write
		 * was successful.
		 */
		if (ret >= 0 && r < 0)
			ret = r;
	} else {
		/* Propagate the phy page selection error code */
		ret = oldpage;
	}

	/* mutex_unlock(&phydev->mdio.bus->mdio_lock); */
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int ytphy_top_select_page(struct phy_device *phydev, int page)
{
	int ret, oldpage;

	oldpage = ret = ytphy_top_save_page(phydev);
	if (ret < 0)
		return ret;

	if (oldpage != page) {
		ret = __ytphy_top_write_page(phydev, page);
		if (ret < 0)
			return ret;
	}

	return oldpage;
}

static int ytphy_top_restore_page(struct phy_device *phydev, int oldpage,
				  int ret)
{
	int r;

	if (oldpage >= 0) {
		r = __ytphy_top_write_page(phydev, oldpage);

		/* Propagate the operation return code if the page write
		 * was successful.
		 */
		if (ret >= 0 && r < 0)
			ret = r;
	} else {
		/* Propagate the phy page selection error code */
		ret = oldpage;
	}

	/* mutex_unlock(&phydev->mdio.bus->mdio_lock); */
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int yt861x_soft_reset_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage >= 0)
		ret = __ytphy_soft_reset(phydev);

	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8824_soft_reset_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;

	oldpage = ytphy_top_select_page(phydev, reg_space);
	if (oldpage >= 0)
		ret = __ytphy_soft_reset(phydev);

	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8614_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0) 
		return ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	return 0;
}

static int yt8614Q_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;

	ret = yt861x_soft_reset_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	return 0;
}

static int yt8618_config_init_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	if (reg_space == YT8614_REG_SPACE_UTP) {
		ret = __ytphy_write_ext(phydev, 0x41, 0x33);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x42, 0x66);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x43, 0xaa);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x44, 0xd0d);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x57, 0x234f);
		if (ret < 0)
			goto err_restore_page;
	} else if (reg_space == YT8614_REG_SPACE_QSGMII) {
		ret = __ytphy_write_ext(phydev, 0x3, 0x4F80);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xe, 0x4F80);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8618_config_init(struct phy_device *phydev)
{
	int ret;

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
	ret = ytphy_config_init(phydev);
#else
	ret = genphy_config_init(phydev);
#endif
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa040, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa041, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa042, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa043, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa044, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa045, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa046, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa047, 0xbb00);
	if (ret < 0)
		return ret;

	ret = yt8618_config_init_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt8618_config_init_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;

	yt8618_soft_reset(phydev);

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d\n", __func__, phydev->addr);
#else
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d\n", __func__, phydev->mdio.addr);
#endif

	return ret;
}

static int yt8628_config_init_paged(struct phy_device *phydev, int reg_space)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret = 0, oldpage;
	u16 cali_out;
	int port;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	port = phydev->addr - priv->phy_base_addr;
#else
	port = phydev->mdio.addr - priv->phy_base_addr;
#endif
	cali_out = priv->data;
	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	if (reg_space == YT8628_REG_SPACE_QSGMII_OUSGMII) {
		if (port == 0) {
			ret = __ytphy_read_ext(phydev, 0x1872);
			if (ret < 0)
				goto err_restore_page;
			ret &= (~(BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8)));
			ret |= (cali_out << 8);
			ret |= BIT(15);
			ret = __ytphy_write_ext(phydev, 0x1872, ret);
			if (ret < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0x1871);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1871, ret | BIT(1));
			if (ret < 0)
				goto err_restore_page;

			ret = __ytphy_write_ext(phydev, 0x1865, 0xa73);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1820, 0xa0b);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1860, 0x40c8);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1860, 0x4048);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1873, 0x4844);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1860, 0x40c8);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1860, 0x4048);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 4) {
			ret = __ytphy_read_ext(phydev, 0x1872);
			if (ret < 0)
				goto err_restore_page;
			ret &= (~(BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8)));
			ret |= (cali_out << 8);
			ret |= BIT(15);
			ret = __ytphy_write_ext(phydev, 0x1872, ret);
			if (ret < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0x1871);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1871, ret | BIT(1));
			if (ret < 0)
				goto err_restore_page;

			ret = __ytphy_write_ext(phydev, 0x1873, 0x4844);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1860, 0x40c8);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1860, 0x4048);
			if (ret < 0)
				goto err_restore_page;
		}
	} else if (reg_space == YT8614_REG_SPACE_UTP) {
		ret = __ytphy_write_ext(phydev, 0x50c, 0x8787);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x50d, 0x8787);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x506, 0xc3c3);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x507, 0xc3c3);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x41, 0x4a50);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x42, 0x0da7);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x43, 0x7fff);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x508, 0x12);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x528, 0x8000);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x47, 0x2f);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x500, 0x8080);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x501, 0x8080);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x4035, 0x1007);
		if (ret < 0)
			goto err_restore_page;

		if (port == 0) {
			ret = __ytphy_write_ext(phydev, 0x1228, 0x0);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1042, 0xf);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x1004, 0x12);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x101b, 0x2fff);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x101b, 0x3fff);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 4) {
			ret = __ytphy_write_ext(phydev, 0x2228, 0x0);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x2042, 0xf);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x2004, 0x12);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x201b, 0x2fff);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x201b, 0x3fff);
			if (ret < 0)
				goto err_restore_page;
		}

		if (port == 1) {
			ret = __ytphy_write_ext(phydev, 0x1255, 0x0);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 2) {
			ret = __ytphy_write_ext(phydev, 0x1282, 0x0);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 3) {
			ret = __ytphy_write_ext(phydev, 0x12af, 0x0);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 5) {
			ret = __ytphy_write_ext(phydev, 0x2255, 0x0);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 6) {
			ret = __ytphy_write_ext(phydev, 0x2282, 0x0);
			if (ret < 0)
				goto err_restore_page;
		} else if (port == 7) { 
			ret = __ytphy_write_ext(phydev, 0x22af, 0x0);
			if (ret < 0)
				goto err_restore_page;
		}
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8824_config_init_paged(struct phy_device *phydev, int reg_space)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret = 0, oldpage;
	u16 var_A, var_B;
	int port;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	port = phydev->addr - priv->phy_base_addr;
#else
	port = phydev->mdio.addr - priv->phy_base_addr;
#endif	

	oldpage = ytphy_top_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	if (reg_space == YT8824_REG_SPACE_SERDES) {
		if (port == 0) {
			/* Serdes optimization */
			ret = __ytphy_write_ext(phydev, 0x4be, 0x5);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x406, 0x800);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x48e, 0x3900);
			if (ret < 0)
				goto err_restore_page;

			/* pll calibration */
			ret = __ytphy_write_ext(phydev, 0x400, 0x70);
			if (ret < 0)
				goto err_restore_page;
			/* expect bit3 1'b1 */
			ret = __ytphy_read_ext(phydev, 0x4bc);
			if (ret < 0)
				goto err_restore_page;
			ret = var_B = __ytphy_read_ext(phydev, 0x57);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x449, 0x73e0);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x4ca, 0x330);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x444, 0x8);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x4c5, 0x6409);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x4cc, 0x1100);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x4c1, 0x664);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x442, 0x9881);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x44b, 0x11);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x442, 0x9885);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x442, 0x988d);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x44b, 0x13);
			if (ret < 0)
				goto err_restore_page;
			/* expect bit3 1'b1 */
			ret = __ytphy_read_ext(phydev, 0x4bc);
			if (ret < 0)
				goto err_restore_page;
			ret = var_A = __ytphy_read_ext(phydev, 0x57);
			if (ret < 0)
				goto err_restore_page;

			/* config band val(var_A, var_B) calibrated */
			ret = __ytphy_write_ext(phydev, 0x436, var_A);
			if (ret < 0)
				goto err_restore_page;
			/* sds_ext_0x4cc[8:0] var_B */
			ret = __ytphy_read_ext(phydev, 0x4cc);
			if (ret < 0)
				goto err_restore_page;
			ret = (ret & 0xfe00) | (var_B & 0x01ff);
			ret = __ytphy_write_ext(phydev, 0x4cc, ret);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x4c1, 0x264);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x442, 0x9885);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x442, 0x988d);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x4c1, 0x64);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x400, 0x70);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x49e, 0x200);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x49e, 0x0);
			if (ret < 0)
				goto err_restore_page;
			ret = __ytphy_write_ext(phydev, 0x38, 0x1777);
			if (ret < 0)
				goto err_restore_page;

			/* read pll lock state 3 times with interval 1s
			 * last val is 0x4777 expected.
			 */
			msleep(1000);
			ret = __ytphy_read_ext(phydev, 0x38);
			if (ret < 0)
				goto err_restore_page;
			msleep(1000);
			ret = __ytphy_read_ext(phydev, 0x38);
			if (ret < 0)
				goto err_restore_page;
			msleep(1000);
			ret = __ytphy_read_ext(phydev, 0x38);
			if (ret < 0)
				goto err_restore_page;
			if (ret != 0x4777)
				goto err_restore_page;

			/* configuration for fib chip(support 2.5G)
			 * ret = __ytphy_top_write_ext(phydev, 0xa086, 0x8139);
			 * if (ret < 0)
			 * 	goto err_restore_page;
			 */
		}
	} else if (reg_space == YT8614_REG_SPACE_UTP) {
		if (port == 3) {
			ret = __ytphy_write_ext(phydev, 0xa218, 0x6e);
			if (ret < 0)
				goto err_restore_page;
		}

		ret = __ytphy_write_ext(phydev, 0x1, 0x3);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xa291, 0xffff);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xa292, 0xffff);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x4e2, 0x16c);
		if (ret < 0)
			goto err_restore_page;

		/* 100M template optimization */
		ret = __ytphy_write_ext(phydev, 0x46e, 0x4545);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x46f, 0x4545);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x470, 0x4545);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x471, 0x4545);
		if (ret < 0)
			goto err_restore_page;

		/* 10M template optimization */
		ret = __ytphy_write_ext(phydev, 0x46b, 0x1818);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x46c, 0x1818);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xa2db, 0x7737);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xa2dc, 0x3737);
		if (ret < 0)
			goto err_restore_page;

		/* link signal optimization */
		ret = __ytphy_write_ext(phydev, 0xa003, 0x3);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x34a, 0xff03);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xf8, 0xb3ff);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x32c, 0x5094);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x32d, 0xd094);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x322, 0x6440);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x4d3, 0x5220);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x4d2, 0x5220);
		if (ret < 0)
			goto err_restore_page;
		/* ret = __phy_write(phydev, 0x0, 0x9000);
		 * if (ret < 0)
		 * 	goto err_restore_page;
		 */
	}

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8628_config_init(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret;

	ret = ytphy_write_ext(phydev, 0xa005, 0x4080);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa00f, 0xffff);
	if (ret < 0)
		return ret;

	ret = yt8628_config_init_paged(phydev,
				       YT8628_REG_SPACE_QSGMII_OUSGMII);
	if (ret < 0)
		return ret;

	ret = yt8628_config_init_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt8628_soft_reset(phydev);
	if (ret < 0)
		return ret;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode: %d, phy base addr = %d\n",
		    __func__,
		    phydev->addr, priv->chip_mode, priv->phy_base_addr);
#else
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode: %d, phy base addr = %d\n",
		    __func__,
		    phydev->mdio.addr, priv->chip_mode, priv->phy_base_addr);
#endif
	return 0;
}

static int yt8614_config_init_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	if (reg_space == YT8614_REG_SPACE_UTP) {
		ret = __ytphy_write_ext(phydev, 0x41, 0x33);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x42, 0x66);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x43, 0xaa);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x44, 0xd0d);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x57, 0x2929);
		if (ret < 0)
			goto err_restore_page;
	} else if (reg_space == YT8614_REG_SPACE_QSGMII) {
		ret = __ytphy_write_ext(phydev, 0x3, 0x4F80);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xe, 0x4F80);
		if (ret < 0)
			goto err_restore_page;
	} else if (reg_space == YT8614_REG_SPACE_SGMII) {
		ret = __ytphy_write_ext(phydev, 0x3, 0x2420);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xe, 0x24a0);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0x1be, 0xb01f);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8614_config_init(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret = 0;

	ret = ytphy_write_ext(phydev, 0xa040, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa041, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa042, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa043, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa044, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa045, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa046, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa047, 0xbb00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa08e, 0xf10);
	if (ret < 0)
		return ret;

	ret = yt8614_config_init_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt8614_config_init_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;

	ret = yt8614_config_init_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	yt8614_soft_reset(phydev);

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode: %d, polling mode = %d\n",
		    __func__,
		    phydev->addr, priv->chip_mode, priv->polling_mode);
#else
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode: %d, polling mode = %d\n",
		    __func__,
		    phydev->mdio.addr, priv->chip_mode, priv->polling_mode);
#endif

	return ret;
}

static int yt8614Q_config_init_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	if (reg_space == YT8614_REG_SPACE_QSGMII) {
		ret = __ytphy_write_ext(phydev, 0x3, 0x4F80);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xe, 0x4F80);
		if (ret < 0)
			goto err_restore_page;
	} else if (reg_space == YT8614_REG_SPACE_SGMII) {
		ret = __ytphy_write_ext(phydev, 0x3, 0x2420);
		if (ret < 0)
			goto err_restore_page;
		ret = __ytphy_write_ext(phydev, 0xe, 0x24a0);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8614Q_config_init(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret = 0;

	ret = ytphy_write_ext(phydev, 0xa056, 0x7);
	if (ret < 0)
		return ret;

	ret = yt8614Q_config_init_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;

	ret = yt8614Q_config_init_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	ret = yt8614Q_soft_reset(phydev);
	if (ret < 0)
		return ret;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode: %d, polling mode = %d\n",
		    __func__,
		    phydev->addr, priv->chip_mode, priv->polling_mode);
#else
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, chip mode: %d, polling mode = %d\n",
		    __func__,
		    phydev->mdio.addr, priv->chip_mode, priv->polling_mode);
#endif
	return 0;
}

static int yt8618_aneg_done(struct phy_device *phydev)
{
	return !!(yt861x_aneg_done_paged(phydev, YT8614_REG_SPACE_UTP));
}

static int yt861x_aneg_done_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage >= 0)
		ret = !!(__phy_read(phydev, REG_PHY_SPEC_STATUS) &
				(BIT(YTXXXX_LINK_STATUS_BIT)));

	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8614_aneg_done(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int link_fiber = 0, link_utp = 0;

	if (priv->polling_mode & YT_PHY_MODE_FIBER)
		link_fiber = yt861x_aneg_done_paged(phydev,
						    YT8614_REG_SPACE_SGMII);

	if (priv->polling_mode & YT_PHY_MODE_UTP)
		link_fiber = yt861x_aneg_done_paged(phydev,
						    YT8614_REG_SPACE_UTP);

	return !!(link_fiber | link_utp);
}

static int yt8614Q_aneg_done(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int link_fiber = 0;

	if (priv->polling_mode & YT_PHY_MODE_FIBER) {
		/* reading Fiber */
		link_fiber = yt861x_aneg_done_paged(phydev,
						    YT8614_REG_SPACE_SGMII);
	}

	return !!(link_fiber);
}

static int yt861x_read_status_paged(struct phy_device *phydev, int reg_space,
				    int *status, int *lpa)
{
	int latch_val, curr_val;
	int ret = 0, oldpage;
	int link;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	ret = *lpa = __phy_read(phydev, MII_LPA);
	if (ret < 0)
		goto err_restore_page;

	ret = *status = __phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (ret < 0)
		goto err_restore_page;

	ret = link = !!(*status & BIT(YTXXXX_LINK_STATUS_BIT));

	if (YT8614_REG_SPACE_SGMII == reg_space) {
		/* for fiber, from 1000m to 100m,
		 * there is not link down from 0x11,
		 * and check reg 1 to identify such case
		 */
		latch_val = __phy_read(phydev, MII_BMSR);
		curr_val = __phy_read(phydev, MII_BMSR);
		if (link && latch_val != curr_val) {
			ret = link = 0;
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, phydev->addr,
				    latch_val, curr_val);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, phydev->mdio.addr,
				    latch_val, curr_val);
#endif
		}
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8614_read_status(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int link_utp = 0, link_fiber = 0;
	int val;
	int lpa;

	phydev->pause = 0;
	phydev->asym_pause = 0;
	if (priv->polling_mode & YT_PHY_MODE_UTP) {
		link_utp = yt861x_read_status_paged(phydev,
						    YT8614_REG_SPACE_UTP, &val,
						    &lpa);
		if (link_utp < 0)
			return link_utp;

		if (link_utp)
			ytxxxx_adjust_status(phydev, val, 1);

		phydev->pause = !!(lpa & BIT(10));
		phydev->asym_pause = !!(lpa & BIT(11));
	}

	if (priv->polling_mode & YT_PHY_MODE_FIBER) {
		link_fiber = yt861x_read_status_paged(phydev,
						      YT8614_REG_SPACE_SGMII,
						      &val, &lpa);
		if (link_fiber < 0)
			return link_fiber;

		if (link_fiber)
			ytxxxx_adjust_status(phydev, val, 0);

		phydev->pause = !!(lpa & BIT(7));
		phydev->asym_pause = !!(lpa & BIT(8));
	}

	if (link_utp || link_fiber) {
		if (phydev->link == 0)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media %s\n",
				    __func__, phydev->addr,
				    (link_utp && link_fiber) ?
				    "both UTP and Fiber" :
				    (link_utp ? "UTP" : "Fiber"));
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media %s\n",
				    __func__, phydev->mdio.addr,
				    (link_utp && link_fiber) ?
				    "both UTP and Fiber" :
				    (link_utp ? "UTP" : "Fiber"));
#endif
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->addr);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->mdio.addr);
#endif
		phydev->link = 0;
	}

	if (priv->polling_mode & YT_PHY_MODE_UTP) {
		if (link_utp)
			ytphy_write_ext(phydev, YTPHY_REG_SMI_MUX,
					YT8614_REG_SPACE_UTP);
	}

	return 0;
}

static int yt8614Q_read_status(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int link_fiber = 0;
	int val;
	int lpa;

	phydev->pause = 0;
	phydev->asym_pause = 0;
	if (priv->polling_mode & YT_PHY_MODE_FIBER) {
		/* reading Fiber/sgmii */
		link_fiber = yt861x_read_status_paged(phydev,
						      YT8614_REG_SPACE_SGMII,
						      &val, &lpa);
		if (link_fiber < 0)
			return link_fiber;

		if (link_fiber)
			ytxxxx_adjust_status(phydev, val, 0);

		phydev->pause = !!(lpa & BIT(7));
		phydev->asym_pause = !!(lpa & BIT(8));
	}

	if (link_fiber) {
		if (phydev->link == 0)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media Fiber\n",
				    __func__, phydev->addr);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media Fiber\n",
				    __func__, phydev->mdio.addr);
#endif
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->addr);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->mdio.addr);
#endif
		phydev->link = 0;
	}

	return 0;
}

static int yt8618_read_status(struct phy_device *phydev)
{
	int link_utp;
	int val;
	int lpa;

	phydev->pause = 0;
	phydev->asym_pause = 0;
	link_utp = yt861x_read_status_paged(phydev,
					    YT8614_REG_SPACE_UTP, &val, &lpa);
	if (link_utp < 0)
		return link_utp;

	if (link_utp) {
		phydev->link = 1;
		ytxxxx_adjust_status(phydev, val, 1);
	} else
		phydev->link = 0;

	phydev->pause = !!(lpa & BIT(10));
	phydev->asym_pause = !!(lpa & BIT(11));

	return 0;
}

__attribute__((unused)) static int
yt861x_suspend_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;
	int value;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	ret = value = __phy_read(phydev, MII_BMCR);
	if (ret < 0)
		goto err_restore_page;
	ret = __phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

__attribute__((unused)) static int
yt861x_resume_paged(struct phy_device *phydev, int reg_space)
{
	int ret = 0, oldpage;
	int value;

	oldpage = ytphy_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	ret = value = __phy_read(phydev, MII_BMCR);
	if (ret < 0)
		goto err_restore_page;
	ret = __phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8618_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;
#endif	/*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8618_resume(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;
#endif	/*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8628_suspend(struct phy_device *phydev)
{
	int ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt861x_suspend_paged(phydev, YT8628_REG_SPACE_QSGMII_OUSGMII);
	if (ret < 0)
		return ret;

	return 0;
}

static int yt8628_resume(struct phy_device *phydev)
{
	int ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt861x_resume_paged(phydev, YT8628_REG_SPACE_QSGMII_OUSGMII);
	if (ret < 0)
		return ret;

	return 0;
}

static int yt8614_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;
#endif	/*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8614Q_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	ret = yt861x_suspend_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;
#endif	/*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8614_resume(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;
#endif	/* !(SYS_WAKEUP_BASED_ON_ETH_PKT) */

	return 0;
}

static int yt8614Q_resume(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_SGMII);
	if (ret < 0)
		return ret;

	ret = yt861x_resume_paged(phydev, YT8614_REG_SPACE_QSGMII);
	if (ret < 0)
		return ret;
#endif	/* !(SYS_WAKEUP_BASED_ON_ETH_PKT) */

	return 0;
}

static int ytxxxx_soft_reset(struct phy_device *phydev)
{
	int ret, val;

	val = ytphy_read_ext(phydev, 0xa001);
	ytphy_write_ext(phydev, 0xa001, (val & ~0x8000));

	ret = ytphy_write_ext(phydev, 0xa000, 0);

	return ret;
}

static int yt8824_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = yt8824_soft_reset_paged(phydev, YT8824_REG_SPACE_UTP);
	if (ret < 0) 
		return ret;

	ret = yt8824_soft_reset_paged(phydev, YT8824_REG_SPACE_SERDES);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * utp(1)
 * sds(2)
 * 	internal loopback(1)
 * 	external loopback(2)
 * 	remote loopback(3)
 * 		10M(1)
 * 		100M(2)
 * 		1000M(3)
 * 		2500M(4)
 * eg,
 * utp internal loopback 10M(1 1 1)
 * sds remote loopback 1000M(2 3 3)
 */
static int yt8531_loopback(struct phy_device *phydev, char* type, char* mode,
			   char* speed)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (!strncasecmp(type, "1", 1)) {	/* utp */
		/* utp internal loopback */
		if (!strncasecmp(mode, "1", 1)) {
			oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			if (phydev->phy_id == PHY_ID_YT8821) {
				ret = __ytphy_read_ext(phydev, 0x27);
				if (ret < 0)
					goto err_restore_page;

				ret &= (~BIT(15));
				ret = __ytphy_write_ext(phydev, 0x27, ret);
				if (ret < 0)
					goto err_restore_page;
			}

			if (!strncasecmp(speed, "1", 1)) {
				/* 10M */
				ret = __phy_write(phydev, 0x0, 0x4100);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "2", 1)) {
				/* 100M */
				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_ext(phydev, 0x39e,
								0);
					if (ret < 0)
						goto err_restore_page;

					ret = __ytphy_write_ext(phydev, 0x39f,
								0);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x6100);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "3", 1)) {
				/* 1000M */
				ret = __phy_write(phydev, 0x0, 0x4140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "4", 1) &&
					phydev->phy_id == PHY_ID_YT8821) {
				/* 2500M */
				ret = __phy_write(phydev, 0x0, 0x6140);
				if (ret < 0)
					goto err_restore_page;
			} else
				pr_err("speed(%s) not support.\n", speed);
		} else if (!strncasecmp(mode, "2", 1)) {
			/* utp external loopback */
			oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0x27);
			if (ret < 0)
				goto err_restore_page;

			ret &= ~BIT(15);
			ret = __ytphy_write_ext(phydev, 0x27, ret);
			if (ret < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0xa);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(4);
			ret = __ytphy_write_ext(phydev, 0xa, ret);
			if (ret < 0)
				goto err_restore_page;

			if (!strncasecmp(speed, "1", 1)) {
				/* 10M */
				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __phy_write(phydev, 0x9, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1c41);
					if (ret < 0)
						goto err_restore_page;

					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x0, 0x9000);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x0, 0x8100);
					if (ret < 0)
						goto err_restore_page;
				}
			} else if (!strncasecmp(speed, "2", 1)) {
				/* 100M */
				ret = __phy_write(phydev, 0x0, 0xa100);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "3", 1)) {
				/* 1000M */
				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __phy_write(phydev, 0x10, 0x2);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x8140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "4", 1) &&
					phydev->phy_id == PHY_ID_YT8821) {
				/* 2500M */
				ret = __phy_write(phydev, 0x10, 0x2);
				if (ret < 0)
					goto err_restore_page;

				ret = __phy_write(phydev, 0x0, 0xa140);
				if (ret < 0)
					goto err_restore_page;
			} else
				pr_err("speed(%s) not support.\n", speed);
		} else if (!strncasecmp(mode, "3", 1)) {
			/* utp remote loopback */
			oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0xa006);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(5);
			ret = __ytphy_write_ext(phydev, 0xa006, ret);
			if (ret < 0)
				goto err_restore_page;

			if (!strncasecmp(speed, "1", 1)) {
				/* 10M */
				ret = __phy_write(phydev, 0x9, 0x0);
				if (ret < 0)
					goto err_restore_page;

				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1c41);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x4, 0x1061);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "2", 1)) {
				/* 100M */
				ret = __phy_write(phydev, 0x9, 0x0);
				if (ret < 0)
					goto err_restore_page;

				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1d01);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x4, 0x1181);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "3", 1)) {
				/* 1000M */
				ret = __phy_write(phydev, 0x9, 0x200);
				if (ret < 0)
					goto err_restore_page;

				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1c01);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x4, 0x1001);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "4", 1) &&
					phydev->phy_id == PHY_ID_YT8821) {
				/* 2500M */
				ret = __phy_write(phydev, 0x9, 0x0);
				if (ret < 0)
					goto err_restore_page;

				ret = __phy_write(phydev, 0x4, 0x1c01);
				if (ret < 0)
					goto err_restore_page;

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else
				pr_err("speed(%s) not support.\n", speed);
		} else {
			pr_err("mode(%s) not support.\n", mode);
			return 0;
		}
	} else if (!strncasecmp(type, "2", 1)) {	/* sds */
		/* sds internal loopback */
		if (!strncasecmp(mode, "1", 1) &&
			(phydev->phy_id == PHY_ID_YT8531S ||
			phydev->phy_id == PHY_ID_YT8821)) {
			oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __phy_read(phydev, 0x0);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(14);
			ret = __phy_write(phydev, 0x0, ret);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1) &&
			(phydev->phy_id == PHY_ID_YT8531S ||
			phydev->phy_id == PHY_ID_YT8821)) {
			/* do nothing */
		} else if (!strncasecmp(mode, "3", 1) &&
			(phydev->phy_id == PHY_ID_YT8531S ||
			phydev->phy_id == PHY_ID_YT8821)) {
			/* sds remote loopback */
			oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0xa006);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(6);
			ret = __ytphy_write_ext(phydev, 0xa006, ret);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_read(phydev, 0x0);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(15);
			ret = __phy_write(phydev, 0x0, ret);
			if (ret < 0)
				goto err_restore_page;
		} else {
			pr_err("mode(%s) not support.\n", mode);
			return 0;
		}
	} else {
		pr_err("type(%s) not support.\n", type);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/*
 * utp(1)
 * sds(2)
 * 	internal loopback(1)
 * 	external loopback(2)
 * 	remote loopback(3)
 * 		10M(1)
 * 		100M(2)
 * 		1000M(3)
 * 		2500M(4)
 * eg,
 * utp internal loopback 10M(1 1 1)
 * sds remote loopback 1000M(2 3 3)
 */
static ssize_t yt_loopback_cb(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char speed[8] = {0};
	char type[8] = {0};
	char mode[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(type, token);
	if (strncasecmp(type, "1", 1) && strncasecmp(type, "2", 1)) {
		pr_err("type(%s) not support.\n", type);
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(mode, token);
	if (strncasecmp(mode, "1", 1) && strncasecmp(mode, "2", 1) &&
		strncasecmp(mode, "3", 1)) {
		pr_err("mode(%s) not support.\n", mode);
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(speed, token);
	if (strncasecmp(speed, "1", 1) && strncasecmp(speed, "2", 1) &&
		strncasecmp(speed, "3", 1) && strncasecmp(speed, "4", 1)) {
		pr_err("speed(%s) not support.\n", speed);
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].loopback(phydev, type, mode,
							 speed);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" loopback: echo ethx utp(1)|sgmii(2) internal(1)|external(2)|remote(3) 10M(1)|100M(2)|1000M(3)|2500M(4) > /sys/kernel/debug/yt/loopback\n");
	pr_err(" eg, utp internal loopback 10M, echo ethx 1 1 1 > /sys/kernel/debug/yt/loopback\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(loopback);

/* mode
 * utp start(1)/utp stop(2)/sds start(3)/sds stop(4)/
 */
static int yt8531_generator(struct phy_device *phydev, char* mode)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	/* utp start */
	if (!strncasecmp(mode, "1", 1)) {
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0xa0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0xa0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* start generator */
		ret |= BIT(12);
		ret = __ytphy_write_ext(phydev, 0xa0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

	/* utp stop */
	if (!strncasecmp(mode, "2", 1)) {
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0xa0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0xa0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* stop generator */
		ret &= ~BIT(12);
		ret = __ytphy_write_ext(phydev, 0xa0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

	/* sds start */
	if (!strncasecmp(mode, "3", 1) &&
		(phydev->phy_id == PHY_ID_YT8531S ||
		phydev->phy_id == PHY_ID_YT8821)) {
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0x1a0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0x1a0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* start generator */
		ret |= BIT(12);
		ret = __ytphy_write_ext(phydev, 0x1a0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

	/* sds stop */
	if (!strncasecmp(mode, "4", 1) &&
		(phydev->phy_id == PHY_ID_YT8531S ||
		phydev->phy_id == PHY_ID_YT8821)) {
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0x1a0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0x1a0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* stop generator */
		ret &= ~BIT(12);
		ret = __ytphy_write_ext(phydev, 0x1a0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* mode
 * utp start(1)/utp stop(2)/sds start(3)/sds stop(4)/
 */
static ssize_t yt_generator_cb(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char mode[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(mode, token);
	/* mode
	 * utp start(1)/utp stop(2)/sds start(3)/sds stop(4)/
	 */
	if (strncasecmp(mode, "1", 1) && strncasecmp(mode, "2", 1) &&
		strncasecmp(mode, "3", 1) && strncasecmp(mode, "4", 1)) {
		pr_err("mode(%s) not support.\n", mode);
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].generator(phydev, mode);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" generator: \t\techo ethx utp(1)|sgmii(2)> /sys/kernel/debug/yt/generator\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(generator);

/* mode
 * utp(1)/sds(2)
 */
static int yt8531_checker(struct phy_device *phydev, char* mode)
{
	char chipname[16] = {0};
	int ret = 0, oldpage;
	u16 val_h;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (phydev->phy_id == PHY_ID_YT8531S)
		sprintf(chipname, "%s", "YT8531S");
	else if (phydev->phy_id == PHY_ID_YT8531)
		sprintf(chipname, "%s", "YT8531");
	else if (phydev->phy_id == PHY_ID_YT8821)
		sprintf(chipname, "%s", "YT8821");

	if (!strncasecmp(mode, "1", 1)) {
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable checker ext 0xa0 bit15:14
		 * 2'b10 enable, 2'b01 disable
		 */
		ret = __ytphy_read_ext(phydev, 0xa0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~BIT(14);
		ret = __ytphy_write_ext(phydev, 0xa0, ret);
		if (ret < 0)
			goto err_restore_page;

		/* ib */
		ret = __ytphy_read_ext(phydev, 0xa3);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xa4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xa5);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xa6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xa7);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xa8);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xa9);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib crc err(64-1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xaa);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xab);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xac);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib no sfd: %d\n", chipname, ret);

		/* ob */
		ret = __ytphy_read_ext(phydev, 0xad);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xae);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xaf);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xb0);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xb1);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xb2);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xb3);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob crc err(64-1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xb4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xb5);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xb6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob no sfd: %d\n", chipname, ret);
	}
	
	if (!strncasecmp(mode, "2", 1) &&
		((phydev->phy_id == PHY_ID_YT8531S) ||
		(phydev->phy_id == PHY_ID_YT8821))) {
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable checker ext 0x1a0 bit15:14
		 * 2'b10 enable
		 * 2'b01 disable
		 */
		ret = __ytphy_read_ext(phydev, 0x1a0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~BIT(14);
		ret = __ytphy_write_ext(phydev, 0x1a0, ret);
		if (ret < 0)
			goto err_restore_page;

		/* ib */
		ret = __ytphy_read_ext(phydev, 0x1a3);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1a4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1a5);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1a6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1a7);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1a8);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1a9);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib crc err(64-1518Byte): %d\n",
		       chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1aa);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1ab);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1ac);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib no sfd: %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1ad);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1ae);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1af);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1b0);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1b1);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1b2);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1b3);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob crc err(64-1518Byte): %d\n",
		       chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1b4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1b5);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1b6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob no sfd: %d\n", chipname, ret);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* mode
 * utp(1)/sds(2)
 */
static ssize_t yt_checker_cb(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char mode[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(mode, token);
	if (strncasecmp(mode, "1", 1) && strncasecmp(mode, "2", 1)) {
		pr_err("mode(%s) not support.\n", mode);
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].checker(phydev, mode);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" checker: \t\techo ethx utp(1)|sgmii(2)> /sys/kernel/debug/yt/checker\n");
	pr_err(" eg, echo ethx 1 > /sys/kernel/debug/yt/checker\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(checker);

/* 
 * 10M(1):
 * test_mode
 * 1: packet with all ones, 10MHz sine wave, for harmonic test
 * 2: pseudo random, for TP_idle/Jitter/Different voltage test
 * 3: normal link pulse only
 * 4: 5MHz sine wave
 * 5: Normal mode
 * 
 * 100M(2):
 * test_mode - NOT CARE
 * 
 * 1000BT(3):
 * test_mode
 * 1: Test Mode 1, Transmit waveform test
 * 2: Test Mode 2, Transmit Jitter test (master mode)
 * 3: Test Mode 3, Transmit Jitter test (slave mode)
 * 4: Test Mode 4, Transmit distortion test
 * 
 * 2500BT(4):
 * 1(test mode1)
 * 2(test mode2)
 * 3(test mode3)
 * 4(test mode4)
 * 	1(Dual tone 1)
 * 	2(Dual tone 2)
 * 	3(Dual tone 3)
 * 	4(Dual tone 4)
 * 	5(Dual tone 5)
 * 5(test mode5)
 * 6(test mode6)
 * 7(test mode7)
 */
static int yt8821_template(struct phy_device *phydev, char* speed, char* mode,
			   char* tone)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	if (!strncasecmp(speed, "4", 1)) {
		/* 2500M */
		ret = __ytphy_write_ext(phydev, 0x27, 0x2010);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_mmd(phydev, 0x1, 0x0, 0x2058);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0xa140);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0x2000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "2", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0x4000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "3", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0x6000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "4", 1)) {
			if (!strncasecmp(tone, "1", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x8400);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "2", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x8800);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "3", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x9000);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "4", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x9400);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "5", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x9800);
				if (ret < 0)
					goto err_restore_page;
			}
		} else if(!strncasecmp(mode, "5", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0xa000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "6", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0xc000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "7", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0xe000);
			if (ret < 0)
				goto err_restore_page;
		}
	} else if (!strncasecmp(speed, "3", 1)) {
		/* 1000M */
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0x8140);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __phy_write(phydev, 0x9, 0x2200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __phy_write(phydev, 0x9, 0x5a00);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __phy_write(phydev, 0x9, 0x7200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __phy_write(phydev, 0x9, 0x8200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else if (!strncasecmp(speed, "2", 1)) {
		/* 100M */
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0xa100);
		if (ret < 0)
			goto err_restore_page;
	} else if (!strncasecmp(speed, "1", 1)) {
		/* 10M */
		ret = __phy_write(phydev, 0x0, 0x8100);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x42a, 0x1);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x414, 0xff01);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x4f2, 0x7777);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x209);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20A);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20B);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20C);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "5", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20D);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else
		pr_err("speed(%s) not support.\n", speed);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* 
 * 10M(1):
 * test_mode
 * 1: packet with all ones, 10MHz sine wave, for harmonic test
 * 2: pseudo random, for TP_idle/Jitter/Different voltage test
 * 3: normal link pulse only
 * 4: 5MHz sine wave
 * 5: Normal mode
 * 
 * 100M(2):
 * test_mode - NOT CARE
 * 
 * 1000BT(3):
 * test_mode
 * 1: Test Mode 1, Transmit waveform test
 * 2: Test Mode 2, Transmit Jitter test (master mode)
 * 3: Test Mode 3, Transmit Jitter test (slave mode)
 * 4: Test Mode 4, Transmit distortion test
 * 
 * 2500BT(4):
 * 1(test mode1)
 * 2(test mode2)
 * 3(test mode3)
 * 4(test mode4)
 * 	1(Dual tone 1)
 * 	2(Dual tone 2)
 * 	3(Dual tone 3)
 * 	4(Dual tone 4)
 * 	5(Dual tone 5)
 * 5(test mode5)
 * 6(test mode6)
 * 7(test mode7)
 */
static int yt8531_template(struct phy_device *phydev, char* speed, char* mode,
			   char* tone)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	if (!strncasecmp(speed, "3", 1)) {
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0x8140);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __phy_write(phydev, 0x9, 0x2200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __phy_write(phydev, 0x9, 0x5a00);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __phy_write(phydev, 0x9, 0x7200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __phy_write(phydev, 0x9, 0x8200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else if (!strncasecmp(speed, "2", 1)) {
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0xa100);
		if (ret < 0)
			goto err_restore_page;
	} else if (!strncasecmp(speed, "1", 1)) {
		ret = __phy_write(phydev, 0x0, 0x8100);
		if (ret < 0)
			goto err_restore_page;

		if (phydev->phy_id == PHY_ID_YT8531S) {
			ret = __ytphy_write_ext(phydev, 0xa071, 0x9007);
			if (ret < 0)
				goto err_restore_page;
		}

		if (!strncasecmp(mode, "1", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x209);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20A);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20B);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20C);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "5", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20D);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else
		pr_err("speed(%s) not support.\n", speed);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* 
 * 10M(1):
 * test_mode
 * 1: packet with all ones, 10MHz sine wave, for harmonic test
 * 2: pseudo random, for TP_idle/Jitter/Different voltage test
 * 3: normal link pulse only
 * 4: 5MHz sine wave
 * 5: Normal mode
 * 
 * 100M(2):
 * test_mode - NOT CARE
 * 
 * 1000BT(3):
 * test_mode
 * 1: Test Mode 1, Transmit waveform test
 * 2: Test Mode 2, Transmit Jitter test (master mode)
 * 3: Test Mode 3, Transmit Jitter test (slave mode)
 * 4: Test Mode 4, Transmit distortion test
 * 
 * 2500BT(4):
 * 1(test mode1)
 * 2(test mode2)
 * 3(test mode3)
 * 4(test mode4)
 * 	1(Dual tone 1)
 * 	2(Dual tone 2)
 * 	3(Dual tone 3)
 * 	4(Dual tone 4)
 * 	5(Dual tone 5)
 * 5(test mode5)
 * 6(test mode6)
 * 7(test mode7)
 */
static ssize_t yt_template_cb(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char speed[8] = {0};
	char mode[8] = {0};
	char tone[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(speed, token);
	if (strncasecmp(speed, "1", 1) && strncasecmp(speed, "2", 1) &&
		strncasecmp(speed, "3", 1) && strncasecmp(speed, "4", 1)) {
		pr_err("speed(%s) not support.\n", speed);
		goto error;
	}

	if (strncasecmp(speed, "2", 1)) {	/* 100M not care test mode */
		token = strsep(&pos, DELIMITER);
		if (!token) {
			pr_err("register space is not specified.\n");
			goto error;
		}

		strcpy(mode, token);

		if (!strncasecmp(speed, "1", 1))	/* 10M */
			if (strncasecmp(mode, "1", 1) &&
				strncasecmp(mode, "2", 1) &&
				strncasecmp(mode, "3", 1) &&
				strncasecmp(mode, "4", 1) &&
				strncasecmp(mode, "5", 1)) {
				pr_err("speed(%s) mode(%s) not support.\n",
				       speed, mode);
				goto error;
			}

		if (!strncasecmp(speed, "3", 1))	/* 1000M */
			if (strncasecmp(mode, "1", 1) &&
				strncasecmp(mode, "2", 1) &&
				strncasecmp(mode, "3", 1) &&
				strncasecmp(mode, "4", 1)) {
				pr_err("speed(%s) mode(%s) not support.\n",
				       speed, mode);
				goto error;
			}

		if (!strncasecmp(speed, "4", 1))	/* 2500M */
			if (strncasecmp(mode, "1", 1) &&
				strncasecmp(mode, "2", 1) &&
				strncasecmp(mode, "3", 1) &&
				strncasecmp(mode, "4", 1) &&
				strncasecmp(mode, "5", 1) &&
				strncasecmp(mode, "6", 1) &&
				strncasecmp(mode, "7", 1)) {
				pr_err("speed(%s) mode(%s) not support.\n",
				       speed, mode);
				goto error;
			}
	}

	if (!strncasecmp(speed, "4", 1) && !strncasecmp(mode, "4", 1)) {
		/* 2500M test mode4 */
		token = strsep(&pos, DELIMITER);
		if (!token) {
			pr_err("register space is not specified.\n");
			goto error;
		}

		strcpy(tone, token);
		if (strncasecmp(tone, "1", 1) &&
			strncasecmp(tone, "2", 1) &&
			strncasecmp(tone, "3", 1) &&
			strncasecmp(tone, "4", 1) &&
			strncasecmp(tone, "5", 1)) {
			pr_err("speed(%s) mode(%s) tone(%s) not support.\n",
			       speed, mode, tone);
			goto error;
		}
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].template(phydev, speed, mode,
							 tone);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n"
	       "template(  10M): test mode1(1) - packet with all ones, 10MHz sine wave, for harmonic test\n"
	       "template(  10M): test mode1(2) - pseudo random, for TP_idle/Jitter/Different voltage test\n"
	       "template(  10M): test mode1(3) - normal link pulse only\n"
	       "template(  10M): test mode1(4) - 5MHz sine wave\n"
	       "template(  10M): test mode1(5) - Normal mode\n"
	       "template(  10M): echo ethx 10M(1) test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4)|test mode5(5) > /sys/kernel/debug/yt/template\n");
	pr_err("template( 100M): echo ethx 100M(2) > /sys/kernel/debug/yt/template\n");
	pr_err("template(1000M): test mode1(1) - Transmit waveform test\n"
	       "template(1000M): test mode1(2) - Transmit Jitter test (master mode)\n"
	       "template(1000M): test mode1(3) - Transmit Jitter test (slave mode)\n"
	       "template(1000M): test mode1(4) - Transmit distortion test\n"
	       "template(1000M): echo ethx 1000M(3) test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4) > /sys/kernel/debug/yt/template\n");
	pr_err("template(2500M): echo ethx 2500M(4) test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4)|test mode5(5)|test mode6(6)|test mode7(7)\n"
	       "\t[dual tone1(1)|dual tone2(2)|dual tone3(3)|dual tone4(4)|dual tone5(5)] > /sys/kernel/debug/yt/template\n");
	pr_err("echo ethx 10M(1)|100M(2)|1000M(3)|2500M(4) [test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4)|test mode5(5)|test mode6(6)|test mode7(7)]\n"
	       "\t[dual tone1(1)|dual tone2(2)|dual tone3(3)|dual tone4(4)|dual tone5(5)] > /sys/kernel/debug/yt/template\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(template);

static inline unsigned int log10_kernel(unsigned int x)
{
	unsigned int result = 0;
	while (x >= 10) {
		x /= 10;
		result++;
	}

	return result;
}

static int yt8821_snr(struct phy_device *phydev)
{
	u8 snr0 = 0, snr1 = 0, snr2 = 0, snr3 = 0;
	u16 mse0, mse1, mse2, mse3;
	int ret = 0, oldpage;
	
#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* read utp ext 0x51F, 0x520, 0x521, 0x522 bit15:0
	 * and get 4 pair mse of mdi
	 */
	ret = __ytphy_read_ext(phydev, 0x51F);
	if (ret < 0)
		goto err_restore_page;
	mse0 = ret;

	ret = __ytphy_read_ext(phydev, 0x520);
	if (ret < 0)
		goto err_restore_page;
	mse1 = ret;

	ret = __ytphy_read_ext(phydev, 0x521);
	if (ret < 0)
		goto err_restore_page;
	mse2 = ret;

	ret = __ytphy_read_ext(phydev, 0x522);
	if (ret < 0)
		goto err_restore_page;
	mse3 = ret;

	/* 2500M snr: 10*log10(87040/mse)
	 * 1000M snr: 10*log10(29696/mse)
	 * 100M snr: 10*log10(32768/mse)
	 */	
	if (phydev->speed == SPEED_2500) {
		if (mse0)
			snr0 = (u8)(10 * log10_kernel(87040 / mse0));
		if (mse1)
			snr1 = (u8)(10 * log10_kernel(87040 / mse1));
		if (mse2)
			snr2 = (u8)(10 * log10_kernel(87040 / mse2));
		if (mse3)
			snr3 = (u8)(10 * log10_kernel(87040 / mse3));
	} else if (phydev->speed == SPEED_1000) {
		if (mse0)
			snr0 = (u8)(10 * log10_kernel(29696 / mse0));
		if (mse1)
			snr1 = (u8)(10 * log10_kernel(29696 / mse1));
		if (mse2)
			snr2 = (u8)(10 * log10_kernel(29696 / mse2));
		if (mse3)
			snr3 = (u8)(10 * log10_kernel(29696 / mse3));
	} else if (phydev->speed == SPEED_100) {
		if (mse0)
			snr0 = (u8)(10 * log10_kernel(32768 / mse0));
		if (mse1)
			snr1 = (u8)(10 * log10_kernel(32768 / mse1));
		if (mse2)
			snr2 = (u8)(10 * log10_kernel(32768 / mse2));
		if (mse3)
			snr3 = (u8)(10 * log10_kernel(32768 / mse3));
	}

	pr_err("snr0: %d, snr1: %d, snr2: %d, snr3: %d\n",
	       snr0, snr1, snr2, snr3);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8531_snr(struct phy_device *phydev)
{
	u8 snr0 = 0, snr1 = 0, snr2 = 0, snr3 = 0;
	u16 mse0, mse1, mse2, mse3;
	int ret = 0, oldpage;
	
#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set utp ext 0x59 bit15 */
	ret = __ytphy_read_ext(phydev, 0x59);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0x59, ret | BIT(15));
	if (ret < 0)
		goto err_restore_page;

	/* clear utp ext 0xf8 bit10 */
	ret = __ytphy_read_ext(phydev, 0xf8);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0xf8, ret & (~BIT(10)));
	if (ret < 0)
		goto err_restore_page;

	/* wait 500ms */
	mdelay(500);

	/* read utp ext 0x5a, 0x5b, 0x5c, 0x5d bit14:0 */
	ret = __ytphy_read_ext(phydev, 0x5a);
	if (ret < 0)
		goto err_restore_page;
	mse0 = ret & (~BIT(15));

	ret = __ytphy_read_ext(phydev, 0x5b);
	if (ret < 0)
		goto err_restore_page;
	mse1 = ret & (~BIT(15));

	ret = __ytphy_read_ext(phydev, 0x5c);
	if (ret < 0)
		goto err_restore_page;
	mse2 = ret & (~BIT(15));

	ret = __ytphy_read_ext(phydev, 0x5d);
	if (ret < 0)
		goto err_restore_page;
	mse3 = ret & (~BIT(15));

	/* 1000M snr: 10*log10(29696/mse)
	 * 100M snr: 10*log10(32768/mse)
	 */
	if (phydev->speed == SPEED_1000) {
		if (mse0)
			snr0 = (u8)(10 * log10_kernel(29696 / mse0));
		if (mse1)
			snr1 = (u8)(10 * log10_kernel(29696 / mse1));
		if (mse2)
			snr2 = (u8)(10 * log10_kernel(29696 / mse2));
		if (mse3)
			snr3 = (u8)(10 * log10_kernel(29696 / mse3));
	} else if (phydev->speed == SPEED_100) {
		if (mse0)
			snr0 = (u8)(10 * log10_kernel(32768 / mse0));
		if (mse1)
			snr1 = (u8)(10 * log10_kernel(32768 / mse1));
		if (mse2)
			snr2 = (u8)(10 * log10_kernel(32768 / mse2));
		if (mse3)
			snr3 = (u8)(10 * log10_kernel(32768 / mse3));
	}

	pr_err("snr0: %d, snr1: %d, snr2: %d, snr3: %d\n",
	       snr0, snr1, snr2, snr3);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static ssize_t yt_snr_cb(struct file *file, const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].snr(phydev);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" snr: \t\techo ethx > /sys/kernel/debug/yt/snr\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(snr);

static int yt8821_csd(struct phy_device *phydev)
{
	u8 ch3_state, ch2_state, ch1_state, ch0_state;
	int ret = 0, oldpage;
	u16 distance;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set utp ext 0x80 val 0x468b */
	ret = __ytphy_write_ext(phydev, 0x80, 0x468b);
	if (ret < 0)
		goto err_restore_page;

	/* till utp ext 0x84 bit15 1'b0, csd finished. */
	do {
		ret = __ytphy_read_ext(phydev, 0x84);
		if (ret < 0)
			goto err_restore_page;

		usleep_range(10000, 20000);
	} while (ret & BIT(15));

	/* ext reg 0x85 open & short detect
	 * bit15:14 self_st_33_1
	 * bit13:12 self_st_33_2
	 * bit11:10 self_st_22_1
	 * bit9:8 self_st_22_2
	 * bit7:6 self_st_11_1
	 * bit5:4 self_st_11_2
	 * bit3:2 self_st_00_1
	 * bit1:0 self_st_00_2
	 * 2'b00 normal, 2'b01 error happened during last csd test,
	 * 2'b10 short, 2'b11 open
	 */
	ret = __ytphy_read_ext(phydev, 0x85);
	if (ret < 0)
		goto err_restore_page;

	ch3_state = (ret & 0xc000) >> 14;	/* self_st_33_1 */
	ch2_state = (ret & 0x0c00) >> 10;	/* self_st_22_1 */
	ch1_state = (ret & 0x00c0) >> 6;	/* self_st_11_1 */
	ch0_state = (ret & 0x000c) >> 2;	/* self_st_00_1 */

	if (ch3_state == 0) {
		pr_err("channel 3 is normal\n");
	} else if (ch3_state == 1) {
		pr_err("channel 3 error happened during last csd test.\n");
	} else {
		/* ext reg 0x90, 
		 * The intra pair damage location of channel 3. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x90);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 3: %d(cm).\n",
		       ch3_state == 2 ? "short" :
		       ch3_state == 3 ? "open" : "other",
		       distance);
	}

	if (ch2_state == 0) {
		pr_err("channel 2 is normal\n");
	} else if (ch2_state == 1) {
		pr_err("channel 2 error happened during last csd test.\n");
	} else {
		/* ext reg 0x8e, 
		 * The intra pair damage location of channel 2. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x8e);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 2: %d(cm).\n",
		       ch2_state == 2 ? "short" :
		       ch2_state == 3 ? "open" : "other",
		       distance);
	}

	if (ch1_state == 0) {
		pr_err("channel 1 is normal\n");
	} else if (ch1_state == 1) {
		pr_err("channel 1 error happened during last csd test.\n");
	} else {
		/* ext reg 0x8c, 
		 * The intra pair damage location of channel 1. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x8c);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 1: %d(cm).\n",
		       ch1_state == 2 ? "short" :
		       ch1_state == 3 ? "open" : "other",
		       distance);
	}

	if (ch0_state == 0) {
		pr_err("channel 0 is normal\n");
	} else if (ch0_state == 1) {
		pr_err("channel 0 error happened during last csd test.\n");
	} else {
		/* ext reg 0x8a, 
		 * The intra pair damage location of channel 0. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x8a);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 0: %d(cm).\n",
		       ch0_state == 2 ? "short" :
		       ch0_state == 3 ? "open" : "other",
		       distance);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8531_csd(struct phy_device *phydev)
{
	u8 ch3_state, ch2_state, ch1_state, ch0_state;
	int ret = 0, oldpage;
	u16 distance;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set utp ext 0x98 val 0xb0a6 */
	ret = __ytphy_write_ext(phydev, 0x98, 0xb0a6);
	if (ret < 0)
		goto err_restore_page;

	/* clear utp ext 0x27 bit15 */
	ret = __ytphy_read_ext(phydev, 0x27);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0x27, ret & (~BIT(15)));
	if (ret < 0)
		goto err_restore_page;

	/* set utp mii 0x0 0x8000 */
	ret = __phy_write(phydev, 0x0, 0x8000);
	if (ret < 0)
		goto err_restore_page;

	/* set utp ext 0x80 bit0 */
	ret = __ytphy_read_ext(phydev, 0x80);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0x80, ret | BIT(0));
	if (ret < 0)
		goto err_restore_page;

	/* till utp ext 0x84 bit15 1'b0, csd finished.
	 * read utp ext 0x84 bit7:0, check normal/open/short of pair
	 */
	do {
		ret = __ytphy_read_ext(phydev, 0x84);
		if (ret < 0)
			goto err_restore_page;

		usleep_range(10000, 20000);
	} while (ret & BIT(15));

	ret = __ytphy_read_ext(phydev, 0x84);
	if (ret < 0)
		goto err_restore_page;

	/* bit7:6 Intra pair status of channel 3
	 * bit5:4 Intra pair status of channel 2
	 * bit3:2 Intra pair status of channel 1
	 * bit1:0 Intra pair status of channel 0
	 * 2'b00 normal, 2'b01 error happened during last csd test,
	 * 2'b10 short, 2'b11 open
	 * read ext 0x87 ~ 0x8a to get pair0 ~ pair3 the distance of damage(cm)
	 */
	ch3_state = (ret & 0xc0) >> 6;
	ch2_state = (ret & 0x30) >> 4;
	ch1_state = (ret & 0x0c) >> 2;
	ch0_state = (ret & 0x03);

	if (ch3_state == 0) {
		pr_err("channel 3 is normal\n");
	} else if (ch3_state == 1) {
		pr_err("channel 3 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x8a);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 3 damage(%s) at %d(cm)\n",
			ch3_state == 2 ? "short" :
			ch3_state == 3 ? "open" : "other",
			distance);
	}

	if (ch2_state == 0) {
		pr_err("channel 2 is normal\n");
	} else if (ch2_state == 1) {
		pr_err("channel 2 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x89);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 2 damage(%s) at %d(cm)\n",
			ch2_state == 2 ? "short" :
			ch2_state == 3 ? "open" : "other",
			distance);
	}

	if (ch1_state == 0) {
		pr_err("channel 1 is normal\n");
	} else if (ch1_state == 1) {
		pr_err("channel 1 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x88);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 1 damage(%s) at %d(cm)\n",
			ch1_state == 2 ? "short" :
			ch1_state == 3 ? "open" : "other",
			distance);
	}

	if (ch0_state == 0) {
		pr_err("channel 0 is normal\n");
	} else if (ch0_state == 1) {
		pr_err("channel 0 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x87);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 0 damage(%s) at %d(cm)\n",
			ch0_state == 2 ? "short" :
			ch0_state == 3 ? "open" : "other",
			distance);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static ssize_t yt_csd_cb(struct file *file, const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].csd(phydev);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" csd: \t\techo ethx > /sys/kernel/debug/yt/csd\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(csd);

static int yt8531_drv_strength(struct phy_device *phydev)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __ytphy_read_ext(phydev, 0xa010);
	if (ret < 0)
		goto err_restore_page;
	/*
	 * ext reg 0xa010
	 * bit15:13 rgmii rxc
	 * bit12 bit5:4 rgmii rxd rx_ctl
	 * 111 max, 000 min
	 */
	ret = __ytphy_write_ext(phydev, 0xa010, ret | 0xf030);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static ssize_t yt_drv_strength_cb(struct file *file,
				  const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].drive_strength(phydev);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" rgmii drive strength: \t\techo ethx > /sys/kernel/debug/yt/dump\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(drv_strength);

static int yt8531_phyreg_dump(struct phy_device *phydev, char* reg_space)
{
	int ret = 0, oldpage;
	u8 idx;

#define UTP_REG_SPACE	0x0

	if (!strncasecmp(reg_space, "utp", 3)) {
		pr_err("utp reg space: \n");
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x20; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __ytphy_read_ext(phydev, 0xa001);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa001: \t0x%04x\n", ret);

		ret = __ytphy_read_ext(phydev, 0xa003);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa003: \t0x%04x\n", ret);
	} else {
		pr_err("reg space(%s) not support\n", reg_space);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8531S_phyreg_dump(struct phy_device *phydev, char* reg_space)
{
	int ret = 0, oldpage;
	u8 idx;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (!strncasecmp(reg_space, "utp", 3)) {
		pr_err("utp reg space: \n");
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x20; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __ytphy_read_ext(phydev, 0xa001);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa001: \t0x%04x\n", ret);

		ret = __ytphy_read_ext(phydev, 0xa003);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa003: \t0x%04x\n", ret);
	} else if (!strncasecmp(reg_space, "sgmii", 5)) {
		pr_err("sgmii reg space: \n");
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x9; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __phy_read(phydev, 0xf);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0xf, ret);

		ret = __phy_read(phydev, 0x11);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0x11, ret);

		for (idx = 0x14; idx < 0x16; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}
	} else {
		pr_err("reg space(%s) not support\n", reg_space);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8821_phyreg_dump(struct phy_device *phydev, char* reg_space)
{
	int ret = 0, oldpage;
	u8 idx;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (!strncasecmp(reg_space, "utp", 3)) {
		pr_err("utp reg space: \n");
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x20; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __ytphy_read_ext(phydev, 0xa001);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa001: \t0x%04x\n", ret);

		ret = __ytphy_read_ext(phydev, 0xa003);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa003: \t0x%04x\n", ret);
	} else if (!strncasecmp(reg_space, "sgmii", 5)) {
		pr_err("sgmii reg space: \n");
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x9; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __phy_read(phydev, 0xf);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0xf, ret);

		ret = __phy_read(phydev, 0x11);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0x11, ret);

		for (idx = 0x14; idx < 0x18; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}
	} else {
		pr_err("reg space(%s) not support\n", reg_space);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/*
 * ethx udp
 * ethx sds
 */
static ssize_t yt_phyreg_dump_cb(struct file *file, const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char reg_space[16] = {0};
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(reg_space, token);
	if (strncasecmp(reg_space, "utp", 3) &&
			strncasecmp(reg_space, "sgmii", 5) &&
			strncasecmp(reg_space, "qsgmii", 6))
		goto error;

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].phyreg_dump(phydev, reg_space);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" reg dump: \t\techo ethx reg_space(utp|sgmii|qsgmii) > /sys/kernel/debug/yt/dump\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(phyreg_dump);

/* ethx mii read reg(0x)
 * ethx mii write reg(0x) val(0x)
 * ethx ext read reg(0x)
 * ethx ext write reg(0x) val(0x)
 * ethx mmd read  mmd(0x) reg(0x)
 * ethx mmd write mmd(0x) reg(0x) val(0x)
 */
static ssize_t yt_phyreg_cb(struct file *file, const char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char type[8] = {0};
	char eth[8] = {0};
	char cmd[8] = {0};
	u16 regAddr = 0;
	u16 regData = 0;
	u16 device = 0;
	char *token;
	char *pos;
	int ret;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) 
		goto error;

	strcpy(type, token);
	if (strncasecmp(type, "mii", 3) && strncasecmp(type, "ext", 3) &&
			strncasecmp(type, "mmd", 3))
		goto error;

	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(cmd, token);
	if (strncasecmp(type, "mmd", 3) == 0) {
		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &device);
		if (ret) {
			pr_err("Failed to convert device to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}
	}

	if (strncasecmp(cmd, "write", 5) == 0) {
		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &regAddr);
		if (ret) {
			pr_err("Failed to convert regAddr to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}

		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &regData);
		if (ret) {
			pr_err("Failed to convert regData to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}

		if (strncasecmp(type, "mmd", 3) == 0) {
			pr_err("write mmd = 0x%x reg = 0x%x val = 0x%x\n",
				device, regAddr, regData);
			ytphy_write_mmd(phydev, device, regAddr, regData);
		} else {
			if (strncasecmp(type, "mii", 3) == 0) {
				phy_write(phydev, regAddr, regData);
				pr_err("write mii reg = 0x%x val = 0x%x\n",
				       regAddr, regData);
			} else if (strncasecmp(type, "ext", 3) == 0) {
				ytphy_write_ext(phydev, regAddr, regData);
				pr_err("write ext reg = 0x%x val = 0x%x\n",
				       regAddr, regData);
			}
		}
	} else if (strncasecmp(cmd, "read", 4) == 0) {
		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &regAddr);
		if (ret) {
			pr_err("Failed to convert regAddr to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}

		if (strncasecmp(type, "mmd", 3) == 0) {
			regData = ytphy_read_mmd(phydev, device, regAddr);
			pr_err("read mmd = 0x%x, reg = 0x%x, val = 0x%x\n",
			       device, regAddr, regData);
		} else {
			if (strncasecmp(type, "mii", 3) == 0)
				regData = phy_read(phydev, regAddr);
			else if (strncasecmp(type, "ext", 3) == 0)
				regData = ytphy_read_ext(phydev, regAddr);
			pr_err("read %s reg = 0x%x, val = 0x%x\n", type,
			       regAddr, regData);
		}
	} else
		goto error;

	if (dev)
		dev_put(dev);

	return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" read mii reg: \t\techo ethx mii read reg > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" write mii reg val: \techo ethx mii write reg val > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" read ext reg: \t\techo ethx ext read reg > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" write ext reg val: \techo ethx ext write reg val > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" read mmd reg: \t\techo ethx mmd read device reg > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" write mmd reg val: \techo ethx mmd write device reg val > /sys/kernel/debug/yt/phyreg\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(phyreg);

/* YT8510/YT8511/YT8512/YT8522/YT8531/YT8543 */
static int ytphy_config_intr(struct phy_device *phydev)
{
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		return phy_write(phydev, YTPHY_UTP_INTR_REG,
				 YTPHY_INTR_LINK_STATUS);
	return 0;
}

/* YT8521S/YT8531S */
static int yt85xxs_config_intr(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		ret = __phy_write(phydev, YTPHY_UTP_INTR_REG,
				  YTPHY_INTR_LINK_STATUS);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

static int yt8824_config_intr(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_top_select_page(phydev, YT8824_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		ret = __phy_write(phydev, YTPHY_UTP_INTR_REG,
				  YTPHY_INTR_LINK_STATUS);

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8628_config_intr(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		ret = __ytphy_read_ext(phydev, YT8628_PIN_MUX_CFG_REG);
		if (ret < 0)
			goto err_restore_page;

		ret |= 	YT8628_INT_PIN_MUX;
		ret = __ytphy_write_ext(phydev, YT8628_PIN_MUX_CFG_REG, ret);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, YTPHY_UTP_INTR_REG,
				  YTPHY_INTR_LINK_STATUS);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
static irqreturn_t ytphy_handle_interrupt(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, YTPHY_UTP_INTR_STATUS_REG);
	if (ret > 0) {
		phy_trigger_machine(phydev);
		return IRQ_HANDLED;
	}
	else
		return IRQ_NONE;
}

static irqreturn_t yt85xxs_handle_interrupt(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, YTPHY_UTP_INTR_STATUS_REG);
	if (ret < 0)
		goto err_restore_page;

	phy_trigger_machine(phydev);

err_restore_page:
	ytphy_restore_page(phydev, oldpage, ret);
	if (ret > 0)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static irqreturn_t yt8824_handle_interrupt(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_top_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, YTPHY_UTP_INTR_STATUS_REG);
	if (ret < 0)
		goto err_restore_page;

	phy_trigger_machine(phydev);

err_restore_page:
	ytphy_top_restore_page(phydev, oldpage, ret);
	if (ret > 0)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}
#else
static int ytphy_ack_interrupt(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, YTPHY_UTP_INTR_STATUS_REG);
	
	return ret < 0 ? ret : 0;
}

static int yt85xxs_ack_interrupt(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, YTPHY_UTP_INTR_STATUS_REG);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret < 0 ? ret : 0);
}

static int yt8824_ack_interrupt(struct phy_device *phydev)
{
	int ret = 0, oldpage;

	oldpage = ytphy_top_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, YTPHY_UTP_INTR_STATUS_REG);

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret < 0 ? ret : 0);
}
#endif

#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
enum stat_access_type {
	PHY,
	SDS,
	MMD,
	COMMON,
};

struct yt_hw_stat {
	const char *string;
	enum stat_access_type access_type;
	u16 reg;
	u8 bits;
};

static struct yt_hw_stat yt8821_hw_stats[] = {
	{ "UTP ib valid(64B~1518B)", PHY, 0xa3, 32 },
	{ "UTP ib valid(>1518B)", PHY, 0xa5, 32 },
	{ "UTP ib valid(<64B)", PHY, 0xa7, 32 },
	{ "UTP ib crc err(64B~1518B)", PHY, 0xa9, 16 },
	{ "UTP ib crc err(>1518B)", PHY, 0xaa, 16 },
	{ "UTP ib crc err(<64B)", PHY, 0xab, 16 },
	{ "UTP ib no sfd", PHY, 0xac, 16 },

	{ "UTP ob valid(64B~1518B)", PHY, 0xad, 32 },
	{ "UTP ob valid(>1518B)", PHY, 0xaf, 32 },
	{ "UTP ob valid(<64B)", PHY, 0xb1, 32 },
	{ "UTP ob crc err(64B~1518B)", PHY, 0xb3, 16 },
	{ "UTP ob crc err(>1518B)", PHY, 0xb4, 16 },
	{ "UTP ob crc err(<64B)", PHY, 0xb5, 16 },
	{ "UTP ob no sfd", PHY, 0xb6, 16 },

	{ "SERDES ib valid(64B~1518B)", SDS, 0x1a3, 32 },
	{ "SERDES ib valid(>1518B)", SDS, 0x1a5, 32 },
	{ "SERDES ib valid(<64B)", SDS, 0x1a7, 32 },
	{ "SERDES ib crc err(64B~1518B)", SDS, 0x1a9, 16 },
	{ "SERDES ib crc err(>1518B)", SDS, 0x1aa, 16 },
	{ "SERDES ib crc err(<64B)", SDS, 0x1ab, 16 },
	{ "SERDES ib no sfd", SDS, 0x1ac, 16 },

	{ "SERDES ob valid(64B~1518B)", SDS, 0x1ad, 32 },
	{ "SERDES ob valid(>1518B)", SDS, 0x1af, 32 },
	{ "SERDES ob valid(<64B)", SDS, 0x1b1, 32 },
	{ "SERDES ob crc err(64B~1518B)", SDS, 0x1b3, 16 },
	{ "SERDES ob crc err(>1518B)", SDS, 0x1b4, 16 },
	{ "SERDES ob crc err(<64B)", SDS, 0x1b5, 16 },
	{ "SERDES ob no sfd", SDS, 0x1b6, 16 },
};

static struct yt_hw_stat yt8531_hw_stats[] = {
	{ "UTP ib valid(64B~1518B)", PHY, 0xa3, 32 },
	{ "UTP ib valid(>1518B)", PHY, 0xa5, 32 },
	{ "UTP ib valid(<64B)", PHY, 0xa7, 32 },
	{ "UTP ib crc err(64B~1518B)", PHY, 0xa9, 16 },
	{ "UTP ib crc err(>1518B)", PHY, 0xaa, 16 },
	{ "UTP ib crc err(<64B)", PHY, 0xab, 16 },
	{ "UTP ib no sfd", PHY, 0xac, 16 },

	{ "UTP ob valid(64B~1518B)", PHY, 0xad, 32 },
	{ "UTP ob valid(>1518B)", PHY, 0xaf, 32 },
	{ "UTP ob valid(<64B)", PHY, 0xb1, 32 },
	{ "UTP ob crc err(64B~1518B)", PHY, 0xb3, 16 },
	{ "UTP ob crc err(>1518B)", PHY, 0xb4, 16 },
	{ "UTP ob crc err(<64B)", PHY, 0xb5, 16 },
	{ "UTP ob no sfd", PHY, 0xb6, 16 },
};

static struct yt_hw_stat yt8531S_hw_stats[] = {
	{ "UTP ib valid(64B~1518B)", PHY, 0xa3, 32 },
	{ "UTP ib valid(>1518B)", PHY, 0xa5, 32 },
	{ "UTP ib valid(<64B)", PHY, 0xa7, 32 },
	{ "UTP ib crc err(64B~1518B)", PHY, 0xa9, 16 },
	{ "UTP ib crc err(>1518B)", PHY, 0xaa, 16 },
	{ "UTP ib crc err(<64B)", PHY, 0xab, 16 },
	{ "UTP ib no sfd", PHY, 0xac, 16 },

	{ "UTP ob valid(64B~1518B)", PHY, 0xad, 32 },
	{ "UTP ob valid(>1518B)", PHY, 0xaf, 32 },
	{ "UTP ob valid(<64B)", PHY, 0xb1, 32 },
	{ "UTP ob crc err(64B~1518B)", PHY, 0xb3, 16 },
	{ "UTP ob crc err(>1518B)", PHY, 0xb4, 16 },
	{ "UTP ob crc err(<64B)", PHY, 0xb5, 16 },
	{ "UTP ob no sfd", PHY, 0xb6, 16 },

	{ "SERDES ib valid(64B~1518B)", SDS, 0x1a3, 32 },
	{ "SERDES ib valid(>1518B)", SDS, 0x1a5, 32 },
	{ "SERDES ib valid(<64B)", SDS, 0x1a7, 32 },
	{ "SERDES ib crc err(64B~1518B)", SDS, 0x1a9, 16 },
	{ "SERDES ib crc err(>1518B)", SDS, 0x1aa, 16 },
	{ "SERDES ib crc err(<64B)", SDS, 0x1ab, 16 },
	{ "SERDES ib no sfd", SDS, 0x1ac, 16 },

	{ "SERDES ob valid(64B~1518B)", SDS, 0x1ad, 32 },
	{ "SERDES ob valid(>1518B)", SDS, 0x1af, 32 },
	{ "SERDES ob valid(<64B)", SDS, 0x1b1, 32 },
	{ "SERDES ob crc err(64B~1518B)", SDS, 0x1b3, 16 },
	{ "SERDES ob crc err(>1518B)", SDS, 0x1b4, 16 },
	{ "SERDES ob crc err(<64B)", SDS, 0x1b5, 16 },
	{ "SERDES ob no sfd", SDS, 0x1b6, 16 },
};

static struct yt_hw_stat yt8512_hw_stats[] = {
	{ "UTP ib valid(64B~1518B)", PHY, 0x40a3, 32 },
	{ "UTP ib valid(>1518B)", PHY, 0x40a5, 32 },
	{ "UTP ib valid(<64B)", PHY, 0x40a7, 32 },
	{ "UTP ib crc err(64B~1518B)", PHY, 0x40a9, 16 },
	{ "UTP ib crc err(>1518B)", PHY, 0x40aa, 16 },
	{ "UTP ib crc err(<64B)", PHY, 0x40ab, 16 },
	{ "UTP ib no sfd", PHY, 0x40ac, 16 },

	{ "UTP ob valid(64B~1518B)", PHY, 0x40ad, 32 },
	{ "UTP ob valid(>1518B)", PHY, 0x40af, 32 },
	{ "UTP ob valid(<64B)", PHY, 0x40b1, 32 },
	{ "UTP ob crc err(64B~1518B)", PHY, 0x40b3, 16 },
	{ "UTP ob crc err(>1518B)", PHY, 0x40b4, 16 },
	{ "UTP ob crc err(<64B)", PHY, 0x40b5, 16 },
	{ "UTP ob no sfd", PHY, 0x40b6, 16 },
};

static struct yt_hw_stat yt8614_hw_stats[] = {
	{ "media ib valid(64B~1518B)", COMMON, 0xa0a3, 32 },
	{ "media ib valid(>1518B)", COMMON, 0xa0a5, 32 },
	{ "media ib valid(<64B)", COMMON, 0xa0a7, 32 },
	{ "media ib crc err(64B~1518B)", COMMON, 0xa0a9, 16 },
	{ "media ib crc err(>1518B)", COMMON, 0xa0aa, 16 },
	{ "media ib crc err(<64B)", COMMON, 0xa0ab, 16 },
	{ "media ib no sfd", COMMON, 0xa0ac, 16 },

	{ "media ob valid(64B~1518B)", COMMON, 0xa0ad, 32 },
	{ "media ob valid(>1518B)", COMMON, 0xa0af, 32 },
	{ "media ob valid(<64B)", COMMON, 0xa0b1, 32 },
	{ "media ob crc err(64B~1518B)", COMMON, 0xa0b3, 16 },
	{ "media ob crc err(>1518B)", COMMON, 0xa0b4, 16 },
	{ "media ob crc err(<64B)", COMMON, 0xa0b5, 16 },
	{ "media ob no sfd", COMMON, 0xa0b6, 16 },
};

static struct yt_hw_stat yt8111_hw_stats[] = {
	{ "UTP ib valid(64B~1518B)", PHY, 0x110f, 32 },
	{ "UTP ib valid(>1518B)", PHY, 0x1111, 32 },
	{ "UTP ib valid(<64B)", PHY, 0x1113, 32 },
	{ "UTP ib crc err(64B~1518B)", PHY, 0x1115, 16 },
	{ "UTP ib crc err(>1518B)", PHY, 0x1116, 16 },
	{ "UTP ib crc err(<64B)", PHY, 0x1117, 16 },
	{ "UTP ib no sfd", PHY, 0x1118, 16 },

	{ "UTP ob valid(64B~1518B)", PHY, 0x1119, 32 },
	{ "UTP ob valid(>1518B)", PHY, 0x111b, 32 },
	{ "UTP ob valid(<64B)", PHY, 0x111d, 32 },
	{ "UTP ob crc err(64B~1518B)", PHY, 0x111f, 16 },
	{ "UTP ob crc err(>1518B)", PHY, 0x1120, 16 },
	{ "UTP ob crc err(<64B)", PHY, 0x1121, 16 },
	{ "UTP ob no sfd", PHY, 0x1122, 16 },
};

static int yt8821_get_sset_count(struct phy_device *phydev);
static void yt8821_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8821_get_stat(struct phy_device *phydev, int i);
static void yt8821_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data);
static int yt8531_get_sset_count(struct phy_device *phydev);
static void yt8531_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8531_get_stat(struct phy_device *phydev, int i);
static void yt8531_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data);
static int yt8531S_get_sset_count(struct phy_device *phydev);
static void yt8531S_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8531S_get_stat(struct phy_device *phydev, int i);
static void yt8531S_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data);
static int yt8512_get_sset_count(struct phy_device *phydev);
static void yt8512_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8512_get_stat(struct phy_device *phydev, int i);
static void yt8512_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data);
static int yt8614_get_sset_count(struct phy_device *phydev);
static void yt8614_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8614_get_stat(struct phy_device *phydev, int i);
static void yt8614_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data);
static int yt8111_get_sset_count(struct phy_device *phydev);
static void yt8111_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8111_get_stat(struct phy_device *phydev, int i);
static void yt8111_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data);
static int yt8628_get_sset_count(struct phy_device *phydev);
static void yt8628_get_strings(struct phy_device *phydev, u8 *data);
static u64 yt8628_get_stat(struct phy_device *phydev, int i);
static void yt8628_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data);

static int yt8614_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(yt8614_hw_stats);
}

static void yt8614_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yt8614_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			yt8614_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 yt8614_get_stat(struct phy_device *phydev, int i)
{
	struct yt_hw_stat stat = yt8614_hw_stats[i];
	int len_l = min(stat.bits, (u8)16);
	int len_h = stat.bits - len_l;
	int val;
	u64 ret;

	val = ytphy_read_ext(phydev, stat.reg);
	if (val < 0)
		return U64_MAX;

	ret = val & 0xffff;
	if (len_h) {
		val = ytphy_read_ext(phydev, stat.reg + 1);
		if (val < 0)
			return U64_MAX;

		ret = ((ret << 16) | val);
	}

	return ret;
}

static void yt8614_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i, ret;

	ret = ytphy_read_ext(phydev, 0xa0a0);
	if (ret < 0)
		return;

	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = ytphy_write_ext(phydev, 0xa0a0, ret);
	if (ret < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(yt8614_hw_stats); i++)
		data[i] = yt8614_get_stat(phydev, i);
}

static int yt8512_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(yt8512_hw_stats);
}

static void yt8512_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yt8512_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			yt8512_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 yt8512_get_stat(struct phy_device *phydev, int i)
{
	struct yt_hw_stat stat = yt8512_hw_stats[i];
	int len_l = min(stat.bits, (u8)16);
	int len_h = stat.bits - len_l;
	int val;
	u64 ret;

	val = ytphy_read_ext(phydev, stat.reg);
	if (val < 0)
		return U64_MAX;

	ret = val & 0xffff;
	if (len_h) {
		val = ytphy_read_ext(phydev, stat.reg + 1);
		if (val < 0)
			return U64_MAX;

		ret = ((ret << 16) | val);
	}

	return ret;
}

static void yt8512_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i, ret;

	ret = ytphy_read_ext(phydev, 0x40a0);
	if (ret < 0)
		return;

	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = ytphy_write_ext(phydev, 0x40a0, ret);
	if (ret < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(yt8512_hw_stats); i++)
		data[i] = yt8512_get_stat(phydev, i);
}

static int yt8531S_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(yt8531S_hw_stats);
}

static void yt8531S_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yt8531S_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			yt8531S_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 yt8531S_get_stat(struct phy_device *phydev, int i)
{
	struct yt_hw_stat stat = yt8531S_hw_stats[i];
	int len_l = min(stat.bits, (u8)16);
	int len_h = stat.bits - len_l;
	int oldpage;
	int val;
	u64 ret;

	if (stat.access_type == SDS) {
		oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_FIBER);
		if (oldpage < 0)
			goto err_restore_page;

		val = __ytphy_read_ext(phydev, stat.reg);
		if (val < 0)
			goto err_restore_page;

		ret = val & 0xffff;
		if (len_h) {
			val = __ytphy_read_ext(phydev, stat.reg + 1);
			if (val < 0)
				goto err_restore_page;

			ret = ((ret << 16) | val);
		}
	}
	else if (stat.access_type == PHY) {
		oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
		if (oldpage < 0)
			goto err_restore_page;

		val = __ytphy_read_ext(phydev, stat.reg);
		if (val < 0)
			goto err_restore_page;

		ret = val & 0xffff;
		if (len_h) {
			val = __ytphy_read_ext(phydev, stat.reg + 1);
			if (val < 0)
				goto err_restore_page;

			ret = ((ret << 16) | val);
		}
	}

err_restore_page:
	ytphy_restore_page(phydev, oldpage, ret);
	if (ret < 0)
		return U64_MAX;
	else
		return ret;
}

static void yt8531S_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret = 0, oldpage;
	int i;

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __ytphy_read_ext(phydev, 0xa0);
	if (ret < 0)
		goto err_restore_page;
	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = __ytphy_write_ext(phydev, 0xa0, ret);
	if (ret < 0)
		goto err_restore_page;

	ytphy_restore_page(phydev, oldpage, ret);

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_FIBER);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __ytphy_read_ext(phydev, 0x1a0);
	if (ret < 0)
		goto err_restore_page;
	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = __ytphy_write_ext(phydev, 0x1a0, ret);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	ytphy_restore_page(phydev, oldpage, ret);
	if (ret < 0)
		return;
	else {
		u8 loop = (priv->chip_mode != 0) ?
				ARRAY_SIZE(yt8531S_hw_stats) :
				ARRAY_SIZE(yt8531S_hw_stats) / 2;

		for (i = 0; i < loop; i++)
			data[i] = yt8531S_get_stat(phydev, i);
	}
}

static int yt8531_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(yt8531_hw_stats);
}

static void yt8531_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yt8531_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			yt8531_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 yt8531_get_stat(struct phy_device *phydev, int i)
{
	struct yt_hw_stat stat = yt8531_hw_stats[i];
	int len_l = min(stat.bits, (u8)16);
	int len_h = stat.bits - len_l;
	int val;
	u64 ret;

	val = ytphy_read_ext(phydev, stat.reg);
	if (val < 0)
		return U64_MAX;

	ret = val & 0xffff;
	if (len_h) {
		val = ytphy_read_ext(phydev, stat.reg + 1);
		if (val < 0)
			return U64_MAX;

		ret = ((ret << 16) | val);
	}

	return ret;
}

static void yt8531_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i, ret;

	ret = ytphy_read_ext(phydev, 0xa0);
	if (ret < 0)
		return;

	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = ytphy_write_ext(phydev, 0xa0, ret);
	if (ret < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(yt8531_hw_stats); i++)
		data[i] = yt8531_get_stat(phydev, i);
}

static int yt8821_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(yt8821_hw_stats);
}

static void yt8821_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yt8821_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			yt8821_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 yt8821_get_stat(struct phy_device *phydev, int i)
{
	struct yt_hw_stat stat = yt8821_hw_stats[i];
	int len_l = min(stat.bits, (u8)16);
	int len_h = stat.bits - len_l;
	int oldpage;
	int val;
	u64 ret;

	if (stat.access_type == SDS) {
		oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_FIBER);
		if (oldpage < 0)
			goto err_restore_page;

		val = __ytphy_read_ext(phydev, stat.reg);
		if (val < 0)
			goto err_restore_page;

		ret = val & 0xffff;
		if (len_h) {
			val = __ytphy_read_ext(phydev, stat.reg + 1);
			if (val < 0)
				goto err_restore_page;

			ret = ((ret << 16) | val);
		}
	}
	else if (stat.access_type == PHY) {
		oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
		if (oldpage < 0)
			goto err_restore_page;

		val = __ytphy_read_ext(phydev, stat.reg);
		if (val < 0)
			goto err_restore_page;

		ret = val & 0xffff;
		if (len_h) {
			val = __ytphy_read_ext(phydev, stat.reg + 1);
			if (val < 0)
				goto err_restore_page;

			ret = ((ret << 16) | val);
		}
	}

err_restore_page:
	ytphy_restore_page(phydev, oldpage, ret);
	if (ret < 0)
		return U64_MAX;
	else
		return ret;
}

static void yt8821_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int ret = 0, oldpage;
	int i;

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __ytphy_read_ext(phydev, 0xa0);
	if (ret < 0)
		goto err_restore_page;
	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = __ytphy_write_ext(phydev, 0xa0, ret);
	if (ret < 0)
		goto err_restore_page;

	ytphy_restore_page(phydev, oldpage, ret);

	oldpage = ytphy_select_page(phydev, YTPHY_REG_SPACE_FIBER);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __ytphy_read_ext(phydev, 0x1a0);
	if (ret < 0)
		goto err_restore_page;
	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = __ytphy_write_ext(phydev, 0x1a0, ret);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	ytphy_restore_page(phydev, oldpage, ret);
	if (ret < 0)
		return;
	else
		for (i = 0; i < ARRAY_SIZE(yt8821_hw_stats); i++)
			data[i] = yt8821_get_stat(phydev, i);
}

static int yt8111_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(yt8111_hw_stats);
}

static void yt8111_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(yt8111_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			yt8111_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 yt8111_get_stat(struct phy_device *phydev, int i)
{
	struct yt_hw_stat stat = yt8111_hw_stats[i];
	int len_l = min(stat.bits, (u8)16);
	int len_h = stat.bits - len_l;
	int val;
	u64 ret;

	val = ytphy_read_ext(phydev, stat.reg);
	if (val < 0)
		return U64_MAX;

	ret = val & 0xffff;
	if (len_h) {
		val = ytphy_read_ext(phydev, stat.reg + 1);
		if (val < 0)
			return U64_MAX;

		ret = ((ret << 16) | val);
	}

	return ret;
}

static void yt8111_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i, ret;

	ret = ytphy_read_ext(phydev, 0x110e);
	if (ret < 0)
		return;

	ret |= BIT(15);
	ret &= ~BIT(14);
	ret = ytphy_write_ext(phydev, 0x110e, ret);
	if (ret < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(yt8111_hw_stats); i++)
		data[i] = yt8111_get_stat(phydev, i);
}

__attribute__((unused)) static int
yt8628_get_sset_count(struct phy_device *phydev)
{
	return 0;
}

__attribute__((unused)) static void
yt8628_get_strings(struct phy_device *phydev, u8 *data)
{
	return;
}

__attribute__((unused)) static u64
yt8628_get_stat(struct phy_device *phydev, int i)
{
	return 0;
}

__attribute__((unused)) static void
yt8628_get_stats(struct phy_device *phydev,
 		 struct ethtool_stats *stats, u64 *data)
{
	return;
}
#endif

static int yt8821_init(struct phy_device *phydev)
{
	int ret = 0;

	ret = ytphy_write_ext(phydev, 0xa000, 0x2);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x23, 0x8605);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa000, 0x0);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x34e, 0x8080);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x4d2, 0x5200);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x4d3, 0x5200);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x372, 0x5a3c);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x374, 0x7c6c);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x336, 0xaa0a);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x340, 0x3022);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x36a, 0x8000);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x4b3, 0x7711);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x4b5, 0x2211);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x56, 0x20);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x56, 0x3f);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x97, 0x380c);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x660, 0x112a);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x450, 0xe9);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x466, 0x6464);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x467, 0x6464);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x468, 0x6464);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x469, 0x6464);
	if (ret < 0)
		return ret;

	return ret;
}

static int yt8821_config_init(struct phy_device *phydev)
{
	int ret, val;

	val = ytphy_read_ext(phydev, 0xa001);
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		val &= ~(BIT(0));
		val &= ~(BIT(1));
		val &= ~(BIT(2));
		ret = ytphy_write_ext(phydev, 0xa001, val);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0xa000, 2);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, MII_BMCR);
		val |= BIT(YTXXXX_AUTO_NEGOTIATION_BIT);
		phy_write(phydev, MII_BMCR, val);

		ret = ytphy_write_ext(phydev, 0xa000, 0x0);
		if (ret < 0)
			return ret;
	}
#if (KERNEL_VERSION(4, 10, 17) < LINUX_VERSION_CODE)
	else if (phydev->interface == PHY_INTERFACE_MODE_2500BASEX) 
	{
		val |= BIT(0);
		val &= ~(BIT(1));
		val &= ~(BIT(2));
		ret = ytphy_write_ext(phydev, 0xa001, val);
		if (ret < 0)
			return ret;

		ret = ytphy_write_ext(phydev, 0xa000, 0x0);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, MII_ADVERTISE);
		val |= BIT(YTXXXX_PAUSE_BIT);
		val |= BIT(YTXXXX_ASYMMETRIC_PAUSE_BIT);
		phy_write(phydev, MII_ADVERTISE, val);

		ret = ytphy_write_ext(phydev, 0xa000, 2);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, MII_ADVERTISE);
		val |= BIT(YT8821_SDS_PAUSE_BIT);
		val |= BIT(YT8821_SDS_ASYMMETRIC_PAUSE_BIT);
		phy_write(phydev, MII_ADVERTISE, val);

		val = phy_read(phydev, MII_BMCR);
		val &= (~BIT(YTXXXX_AUTO_NEGOTIATION_BIT));
		phy_write(phydev, MII_BMCR, val);

		ret = ytphy_write_ext(phydev, 0xa000, 0x0);
		if (ret < 0)
			return ret;
	}
#endif    
	else 
	{  
		val |= BIT(0);
		val &= ~(BIT(1));
		val |= BIT(2);
		ret = ytphy_write_ext(phydev, 0xa001, val);
		if (ret < 0)
			return ret;
	}

	ret = yt8821_init(phydev);
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	/* soft reset */
	ytxxxx_soft_reset(phydev);

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev, "%s done, phy addr: %d\n",
		    __func__, phydev->addr);
#else
	netdev_info(phydev->attached_dev, "%s done, phy addr: %d\n",
		    __func__, phydev->mdio.addr);
#endif

	return ret;
}

static int yt8824_config_init(struct phy_device *phydev)
{
	struct yt8xxx_priv *priv = phydev->priv;
	int ret;

	ret = yt8824_config_init_paged(phydev, YT8824_REG_SPACE_SERDES);
	if (ret < 0)
		return ret;

	ret = yt8824_config_init_paged(phydev, YT8614_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	ret = yt8824_soft_reset(phydev);
	if (ret < 0)
		return ret;

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, phy base addr = %d\n",
		    __func__, phydev->addr, priv->phy_base_addr);
#else
	netdev_info(phydev->attached_dev,
		    "%s done, phy addr: %d, phy base addr = %d\n",
		    __func__, phydev->mdio.addr, priv->phy_base_addr);
#endif
	return 0;
}

#if (KERNEL_VERSION(6, 0, 19) < LINUX_VERSION_CODE)
static int yt8821_get_rate_matching(struct phy_device *phydev,
				    phy_interface_t iface)
{
	int val;

	val = ytphy_read_ext(phydev, 0xa001);
	if (val < 0)
		return val;

	if (val & (BIT(2) | BIT(1) | BIT(0)))
		return RATE_MATCH_PAUSE;

	return RATE_MATCH_NONE;
}
#endif

static int yt8821_aneg_done(struct phy_device *phydev)
{
	int link_utp = 0;

	/* reading UTP */
	ytphy_write_ext(phydev, 0xa000, 0);
	link_utp = !!(phy_read(phydev, REG_PHY_SPEC_STATUS) & (BIT(10)));

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev, "%s, phy addr: %d, link_utp: %d\n",
		    __func__, phydev->addr, link_utp);
#else
	netdev_info(phydev->attached_dev, "%s, phy addr: %d, link_utp: %d\n",
		    __func__, phydev->mdio.addr, link_utp);
#endif

	return !!(link_utp);
}

static int yt8824_aneg_done(struct phy_device *phydev)
{
	int link_utp = 0;
	int oldpage;

	oldpage = ytphy_top_select_page(phydev, YT8824_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;
	
	link_utp = !!(__phy_read(phydev, REG_PHY_SPEC_STATUS) & (BIT(10)));

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev, "%s, phy addr: %d, link_utp: %d\n",
		    __func__, phydev->addr, link_utp);
#else
	netdev_info(phydev->attached_dev, "%s, phy addr: %d, link_utp: %d\n",
		    __func__, phydev->mdio.addr, link_utp);
#endif

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, link_utp);
}

static int yt8821_adjust_status(struct phy_device *phydev, int val)
{
	int speed_mode_bit15_14, speed_mode_bit9;
	int speed_mode, duplex;
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
	int speed = -1;
#else
	int speed = SPEED_UNKNOWN;
#endif

	duplex = (val & YTXXXX_DUPLEX) >> YTXXXX_DUPLEX_BIT;

	/* Bit9-Bit15-Bit14 speed mode 100-2.5G; 010-1000M; 001-100M; 000-10M */
	speed_mode_bit15_14 = (val & YTXXXX_SPEED_MODE) >>
				YTXXXX_SPEED_MODE_BIT;
	speed_mode_bit9 = (val & BIT(9)) >> 9;
	speed_mode = (speed_mode_bit9 << 2) | speed_mode_bit15_14;
	switch (speed_mode) {
	case 0:
		speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
		speed = SPEED_1000;
		break;
	case 4:
		speed = SPEED_2500;
		break;
	default:
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
		speed = -1;
#else
		speed = SPEED_UNKNOWN;
#endif
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

static int yt8821_read_status(struct phy_device *phydev)
{
	int link_utp = 0;
	int link;
	int ret;
	int val;

	/* reading UTP */
	ret = ytphy_write_ext(phydev, 0xa000, 0);
	if (ret < 0)
		return ret;

	genphy_read_status(phydev);

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	link = val & (BIT(YTXXXX_LINK_STATUS_BIT));
	if (link) {
		link_utp = 1;
		/* speed(2500), duplex */
		yt8821_adjust_status(phydev, val);
	} else {
		link_utp = 0;
	}

	if (link_utp) {
		if (phydev->link == 0)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media: UTP, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->addr, (unsigned int)val);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link up, media: UTP, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->mdio.addr,
				    (unsigned int)val);
#endif
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->addr);
#else
			netdev_info(phydev->attached_dev,
				    "%s, phy addr: %d, link down\n",
				    __func__, phydev->mdio.addr);
#endif

		phydev->link = 0;
	}

	if (link_utp)
		ytphy_write_ext(phydev, 0xa000, 0);

#if (KERNEL_VERSION(4, 10, 17) < LINUX_VERSION_CODE)
	val = ytphy_read_ext(phydev, 0xa001);
	if ((val & (BIT(2) | BIT(1) | BIT(0))) == 0x0) {
		switch (phydev->speed) {
		case SPEED_2500:
			phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
			break;
		case SPEED_1000:
		case SPEED_100:
		case SPEED_10:
			phydev->interface = PHY_INTERFACE_MODE_SGMII;
			break;
		}
	}
#endif            

	return 0;
}

static int yt8824_read_status_paged(struct phy_device *phydev, int reg_space,
				    int *status, int *lpa)
{
	int ret = 0, oldpage;
	int link;

	oldpage = ytphy_top_select_page(phydev, reg_space);
	if (oldpage < 0)
		goto err_restore_page;

	ret = *lpa = __phy_read(phydev, MII_LPA);
	if (ret < 0)
		goto err_restore_page;

	ret = *status = __phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (ret < 0)
		goto err_restore_page;

	ret = link = !!(*status & BIT(YTXXXX_LINK_STATUS_BIT));

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8824_read_status(struct phy_device *phydev)
{
	int link_utp;
	int lpa;
	int val;

	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->link = 0;

	link_utp = yt8824_read_status_paged(phydev,
					    YT8824_REG_SPACE_UTP, &val, &lpa);
	if (link_utp < 0)
		return link_utp;

	if (link_utp) {
		phydev->link = 1;
		phydev->pause = !!(lpa & BIT(10));
		phydev->asym_pause = !!(lpa & BIT(11));

		/* update speed & duplex */
		yt8821_adjust_status(phydev, val);
	} else
		phydev->link = 0;

	return 0;
}

#if (KERNEL_VERSION(5, 0, 21) < LINUX_VERSION_CODE)
static int yt8821_get_features(struct phy_device *phydev)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
			 phydev->supported, 1);
	return genphy_read_abilities(phydev);
}

static int yt8111_get_features(struct phy_device *phydev)
{
	linkmode_set_bit_array(phy_basic_ports_array,
			       ARRAY_SIZE(phy_basic_ports_array),
			       phydev->supported);

#if (KERNEL_VERSION(5, 18, 19) < LINUX_VERSION_CODE)
	linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			 phydev->supported);
#endif

	return 0;
}
#endif

#if (KERNEL_VERSION(4, 9, 337) < LINUX_VERSION_CODE)
/* Speed Auto Downgrade Control Register (0x14) */
#define YT8821_AUTO_SPEED_DOWNGRADE_CTRL		0x14
#define YT8821_SMART_SPEED_ENABLE			BIT(5)
#define YT8821_SMART_SPEED_RETRY_LIMIT_MASK	(BIT(4) | BIT(3) | BIT(2))
#define YT8821_SMART_SPEED_BYPASS_TIMER			BIT(1)
#define YT8821_SMART_SPEED_RETRY_LIMIT_MASK_OFFSET	2
#define YT8821_DEFAULT_DOWNSHIFT			5
#define YT8821_MIN_DOWNSHIFT				2
#define YT8821_MAX_DOWNSHIFT				9

int yt8821_get_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, void *data);
int yt8821_set_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, const void *data);
static int yt8821_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val;

	val = phy_read(phydev, YT8821_AUTO_SPEED_DOWNGRADE_CTRL);
	if (val < 0)
		return val;

	if (val & YT8821_SMART_SPEED_ENABLE)
		*data = ((val & YT8821_SMART_SPEED_RETRY_LIMIT_MASK) >> 
				YT8821_SMART_SPEED_RETRY_LIMIT_MASK_OFFSET) + 2;
	else
		*data = DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int yt8821_set_downshift(struct phy_device *phydev, u8 cnt)
{
	u16 mask, set;
	int new, ret;

	mask = YT8821_SMART_SPEED_ENABLE |
		YT8821_SMART_SPEED_RETRY_LIMIT_MASK |
		YT8821_SMART_SPEED_BYPASS_TIMER;
	switch (cnt) {
	case DOWNSHIFT_DEV_DEFAULT_COUNT:
		cnt = YT8821_DEFAULT_DOWNSHIFT;
#if (KERNEL_VERSION(5, 3, 18) < LINUX_VERSION_CODE)
		fallthrough;
#endif
	case YT8821_MIN_DOWNSHIFT ... YT8821_MAX_DOWNSHIFT:
		cnt = cnt - 2;
		set = YT8821_SMART_SPEED_ENABLE |
		      (cnt << YT8821_SMART_SPEED_RETRY_LIMIT_MASK_OFFSET) |
		      YT8821_SMART_SPEED_BYPASS_TIMER;
		break;
	case DOWNSHIFT_DEV_DISABLE:
		set = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = phy_read(phydev, YT8821_AUTO_SPEED_DOWNGRADE_CTRL);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	ret = phy_write(phydev, YT8821_AUTO_SPEED_DOWNGRADE_CTRL, new);
	if (ret < 0)
		return ret;

	return ytphy_soft_reset(phydev);
}

#if (KERNEL_VERSION(5, 1, 21) < LINUX_VERSION_CODE)
/* EEE 1000BT Control2 */
#define YT8821_EEE_CTRL2_1G			0x34
#define YT8821_BYPASS_FAST_LINK_DOWN_1G		BIT(15)
/* EEE 1000BT Control3 */
#define YT8821_EEE_CTRL3_1G			0x37
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G	(BIT(15) | BIT(14) | BIT(13))
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_OFFSET	13
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_00MS		0
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_05MS		1
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_10MS		2
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_20MS		3
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_40MS		4
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_80MS		5
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_160MS	6
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_320MS	7
/* EEE 2500BT Control */
#define YT8821_EEE_CTRL_2P5G			0x355
#define YT8821_BYPASS_FAST_LINK_DOWN_2P5G	BIT(7)
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_2P5G	(BIT(6) | BIT(5) | BIT(4))
#define YT8821_EEE_FAST_LINK_DOWN_TIMER_2P5G_OFFSET	4

static int yt8821_get_fast_link_down(struct phy_device *phydev, u8 *msecs)
{
	int val;

	if (phydev->speed == SPEED_1000) {
		val = ytphy_read_ext(phydev, YT8821_EEE_CTRL2_1G);
		if (val < 0)
			return val;

		if (val & YT8821_BYPASS_FAST_LINK_DOWN_1G) {
			*msecs = ETHTOOL_PHY_FAST_LINK_DOWN_OFF;
			return 0;
		}

		val = ytphy_read_ext(phydev, YT8821_EEE_CTRL3_1G);
		if (val < 0)
			return val;

		val = (val & YT8821_EEE_FAST_LINK_DOWN_TIMER_1G) >>
			YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_OFFSET;

		switch (val) {
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_00MS:
			*msecs = 0;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_05MS:
			*msecs = 5;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_10MS:
			*msecs = 10;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_20MS:
			*msecs = 20;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_40MS:
			*msecs = 40;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_80MS:
			*msecs = 80;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_160MS:
			*msecs = 160;
			break;
		default:
			return -EINVAL;
		}
	} else if (phydev->speed == SPEED_2500) {
		val = ytphy_read_ext(phydev, YT8821_EEE_CTRL_2P5G);
		if (val < 0)
			return val;

		if (val & YT8821_BYPASS_FAST_LINK_DOWN_2P5G) {
			*msecs = ETHTOOL_PHY_FAST_LINK_DOWN_OFF;
			return 0;
		}

		val = (val & YT8821_EEE_FAST_LINK_DOWN_TIMER_2P5G) >>
			YT8821_EEE_FAST_LINK_DOWN_TIMER_2P5G_OFFSET;

		switch (val) {
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_00MS:
			*msecs = 0;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_05MS:
			*msecs = 5;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_10MS:
			*msecs = 10;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_20MS:
			*msecs = 20;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_40MS:
			*msecs = 40;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_80MS:
			*msecs = 80;
			break;
		case YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_160MS:
			*msecs = 160;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int yt8821_set_fast_link_down(struct phy_device *phydev, const u8 *msecs)
{
	u16 mask, set;
	int val, ret;

	if (phydev->speed == SPEED_1000) {
		if (*msecs == ETHTOOL_PHY_FAST_LINK_DOWN_OFF) {
			val = ytphy_read_ext(phydev, YT8821_EEE_CTRL2_1G);
			if (val < 0)
				return val;

			val |= YT8821_BYPASS_FAST_LINK_DOWN_1G;
			return ytphy_write_ext(phydev, YT8821_EEE_CTRL2_1G,
					       val);
		} else {
			val = ytphy_read_ext(phydev, YT8821_EEE_CTRL2_1G);
			if (val < 0)
				return val;

			val &= ~YT8821_BYPASS_FAST_LINK_DOWN_1G;
			ret = ytphy_write_ext(phydev, YT8821_EEE_CTRL2_1G,
					      val);
			if (ret < 0)
				return ret;

			if (*msecs < 5)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_00MS;
			else if (*msecs < 10)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_05MS;
			else if (*msecs < 20)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_10MS;
			else if (*msecs < 30)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_20MS;
			else if (*msecs < 60)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_40MS;
			else if (*msecs < 120)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_80MS;
			else
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_160MS;

			val = ytphy_read_ext(phydev, YT8821_EEE_CTRL3_1G);
			if (val < 0)
				return val;

			mask = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G;
			val = (val & (~mask)) |
				(set <<
				YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_OFFSET);
			ret = ytphy_write_ext(phydev, YT8821_EEE_CTRL3_1G, val);
			if (ret < 0)
				return ret;
		}
	} else if (phydev->speed == SPEED_2500) {
		if (*msecs == ETHTOOL_PHY_FAST_LINK_DOWN_OFF) {
			val = ytphy_read_ext(phydev, YT8821_EEE_CTRL_2P5G);
			if (val < 0)
				return val;

			val |= YT8821_BYPASS_FAST_LINK_DOWN_2P5G;
			return ytphy_write_ext(phydev, YT8821_EEE_CTRL_2P5G,
					       val);
		} else {
			if (*msecs < 5)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_00MS;
			else if (*msecs < 10)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_05MS;
			else if (*msecs < 20)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_10MS;
			else if (*msecs < 30)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_20MS;
			else if (*msecs < 60)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_40MS;
			else if (*msecs < 120)
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_80MS;
			else
				set = YT8821_EEE_FAST_LINK_DOWN_TIMER_1G_160MS;

			val = ytphy_read_ext(phydev, YT8821_EEE_CTRL_2P5G);
			if (val < 0)
				return val;

			mask = YT8821_BYPASS_FAST_LINK_DOWN_2P5G |
				YT8821_EEE_FAST_LINK_DOWN_TIMER_2P5G;
			val = (val & (~mask)) |
				(set <<
				YT8821_EEE_FAST_LINK_DOWN_TIMER_2P5G_OFFSET);
			ret = ytphy_write_ext(phydev, YT8821_EEE_CTRL_2P5G,
					      val);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}
#endif

int yt8821_get_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return yt8821_get_downshift(phydev, data);
#if (KERNEL_VERSION(5, 1, 21) < LINUX_VERSION_CODE)
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		return yt8821_get_fast_link_down(phydev, data);
#endif
	default:
		return -EOPNOTSUPP;
	}
}

int yt8821_set_tunable(struct phy_device *phydev,
		       struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return yt8821_set_downshift(phydev, *(const u8 *)data);
#if (KERNEL_VERSION(5, 1, 21) < LINUX_VERSION_CODE)
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		return yt8821_set_fast_link_down(phydev, data);
#endif
	default:
		return -EOPNOTSUPP;
	}
}
#endif

static int ytxxxx_suspend(struct phy_device *phydev)
{
	int wol_enabled = 0;
	int value = 0;

#if (YTPHY_WOL_FEATURE_ENABLE)
	value = phy_read(phydev, YTPHY_UTP_INTR_REG);
	wol_enabled = value & YTPHY_WOL_FEATURE_INTR;
#endif

	if (!wol_enabled) {
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);
	}

	return 0;
}

static int ytxxxx_resume(struct phy_device *phydev)
{
	int value;

	value = phy_read(phydev, MII_BMCR);
	value &= ~BMCR_PDOWN;
	value &= ~BMCR_ISOLATE;

	phy_write(phydev, MII_BMCR, value);

	return 0;
}

static int yt8824_power_down(struct phy_device *phydev)
{
	int ret = 0, oldpage;
	
	oldpage = ytphy_top_select_page(phydev, YT8824_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;
	
	ret = __phy_read(phydev, MII_BMCR);
	if (ret < 0)
		goto err_restore_page;
	
	ret = __phy_write(phydev, MII_BMCR, ret | BMCR_PDOWN);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8824_power_on(struct phy_device *phydev)
{
	int ret = 0, oldpage;
	
	oldpage = ytphy_top_select_page(phydev, YT8824_REG_SPACE_UTP);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, MII_BMCR);
	if (ret < 0)
		goto err_restore_page;
	
	ret &= ~BMCR_PDOWN;
	ret &= ~BMCR_ISOLATE;

	ret = __phy_write(phydev, MII_BMCR, ret);
	
err_restore_page:
	return ytphy_top_restore_page(phydev, oldpage, ret);
}

static int yt8824_suspend(struct phy_device *phydev)
{
	int wol_enabled = 0;

#if (YTPHY_WOL_FEATURE_ENABLE)
	struct ethtool_wolinfo wol;
	int ret = 0;

	memset(&wol, 0x0, sizeof(wol));

	ret = yt8824_wol_feature_get(phydev, &wol);
	if (ret < 0)
		return ret;

	if (wol.wolopts & WAKE_MAGIC)
		wol_enabled = 1;
#endif

	if (!wol_enabled)
		return yt8824_power_down(phydev);

	return 0;
}

static int yt8824_resume(struct phy_device *phydev)
{	
	return yt8824_power_on(phydev);
}

static int ytphy_config_aneg(struct phy_device *phydev)
{
	return 0;
}

static int yt8111_config_init(struct phy_device *phydev)
{
	int ret;
	
	ret = ytphy_write_ext(phydev, 0x221, 0x1f1f);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0x2506, 0x3f5);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0x510, 0x64f0);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x511, 0x70f0);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x512, 0x78f0);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0x507, 0xff80);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa401, 0xa04);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa400, 0xa04);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa108, 0x300);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa109, 0x800);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa304, 0x4);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa301, 0x810);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0x500, 0x2f);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa206, 0x1500);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa203, 0x1414);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa208, 0x1515);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa209, 0x1714);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa20b, 0x2d04);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0xa20c, 0x1500);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0x522, 0xfff);
	if (ret < 0)
		return ret;
	
	ret = ytphy_write_ext(phydev, 0x403, 0xff4);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa51f, 0x1070);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x51a, 0x6f0);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa403, 0x1c00);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0x506, 0xffe0);
	if (ret < 0)
		return ret;

	/* soft reset */
	ytphy_soft_reset(phydev);

#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
	netdev_info(phydev->attached_dev, "%s done, phy addr: %d\n",
		    __func__, phydev->addr);
#else
	netdev_info(phydev->attached_dev, "%s done, phy addr: %d\n",
		    __func__, phydev->mdio.addr);
#endif

	return ret;
}

static int yt8111_read_status(struct phy_device *phydev)
{
	int val;

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	phydev->link = (val & BIT(10)) > 0 ? 1 : 0;
	phydev->speed = SPEED_10;
	phydev->duplex = DUPLEX_FULL;

	return 0;
}

static struct phy_driver ytphy_drvs[] = {
	{
		.phy_id		= PHY_ID_YT8510,
		.name		= "YT8510 100M Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8512_get_sset_count,
		.get_strings	= yt8512_get_strings,
		.get_stats	= yt8512_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
		.config_init	= ytphy_config_init,
#else
		.config_init	= genphy_config_init,
#endif
		.read_status	= genphy_read_status,
	}, {
		.phy_id		= PHY_ID_YT8511,
		.name		= "YT8511 Gigabit Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8531_get_sset_count,
		.get_strings	= yt8531_get_strings,
		.get_stats	= yt8531_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if GMAC_CLOCK_INPUT_NEEDED
		.config_init	= yt8511_config_init,
#else
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE) || (KERNEL_VERSION(5, 3, 0) < LINUX_VERSION_CODE)
		.config_init	= ytphy_config_init,
#else
		.config_init	= genphy_config_init,
#endif
#endif
		.read_status	= genphy_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8512,
		.name		= "YT8512 100M Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8512_get_sset_count,
		.get_strings	= yt8512_get_strings,
		.get_stats	= yt8512_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
		.config_init	= yt8512_config_init,
		.probe		= yt8512_probe,
		.read_status	= yt8512_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8522,
		.name		= "YT8522 100M Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_BASIC_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8512_get_sset_count,
		.get_strings	= yt8512_get_strings,
		.get_stats	= yt8512_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
		.probe		= yt8522_probe,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
		.config_init	= yt8522_config_init,
		.read_status	= yt8522_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8521,
		.name		= "YT8521 Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8531S_get_sset_count,
		.get_strings	= yt8531S_get_strings,
		.get_stats	= yt8531S_get_stats,
#endif
		.config_intr	= yt85xxs_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt85xxs_handle_interrupt,
#else
		.ack_interrupt	= yt85xxs_ack_interrupt,
#endif
		.probe		= yt8521_probe,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytxxxx_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8521_aneg_done,
#endif
		.config_init	= yt8521_config_init,
		.read_status	= yt8521_read_status,
		.suspend	= yt8521_suspend,
		.resume		= yt8521_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, {
		/* same as 8521 */
		.phy_id		= PHY_ID_YT8531S,
		.name		= "YT8531S Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8531S_get_sset_count,
		.get_strings	= yt8531S_get_strings,
		.get_stats	= yt8531S_get_stats,
#endif
		.config_intr	= yt85xxs_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt85xxs_handle_interrupt,
#else
		.ack_interrupt	= yt85xxs_ack_interrupt,
#endif
		.probe		= yt8521_probe,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytxxxx_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8521_aneg_done,
#endif
		.config_init	= yt8531S_config_init,
		.read_status	= yt8521_read_status,
		.suspend	= yt8521_suspend,
		.resume		= yt8521_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, {
		/* same as 8511 */
		.phy_id		= PHY_ID_YT8531,
		.name		= "YT8531 Gigabit Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8531_get_sset_count,
		.get_strings	= yt8531_get_strings,
		.get_stats	= yt8531_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_init	= yt8531_config_init,
		.read_status	= genphy_read_status,
#if (KERNEL_VERSION(5, 0, 21) < LINUX_VERSION_CODE)
		.link_change_notify = ytphy_link_change_notify,
#endif
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, 
#ifdef YTPHY_YT8543_ENABLE
	{
		.phy_id		= PHY_ID_YT8543,
		.name		= "YT8543 Dual Port Gigabit Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8531_get_sset_count,
		.get_strings	= yt8531_get_strings,
		.get_stats	= yt8531_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_init	= yt8543_config_init,
		.read_status	= yt8543_read_status,
		.suspend	= ytxxxx_suspend,
		.resume		= ytxxxx_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol	= &ytphy_wol_feature_get,
		.set_wol	= &ytphy_wol_feature_set,
#endif
	}, 
#endif
	{
		.phy_id		= PHY_ID_YT8618,
		.name		= "YT8618 Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8614_get_sset_count,
		.get_strings	= yt8614_get_strings,
		.get_stats	= yt8614_get_stats,
#endif
		.config_intr	= yt85xxs_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt85xxs_handle_interrupt,
#else
		.ack_interrupt	= yt85xxs_ack_interrupt,
#endif
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= yt8618_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8618_aneg_done,
#endif
		.config_init	= yt8618_config_init,
		.read_status	= yt8618_read_status,
		.suspend	= yt8618_suspend,
		.resume		= yt8618_resume,
	},
	{
		.phy_id		= PHY_ID_YT8614,
		.name		= "YT8614 Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8614_get_sset_count,
		.get_strings	= yt8614_get_strings,
		.get_stats	= yt8614_get_stats,
#endif
		.config_intr	= yt85xxs_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt85xxs_handle_interrupt,
#else
		.ack_interrupt	= yt85xxs_ack_interrupt,
#endif
		.probe		= yt8614_probe,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= yt8614_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8614_aneg_done,
#endif
		.config_init	= yt8614_config_init,
		.read_status	= yt8614_read_status,
		.suspend	= yt8614_suspend,
		.resume		= yt8614_resume,
	},
	{
		.phy_id		= PHY_ID_YT8614Q,
		.name		= "YT8614Q Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8614_get_sset_count,
		.get_strings	= yt8614_get_strings,
		.get_stats	= yt8614_get_stats,
#endif
		.probe		= yt8614Q_probe,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= yt8614Q_soft_reset,
#endif
		.config_aneg	= ytphy_config_aneg,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8614Q_aneg_done,
#endif
		.config_init	= yt8614Q_config_init,
		.read_status	= yt8614Q_read_status,
		.suspend	= yt8614Q_suspend,
		.resume		= yt8614Q_resume,
	},
	{
		.phy_id		= PHY_ID_YT8821,
		.name		= "YT8821 2.5Gbps Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8821_get_sset_count,
		.get_strings	= yt8821_get_strings,
		.get_stats	= yt8821_get_stats,
#endif
#if (KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE)
		.features	= PHY_GBIT_FEATURES,
#endif
		.config_intr	= yt85xxs_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt85xxs_handle_interrupt,
#else
		.ack_interrupt	= yt85xxs_ack_interrupt,
#endif
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytxxxx_soft_reset,
#endif
		.config_aneg	= genphy_config_aneg,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8821_aneg_done,
#endif
#if (KERNEL_VERSION(5, 0, 21) < LINUX_VERSION_CODE)
		.get_features	= yt8821_get_features,
#endif
		.config_init	= yt8821_config_init,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.set_wol	= &ytphy_wol_feature_set,
		.get_wol	= &ytphy_wol_feature_get,
#endif
#if (KERNEL_VERSION(6, 0, 19) < LINUX_VERSION_CODE)
		.get_rate_matching	= yt8821_get_rate_matching,
#endif
		.read_status	= yt8821_read_status,
		.suspend	= ytxxxx_suspend,
		.resume		= ytxxxx_resume,
#if (KERNEL_VERSION(4, 9, 337) < LINUX_VERSION_CODE)
		.get_tunable	= yt8821_get_tunable,
		.set_tunable	= yt8821_set_tunable,
#endif
	}, {
		.phy_id		= PHY_ID_YT8111,
		.name		= "YT8111 10base-T1L Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
#if (KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE)
		.features	= PHY_10BT_FEATURES | PHY_DEFAULT_FEATURES,
#endif
#if (KERNEL_VERSION(5, 0, 21) < LINUX_VERSION_CODE)
		.get_features	= yt8111_get_features,
#endif
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8111_get_sset_count,
		.get_strings	= yt8111_get_strings,
		.get_stats	= yt8111_get_stats,
#endif
		.config_intr	= ytphy_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= ytphy_handle_interrupt,
#else
		.ack_interrupt	= ytphy_ack_interrupt,
#endif
		.config_aneg	= ytphy_config_aneg,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= ytphy_soft_reset,
#endif
		.config_init	= yt8111_config_init,
		.read_status	= yt8111_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		.phy_id		= PHY_ID_YT8628,
		.name		= "YT8628 Octal Ports Gigabit Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
		.features	= PHY_GBIT_FEATURES,
#if (KERNEL_VERSION(4, 4, 302) < LINUX_VERSION_CODE)
		.get_sset_count	= yt8628_get_sset_count,
		.get_strings	= yt8628_get_strings,
		.get_stats	= yt8628_get_stats,
#endif
		.config_intr	= yt8628_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt85xxs_handle_interrupt,
#else
		.ack_interrupt	= yt85xxs_ack_interrupt,
#endif
		.config_aneg	= ytphy_config_aneg,
		.probe		= yt8628_probe,
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= yt8628_soft_reset,
#endif
		.config_init	= yt8628_config_init,
		.read_status	= yt8618_read_status,
		.suspend	= yt8628_suspend,
		.resume		= yt8628_resume,
	}, {
		.phy_id		= PHY_ID_YT8824,
		.name		= "YT8824 Quad Ports 2.5Gbps Ethernet",
		.phy_id_mask	= MOTORCOMM_PHY_ID_MASK,
#if (KERNEL_VERSION(5, 1, 0) > LINUX_VERSION_CODE)
		.features	= PHY_GBIT_FEATURES,
#endif
		.config_intr	= yt8824_config_intr,
#if (KERNEL_VERSION(5, 10, 232) < LINUX_VERSION_CODE)
		.handle_interrupt	= yt8824_handle_interrupt,
#else
		.ack_interrupt	= yt8824_ack_interrupt,
#endif
#if (KERNEL_VERSION(3, 15, 0) > LINUX_VERSION_CODE)
#else
		.soft_reset	= yt8824_soft_reset,
#endif
		.config_aneg	= ytphy_config_aneg,
		.probe		= yt8824_probe,
#if (KERNEL_VERSION(3, 14, 79) < LINUX_VERSION_CODE)
		.aneg_done	= yt8824_aneg_done,
#endif
#if (KERNEL_VERSION(5, 0, 21) < LINUX_VERSION_CODE)
		.get_features	= genphy_c45_pma_read_abilities,
#endif
		.config_init	= yt8824_config_init,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.set_wol	= &yt8824_wol_feature_set,
		.get_wol	= &yt8824_wol_feature_get,
#endif
		.read_status	= yt8824_read_status,
		.suspend	= yt8824_suspend,
		.resume		= yt8824_resume,
	}, 
};

static int yt_phydrv_ver_show(struct seq_file *m, void *v) {
	seq_printf(m, "Driver Version: %s\n", YTPHY_LINUX_VERSION);

	return 0;
}

MOTORCOMM_DEBUG_READ_ENTRY(phydrv_ver);

#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
static int ytphy_drivers_register(struct phy_driver *phy_drvs, int size)
{
	int i, j;
	int ret;

	for (i = 0; i < size; i++) {
		ret = phy_driver_register(&phy_drvs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (j = 0; j < i; j++)
		phy_driver_unregister(&phy_drvs[j]);

	return ret;
}

static void ytphy_drivers_unregister(struct phy_driver *phy_drvs, int size)
{
	int i;

	for (i = 0; i < size; i++)
		phy_driver_unregister(&phy_drvs[i]);
}

static int __init ytphy_init(void)
{
	/* Create debugfs directory */
	root_dir = debugfs_create_dir(MODULE_NAME, NULL);
	if (!root_dir) {
		pr_err(MODULE_NAME ": Failed to create debugfs directory\n");
		return -ENOMEM;
	}

	/* Create debugfs file read only */
	if (!debugfs_create_file(PHYDRV_VER, 0444, root_dir, NULL,
				 &yt_phydrv_ver_fops)) {
		pr_err(MODULE_NAME ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(PHYREG_DUMP, 0222, root_dir, NULL,
				 &yt_phyreg_dump_fops)) {
		pr_err(PHYREG_DUMP ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(DRV_STRENGTH, 0222, root_dir, NULL,
				 &yt_drv_strength_fops)) {
		pr_err(DRV_STRENGTH ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(PHYREG, 0222, root_dir, NULL,
				 &yt_phyreg_fops)) {
		pr_err(PHYREG ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(TEMPLATE, 0222, root_dir, NULL,
				 &yt_template_fops)) {
		pr_err(TEMPLATE ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(CSD, 0222, root_dir, NULL,
				 &yt_csd_fops)) {
		pr_err(CSD ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(SNR, 0222, root_dir, NULL,
				 &yt_snr_fops)) {
		pr_err(SNR ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(CHECKER, 0222, root_dir, NULL,
				 &yt_checker_fops)) {
		pr_err(CHECKER ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(GENERATOR, 0222, root_dir, NULL,
				 &yt_generator_fops)) {
		pr_err(GENERATOR ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(LOOPBACK, 0222, root_dir, NULL,
				 &yt_loopback_fops)) {
		pr_err(LOOPBACK ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	return ytphy_drivers_register(ytphy_drvs, ARRAY_SIZE(ytphy_drvs));
}

static void __exit ytphy_exit(void)
{
	debugfs_remove_recursive(root_dir);
	pr_info(MODULE_NAME ": Module unloaded\n");
	ytphy_drivers_unregister(ytphy_drvs, ARRAY_SIZE(ytphy_drvs));
}

module_init(ytphy_init);
module_exit(ytphy_exit);
#else
/* for linux 4.x ~ */
/* module_phy_driver(ytphy_drvs);*/
static int __init phy_module_init(void)
{
	/* Create debugfs directory */
	root_dir = debugfs_create_dir(MODULE_NAME, NULL);
	if (!root_dir) {
		pr_err(MODULE_NAME ": Failed to create debugfs directory\n");
		return -ENOMEM;
	}

	/* Create debugfs file read only */
	if (!debugfs_create_file(PHYDRV_VER, 0444, root_dir, NULL,
				 &yt_phydrv_ver_fops)) {
		pr_err(PHYDRV_VER ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(PHYREG_DUMP, 0222, root_dir, NULL,
				 &yt_phyreg_dump_fops)) {
		pr_err(PHYREG_DUMP ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(DRV_STRENGTH, 0222, root_dir, NULL,
				 &yt_drv_strength_fops)) {
		pr_err(DRV_STRENGTH ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(PHYREG, 0222, root_dir, NULL,
				 &yt_phyreg_fops)) {
		pr_err(PHYREG ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(TEMPLATE, 0222, root_dir, NULL,
				 &yt_template_fops)) {
		pr_err(TEMPLATE ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(CSD, 0222, root_dir, NULL,
				 &yt_csd_fops)) {
		pr_err(CSD ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(SNR, 0222, root_dir, NULL,
				 &yt_snr_fops)) {
		pr_err(SNR ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(CHECKER, 0222, root_dir, NULL,
				 &yt_checker_fops)) {
		pr_err(CHECKER ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(GENERATOR, 0222, root_dir, NULL,
				 &yt_generator_fops)) {
		pr_err(GENERATOR ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(LOOPBACK, 0222, root_dir, NULL,
				 &yt_loopback_fops)) {
		pr_err(LOOPBACK ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	return phy_drivers_register(ytphy_drvs, ARRAY_SIZE(ytphy_drvs),
				    THIS_MODULE);
}
static void __exit phy_module_exit(void)
{
	debugfs_remove_recursive(root_dir);
	pr_info(MODULE_NAME ": Module unloaded\n");

	phy_drivers_unregister(ytphy_drvs, ARRAY_SIZE(ytphy_drvs));
}

module_init(phy_module_init);
module_exit(phy_module_exit);
#endif

MODULE_DESCRIPTION("Motorcomm PHY driver");
MODULE_VERSION(YTPHY_LINUX_VERSION);	/* for modinfo xxxx.ko in userspace */
MODULE_AUTHOR("Leilei Zhao");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_YT8510, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8511, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8512, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8522, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8521, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8531S, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8531, MOTORCOMM_PHY_ID_MASK },
#ifdef YTPHY_YT8543_ENABLE
	{ PHY_ID_YT8543, MOTORCOMM_PHY_ID_MASK },
#endif
	{ PHY_ID_YT8614, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8614Q, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8618, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8821, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8111, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8628, MOTORCOMM_PHY_ID_MASK },
	{ PHY_ID_YT8824, MOTORCOMM_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);

