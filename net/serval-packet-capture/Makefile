include $(TOPDIR)/rules.mk

PKG_NAME=packet-capture
PKG_VERSION:=MESHOBSERVER0.1
PKG_RELEASE:=2

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/jLanc/MeshObserver-Packet-Capture
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=977bdf3155b14fc55327569b805f4a42b7ae633e
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=+libpcap-dev

include $(INCLUDE_DIR)/package.mk

define Package/packet-capture
	TITLE:=Serval Mesh Observer Packet Capture Service
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+libpcap-dev
	MAINTAINER:=Serval Mesh Observer Maintainer
endef

define Package/serval-dna/description
	Packet capture is a daemon that uses a round robin technique to capture packets from the mon0 interface defined in the Mesh Observer firmware, as well as 4 serial ports, which are UTAR serial to bridge USB adapters intercepting information from the Mesh Extender Breakout Board.

Captured packets are sent down the wire and into the networking lab.
endef

$(eval $(call BuildPackage,packet-capture))
