/*
 * This file is part of remdiff.
 * Copyright © Richard Kettlewell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef REMDIFF_H
#define REMDIFF_H
/** @file remdiff.h
 * @brief Common definitions
 */

#include "config.h"
#include <climits>
#include <string>
#include <vector>
#include <map>
#include <unistd.h>

enum {
  OPT_NORMAL = (UCHAR_MAX + 1),
  OPT_UNI_NEW_FILE,
  OPT_HELP,
  OPT_VERSION,
  OPT_DEBUG,
};

/** @brief Treat first file as empty if missing */
#define NEW_AS_EMPTY_1 1

/** @brief Treat second file as empty if missing */
#define NEW_AS_EMPTY_2 2

/** @brief Report identical files */
#define REPORT_IDENTICAL 4

#endif
