# This OpwnWRT Makefile builds the Serval LBARD daemon as an OpenWRT package,
# using the source code from a local working copy (possibly modified), instead
# of downloading it from GitHub.  This Makefile is intended for use by
# developers to make the edit-compile-flash-test work cycle as fast as
# possible.
#
# This Makefile is NOT SUITABLE for making public releases of the Serval DNA
# package; use the https://github.com/servalproject/openwrt-packages repository
# for that.
#
# @author Paul Gardner-Stephen <paul@servalproject.org>
# @author Andrew Bettison <andrew@servalproject.org>

THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

include $(TOPDIR)/rules.mk

PKG_NAME:=lbard
PKG_VERSION=devel
PKG_RELEASE=1

PKG_SOURCE_PROTO:=update-from-local-git
PKG_SOURCE_URL:=$(realpath $(dir $(THIS_MAKEFILE))../../..)
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=$(shell cd $(PKG_SOURCE_URL) && ./version_string.sh)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz

PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=

Validate/update-from-local-git=VERSION SUBDIR

define DownloadMethod/update-from-local-git
	echo "Copying files from $(PKG_SOURCE_URL)..."; \
	cd $(PKG_SOURCE_URL) && \
	git status --short >/dev/null && \
	mkdir -p $(TMP_DIR)/dl && \
	cd $(TMP_DIR)/dl && \
	rm -rf $(SUBDIR) && \
	[ \! -d $(SUBDIR) ] && \
	( cd $(PKG_SOURCE_URL) && \
		git ls-files --cached --exclude-standard | \
		cpio -p --make-directories --link --dereference --preserve-modification-time --quiet $(TMP_DIR)/dl/$(SUBDIR)) && \
	echo "Packing files..." && \
	$(call dl_pack,$(TMP_DIR)/dl/$(FILE),$(SUBDIR)) && \
	mv $(TMP_DIR)/dl/$(FILE) $(DL_DIR)/ && \
	rm -rf $(SUBDIR);
endef

include $(INCLUDE_DIR)/package.mk

define Package/serval-lbard
	TITLE:=Serval Low-Bandwidth Asynchronous Rhizome Daemon
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Mesh networking
	DEPENDS:=
	MAINTAINER:=Serval DNA Maintainer <serval-dna@servalproject.org>
endef

define Package/serval-lbard/description
  Serval LBARD is a daemon that communicates with the Serval DNA daemon to
  transport Rhizome bundles via low-bandwidth and/or otherwise unusual
  transports.
endef

define Build/Configure
	echo "$(PKG_SOURCE_VERSION)" >$(PKG_BUILD_DIR)/$(CONFIGURE_PATH)/$(strip $(3))/VERSION.txt
	$(call Build/Configure/Default,$(1),$(2),$(3))
endef

define Package/serval-lbard/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/servald $(1)/usr/bin
endef

$(eval $(call BuildPackage,serval-dna))
