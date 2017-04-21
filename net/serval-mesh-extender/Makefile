include $(TOPDIR)/rules.mk

PKG_NAME:=serval-mesh-extender
PKG_VERSION:=START.20170422.0455.05
PKG_RELEASE:=11

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
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/dos
	$(INSTALL_BIN) ./files/dos/MeshExtender $(1)/dos
	$(INSTALL_BIN) ./files/etc/banner.meshextender $(1)/etc
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/dhcp $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/fstab $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/network.template $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/system $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/uhttpd $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/wireless.template $(1)/etc/config
	$(INSTALL_DIR) $(1)/etc/hotplug.d
	$(INSTALL_DIR) $(1)/etc/hotplug.d/button
	$(INSTALL_BIN) ./files/etc/hotplug.d/button/buttons $(1)/etc/hotplug.d/button
	$(INSTALL_BIN) ./files/etc/hotplug2.rules $(1)/etc
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/dropbear $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/inittab $(1)/etc
	$(INSTALL_BIN) ./files/etc/profile $(1)/etc
	$(INSTALL_DIR) $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S15wireless_setup $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S47mountstuff $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S49servald $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S94captiveportal $(1)/etc/rc.d
	$(INSTALL_DIR) $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/manuallbard $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/meshmsresponder $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/ota-bundleid.txt $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/otaupdate $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/runlbard $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/runservald $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/serval.conf $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/rfd900-43-91.ihx $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/rfd900-82-86.ihx $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/rfd900-82-91.ihx $(1)/etc/serval
	$(INSTALL_DIR) $(1)/mesh-extender
	$(INSTALL_BIN) ./files/mesh-extender/upgrade $(1)/mesh-extender
	$(INSTALL_DIR) $(1)/serval
	$(INSTALL_BIN) ./files/serval/MeshExtender $(1)/serval
	$(INSTALL_DIR) $(1)/serval/www
	$(INSTALL_BIN) ./files/serval/www/index.html $(1)/serval/www
	$(INSTALL_DIR) $(1)/serval-var
	$(INSTALL_BIN) ./files/serval-var/MeshExtender $(1)/serval-var
endef

$(eval $(call BuildPackage,serval-mesh-extender))
