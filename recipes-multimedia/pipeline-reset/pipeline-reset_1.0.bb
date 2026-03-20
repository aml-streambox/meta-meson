DESCRIPTION = "Pipeline reset tool for vdin1 and Wave521 encoder recovery"
SUMMARY = "Tool to reset vdin1 (/dev/video71) and Wave521 encoder (/dev/amvenc_multi) to clean state after unclean shutdown"
HOMEPAGE = "https://github.com/anomalyco/aml-comp"
LICENSE = "AMLOGIC"
LIC_FILES_CHKSUM = "file://${COREBASE}/../meta-meson/license/AMLOGIC;md5=6c70138441c57c9e1edb9fde685bd3c8"

SRC_URI = " \
    file://pipeline-reset.c \
    file://Makefile \
"

S = "${WORKDIR}"

inherit autotools-brokensep

do_configure[noexec] = "1"
do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/pipeline-reset ${D}${bindir}/
}

FILES:${PN} = "${bindir}/pipeline-reset"

RDEPENDS:${PN} = ""
