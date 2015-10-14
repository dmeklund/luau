## Process this file with automake to produce Makefile.in
lib_LTLIBRARIES = libuau.la

libuau_la_SOURCES = libuau.c  libuau.h \
                    network.c   network.h  \
                    parseupdates.h \
                    parseupdatesxml.c \
                    install.c   install.h
libuau_la_LIBADD = $(top_builddir)/util/libutil.la
##libuau_la_LDFLAGS = `curl-config --libs` -version-info 2:0:0
libuau_la_LDFLAGS = -lcurl -version-info 3:0:0

include_HEADERS = libuau.h

AM_CFLAGS = `curl-config --cflags` `xml2-config --cflags` -I../util
##AM_LDFLAGS = `curl-config --libs` `xml2-config --libs`
AM_LDFLAGS = -lcurl -lxml2

