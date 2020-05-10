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
#include "compare.h"
#include "misc.h"
#include <cstdio>
#include <csignal>
#include <getopt.h>

/** @brief Counter for allocated option IDs */
static int passthru_option_val = 2 * (UCHAR_MAX) + 1;

/** @brief Option IDs to full option names */
static std::map<int, std::string> passthru_option_map;

/** @brief Help for passed-through options */
static std::vector<std::string> passthru_help;

/** @brief Register a pass-through option
 * @param longopts Option table
 * @param name Option name (no initial "--")
 * @param val Short option name or -1 to allocate one
 * @param value If the option takes a value, the name of the value; otherwise @c
 * nullptr.
 */
static void passthru_option(std::vector<struct option> &longopts,
                            const char *name, int val,
                            const char *value = nullptr) {
  std::string help_string;
  // The full option name
  std::string opt = "--" + std::string(name);
  int has_arg;
  if(val < 0) {
    // No short option name; allocate an ID.
    val = passthru_option_val++;
    help_string = "    " + opt;
  } else
    help_string = "-" + std::string(1, val) + ", " + opt;
  if(value) {
    // Quote the value name in the help string
    help_string += " ";
    help_string += value;
    has_arg = required_argument;
  } else
    has_arg = no_argument;
  // Update the list for --help output
  passthru_help.push_back(help_string);
  // Update the option table for getopt_long
  longopts.push_back(option{ name, has_arg, nullptr, val });
  // Map the ID to the full option name
  passthru_option_map[val] = opt;
}

static void help() {
  printf(
    "remdiff -- remote diff over SSH\n"
    "\n"
    "Usage:\n"
    "  remdiff [OPTIONS] [--] [HOST:]PATH [HOST:]PATH\n"
    "Mode options:\n"
    "  --normal                   Traditional diff\n"
    "  -q, --brief                Report only when files differ\n"
    "  -u, -U NUM, --unified NUM  Unified diff (with NUM lines of context)\n"
    "  -y, --side-by-side         Side-by-side diff\n"
    "Other options:\n"
    "  --help                     Display usage message\n"
    "  --version                  Display version string\n"
    "Diff options supported:\n");
  size_t width = 0;
  for(auto &s : passthru_help) {
    if(s.size() > width) {
      width = s.size();
    }
  }
  size_t total_width = 80 - 1;
  size_t columns = total_width / (width + 1);
  size_t column = 0, x = 0;
  for(auto &s : passthru_help) {
    if(column >= columns) {
      printf("\n");
      column = 0;
      x = 0;
    }
    while(x < 2 + (width + 1) * column) {
      x++;
      putchar(' ');
    }
    fputs(s.c_str(), stdout);
    x += s.size();
    ++column;
  }
  if(column)
    putchar('\n');
  printf(
    "\n"
    "HOST should be something acceptable to your SSH client.\n"
    "Exit status is as for diff: 0 no difference, 1 difference, 2 error.\n");
}

#ifndef TAG
#define TAG "unknown"
#endif

static void version() {
  printf("version %s tag %s\n", PACKAGE_VERSION, TAG);
}

int main(int argc, char **argv) {
  // Parse command line
  int n;
  Comparison c;
  std::string shortopts;
  std::vector<struct option> longopts{
    { "brief", no_argument, nullptr, 'q' },
    { "help", no_argument, nullptr, OPT_HELP },
    { "normal", no_argument, nullptr, OPT_NORMAL },
    { "side-by-side", no_argument, nullptr, 'y' },
    { "_unified", no_argument, nullptr, 'u' },
    { "unified", required_argument, nullptr, 'U' },
    { "version", no_argument, nullptr, OPT_VERSION },
    { "debug", no_argument, nullptr, OPT_DEBUG },
  };

  // Fill in pass-through options
  passthru_option(longopts, "suppress-common-lines", -1);
  passthru_option(longopts, "show-c-function", 'p');
  passthru_option(longopts, "expand-tabs", 't');
  passthru_option(longopts, "initial-tab", 'T');
  passthru_option(longopts, "suppress-blank-empty", -1);
  passthru_option(longopts, "ignore-case", 'i');
  passthru_option(longopts, "ignore-tab-expansion", 'E');
  passthru_option(longopts, "ignore-trailing-space", 'Z');
  passthru_option(longopts, "ignore-space-change", 'b');
  passthru_option(longopts, "ignore-all-space", 'w');
  passthru_option(longopts, "ignore-blank-lines", 'B');
  passthru_option(longopts, "strip-trailing-cr", -1);
  passthru_option(longopts, "minimal", 'd');
  passthru_option(longopts, "speed-large-files", -1);
  passthru_option(longopts, "width", 'w', "WIDTH");
  passthru_option(longopts, "tabsize", -1, "SIZE");
  passthru_option(longopts, "ifdef", 'D', "NAME");
  passthru_option(longopts, "horizon-lines", -1, "LINES");
  passthru_option(longopts, "color", -1, "WHEN");
  passthru_option(longopts, "palette", -1, "PALETTE");

  // Terminate the long options list
  longopts.push_back(option{ nullptr, 0, nullptr, 0 });

  // Compile the short options string
  for(n = 0; longopts[n].name; ++n) {
    if(longopts[n].val <= UCHAR_MAX)
      shortopts += longopts[n].val;
    if(longopts[n].has_arg == required_argument)
      shortopts.push_back(':');
  }

  // Parse the command line
  while((n = getopt_long(argc, argv, shortopts.c_str(), &longopts[0], nullptr))
        >= 0) {
    switch(n) {
    case OPT_HELP: help(); return 0;
    case OPT_NORMAL: c.mode = OPT_NORMAL; break;
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
    case OPT_DEBUG: debug = true; break;
    default: {
      auto it = passthru_option_map.find(n);
      if(it != passthru_option_map.end()) {
        if(optarg)
          c.extra_args.push_back(it->second + "=" + optarg);
        else
          c.extra_args.push_back(it->second);
        break;
      }
      return 2;
    }
    }
  }

  // Check we have what we expected
  if(argc - optind != 2) {
    fprintf(stderr, "ERROR: expected two arguments\n");
    return 2;
  }

  // Suppress SIGPIPE
  signal(SIGPIPE, SIG_IGN);

  std::string f1 = argv[optind], f2 = argv[optind + 1];

  // If one is a directory and the other a file, compare the file in the
  // directory.
  // TODO

  // If both are directories and --recursive, recursively compare files.
  // TODO

  try {
    return c.compare_files(f1, f2);
  } catch(std::runtime_error &e) {
    fprintf(stderr, "ERROR: %s\n", e.what());
    return 2;
  }
}
