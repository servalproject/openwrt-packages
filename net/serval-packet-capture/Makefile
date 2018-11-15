include $(TOPDIR)/rules.mk

PKG_NAME=packet-capture
PKG_VERSION:=MESHOBSERVER0.1
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/jLanc/MeshObserver-Packet-Capture.git

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=+libpcap-dev

include $(INCLUDE_DIR)/package.mk

define Package/packet-capture
	TITLE:=Serval Mesh Observer Packet Capture Service
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+librt +libsodium1 +libpcap-dev
	MAINTAINER:=Serval Mesh Observer Maintainer
endef

define Package/serval-dna/description
	Packet capture is a daemon that uses a round robin technique to capture packets from the mon0 interface defined in the Mesh Observer firmware, as well as 4 serial ports, which are UTAR serial to bridge USB adapters intercepting information from the Mesh Extender Breakout Board.

Captured packets are sent down the wire and into the networking lab.
endef

define Build/Configure
	$(call Build/Configure/Default,--with-linux-headers=$(LINUX_DIR))
endef

define Package/packet-capture/install
	$(INSTALL_DIR) $(1)/usr/bin
endef

$(eval $(call BuildPackage,packet-capture))
