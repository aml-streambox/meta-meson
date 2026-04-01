DESCRIPTION = "VFM Capture SDK - Zero-copy HDMI capture library with Vulkan GPU conversion"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/../meta-meson/license/AMLOGIC;md5=6c70138441c57c9e1edb9fde685bd3c8"

DEPENDS += "vulkan-headers vulkan-loader"

inherit pkgconfig

do_configure[noexec] = "1"

EXTRA_OEMAKE = "TARGET_DIR=${D} STAGING_DIR=${D} DESTDIR=${D} PKG_CONFIG=${STAGING_BINDIR_NATIVE}/pkg-config"

do_compile() {
    oe_runmake -C ${S} ${EXTRA_OEMAKE} all
}

do_install() {
    install -d -m 0755 ${D}${libdir}
    install -d -m 0755 ${D}${bindir}
    install -d -m 0755 ${D}${includedir}
    install -D -m 0755 ${S}/libvfmcap.so ${D}${libdir}/libvfmcap.so
    install -D -m 0755 ${S}/vfmcap-demo ${D}${bindir}/vfmcap-demo
    install -D -m 0644 ${S}/include/vfmcap.h ${D}${includedir}/vfmcap.h
}

FILES:${PN} = " ${libdir}/libvfmcap.so ${bindir}/vfmcap-demo "
FILES:${PN}-dev = " ${includedir}/* "

INSANE_SKIP:${PN} = "dev-so useless-rpaths"
