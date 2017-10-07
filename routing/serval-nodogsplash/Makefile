#
# Copyright (C) 2013 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=serval-nodogsplash
PKG_FIXUP:=autoreconf
PKG_VERSION:=0.9_beta9.9.9-MeshExtender2.0
PKG_RELEASE:=6

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)/
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=git://github.com/servalproject/nodogsplash.git
PKG_SOURCE_VERSION:=098822371b2ca9fb95886477d65931c34483e519
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1
PKG_LICENSE:=GPL-2.0+

include $(INCLUDE_DIR)/package.mk


define Package/serval-nodogsplash
	SUBMENU:=Captive Portals
	SECTION:=net
	CATEGORY:=Network
	DEPENDS:=+libpthread +iptables-mod-ipopt
	TITLE:=Open public network gateway daemon (Serval Mesh Extender version)
	URL:=https://github.com/servalproject/nodogsplash
endef

define Package/serval-nodogsplash/description
	Nodogsplash offers a simple way to open a free hotspot providing
	restricted access to an internet connection.

	Modified for Serval Mesh Extender use:
	1. Add mime-type for Android APKs
	2. Add Serval Mesh Extender default configuration
endef

define Package/serval-nodogsplash/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nodogsplash $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ndsctl $(1)/usr/bin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) files/nodogsplash.init $(1)/etc/init.d/nodogsplash
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) files/nodogsplash.config $(1)/etc/config/nodogsplash

	$(INSTALL_DIR) $(1)/etc/$(PKG_NAME)/htdocs/images
	$(CP) $(PKG_BUILD_DIR)/resources/splash.html $(1)/etc/$(PKG_NAME)/htdocs/
	$(CP) $(PKG_BUILD_DIR)/resources/infoskel.html $(1)/etc/$(PKG_NAME)/htdocs/
	$(CP) $(PKG_BUILD_DIR)/resources/splash.jpg $(1)/etc/$(PKG_NAME)/htdocs/images/
endef

define Package/serval-nodogsplash/conffiles
/etc/nodogsplash/nodogsplash.conf
endef

$(eval $(call BuildPackage,serval-nodogsplash))
