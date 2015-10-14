## Process this file with automake to produce Makefile.in
bin_PROGRAMS = luau-register luau
lib_LTLIBRARIES = libuau-db.la

luau_register_SOURCES = luau-register.c
luau_register_LDADD = $(top_builddir)/src/libuau.la libuau-db.la

luau_SOURCES = luau.c
luau_LDADD = $(top_builddir)/src/libuau.la libuau-db.la
luau_LDFLAGS = -lreadline -lhistory -ltermcap

libuau_db_la_SOURCES = libuau-db.c libuau-db.h \
                       database.c  database.h
libuau_db_la_LIBADD = $(top_builddir)/util/libutil.la $(top_builddir)/src/libuau.la
libuau_db_la_LDFLAGS = -lcurl -version-info 3:1:0

include_HEADERS = libuau-db.h

AM_CFLAGS = `curl-config --cflags` `xml2-config --cflags` -I../util -I../src
AM_LDFLAGS = -lcurl -lxml2 -ldb

install-exec-hook:
	mkdir -p /var/lib/luau #yeah, this shouldn't be hardcoded, but it is in Luau at the moment, so we have to
	ldconfig
	$(bindir)/luau-register -u http://luau.sourceforge.net/luau.repository.xml -d "2005-01-15" -v 0.1.9 -i "4.0" -n "luau" -f "Luau Software Updater" -s "Automatically download and install updates" luau
