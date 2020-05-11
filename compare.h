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
#include <regex>

namespace SFTP {
class Connection;
}

/** @defgroup ComparisonFlags Flags for Comparison
 *
 * Flag bits for the @ref Comparison::flags field.
 * @{
 */

/** @brief Treat first file as empty if missing */
const unsigned NEW_AS_EMPTY_1 = 1;

/** @brief Treat second file as empty if missing */
const unsigned NEW_AS_EMPTY_2 = 2;

/** @brief Report identical files */
const unsigned REPORT_IDENTICAL = 4;

/** @} */

/** @brief Context for one or more comparisons
 */
class Comparison {
public:
  ~Comparison();

  /** @brief Comparison mode (corresponding to an option character) */
  int mode = 'u';

  /** @brief Context argument to @c -U and similar options */
  const char *context = nullptr;

  /** @brief Arguments passed through to @c diff */
  std::vector<std::string> extra_args;

  /** @brief Compare two files
   * @param f1 First filename
   * @param f2 Second filename
   * @return diff status
   */
  int compare_files(const std::string &f1, const std::string &f2);

  /** @brief Comparison flags
   *
   * Possible bits are found in @ref ComparisonFlags :
   * - @ref NEW_AS_EMPTY_1 : if the first file is missing, treat as empty
   * - @ref NEW_AS_EMPTY_2 : if the second file is missing, treat as empty
   * - @ref REPORT_IDENTICAL : report identical files
   */
  unsigned flags;

private:
  /** @brief Substitution rule for replacing filenames in output */
  struct Replacement {
    /** @brief Pattern to match lines needing replacement */
    std::regex pattern;

    /** @brief Format of substitution */
    std::string to;

    /** @brief Execute the substitution */
    std::string replace(const std::string &s) {
      return std::regex_replace(s, pattern, to);
    };
  };

  /** @brief Hostnames to SFTP connections */
  std::map<std::string, SFTP::Connection *> conns;

  /** @brief Background threads */
  std::vector<std::thread> threads;

  /** @brief File descriptors to drain */
  std::vector<int> fds;

  /** @brief Sequence of replacements to execute on each line */
  std::vector<Replacement> replacements;

  /** @brief Add a file, either directly or replacing it with a pipe
   * @brief f Filename
   * @brief args Argument list to update
   * @brief fileno File number (1 for old, 2 for new)
   */
  void add_file(const std::string &f, std::vector<std::string> &args,
                int fileno);

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
