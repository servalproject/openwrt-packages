include $(TOPDIR)/rules.mk

PKG_NAME:=serval-dna
PKG_VERSION:=MESHEXTENDER-20150915.1
PKG_RELEASE=16

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/servalproject/serval-dna.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=3049252c8ac521302cbaa1c6818343e2f67bbeeb
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=

include $(INCLUDE_DIR)/package.mk

define Package/serval-dna
	TITLE:=Serval DNA mesh networking daemon
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=+librt
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/serval-dna/description
  Serval DNA is a daemon that implements the Serval Mesh network protocol as an
  overlay network encapsulated within UDP/IP packets over Wi-Fi.  The daemon,
  when running, joins the OpenWRT router to the local Serval Mesh Wi-Fi
  network.  This allows it to participate in dynamic mesh routing (using
  Serval's own mesh routing protocol), file distribution (using the Rhizome
  store-and-forward protocol), secure text messaging (using the MeshMS
  protocol), and to relay secure voice calls (using the VoMP voice-over-mesh
  protocol) and other secure Serval traffic (such as tunnelled SOCKS proxy
  connections) between Android devices running the Serval Mesh app.

  The Serval Mesh Extender device is essentially a Wi-Fi router running the
  Serval DNA daemon on OpenWRT.
endef

define Build/Configure
	echo "$(PKG_VERSION)" >$(PKG_BUILD_DIR)/$(CONFIGURE_PATH)/$(strip $(3))/VERSION.txt
	$(call Build/Configure/Default,$(1),$(2),$(3))
endef

define Package/serval-dna/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/servald $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-dna))
