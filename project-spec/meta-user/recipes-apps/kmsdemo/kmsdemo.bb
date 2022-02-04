#
# This file is the kmsdemo recipe.
#

SUMMARY = "Simple kmsdemo application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI = "file://kmsdemo.c \
	   file://Makefile \
		  "

S = "${WORKDIR}"

DEPENDS += "libvrc"

do_compile() {
	${CC} ${CFLAGS} ${LDFLAGS} -o kmsdemo kmsdemo.c -lvrc
}

do_install() {
	     install -d ${D}${bindir}
	     install -m 0755 kmsdemo ${D}${bindir}
}

FILES_${PN} += "kmsdemo"
