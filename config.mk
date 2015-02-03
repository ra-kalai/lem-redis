CC := gcc

PKG_CONFIG_PATH := /usr/local/lib/pkgconfig/
PKG_CONFIG := pkg-config
cmoddir = $(shell \
            PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) $(PKG_CONFIG) --variable=INSTALL_CMOD lem)

CFLAGS := -Wall -Wno-strict-aliasing -fPIC -nostartfiles -shared \
       -Ihiredis \
       -g \
       $(shell \
         PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
         $(PKG_CONFIG) --cflags lem)

LDFLAGS := -Os
