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
#include "sftp.h"
#include "misc.h"
#include "sftp-internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstring>
#include <cinttypes>

std::mutex SFTP::Connection::static_lock;
std::map<std::string, SFTP::Connection *> SFTP::Connection::conns;

SFTP::Connection::Connection(const std::string &name_) : name(name_) {}

SFTP::Connection::~Connection() {
  disconnect();
}

void SFTP::Connection::connect() {
  if(rfd >= 0)
    return;
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  int wpipe[2] = { -1, -1 }, rpipe[2] = { -1, -1 };
  try {
    // Create pipes to subprocess
    if(pipe(wpipe) < 0)
      syserror("pipe", errno);
    if(pipe(rpipe) < 0)
      syserror("pipe", errno);
    // Create subprocess
    switch((pid = fork())) {
    case -1: syserror("fork", errno);
    case 0:
      // Plumb pipes onto stdin/stdout
      if(dup2(wpipe[0], 0) < 0 || dup2(rpipe[1], 1) < 0) {
        fprintf(stderr, "ERROR: dup2: %s\n", strerror(errno));
        _Exit(2);
      }
      if(::close(rpipe[0]) < 0 || ::close(rpipe[1]) < 0 || ::close(wpipe[0]) < 0
         || ::close(wpipe[1]) < 0) {
        fprintf(stderr, "ERROR: close: %s\n", strerror(errno));
        _Exit(2);
      }
      // Remotely execute the SFTP subsystem
      execlp("ssh", "ssh", "-s", name.c_str(), "sftp", (char *)nullptr);
      fprintf(stderr, "ERROR: execlp ssh %s: %s\n", name.c_str(),
              strerror(errno));
      _Exit(2);
    }
    // Keep only the pipe endpoints we need
    ::close(wpipe[0]);
    wpipe[0] = -1;
    wfd = wpipe[1];
    wpipe[1] = -1;
    rfd = rpipe[0];
    rpipe[0] = -1;
    ::close(rpipe[1]);
    rpipe[1] = -1;
    // Don't leak pipe endpoints to other processes
    close_on_exec(rfd);
    close_on_exec(wfd);
    // Send SSH_FXP_INIT
    std::string cmd;
    newpacket(cmd, SSH_FXP_INIT);
    pack32(cmd, 3);
    send(cmd);
    // Get the response
    std::string body;
    int reply_type = recv_reply(body);
    if(reply_type != SSH_FXP_VERSION)
      syserror(name + ": unsupported reply");
    size_t pos = 0;
    uint32_t version = unpack32(body, pos);
    if(version < 3)
      syserror(name + ": unsupported SFTP version");
    if(debug)
      fprintf(stderr, "DEBUG: %s %s SSH_FXP_INIT succeeded\n", __func__,
              name.c_str());
    // Start the polling thread
    poller = std::thread(SFTP::Connection::poll, this);
    // Get the home directory
    home = realpath("");
  } catch(std::exception &e) {
    if(rpipe[0] >= 0)
      ::close(rpipe[0]);
    if(rpipe[1] >= 0)
      ::close(rpipe[1]);
    if(wpipe[0] >= 0)
      ::close(wpipe[0]);
    if(wpipe[1] >= 0)
      ::close(wpipe[1]);
    disconnect();
    throw;
  }
}

void SFTP::Connection::disconnect() {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  // Terminate the poller thread
  if(poller.joinable()) {
    {
      std::unique_lock<std::mutex> locked(lock);
      poll_quit = true;
    }
    cond.notify_all();
    if(debug)
      fprintf(stderr, "DEBUG: %s %s joining poller\n", __func__, name.c_str());
    poller.join();
  }
  // Close our pipe endpoints
  if(rfd >= 0)
    ::close(rfd);
  if(wfd >= 0)
    ::close(wfd);
  rfd = wfd = -1;
  // Wait for the subprocess to terminate
  if(pid >= 0) {
    if(debug)
      fprintf(stderr, "DEBUG: %s %s waiting for child process\n", __func__,
              name.c_str());
    while(waitpid(pid, NULL, 0) < 0 && errno == EINTR)
      ;
    pid = -1;
  }
}

