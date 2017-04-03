/*
 *  Apple Airport Express 3rd gen support
 *
 *  Copyright (C) 2016 Steve White <stepwhite@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>
#include <linux/ar8216_platform.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/i2c-gpio.h>
#include <linux/kernel.h>
#include <linux/ctype.h>

#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-ap9x-pci.h"
#include "dev-eth.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-spi.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "machtypes.h"

#define APPLE_K31_KEYS_POLL_INTERVAL         20      /* msecs */

#define APPLE_K31_GPIO_SW4                   4
#define APPLE_K31_GPIO_GREEN_LED             14
#define APPLE_K31_GPIO_AMBER_LED             15
#define APPLE_K31_GPIO_LAN_LED               11
#define APPLE_K31_GPIO_WAN_LED               17

#define APPLE_K31_SCFG_OFFSET                0x00080000
#define APPLE_K31_SCFG_SIZE                  0x40000
// XXX: Not sure if this is a magic or a checksum...
#define APPLE_K31_SCFG_MAGIC                 0xec85ffb7
#define APPLE_K31_SCFG_ETH_ADDR              "ethaddr"
#define APPLE_K31_SCFG_CAL_DATA_0            "radio-cal-ath0"
#define APPLE_K31_SCFG_CAL_DATA_1            "radio-cal-ath1"
#define APPLE_K31_SCFG_SER_NUM               "apple-sn"

static struct mtd_partition apple_k31_partitions[] = {
    {
        .name          = "boot",
        .offset                = 0x00000000,
        .size          = 0x80000,
    }, {
        .name          = "scfg",
        .offset                = APPLE_K31_SCFG_OFFSET,
        .size          = APPLE_K31_SCFG_SIZE,
    }, {
        .name          = "dcfg",
        .offset                = 0x000C0000,
        .size          = 0x140000,
        .mask_flags    = MTD_WRITEABLE,
    }, {
        .name          = "primary",
        .offset                = 0x00200000,
        .size          = 0x700000,
        .mask_flags    = MTD_WRITEABLE,
    }, {
        .name          = "secondary",
        .offset                = 0x00900000,
        .size          = 0x700000,
        .mask_flags    = MTD_WRITEABLE,
    }
};

static struct ar8327_pad_cfg apple_k31_ar8327_pad0_cfg = {
    .mode = AR8327_PAD_MAC_RGMII,
    .txclk_delay_en = true,
    .rxclk_delay_en = true,
    .txclk_delay_sel = AR8327_CLK_DELAY_SEL1,
    .rxclk_delay_sel = AR8327_CLK_DELAY_SEL2,
};

static struct ar8327_platform_data apple_k31_ar8327_data = {
    .pad0_cfg = &apple_k31_ar8327_pad0_cfg,
    .port0_cfg = {
        .force_link = 1,
        .speed = AR8327_PORT_SPEED_1000,
        .duplex = 1,
        .txpause = 1,
        .rxpause = 1,
    },
};

static struct mdio_board_info apple_k31_mdio0_info[] = {
    {
        .bus_id = "ag71xx-mdio.0",
        .phy_addr = 0,
        .platform_data = &apple_k31_ar8327_data,
    },
};

static struct flash_platform_data apple_k31_flash_data = {
    .parts             = apple_k31_partitions,
    .nr_parts  = ARRAY_SIZE(apple_k31_partitions),
};

static struct i2c_gpio_platform_data apple_k31_i2c_gpio_data = {
    .sda_pin        = 12,
    .scl_pin        = 13,
};

static struct platform_device apple_k31_i2c_gpio = {
    .name           = "i2c-gpio",
    .id             = 0,
    .dev            = {
        .platform_data  = &apple_k31_i2c_gpio_data,
    },
};

static struct gpio_keys_button apple_k31_gpio_keys[] __initdata = {
    {
        .desc              = "sw4",
        .type              = EV_KEY,
        .code              = KEY_RESTART,
        .debounce_interval = APPLE_K31_KEYS_POLL_INTERVAL,
        .gpio              = APPLE_K31_GPIO_SW4,
        .active_low        = 1,
    }
};

static struct gpio_led apple_k31_leds_gpio[] __initdata = {
    {
        .name           = "apple:amber:led1",
        .gpio           = APPLE_K31_GPIO_AMBER_LED,
        .active_low     = 1,
    }, {
        .name           = "apple:green:led1",
        .gpio           = APPLE_K31_GPIO_GREEN_LED,
        .active_low     = 1,
    }, {
        .name           = "apple:wan:led",
        .gpio           = APPLE_K31_GPIO_WAN_LED,
        .active_low     = 1,
    }, {
        .name           = "apple:lan:led",
        .gpio           = APPLE_K31_GPIO_LAN_LED,
        .active_low     = 1,
    }
};

