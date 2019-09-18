include $(TOPDIR)/rules.mk

PKG_NAME:=serval-packet-capture
PKG_VERSION:=START.2019.0918.1006.01
PKG_RELEASE:=376

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/thomotron/serval-mesh-observer-packet-capture
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=a87f43bb3f3ab49e8fd60882fa0141b83e16853e
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libc +argp-standalone

include $(INCLUDE_DIR)/package.mk

define Package/serval-packet-capture
	TITLE:=Serval Packet Capture
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+libc +libpcap +argp-standalone
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
	MAKE_PATH:=client
endef

define Package/serval-packet-capture/description
	Capture packets collected from both wifi and 4 serial interfaces.
endef

define Build/Configure
	echo "$(PKG_VERSION)" >$(PKG_BUILD_DIR)/$(CONFIGURE_PATH)/$(strip $(3))/VERSION.txt
	$(call Build/Configure/Default,$(1),$(2),$(3))
endef

define Package/serval-packet-capture/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/client/capture $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-packet-capture))
