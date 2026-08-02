_VERSION = 0.8-dev
VERSION  = `git describe --tags --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.19`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.19`

XWAYLAND =
XLIBS =

# dwl itself only uses C99 features, but wlroots' headers use anonymous unions (C11).
# To avoid warnings about them, we do not use -std=c99 and instead of using the
# gmake default 'CC=c99', we use cc.
CC = cc
