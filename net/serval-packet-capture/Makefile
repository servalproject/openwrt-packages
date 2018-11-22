include $(TOPDIR)/rules.mk

PKG_NAME:=serval-packet-capture
PKG_VERSION:=START.20180813.0810.50
PKG_RELEASE:=11

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/jLanc/MeshObserver-Packet-Capture
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=bd138c00c90ae21606831dda26abee389892330a
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libpcap +libc

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/serval-packet-capture
	TITLE:=Serval Low-Bandwidth Asynchronous Rhizome 
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+libpcap-dev
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/serval-packet-capture/description
	Packet capture is a daemon that uses a round robin technique to capture packets from mon0 and 4 ttyUSBx serial to USB UTAR bridge adapters.
endef

define Build/Configure
	echo "$(PKG_VERSION)" >$(PKG_BUILD_DIR)/$(CONFIGURE_PATH)/$(strip $(3))/VERSION.txt
	$(call Build/Configure/Default,$(1),$(2),$(3))
endef

define Package/serval-packet-capture/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/cap $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-packet-capture))
