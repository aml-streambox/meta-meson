SUMMARY = "aml tvserver streambox"

LICENSE = "AMLOGIC"
LIC_FILES_CHKSUM = "file://${COREBASE}/../meta-meson/license/AMLOGIC;md5=6c70138441c57c9e1edb9fde685bd3c8"

SRC_URI = "git://git@github.com/aml-streambox/aml_tvserver_streambox.git;protocol=ssh;branch=v0.4_dev"
SRCREV = "263dcb28172bf081c957d6038d7ddf4fa68d05f1"
PV = "0.4+git${SRCPV}"
SRC_URI +="file://tvserver.service"
SRC_URI +="file://streambox-tv.service"

DEPENDS = " libbinder sqlite3 aml-audio-service cjson"
DEPENDS += "linux-uapi-headers"
DEPENDS += "aml-ubootenv"
RDEPENDS:${PN} = " liblog libbinder aml-audio-service aml-ubootenv cjson"
do_configure[noexec] = "1"
inherit autotools pkgconfig systemd
S="${WORKDIR}/git"

EXTRA_OEMAKE="OUT_DIR=${B} STAGING_DIR=${STAGING_DIR_TARGET} \
                TARGET_DIR=${D} \
             "
do_compile() {
    cd ${S}
    oe_runmake ${EXTRA_OEMAKE} all
}
do_install() {
   install -d ${D}${libdir}
   install -d ${D}${bindir}
   install -d ${D}${includedir}
   install -d ${D}${sysconfdir}/streambox-tv

    cd ${S}
    oe_runmake ${EXTRA_OEMAKE} install
    install -m 0644 ${S}/client/include/*.h ${D}${includedir}
    # Install default config file
    install -m 0644 ${S}/files/config.json ${D}${sysconfdir}/streambox-tv/config.json
    if [ "${@bb.utils.contains("DISTRO_FEATURES", "systemd", "yes", "no", d)}" = "yes"  ]; then
        install -D -m 0644 ${WORKDIR}/tvserver.service ${D}${systemd_unitdir}/system/tvserver.service
        install -D -m 0644 ${WORKDIR}/streambox-tv.service ${D}${systemd_unitdir}/system/streambox-tv.service
    fi
}
SYSTEMD_SERVICE:${PN} = "tvserver.service streambox-tv.service"

FILES:${PN} = "${libdir}/* ${bindir}/* ${sysconfdir}/streambox-tv/*"
FILES:${PN}-dev = "${includedir}/* "
INSANE_SKIP:${PN} = "dev-so ldflags dev-elf"
INSANE_SKIP:${PN}-dev = "dev-so ldflags dev-elf"
