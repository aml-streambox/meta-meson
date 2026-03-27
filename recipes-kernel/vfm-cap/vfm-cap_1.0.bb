SUMMARY = "VFM Capture Tee kernel module"
DESCRIPTION = "Kernel module that sits in the VFM chain and exposes a V4L2 capture device for zero-copy frame access from vdin0"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COREBASE}/../meta-meson/license/COPYING.GPL;md5=751419260aa954499f7abaabaa882bbe"

DEPENDS += "linux-meson"

PV = "1.0"

SRC_URI += "file://99-vfm-cap.rules"

do_populate_lic[noexec] = "1"
do_configure[noexec] = "1"

inherit module

EXTRA_OEMAKE = "KERNEL_SRC=${STAGING_KERNEL_DIR} M=${S}"

do_compile() {
    oe_runmake -j1 -C ${S} ${EXTRA_OEMAKE}
}

do_install() {
    VFM_CAP_DIR=${D}/lib/modules/${KERNEL_VERSION}/kernel/amlogic/vfm_cap
    install -d ${VFM_CAP_DIR}
    install -m 0644 ${S}/vfm_cap.ko ${VFM_CAP_DIR}

    install -d ${D}${bindir}
    install -m 0755 ${S}/vfm-cap-setup.sh ${D}${bindir}/vfm-cap-setup

    # Install udev rule for /dev/video_cap symlink
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/99-vfm-cap.rules ${D}${sysconfdir}/udev/rules.d/
}

KERNEL_MODULE_AUTOLOAD += "vfm_cap"

FILES:${PN} += "${bindir}/vfm-cap-setup"
FILES:${PN} += "${sysconfdir}/udev/rules.d/99-vfm-cap.rules"

RDEPENDS:${PN} += "kmod"
