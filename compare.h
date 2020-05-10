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
#ifndef COMPARE_H
#define COMPARE_H
/** @file compare.h
 * @brief File comparison
 */

#include <config.h>
#include <string>
#include <vector>
#include <map>
#include <thread>

namespace SFTP {
class Connection;
}

/** @brief Context for a comparison
 */
class Comparison {
public:
  ~Comparison();

  /** @brief Comparison mode (corresponding to an option character) */
  int mode = 'u';

  /** @brief Context argument to -U and similar */
  const char *context = nullptr;

  /** @brief Arguments passed through to a diff */
  std::vector<std::string> extra_args;

  /** @brief Compare two files
   * @param f1 First filename
   * @param f2 Second filename
   * @return diff status
   */
  int compare_files(const std::string &f1, const std::string &f2);

private:
  /** @brief Hostnames to SFTP connections */
  std::map<std::string, SFTP::Connection *> conns;

  /** @brief Background threads */
  std::vector<std::thread> threads;

  /** @brief File descriptors to drain */
  std::vector<int> fds;

  /** @brief Add a file, either directly or replacing it with a pipe
   * @brief f Filename
   * @brief args Argument list to update
   */
  void add_file(const std::string &f, std::vector<std::string> &args);

  /** @brief Run the diff command
   * @param args Argument list
   * @return diff status
   */
  int run_diff(std::vector<std::string> &args);

  /** @brief Background thread to feed a file to a pipe
   * @param conn SFTP Connection
   * @param context Context string for diagnostics
   * @param handle SFTP handle
   * @param fd Output file descriptor
   *
   * @p handle and @p fd will be closed.
   */
  static void feed_file(SFTP::Connection *conn, std::string context,
                        std::string handle, int fd);

  /** @brief Drain and close internal file descriptors */
  void drain_fds();

  /** @brief Join any outstanding threads */
  void join_threads();
};

#endif