void SFTP::Connection::newpacket(std::string &s, uint8_t type) {
  uint8_t header[5] = { 0, 0, 0, 0, type };
  s.assign((char *)header, 5);
}

void SFTP::Connection::pack32(std::string &s, uint32_t n) {
  n = tobe32(n);
  s.append((char *)&n, sizeof n);
}

void SFTP::Connection::pack64(std::string &s, uint64_t n) {
  n = tobe64(n);
  s.append((char *)&n, sizeof n);
}

void SFTP::Connection::packstr(std::string &s, const std::string &str) {
  pack32(s, str.size());
  s.append(str);
}

void SFTP::Connection::send(std::string &s) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  // Substitute in the message length
  uint32_t len = tobe32(s.size() - 4);
  s.replace(0, sizeof len, (char *)&len, sizeof len);
  // Send the packet
  std::lock_guard<std::mutex> g(lock);
  if(writeall(wfd, &s[0], s.size()) < 0)
    syserror(name + ": write", errno);
  // Wake the poll thread.
  cond.notify_all();
}

void SFTP::Connection::recv(char *buffer, size_t n) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s %zu\n", __func__, name.c_str(), n);
  while(n > 0) {
    // Try to satisfy the read from the buffer
    size_t available = input_total - input_ptr;
    if(available > n)
      available = n;
    if(available > 0) {
      memcpy(buffer, input + input_ptr, available);
      buffer += available;
      input_ptr += available;
      n -= available;
      continue;
    }
    // Buffer empty, need to read more
    ssize_t bytes_read = ::read(rfd, input, sizeof input);
    if(bytes_read == 0)
      syserror(name + ": unexpected EOF");
    if(bytes_read < 0) {
      if(errno != EINTR)
        syserror(name + ": read", errno);
      continue;
    }
    input_total = bytes_read;
    input_ptr = 0;
  }
}

int SFTP::Connection::recv_reply(std::string &body) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  // Get the length
  uint32_t len;
  recv((char *)&len, sizeof len);
  len = frombe32(len);
  // Length includes type so must be at least 1
  if(len == 0)
    syserror(name + ": 0-length reply");
  // Get the type
  uint8_t type;
  recv((char *)&type, 1);
  --len;
  // Get the body
  body.resize(len);
  recv(&body[0], len);
  return type;
}

uint32_t SFTP::Connection::unpack32(const std::string &s, size_t &pos) const {
  uint32_t n;
  if(s.size() - pos < sizeof n)
    syserror(name + ": truncated reply");
  memcpy(&n, &s[pos], sizeof n);
  n = frombe32(n);
  pos += 4;
  return n;
}

uint64_t SFTP::Connection::unpack64(const std::string &s, size_t &pos) const {
  uint64_t n;
  if(s.size() - pos < sizeof n)
    syserror(name + ": truncated reply");
  memcpy(&n, &s[pos], sizeof n);
  n = frombe64(n);
  pos += 8;
  return n;
}

std::string SFTP::Connection::unpackstr(const std::string &s,
                                        size_t &pos) const {
  uint32_t len = unpack32(s, pos);
  if(len > s.size() - pos)
    syserror(name + ": truncated reply");
  std::string r(s, pos, len);
  pos += len;
  return r;
}

uint32_t SFTP::Connection::newid() {
  std::lock_guard<std::mutex> g(lock);
  // Skip IDs that are in use
  while(replies.find(nextid) != replies.end()
        || waiting.find(nextid) != waiting.end())
    nextid++;
  uint32_t id = nextid++;
  // Record the ID.
  waiting.insert(id);
  return id;
}

