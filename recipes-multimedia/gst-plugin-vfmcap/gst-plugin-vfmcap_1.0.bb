DESCRIPTION = "GStreamer vfmcap source plugin - zero-copy HDMI capture"

LICENSE = "LGPL-2.1-or-later"
LIC_FILES_CHKSUM = "file://${COREBASE}/../meta-meson/license/AMLOGIC;md5=6c70138441c57c9e1edb9fde685bd3c8"

DEPENDS += "gstreamer1.0 gstreamer1.0-plugins-base"
DEPENDS += "libvfmcap"

inherit autotools pkgconfig

do_configure[noexec] = "1"

EXTRA_OEMAKE = "TARGET_DIR=${D} STAGING_DIR=${D} DESTDIR=${D} PKG_CONFIG=${STAGING_BINDIR_NATIVE}/pkg-config"

do_compile() {
    oe_runmake -C ${S} ${EXTRA_OEMAKE} all
}

do_install() {
    install -d -m 0755 ${D}${libdir}/gstreamer-1.0/
    install -D -m 0755 ${S}/libgststreamboxsrc.so ${D}${libdir}/gstreamer-1.0/
}

FILES:${PN} = "${libdir}/gstreamer-1.0/*"
