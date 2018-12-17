#system package requirements
#sudo apt-get install libnet-dev
#libnet-dev package already packed in my version of openwrt

include $(TOPDIR)/rules.mk

PKG_NAME:=serval-packet-capture
PKG_VERSION:=START.20181217.1337.01
PKG_RELEASE:=343

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/jLanc/MeshObserver-Packet-Capture
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=94ada5e3174559c22d478344dea40bbfc0e49466
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libc +libpcap +serval-packet-capture

include $(INCLUDE_DIR)/package.mk

define Package/serval-packet-capture
	TITLE:=Serval Packet Capture
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+libc +libpcap +serval-packet-capture 
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/serval-packet-capture/description
	Serval Rhizome transport helper for packet radio and other low-bandwidth interfaces.
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
