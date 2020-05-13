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
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <deque>
#include "sftp.h"

Comparison::~Comparison() {
  if(debug)
    fprintf(stderr, "DEBUG: %s\n", __func__);
  // Join any surviving threads
  drain_fds();
  join_threads();
}

int Comparison::compare_files(const std::string &f1, const std::string &f2) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s %s\n", __func__, f1.c_str(), f2.c_str());

  // We will build up the full diff command line here.
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
  default: fprintf(stderr, "ERROR: unsupported mode %d\n", mode); return 2;
  }
  // Add the extra arguments (bizarrely there is no std::vector::append)
  args.insert(args.end(), extra_args.begin(), extra_args.end());

  // Add the filenames, possibly replacing them with pipe endpoints,
  // if they are remote files.
  add_file(f1, args, NEW_AS_EMPTY_1);
  add_file(f2, args, NEW_AS_EMPTY_2);

  // Do the diff
  int rc = run_diff(args);

  // Clean up the infrastructure we created.
  drain_fds();
  join_threads();

  // Done.
  return rc;
}

void Comparison::add_file(const std::string &f, std::vector<std::string> &args,
                          int fileno) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, f.c_str());
  std::string newname = f;

  size_t colon;
  if((colon = f.find(':')) == std::string::npos) {
    // Local file. See if it exists.
    struct stat statbuf;
    if(stat(f.c_str(), &statbuf) < 0) {
      if(errno == ENOENT && (fileno & flags))
        newname = "/dev/null";
      else
        syserror(f);
    } else {
      // Reject directories without even opening them
      if(S_ISDIR(statbuf.st_mode))
        syserror(f, EISDIR);
      newname = f;
    }
  } else {
    // Parse the filename
    std::string host = f.substr(0, colon);
    std::string path = f.substr(colon + 1);

    auto conn = SFTP::Connection::connection(host);

    // Find out the file type
    SFTP::Attributes attrs;
    try {
      conn->stat(path, attrs);
      if(S_ISDIR(attrs.permissions))
        syserror(f, EISDIR);
    } catch(SFTP::Error &e) {
      if(e.status != SSH_FX_NO_SUCH_FILE || !(fileno & flags))
        throw;
      newname = "/dev/null";
    }

    if(newname != "") {
      // Attempt to open the file
      std::string handle = conn->open(path, SSH_FXF_READ);

      // Create a pipe to feed it to the child
      int p[2];
      if(pipe(p) < 0)
        syserror("pipe");
      // Don't leak the writer end of the pipe.
      close_on_exec(p[1]);

      // Create a thread to do feeding
      threads.push_back(
        std::thread(Comparison::feed_file, conn, f, handle, p[1]));
      fds.push_back(p[0]);
      // TODO push this into run_diff?

      // Replace the filename with the reader end of the pipe
      char buffer[128];
      snprintf(buffer, sizeof buffer, "/dev/fd/%d", p[0]);
      newname = buffer;
    }
  }

  // Use the new name
  args.push_back(newname);

  if(newname != f) {
    // Put it back when we're finished
    std::string prefix, pattern, replacement;
    switch(mode) {
    case OPT_NORMAL: break;
    case 'u':
      switch(fileno) {
      case 1:
        replacements.push_back(Replacement{
          std::regex("^--- " + newname, std::regex::extended),
          "--- " + f,
        });
        break;
      case 2:
        replacements.push_back(Replacement{
          std::regex("^\\+\\+\\+ " + newname, std::regex::extended),
          "+++ " + f,
        });
        break;
      }
      break;
    case 'q':
      // handled below
      break;
    case 'y':
      // no filenames
      break;
    default: fprintf(stderr, "ERROR: unsupported mode %d\n", mode); exit(2);
    }
    // TODO the REPORT_IDENTICAL support is not great; files that differ
    // but have, e.g. 'and /dev/fd/3' in the difference will produce mangled
    // output. A fix might be to hash the files (easy for remote files, can hash
    // in feed_file) and synthesize the REPORT_IDENTICAL message if they match.
    if(mode == 'q' || (flags & REPORT_IDENTICAL)) {
      switch(fileno) {
      case 1:
        replacements.push_back(Replacement{
          std::regex("^Files " + newname, std::regex::extended),
          "Files " + f,
        });
        break;
      case 2:
        replacements.push_back(Replacement{
          std::regex(" and " + newname, std::regex::extended),
          " and " + f,
        });
        break;
      }
    }
  }
}

