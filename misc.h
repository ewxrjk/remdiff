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
#ifndef MISC_H
#define MISC_H
/** @file misc.h
 * @brief Utilities
 */

#include <config.h>
#include <sys/types.h>
#include <stdexcept>
#include <system_error>
#include <cstring>

/** @brief Set to enable debug output */
extern bool debug;

/** @brief Make @p fd close on exec
 * @param fd File descriptor reconfigure
 * */
void close_on_exec(int fd);

/** @brief Interrupt-safe wrapper for @c write()
 * @param fd File descriptor to write to
 * @param buffer Buffer to write
 * @param n Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
ssize_t writeall(int fd, const char *buffer, size_t n);

/** @brief Raise a @c std::system_error exception
 * @param context Message for diagnostic
 * @param errno_value Error code or 0 for none
 */
[[noreturn]] void syserror(const std::string &context, int errno_value);

/** @brief Raise a @c std::system_error exception
 * @param context Message for diagnostic
 *
 * @c errno is used as the error code.
 */
[[noreturn]] inline void syserror(const std::string &context) {
  syserror(context, errno);
}

#endif