int SFTP::Connection::await_reply(uint32_t id, std::string &body) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  std::unique_lock<std::mutex> locked(lock);
  for(;;) {
    // If we have a suitable reply, use it
    auto it = replies.find(id);
    if(it != replies.end()) {
      body = it->second.body;
      int type = it->second.type;
      replies.erase(it);
      return type;
    }
    // Wait for a reply to appear
    cond.wait(locked);
  }
}

void SFTP::Connection::poll(SFTP::Connection *self) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, self->name.c_str());
  std::string buffer;
  for(;;) {
    // Pause until there is at least one waiter
    {
      std::unique_lock<std::mutex> locked(self->lock);
      while(!self->waiting.size() && !self->poll_quit) {
        if(debug)
          fprintf(stderr, "DEBUG: %s %s waiting for action\n", __func__,
                  self->name.c_str());
        self->cond.wait(locked);
      }
      if(self->poll_quit) {
        if(debug)
          fprintf(stderr, "DEBUG: %s %s quit\n", __func__, self->name.c_str());
        return;
      }
    }

    // Get a reply
    if(debug)
      fprintf(stderr, "DEBUG: %s %s reading\n", __func__, self->name.c_str());
    int type = self->recv_reply(buffer);
    size_t pos = 0;
    uint32_t id = self->unpack32(buffer, pos);
    {
      if(debug)
        fprintf(stderr, "DEBUG: %s %s stashing\n", __func__,
                self->name.c_str());
      std::lock_guard<std::mutex> g(self->lock);
      // Stash the reply for collection
      self->replies[id] = reply{ type, buffer };
      // No longerr waiting for this ID
      self->waiting.erase(id);
    }
    // Notify all the waiters
    if(debug)
      fprintf(stderr, "DEBUG: %s %s notify\n", __func__, self->name.c_str());
    self->cond.notify_all();
  }
}

void SFTP::Connection::error(const std::string &reply,
                             const std::string &context) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  size_t pos = 4;
  uint32_t err = unpack32(reply, pos);     // uint32 error/status code
  std::string msg = unpackstr(reply, pos); // string error message
  // Don't care about language tag
  if(debug)
    fprintf(stderr, "DEBUG: %s %s err=%u msg=%s\n", __func__, name.c_str(),
            (unsigned)err, msg.c_str());
  if(err != SSH_FX_OK) {
    if(context.size())
      throw Error(err, name + ": " + context + ": " + msg);
    else
      throw Error(err, name + ": " + msg);
  }
}

void SFTP::Connection::unpacknames(const std::string &s, size_t &pos,
                                   std::vector<Attributes> names) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s\n", __func__, name.c_str());
  uint32_t count = unpack32(s, pos);
  while(count-- > 0) {
    Attributes a;
    a.filename = unpackstr(s, pos);
    a.longname = unpackstr(s, pos);
    a.unpack(*this, s, pos);
    names.push_back(a);
  }
}

static std::string format_handle(const std::string &handle) {
  std::string r;
  char buffer[16];
  for(auto ch : handle) {
    snprintf(buffer, sizeof buffer, "%02x", (unsigned char)ch);
    r += buffer;
  }
  return buffer;
}

std::string SFTP::Connection::open(const std::string &path, uint32_t mode) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s %s\n", __func__, name.c_str(), path.c_str());
  const std::string fullpath =
    path.size() > 0 && path.at(0) == '/' ? path : home + "/" + path;
  std::string cmd, reply, handle;
  uint32_t id = newid();
  newpacket(cmd, SSH_FXP_OPEN);
  pack32(cmd, id);        // uint32 id
  packstr(cmd, fullpath); // string filename
  pack32(cmd, mode);      // uint32 path
  pack32(cmd, 0);         // uint32 flags
  send(cmd);
  int type = await_reply(id, reply);
  size_t pos = 4;
  switch(type) {
  case SSH_FXP_HANDLE:
    handle = unpackstr(reply, pos);
    if(debug)
      fprintf(stderr, "DEBUG: %s %s %s -> [%s]\n", __func__, name.c_str(),
              fullpath.c_str(), format_handle(handle).c_str());
    return handle;
  case SSH_FXP_STATUS:
    error(reply, path);
    syserror(name + ": unexpected SFTP status");
  default: syserror(name + ": unexpected SFTP response");
  }
}

