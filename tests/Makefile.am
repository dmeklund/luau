## Process this file with automake to produce Makefile.in
TESTS = testall
INCLUDES = -I../src -I../util
noinst_PROGRAMS = testall
testall_SOURCES = testall.c test.c test.h
testall_LDFLAGS = `pkg-config glib-2.0 --libs` -ldb
testall_LDADD = ../src/libuau.la ../util/libutil.la

AM_CFLAGS = `pkg-config --cflags glib-2.0` -Wall -I$(top_builddir)/src -I$(top_builddir)/util
