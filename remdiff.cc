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
#include "remdiff.h"
#include <cstdio>
#include <getopt.h>

const struct option longopts[] = {
  { "normal", no_argument, nullptr, OPT_NORMAL },
  { "brief", no_argument, nullptr, 'q' },
  { "_unified", no_argument, nullptr, 'u' },
  { "unified", required_argument, nullptr, 'U' },
  { "side-by-side", no_argument, nullptr, 'y' },
  { "help", no_argument, nullptr, OPT_HELP },
  { "version", no_argument, nullptr, OPT_VERSION },
  { nullptr, 0, 0, 0 },
};

static void help() {
  printf(
    "Usage:\n"
    "  remdiff [OPTIONS] [--] [HOST:]FILES\n"
    "Options:\n"
    "  --normal                   Traditional diff\n"
    "  -q, --brief                Report only when files differ\n"
    "  -u, -U NUM, --unified NUM  Unified diff (with NUM lines of context)\n"
    "  -y, --side-by-side         Side-by-side diff\n"
    "  --help                     Display usage message\n"
    "  --version                  Display version string\n");
}

static void version() {
  printf("version %s tag %s\n", PACKAGE_VERSION, TAG);
}

int main(int argc, char **argv) {
  // Parse command line
  int n;
  Comparison c;
  while((n = getopt_long(argc, argv, "quU:y", longopts, nullptr)) >= 0) {
    switch(n) {
    case OPT_HELP: help(); return 0;
    case 'q': c.mode = 'q'; break;
    case 'u':
      c.mode = 'u';
      c.context = nullptr;
      break;
    case 'U':
      c.mode = 'u';
      c.context = optarg;
      break;
    case 'y': c.mode = 'y'; break;
    case OPT_VERSION: version(); return 0;
    default: return 2;
    }
  }

  // Check we have what we expected
  if(argc - optind != 2) {
    fprintf(stderr, "ERROR: expected two arguments\n");
    return 2;
  }

  std::string f1 = argv[optind], f2 = argv[optind + 1];

  // If one is a directory and the other a file, compare the file in the
  // directory.
  // TODO

  // If both are directories and --recursive, recursively compare files.
  // TODO

  return c.compare_files(f1, f2);
}