static inline char * apple_k31_get_scfg(void)
{
    u32 *scfg = (u32 *) KSEG1ADDR(0x1f000000 + APPLE_K31_SCFG_OFFSET);
    char *result;
    
    if (scfg[0] != APPLE_K31_SCFG_MAGIC)
    {
        pr_info("apple_k31: expected scfg magic:0x%08x, read:0x%08x\n", APPLE_K31_SCFG_MAGIC, scfg[0]);
    }
    
    result = (char *)(scfg + 1);
    if (isalnum(*result) == false)
    {
        return NULL;
    }
    
    return result;
}

static char * apple_k31_scfg_find_var(const char *name)
{
    char *ret = NULL;
    char *p;
    int len;
    char *scfg;
    
    scfg = apple_k31_get_scfg();
    if (scfg == NULL)
    {
        return NULL;
    }
    
    len = strlen(name);
    p = scfg;
    while (*p != 0x00 && (int)(p - scfg) < APPLE_K31_SCFG_SIZE)
    {
        if (strncmp(name, p, len) == 0 && p[len] == '=')
        {
            ret = p + len + 1;
            break;
        }
        p += strlen(p) + 1;
    }
    
    return ret;
}

static u8 *apple_k31_scfg_get_calibration_data(char *interface)
{
    u8 *tmpcal;
    u8 *caldata;
    int len;
    int success;
    
    tmpcal = apple_k31_scfg_find_var(interface);
    if (tmpcal == NULL)
    {
        return NULL;
    }
    
    len = strlen(tmpcal);
    if (len == 0 || len % 2 == 1)
    {
        return NULL;
    }
    
    len = len / 2;
    caldata = vmalloc(len);
    
    success = hex2bin(caldata, tmpcal, len);
    
    if (success != 0)
    {
        vfree(caldata);
        caldata = NULL;
    }
    
    return caldata;
}

static int apple_k31_scfg_get_eth_address(u8 *mac)
{
    u8 *tmpmac;
    int i;
    int len;
    
    tmpmac = apple_k31_scfg_find_var(APPLE_K31_SCFG_ETH_ADDR);
    if (tmpmac == NULL)
    {
        return -1;
    }
    
    len = strlen(tmpmac);
    if (len != 17)
    {
        return -1;
    }
    
    for (i = 0; i < 6; i++) {
        int high, low;
        
        high = hex_to_bin(*tmpmac++);
        low = hex_to_bin(*tmpmac++);
        
        if ((high < 0) || (low < 0))
        {
            return -1;
        }
        
        mac[i] = (high << 4) | low;
        
        if (i < 5 && *tmpmac++ != ':')
        {
            return -1;
        }
    }
    
    return 0;
}

static void __init apple_k31_setup(void)
{
    u8 *caldata;
    u8 mac[6];
    int success;
    
    mdiobus_register_board_info(apple_k31_mdio0_info,
                                ARRAY_SIZE(apple_k31_mdio0_info));
    
    ath79_register_mdio(0, 0x0);
    ath79_register_mdio(1, 0x0);
    
    success = apple_k31_scfg_get_eth_address(mac);
    if (success == 0)
    {
        ath79_init_mac(ath79_eth0_data.mac_addr, mac, 1);
        ath79_init_mac(ath79_eth1_data.mac_addr, mac, 2);
        
        caldata = apple_k31_scfg_get_calibration_data(APPLE_K31_SCFG_CAL_DATA_0);
        if (caldata != NULL)
        {
            ath79_register_wmac(caldata, mac);
            ap91_pci_init(caldata, mac);
            vfree(caldata);
        }
    }
    
    ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
    ath79_eth0_data.phy_mask = BIT(0);
    ath79_eth0_data.mii_bus_dev = &ath79_mdio0_device.dev;
    ath79_eth0_pll_data.pll_1000 = 0x06000000;
    ath79_register_eth(0);
    
    ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
    ath79_eth1_data.mii_bus_dev = &ath79_mdio1_device.dev;
    ath79_register_eth(1);
    
    /*
     * XXX: K31 is using w25q128, not m25p80.
     * But this works.
     */
    ath79_register_m25p80(&apple_k31_flash_data);
    
    ath79_register_usb();
    
    /*
     * K31 has a TI TMP512/513 Temperature and Power Supply Monitor
     * on the i2c bus at address 0x5c
     */
    platform_device_register(&apple_k31_i2c_gpio);
    
    ath79_register_gpio_keys_polled(-1, APPLE_K31_KEYS_POLL_INTERVAL,
                                    ARRAY_SIZE(apple_k31_gpio_keys),
                                    apple_k31_gpio_keys);
    
    ath79_register_leds_gpio(1, ARRAY_SIZE(apple_k31_leds_gpio),
                             apple_k31_leds_gpio);
    
    /* K31 also has an i2s bus for audio output... */
}

MIPS_MACHINE(ATH79_MACH_APPLE_K31, "APPLE-K31", "Apple Airport Express K31",
             apple_k31_setup);


