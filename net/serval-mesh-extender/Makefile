include $(TOPDIR)/rules.mk

PKG_NAME:=serval-mesh-extender
PKG_VERSION:=START.20170207.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libc

include $(INCLUDE_DIR)/package.mk

define Package/serval-mesh-extender
	TITLE:=Serval Project Mesh Extender 2.0 Main package
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+serval-dna +serval-lbard +flash-rfd900
	MAINTAINER:=Paul Gardner-Stephen <paul@servalproject.org>
	URL:=http://servalproject.org
endef

define Package/serval-mesh-extender/description
	Top-level package that configures a Serval Mesh Extender 2.0
	for use.
endef

define Package/conffiles
	/etc/init.d/dropbear
endef

define Build/Compile
endef

define Package/serval-mesh-extender/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/foo $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-mesh-extender))
