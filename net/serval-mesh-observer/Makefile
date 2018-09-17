include $(TOPDIR)/rules.mk

PKG_NAME:=serval-mesh-observer
PKG_VERSION:=START.20180429.0810.50
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:= +libc

include $(INCLUDE_DIR)/package.mk

define Package/serval-mesh-observer
	TITLE:=Serval Project Mesh Traffic Observer (for supporting development and optimisation)
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+serval-dna +flash-rfd900
	MAINTAINER:=Paul Gardner-Stephen <paul@servalproject.org>
	URL:=http://servalproject.org
endef

define Package/serval-mesh-observer/description
	Top-level package that configures a Serval Mesh Observer
	for use.
endef

define Package/conffiles #optional - list of config files installed by package
endef

define Build/Compile #optional - how to compile source - when left blank calls make by default
endef

define Package/serval-mesh-observer/install
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/dos
	$(INSTALL_DIR) $(1)/etc/serval
	$(INSTALL_BIN) ./files/dos/MeshObserver $(1)/dos
	$(INSTALL_BIN) ./files/etc/banner.meshobserver $(1)/etc
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/fstab $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/network.template $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/system $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/uhttpd $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/config/wireless.template $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/hotplug2.rules $(1)/etc
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/inittab $(1)/etc
	$(INSTALL_BIN) ./files/etc/profile $(1)/etc
	$(INSTALL_DIR) $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S13pre_wireless_setup $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S15wireless_setup $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S17mountstuff $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S47radioeeprom $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/rc.d/S99mountcheck $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/etc/serval/manuallbard $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/runlbard $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/runservald $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/serval.conf $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/boot_index_bottom.html $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/boot_index_middle.html $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/boot_index_top.html $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/index_postboot.html $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/boot_report $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/rfd900-43-91.ihx $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/rfd900-82-86.ihx $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/rfd900-82-91.ihx $(1)/etc/serval
	$(INSTALL_DIR) $(1)/serval
	$(INSTALL_BIN) ./files/serval/MeshObserver $(1)/serval
	$(INSTALL_BIN) ./files/etc/serval/lbard-restful.conf $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/fixfs $(1)/etc/serval
	$(INSTALL_BIN) ./files/etc/serval/ramfs $(1)/etc/serval
	$(INSTALL_BIN) ./files/serval-var/MeshObserver $(1)/serval-var
	#LBARD install definitions
	$(INSTALL_DIR) $(1)/etc/lbard
	$(INSTALL_DIR) $(1)/etc/lbard/include
	$(INSTALL_BIN) ./net/serval-mesh-observer/files/LBARD/MakeFile $(1)/etc/lbard
	#LBARD header files
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/code_instrumentation.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/fakecsmaradio.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/golay.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/hf.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/lbard.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/radio.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/radio+type.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/serial.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/sha1.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/sha3.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/sync.h $(1)/etc/lbard/include
	$(INSTALL_DATA) ./net/serval-mesh-observer/files/LBARD/include/util.h $(1)/etc/lbard/include
	#LBARD packages
	$(INSTALL_DIR) $(1)/etc/lbard/packages
	$(INSTALL_BIN) ./net/serval-mesh-observer/files/LBARD/packages/lbard/Makefile $(1)/etc/lbard/packages
	#LBARD source files? 
	

endef

$(eval $(call BuildPackage,serval-mesh-observer))
