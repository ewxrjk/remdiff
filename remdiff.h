#ifndef REMDIFF_H
#define REMDIFF_H

#include "config.h"
#include <climits>
#include <string>
#include <vector>
#include <unistd.h>

enum {
  OPT_NORMAL = (UCHAR_MAX + 1),
  OPT_HELP,
  OPT_VERSION,
};

class Comparison {
public:
  int mode = 'u';
  const char *context = nullptr;

  int compare_files(const std::string &f1, const std::string &f2);
  void add_file(const std::string &f, std::vector<std::string> &args);
  void wait_children();
  int run_diff(std::vector<std::string> &args);

private:
  std::vector<pid_t> processes;
};

#endif
