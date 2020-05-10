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
tag:=$(shell git describe --tags --dirty --always)
bin_PROGRAMS=remdiff
remdiff_SOURCES=\
    compare.cc \
    compare.h \
    misc.cc \
    misc.h \
    remdiff.cc \
    remdiff.h \
    sftp.cc \
    sftp.h \
    sftp-internal.h
AM_CXXFLAGS=-DTAG=\"${tag}\"
man_MANS=remdiff.1
EXTRA_DIST=${man_MANS} README.md .clang-format .gitignore Doxyfile \
    debian/changelog debian/compat debian/control debian/copyright debian/rules
ACLOCAL_AMFLAGS=-I m4
