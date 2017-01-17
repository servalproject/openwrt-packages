include $(TOPDIR)/rules.mk

PKG_NAME:=serval-mesh-extender-2.0
PKG_VERSION:=START.20170117.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libc

include $(INCLUDE_DIR)/package.mk

define Package/serval-mesh-extender-2.0
	TITLE:=Serval Project Mesh Extender 2.0 Main package
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+serval-dna +serval-lbard +flash-rfd900
	MAINTAINER:=Paul Gardner-Stephen <paul@servalproject.org>
endef

define Package/serval-mesh-extender/description
	Top-level package that configures a GL-AR150, Domino Core or similar
	device for use as a Serval Mesh Extender.
endef

define Package/conffiles
	/etc/init.d/...
endef

define Package/serval-mesh-extender-2.0/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/flash900 $(1)/usr/bin
endef

