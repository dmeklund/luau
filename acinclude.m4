############
#
# This macro searches for an installed zlib library. If nothing was specified when
# calling configure, it searches first in /usr/local and then in /usr. If the 
# --with-zlib=DIR is specified, it will try to find it in DIR/include/zlib.h and 
# DIR/lib/libz.a. If --without-zlib is specified, the library is not searched at all.
# 
# If either the header file (zlib.h) or the library (libz) is not found, the 
# configuration exits on error, asking for a valid zlib installation directory or 
# --without-zlib.
# 
# The macro defines the symbol HAVE_LIBZ if the library is found. You should use
# autoheader to include a definition for this symbol in a config.h file. Sample
# usage in a C/C++ source is as follows:
# 
#   #ifdef HAVE_LIBZ
#   #include <zlib.h>
#   #endif /* HAVE_LIBZ */
#
############

AC_DEFUN([CHECK_ZLIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if zlib is wanted)
AC_ARG_WITH(zlib,
[  --with-zlib[=DIR]       root directory path of zlib installation [defaults to
                          /usr/local or /usr if not found in /usr/local]
  --without-zlib          disable zlib use completely (not recommended)],
[if test "$withval" != no ; then
  AC_MSG_RESULT(yes)
  ZLIB_HOME="$withval"
else
  AC_MSG_RESULT(no)
fi], [
AC_MSG_RESULT(yes)
ZLIB_HOME=/usr/local
if test ! -f "${ZLIB_HOME}/include/zlib.h"
then
        ZLIB_HOME=/usr
fi
])

#
# Locate zlib, if wanted
#
if test -n "${ZLIB_HOME}"
then
        ZLIB_OLD_LDFLAGS=$LDFLAGS
        ZLIB_OLD_CPPFLAGS=$LDFLAGS
        LDFLAGS="$LDFLAGS -L${ZLIB_HOME}/lib"
        CPPFLAGS="$CPPFLAGS -I${ZLIB_HOME}/include"
        AC_LANG_SAVE
        AC_LANG_C
        AC_CHECK_LIB(z, inflateEnd, [zlib_cv_libz=yes], [zlib_cv_libz=no])
        AC_CHECK_HEADER(zlib.h, [zlib_cv_zlib_h=yes], [zlib_cv_zlib_h=no])
        AC_LANG_RESTORE
        if test "$zlib_cv_libz" = "yes" -a "$zlib_cv_zlib_h" = "yes"
        then
                #
                # If both library and header were found, use them
                #
				with_zlib="yes"
                AC_CHECK_LIB(z, inflateEnd)
                AC_MSG_CHECKING(zlib in ${ZLIB_HOME})
                AC_MSG_RESULT(ok)
        else
                #
                # If either header or library was not found, revert and bomb
                #
                AC_MSG_CHECKING(zlib in ${ZLIB_HOME})
                LDFLAGS="$ZLIB_OLD_LDFLAGS"
                CPPFLAGS="$ZLIB_OLD_CPPFLAGS"
                AC_MSG_RESULT(failed)
                AC_MSG_ERROR(either specify a valid zlib installation with --with-zlib=DIR or disable zlib usage with --without-zlib)
        fi
fi

])







############
#
# The Debug Malloc Library is an useful tool to debug memory problems in your
# programs, but you don't want to compile dmalloc-support into every binary
# you produce, because of the performance loss.
# 
# This macro adds a user command-line flag --with-dmalloc to the configure
# script, which can be specified like this:
# 
# ./configure --with-dmalloc[=PREFIX]
# 
# When activated, the macro will add the flags -IPREFIX/include to $CPPFLAGS
# and -LPREFIX/lib to $LDFLAGS. (If the PREFIX has been omitted /usr/local
# will be used as default.) Also, the flag -DWITH_DMALLOC will added to
# $CPPFLAGS.
# 
# Thus, you can use the following code in your header files to activate or
# deactivate dmalloc support:
# 
# #ifdef DEBUG_DMALLOC
# #  include <dmalloc.h>
# #endif
#
############

AC_DEFUN([PETI_WITH_DMALLOC], [
AC_MSG_CHECKING(whether to use the dmalloc library)
AC_ARG_WITH(dmalloc,
[  --with-dmalloc[=PREFIX]   Compile with dmalloc library default=no],
if test "$withval" = "" -o "$withval" = "yes"; then
    ac_cv_dmalloc="/usr/local"
else
    ac_cv_dmalloc="$withval"
fi
AC_MSG_RESULT(yes)
CPPFLAGS="$CPPFLAGS -DWITH_DMALLOC -I$ac_cv_dmalloc/include"
LDFLAGS="$LDFLAGS -L$ac_cv_dmalloc/lib"
LIBS="$LIBS -ldmalloc"
with_dmalloc="yes",
AC_MSG_RESULT(no)
with_dmalloc="no")
])
