include $(TOPDIR)/rules.mk

PKG_NAME:=serval-packet-capture
PKG_VERSION:=START.20180813.0810.50
PKG_RELEASE:=14

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/jLanc/MeshObserver-Packet-Capture
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=abe467780d59b05116e4e466f7b5c423a5792201
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libpcap +libc

include $(INCLUDE_DIR)/package.mk

define Package/serval-packet-capture
	TITLE:=Serval Low-Bandwidth Asynchronous Rhizome 
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+libpcap +libc
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/serval-packet-capture/description
	Packet capture is a daemon that uses a round robin technique to capture packets from mon0 and 4 ttyUSBx serial to USB UTAR bridge adapters.
endef

define Package/serval-packet-capture/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/cap $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-packet-capture))
