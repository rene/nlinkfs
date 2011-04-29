##
# NLINKFS configure.ac
#
AC_INIT([nlinkfs], 0.1, [rene@renesp.com.br])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
GLIB_CFLAGS=`pkg-config --cflags glib-2.0`
GLIB_LIBS=`pkg-config --libs glib-2.0`
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

AC_CHECK_LIB([fuse],[fuse_main],,AC_MSG_ERROR([ERROR! fuse not found...]))
FUSE_CFLAGS=`pkg-config --cflags fuse`
FUSE_LIBS=`pkg-config --libs fuse`
AC_SUBST(FUSE_CFLAGS)
AC_SUBST(FUSE_LIBS)

# Checks for header files.
AC_CHECK_HEADERS([errno.h sys/types.h sys/stat.h fcntl.h unistd.h utime.h dirent.h glib.h semaphore.h])

# Checks for typedefs, structures, and compiler characteristics.
# Checks for library functions.

# Output files.
AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([
	Makefile
	src/Makefile
])

AC_OUTPUT