void Comparison::feed_file(SFTP::Connection *conn, std::string context,
                           std::string handle, int fd) {
  if(debug)
    fprintf(stderr, "DEBUG: %s\n", __func__);
  uint32_t id;
  uint64_t offset = 0;
  size_t chunk = 4096;
  std::string result;
  size_t inflight_limit = 4;
  std::deque<uint32_t> ids;

  try {
    for(;;) {
      // Make sure there are plenty of reads in flight.
      while(ids.size() < inflight_limit) {
        id = conn->begin_read(handle, offset, chunk);
        offset += chunk;
        ids.push_back(id);
      }
      // Wait for the next read to finish
      id = ids.front();
      ids.pop_front();
      result = conn->finish_read(id);
      if(result.size() == 0)
        break;
      if(writeall(fd, &result[0], result.size()) < 0) {
        if(errno == EPIPE) {
          // diff stopped before reading everything (possibly it never even
          // ran)
          break;
        }
        syserror(context + ": write");
      }
      offset += result.size();
    }
    if(debug)
      fprintf(stderr, "DEBUG: %s complete\n", __func__);
  } catch(std::runtime_error &e) {
    fprintf(stderr, "ERROR: %s\n", e.what());
  }
  // Reap any remaining reads
  while(ids.size() > 0) {
    try {
      id = ids.front();
      ids.pop_front();
      conn->finish_read(id);
    } catch(std::runtime_error &e) {
      // Ignore any errors
    }
  }
  // We own the local and remote file descriptors.
  close(fd);
  conn->close(handle);
}

void Comparison::drain_fds() {
  if(debug)
    fprintf(stderr, "DEBUG: %s\n", __func__);
  // We close the read endpoints. If all went well they are idle
  // by now. Otherwise feed_file is still going and it will get EPIPE.
  for(auto fd : fds)
    close(fd);
  fds.clear();
}

void Comparison::join_threads() {
  if(debug)
    fprintf(stderr, "DEBUG: %s\n", __func__);
  for(auto &t : threads)
    t.join();
  threads.clear();
}

int Comparison::run_diff(std::vector<std::string> &args) {
  if(debug)
    fprintf(stderr, "DEBUG: %s\n", __func__);
  // Convert arguments to C format, as expected by execvp.
  std::vector<const char *> cargs;
  for(auto &a : args) {
    cargs.push_back(a.c_str());
  }
  cargs.push_back(nullptr);
  // Create the pipe for the output.
  int p[2];
  if(pipe(p) < 0) {
    fprintf(stderr, "ERROR: pipe: %s\n", strerror(errno));
    exit(2);
  }
  // Create the child process.
  pid_t pid;
  if((pid = fork()) < 0) {
    fprintf(stderr, "ERROR: fork: %s\n", strerror(errno));
    exit(2);
  }
  if(pid == 0) {
    // Restore SIGPIPE for the child
    signal(SIGPIPE, SIG_DFL);
    // Plumb in the pipes
    if(dup2(p[1], 1) < 0) {
      fprintf(stderr, "ERROR: dup2: %s\n", strerror(errno));
      _Exit(2);
    }
    close(p[0]);
    close(p[1]);
    // Execute diff
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
  std::string line;
  int ch;
  while((ch = getc(fp)) >= 0) {
    line += ch;
    if(ch == '\n') {
      for(auto &r : replacements)
        line = r.replace(line);
      fwrite(&line[0], 1, line.size(), stdout);
      if(ferror(stdout)) {
        fprintf(stderr, "ERROR: writing to stdout: %s\n", strerror(errno));
        exit(2);
      }
      line.clear();
    }
  }
  if(ferror(fp)) {
    fprintf(stderr, "ERROR: reading pipe: %s\n", strerror(errno));
    exit(2);
  }
  fclose(fp);
  int status;
  pid_t rc;
  // Handle diff status
  while((rc = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
    /*repeat*/;
  assert(rc == pid);
  if(WIFSIGNALED(status)) {
    fprintf(stderr, "ERROR: diff: %s\n", strsignal(WTERMSIG(status)));
    exit(2);
  }
  return WEXITSTATUS(status);
}