void SFTP::Connection::close(const std::string &handle) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s]\n", __func__, name.c_str(),
            format_handle(handle).c_str());
  std::string cmd, reply;
  uint32_t id = newid();
  newpacket(cmd, SSH_FXP_CLOSE);
  pack32(cmd, id);      // uint32 id
  packstr(cmd, handle); // string handle
  send(cmd);
  int type = await_reply(id, reply);
  switch(type) {
  case SSH_FXP_STATUS: error(reply); break;
  default: syserror(name + ": unexpected SFTP response");
  }
}

std::string SFTP::Connection::opendir(const std::string &path) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s %s\n", __func__, name.c_str(), path.c_str());
  const std::string fullpath =
    path.size() > 0 && path.at(0) == '/' ? path : home + "/" + path;
  std::string cmd, reply, handle;
  uint32_t id = newid();
  newpacket(cmd, SSH_FXP_OPEN);
  pack32(cmd, id);        // uint32 id
  packstr(cmd, fullpath); // string filename
  pack32(cmd, 0);         // uint32 flags
  send(cmd);
  int type = await_reply(id, reply);
  size_t pos = 4;
  switch(type) {
  case SSH_FXP_HANDLE:
    handle = unpackstr(reply, pos);
    if(debug)
      fprintf(stderr, "DEBUG: %s %s %s -> [%s]\n", __func__, name.c_str(),
              fullpath.c_str(), format_handle(handle).c_str());
    return handle;
  case SSH_FXP_STATUS:
    error(reply, path);
    syserror(name + ": unexpected SFTP status");
  default: syserror(name + ": unexpected SFTP response");
  }
}

void SFTP::Connection::fstat(const std::string &handle, Attributes &attrs) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s]\n", __func__, name.c_str(),
            format_handle(handle).c_str());
  return gstat(handle, attrs, SSH_FXP_FSTAT);
}

void SFTP::Connection::stat(const std::string &path, Attributes &attrs) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s]\n", __func__, name.c_str(),
            path.c_str());
  return gstat(path, attrs, SSH_FXP_STAT);
}

void SFTP::Connection::lstat(const std::string &path, Attributes &attrs) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s]\n", __func__, name.c_str(),
            path.c_str());
  return gstat(path, attrs, SSH_FXP_LSTAT);
}

void SFTP::Connection::gstat(const std::string &handle, Attributes &attrs,
                             uint32_t type) {
  std::string cmd, reply;
  uint32_t id = newid();
  newpacket(cmd, type);
  pack32(cmd, id);      // uint32 id
  packstr(cmd, handle); // string handle
  send(cmd);
  int rtype = await_reply(id, reply);
  size_t pos = 4;
  switch(rtype) {
  case SSH_FXP_ATTRS: attrs.unpack(*this, reply, pos); break;
  case SSH_FXP_STATUS:
    error(reply);
    syserror(name + ": unexpected SFTP status");
  default: syserror(name + ": unexpected SFTP response");
  }
}

std::string SFTP::Connection::realpath(const std::string &path) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s]\n", __func__, name.c_str(),
            path.c_str());
  std::string cmd, reply;
  uint32_t id = newid();
  newpacket(cmd, SSH_FXP_REALPATH);
  pack32(cmd, id);    // uint32 id
  packstr(cmd, path); // string path
  send(cmd);
  int type = await_reply(id, reply);
  size_t pos = 4;
  switch(type) {
  case SSH_FXP_NAME: {
    uint32_t names = unpack32(reply, pos); // uint32 count
    if(names != 1)
      syserror(name + ": unexpected SFTP response");
    return unpackstr(reply, pos); // string name
  }
  case SSH_FXP_STATUS:
    error(reply);
    syserror(name + ": unexpected SFTP status");
  default: syserror(name + ": unexpected SFTP response");
  }
}

