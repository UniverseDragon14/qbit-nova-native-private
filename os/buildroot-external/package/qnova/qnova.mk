################################################################################
#
# qnova (QBIT NOVA Native)
#
################################################################################

# Built from this repository tree. The external tree lives at
# os/buildroot-external, so the repository root is two levels up.
QNOVA_VERSION = 0.5
QNOVA_SITE = $(BR2_EXTERNAL_UNIVERSAL_DRAGON_PATH)/../..
QNOVA_SITE_METHOD = local
QNOVA_LICENSE = Proprietary
QNOVA_DEPENDENCIES = openssl host-pkgconf

# The project Makefile discovers OpenSSL through pkg-config and defaults to
# -std=c17. Pass the cross compiler, the host pkgconf (resolved against the
# target sysroot via TARGET_MAKE_ENV), and the target CFLAGS while keeping the
# C17 standard. -Werror is intentionally dropped here so a warning from a
# different cross toolchain version does not fail an image build.
define QNOVA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		CFLAGS="$(TARGET_CFLAGS) -std=c17" \
		all
endef

define QNOVA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/qnova $(TARGET_DIR)/usr/bin/qnova
endef

$(eval $(generic-package))
