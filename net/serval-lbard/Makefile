include $(TOPDIR)/rules.mk

PKG_NAME:=serval-lbard
PKG_VERSION:=START-20150903
PKG_RELEASE=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/servalproject/lbard.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=rfd900-adhoc
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

define Package/serval-lbard
        TITLE:=Serval Low-Bandwidth Asynchronous Rhizome Daemon
        SECTION:=net
        CATEGORY:=Network
        SUBMENU:=Mesh networking
        DEPENDS:=+serval-dna
        MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/serval-lbard/description
   Serval Rhizome transport helper for packet radio and other low-bandwidth interfaces.
endef

define Build/Configure
        echo "$(PKG_VERSION)" >$(PKG_BUILD_DIR)/$(CONFIGURE_PATH)/$(strip $(3))/VERSION.txt
        $(call Build/Configure/Default,$(1),$(2),$(3))
endef

define Package/serval-lbard/install
        $(INSTALL_DIR) $(1)/usr/bin
        $(INSTALL_BIN) $(PKG_BUILD_DIR)/lbard $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-lbard))
