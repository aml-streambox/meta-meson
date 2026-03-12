SUMMARY = "GStreamer plugin for Amlogic YUV422 10-bit to P010/Wave521 conversion"
DESCRIPTION = " \
    A GStreamer plugin that converts Amlogic VDIN 40-bit packed YUV422 10-bit format \
    to either P010 (YUV420 10-bit) or Wave521 32-bit packed 420 10-bit format. \
    Uses ARM NEON multi-threading for high performance on Amlogic A311D2. \
"
HOMEPAGE = "https://github.com/amlogic/gst-aml-v10conv"
LICENSE = "LGPL-2.1-or-later"
LIC_FILES_CHKSUM = "file://LICENSE;md5=5e94e2734d0bfbf2f5cda3677aa3bc3d"

# For now, use local file source
# TODO: Switch to GitHub once repository is created
# SRC_URI = "git://github.com/amlogic/gst-aml-v10conv.git;protocol=https;branch=main"
# SRCREV = "${AUTOREV}"

# Local source for development
SRC_URI = "file://gst-aml-v10conv"
S = "${WORKDIR}/gst-aml-v10conv"

inherit meson pkgconfig

DEPENDS = " \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-bad \
"

# ARM-specific build flags
TARGET_CC_ARCH += "${LDFLAGS}"
NEON_FLAGS = "-march=armv8-a -mtune=cortex-a55 -O3 -D_GNU_SOURCE"

EXTRA_OEMESON += " \
    -Dc_args='${NEON_FLAGS}' \
"

# Install the plugin to GStreamer's plugin directory
FILES:${PN} += " \
    ${libdir}/gstreamer-1.0/libgstamlv10conv.so \
"

# Debug package
FILES:${PN}-dbg += " \
    ${libdir}/gstreamer-1.0/.debug/libgstamlv10conv.so \
"

# Development package
FILES:${PN}-dev += " \
    ${includedir}/gst-aml-v10conv/*.h \
"

# Package configuration
PACKAGECONFIG ??= ""

# Runtime dependencies
RDEPENDS:${PN} += " \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
"

# QA checks
INSANE_SKIP:${PN} += " \
    dev-so \
    ldflags \
"

# Ensure the plugin is found by GStreamer
pkg_postinst:${PN}() {
    # Force GStreamer registry update
    if [ -x ${bindir}/gst-inspect-1.0 ]; then
        gst-inspect-1.0 --plugin amlv10conv > /dev/null 2>&1 || true
    fi
}
