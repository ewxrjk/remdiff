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
#include "misc.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <system_error>

bool debug;

void close_on_exec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if(flags < 0)
    syserror("fcntl");
  if(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
    syserror("fcntl");
}

ssize_t writeall(int fd, const char *buffer, size_t n) {
  size_t written = 0;

  while(written < n) {
    ssize_t this_write = ::write(fd, buffer + written, n - written);
    if(this_write < 0) {
      if(errno == EINTR)
        continue;
      return -1;
    }
    written += this_write;
  }
  return written;
}

[[noreturn]] void syserror(const std::string &context, int errno_value) {
  if(debug)
    fprintf(stderr, "DEBUG: %s: %s\n", context.c_str(),
            errno_value ? strerror(errno_value) : "(no errno)");
  throw std::system_error(std::error_code(errno_value, std::system_category()),
                          context);
}
