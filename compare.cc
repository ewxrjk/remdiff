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
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>

int Comparison::compare_files(const std::string &f1, const std::string &f2) {
  std::vector<std::string> args;

  args.push_back("diff");

  // Describe what kind of output we want
  switch(mode) {
  case OPT_NORMAL: break;
  case 'u':
    if(context) {
      char buffer[64];
      snprintf(buffer, sizeof buffer, "-U%s", context);
      args.push_back(buffer);
    } else
      args.push_back("-u");
    break;
  case 'q': args.push_back("-q"); break;
  case 'y': args.push_back("-y"); break;
  default:
    fprintf(stderr, "ERROR: unsupported mode %d\n", mode);
    return 2;
    break;
  }

  // Add the filenames
  add_file(f1, args);
  add_file(f2, args);

  // Do the diff
  int rc = run_diff(args);

  wait_children();
  return rc;
}

void Comparison::add_file(const std::string &f,
                          std::vector<std::string> &args) {
  size_t colon;
  if((colon = f.find(':')) == std::string::npos) {
    // Local file
    args.push_back(f);
    return;
  }
  // Parse the filename
  std::string host = f.substr(0, colon);
  std::string path = f.substr(colon + 1);

  // Run SSH to get the file contents
  int p[2];
  if(pipe(p) < 0) {
    fprintf(stderr, "ERROR: pipe: %s\n", strerror(errno));
    exit(2);
  }
  pid_t pid;
  if((pid = fork()) < 0) {
    fprintf(stderr, "ERROR: fork: %s\n", strerror(errno));
    exit(2);
  }
  if(pid == 0) {
    std::string cmd = "cat ";
    // Quote the filename conservatively
    for(size_t n = 0; n < path.size(); n++) {
      char ch = path[n];
      if(ch > 0 && ch < 127 && !isalnum(ch) && ch != '/')
        cmd += '\\';
      cmd += ch;
    }
    if(dup2(p[1], 1) < 0) {
      fprintf(stderr, "ERROR: dup2: %s\n", strerror(errno));
      _Exit(2);
    }
    close(p[0]);
    close(p[1]);
    execlp("ssh", "ssh", "-T", "-x", host.c_str(), cmd.c_str(),
           (char *)nullptr);
    fprintf(stderr, "ERROR: execlp ssh: %s\n", strerror(errno));
    _Exit(2);
  }
  close(p[1]);
  // Wait for the child to produce some output or EOF
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(p[0], &fds);
  select(p[0] + 1, &fds, NULL, NULL, NULL);
  // Ignore select error, just do a nonblocking probe of the file.
  // This can fail; SSH can be slow to exit after closing its stdout.
  int status;
  if(waitpid(pid, &status, WNOHANG) >= 0) {
    if(WIFSIGNALED(status)) {
      fprintf(stderr, "ERROR: ssh: %s\n", strsignal(WTERMSIG(status)));
      exit(2);
    }
    if(WEXITSTATUS(status) != 0) {
      // Assume SSH or cat produced a diagnostic
      exit(2);
    }
    // SSH exited OK; the file was empty.
  } else {
    // SSH still running; clean it up when we're finished.
    processes.push_back(pid);
  }
  char buffer[128];
  snprintf(buffer, sizeof buffer, "/dev/fd/%d", p[0]);
  args.push_back(buffer);
}

void Comparison::wait_children() {
  for(size_t n = 0; n < processes.size(); n++) {
    int rc, status;
    while((rc = waitpid(processes[n], &status, 0)) < 0 && errno == EINTR)
      /*repeat*/;
    if(WIFSIGNALED(status)) {
      fprintf(stderr, "ERROR: ssh: %s\n", strsignal(WTERMSIG(status)));
      exit(2);
    }
    if(WEXITSTATUS(status) != 0) {
      fprintf(stderr, "ERROR: ssh: exit status %d\n", WEXITSTATUS(status));
      exit(2);
    }
  }
}

int Comparison::run_diff(std::vector<std::string> &args) {
  std::vector<const char *> cargs;
  for(auto &a : args) {
    cargs.push_back(a.c_str());
  }
  cargs.push_back(nullptr);
  int p[2];
  if(pipe(p) < 0) {
    fprintf(stderr, "ERROR: pipe: %s\n", strerror(errno));
    exit(2);
  }
  pid_t pid;
  if((pid = fork()) < 0) {
    fprintf(stderr, "ERROR: fork: %s\n", strerror(errno));
    exit(2);
  }
  if(pid == 0) {
    if(dup2(p[1], 1) < 0) {
      fprintf(stderr, "ERROR: dup2: %s\n", strerror(errno));
      _Exit(2);
    }
    close(p[0]);
    close(p[1]);
    execvp(cargs[0], (char **)&cargs[0]);
    fprintf(stderr, "ERROR: execvp %sh: %s\n", cargs[0], strerror(errno));
    _Exit(2);
  }
  close(p[1]);
  // Proxy the output
  FILE *fp;
  if(!(fp = fdopen(p[0], "r"))) {
    fprintf(stderr, "ERROR: fdopen: %s\n", strerror(errno));
    exit(2);
  }
  char *line = NULL;
  size_t n = 0;
  while(getline(&line, &n, fp) != -1) {
    // TODO replace filenames
    fputs(line, stdout);
    if(ferror(stdout)) {
      fprintf(stderr, "ERROR: writing to stdout: %s\n", strerror(errno));
      exit(2);
    }
  }
  if(ferror(fp)) {
    fprintf(stderr, "ERROR: reading pipe: %s\n", strerror(errno));
    exit(2);
  }
  fclose(fp);
  int rc, status;
  // Handle diff status
  while((rc = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
    /*repeat*/;
  if(WIFSIGNALED(status)) {
    fprintf(stderr, "ERROR: diff: %s\n", strsignal(WTERMSIG(status)));
    exit(2);
  }
  return WEXITSTATUS(status);
}
