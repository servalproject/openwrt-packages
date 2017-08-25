include $(TOPDIR)/rules.mk

PKG_NAME:=flash-rfd900
PKG_VERSION:=START.20170825.1336.01
PKG_RELEASE:=39

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/servalproject/flash-rfd900.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=df2056851c5740f3bed08f5f0b005403d02682fa
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libc

include $(INCLUDE_DIR)/package.mk

define Package/flash-rfd900
	TITLE:=Serval Project RFD900 fast re-flashing tool
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+libc
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/flash-rfd900/description
	Reflash RFD900 radio to supplied firmware if it is not already running it.
endef

define Build/Configure
	echo "$(PKG_VERSION)" >$(PKG_BUILD_DIR)/$(CONFIGURE_PATH)/$(strip $(3))/VERSION.txt
	$(call Build/Configure/Default,$(1),$(2),$(3))
endef

define Package/flash-rfd900/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/flash900 $(1)/usr/bin
endef

$(eval $(call BuildPackage,flash-rfd900))
