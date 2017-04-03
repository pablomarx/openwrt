#
# Copyright (C) 2009 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

define Profile/APPLEK31
        NAME:=Apple AirPort Express A1392
        PACKAGES:= \
                kmod-usb-core kmod-usb-ohci kmod-usb2 kmod-ledtrig-usbdev kmod-i2c-core kmod-i2c-gpio-custom
endef

define Profile/APPLEK31/Description
        Package set optimized for the Apple AirPort Express A1392
endef
$(eval $(call Profile,APPLEK31))
