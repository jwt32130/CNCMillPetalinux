#
# This file is the libvrc recipe.
#

SUMMARY = "Simple libvrc application"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI = " \
		file://libvrc.c \
		file://libvrc.h \
		file://Makefile \
		"

S = "${WORKDIR}"

PACKAGE_ARCH = "${MACHINE_ARCH}"
PROVIDES = "vrc"
TARGET_CC_ARCH += "${LDFLAGS}"


do_install() {
	install -d ${D}${libdir}
	install -d ${D}${includedir}

	oe_libinstall -so libvrc ${D}${libdir}

	install -d -m 0655 ${D}${includedir}/VRC

	install -m 0644 ${S}/*.h ${D}${includedir}/VRC/
}
FILES_${PN} = "${libdir}/*.so.* ${includedir}/*"
FILES_${PN}-dev = "${libdir}/*.so"