uint32_t SFTP::Connection::begin_read(const std::string &handle,
                                      uint64_t offset, uint32_t len) {
  std::string cmd;
  uint32_t id = newid();
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s] %" PRIu64 " %" PRIu32 " %#" PRIx32 "\n",
            __func__, name.c_str(), format_handle(handle).c_str(), offset, len,
            id);
  newpacket(cmd, SSH_FXP_READ);
  pack32(cmd, id);      // uint32 id
  packstr(cmd, handle); // string handle
  pack64(cmd, offset);  // uint64 offset
  pack32(cmd, len);     // uint32 len
  send(cmd);
  return id;
}

std::string SFTP::Connection::finish_read(uint32_t id) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s %#" PRIx32 "\n", __func__, name.c_str(), id);
  std::string reply;
  int type = await_reply(id, reply);
  size_t pos = 4;
  switch(type) {
  case SSH_FXP_DATA: return unpackstr(reply, pos); // string data
  case SSH_FXP_STATUS:
    if(unpack32(reply, pos) == SSH_FX_EOF)
      return "";
    error(reply);
    syserror(name + ": unexpected SFTP status");
  default: syserror(name + ": unexpected SFTP response");
  }
}

uint32_t SFTP::Connection::begin_readdir(const std::string &handle) {
  std::string cmd;
  uint32_t id = newid();
  if(debug)
    fprintf(stderr, "DEBUG: %s %s [%s] %#" PRIx32 "\n", __func__, name.c_str(),
            format_handle(handle).c_str(), id);
  newpacket(cmd, SSH_FXP_READDIR);
  pack32(cmd, id);      // uint32 id
  packstr(cmd, handle); // string handle
  send(cmd);
  return id;
}

void SFTP::Connection::finish_readdir(uint32_t id,
                                      std::vector<SFTP::Attributes> &names) {
  if(debug)
    fprintf(stderr, "DEBUG: %s %s %#" PRIx32 "\n", __func__, name.c_str(), id);
  std::string reply;
  int type = await_reply(id, reply);
  size_t pos = 4;
  switch(type) {
  case SSH_FXP_NAME: unpacknames(reply, pos, names); break;
  case SSH_FXP_STATUS:
    error(reply);
    syserror(name + ": unexpected SFTP status");
  default: syserror(name + ": unexpected SFTP response");
  }
}

void SFTP::Attributes::unpack(const SFTP::Connection &c,
                              const std::string &reply, size_t &pos) {
  flags = c.unpack32(reply, pos);
  size = (flags & SSH_FILEXFER_ATTR_SIZE) ? c.unpack64(reply, pos) : 0;
  uid = (flags & SSH_FILEXFER_ATTR_UIDGID) ? c.unpack32(reply, pos) : 0;
  gid = (flags & SSH_FILEXFER_ATTR_UIDGID) ? c.unpack32(reply, pos) : 0;
  permissions =
    (flags & SSH_FILEXFER_ATTR_PERMISSIONS) ? c.unpack32(reply, pos) : 0;
  atime = (flags & SSH_FILEXFER_ACMODTIME) ? c.unpack32(reply, pos) : 0;
  mtime = (flags & SSH_FILEXFER_ACMODTIME) ? c.unpack32(reply, pos) : 0;
  if(flags & SSH_FILEXFER_ATTR_EXTENDED) {
    uint32_t extended_count = c.unpack32(reply, pos);
    std::string type, data;
    while(extended_count-- > 0) {
      type = c.unpackstr(reply, pos);
      data = c.unpackstr(reply, pos);
      extended.push_back({ type, data });
    }
  }
}

SFTP::Connection *SFTP::Connection::connection(const std::string &host) {
  std::unique_lock<std::mutex> locked(static_lock);
  auto it = conns.find(host);
  Connection *conn;
  if(it == conns.end()) {
    conn = new SFTP::Connection(host);
    conns[host] = conn;
  } else
    conn = it->second;

  // Ensure it is connected
  conn->connect();
  return conn;
}
