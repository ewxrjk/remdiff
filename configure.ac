#
# This file is part of remdiff.
# Copyright © Richard Kettlewell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
AC_PREREQ([2.61])
AC_INIT([remdiff], [0], [rjk@greenend.org.uk])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_AUX_DIR([config.aux])
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_SRCDIR([remdiff.cc])
AM_CONFIG_HEADER([config.h])
AC_LANG([C++])
AC_PROG_CXX
AC_C_BIGENDIAN
AC_CHECK_LIB([pthread],[pthread_create])
CXXFLAGS="-std=c++11 ${CXXFLAGS}"
AC_SET_MAKE
AC_DEFINE([_GNU_SOURCE], [1], [use GNU extensions])
if test "x$GXX" = xyes; then
  CXXFLAGS="$CXXFLAGS -Wall -W -Werror -Wpointer-arith -Wwrite-strings"
fi
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
