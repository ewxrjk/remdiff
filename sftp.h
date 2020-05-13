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
#ifndef SFTP_H
#define SFTP_H
/** @file sftp.h
 * @brief SFTP implementation
 */

#include <config.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#define SSH_FXF_READ 0x00000001
#define SSH_FXF_WRITE 0x00000002
#define SSH_FXF_APPEND 0x00000004
#define SSH_FXF_CREAT 0x00000008
#define SSH_FXF_TRUNC 0x00000010
#define SSH_FXF_EXCL 0x00000020
#define SSH_FXF_TEXT 0x00000040

#define SSH_FX_OK 0
#define SSH_FX_EOF 1
#define SSH_FX_NO_SUCH_FILE 2
#define SSH_FX_PERMISSION_DENIED 3
#define SSH_FX_FAILURE 4
#define SSH_FX_BAD_MESSAGE 5
#define SSH_FX_NO_CONNECTION 6
#define SSH_FX_CONNECTION_LOST 7
#define SSH_FX_OP_UNSUPPORTED 8
#define SSH_FX_INVALID_HANDLE 9
#define SSH_FX_NO_SUCH_PATH 10
#define SSH_FX_FILE_ALREADY_EXISTS 11
#define SSH_FX_WRITE_PROTECT 12
#define SSH_FX_NO_MEDIA 13
#define SSH_FX_NO_SPACE_ON_FILESYSTEM 14
#define SSH_FX_QUOTA_EXCEEDED 15
#define SSH_FX_UNKNOWN_PRINCIPAL 16
#define SSH_FX_LOCK_CONFLICT 17
#define SSH_FX_DIR_NOT_EMPTY 18
#define SSH_FX_NOT_A_DIRECTORY 19
#define SSH_FX_INVALID_FILENAME 20
#define SSH_FX_LINK_LOOP 21
#define SSH_FX_CANNOT_DELETE 22
#define SSH_FX_INVALID_PARAMETER 23
#define SSH_FX_FILE_IS_A_DIRECTORY 24
#define SSH_FX_BYTE_RANGE_LOCK_CONFLICT 25
#define SSH_FX_BYTE_RANGE_LOCK_REFUSED 26
#define SSH_FX_DELETE_PENDING 27
#define SSH_FX_FILE_CORRUPT 28
#define SSH_FX_OWNER_INVALID 29
#define SSH_FX_GROUP_INVALID 30
#define SSH_FX_NO_MATCHING_BYTE_RANGE_LOCK 31

#define SSH_FILEXFER_ATTR_SIZE 0x00000001
#define SSH_FILEXFER_ATTR_UIDGID 0x00000002
#define SSH_FILEXFER_ATTR_PERMISSIONS 0x00000004
#define SSH_FILEXFER_ATTR_ACCESSTIME 0x00000008
#define SSH_FILEXFER_ACMODTIME 0x00000008
#define SSH_FILEXFER_ATTR_CREATETIME 0x00000010
#define SSH_FILEXFER_ATTR_MODIFYTIME 0x00000020
#define SSH_FILEXFER_ATTR_ACL 0x00000040
#define SSH_FILEXFER_ATTR_OWNERGROUP 0x00000080
#define SSH_FILEXFER_ATTR_SUBSECOND_TIMES 0x00000100
#define SSH_FILEXFER_ATTR_BITS 0x00000200
#define SSH_FILEXFER_ATTR_ALLOCATION_SIZE 0x00000400
#define SSH_FILEXFER_ATTR_TEXT_HINT 0x00000800
#define SSH_FILEXFER_ATTR_MIME_TYPE 0x00001000
#define SSH_FILEXFER_ATTR_LINK_COUNT 0x00002000
#define SSH_FILEXFER_ATTR_UNTRANSLATED_NAME 0x00004000
#define SSH_FILEXFER_ATTR_CTIME 0x00008000
#define SSH_FILEXFER_ATTR_EXTENDED 0x80000000
#define SSH_FILEXFER_TYPE_REGULAR 1
#define SSH_FILEXFER_TYPE_DIRECTORY 2
#define SSH_FILEXFER_TYPE_SYMLINK 3
#define SSH_FILEXFER_TYPE_SPECIAL 4
#define SSH_FILEXFER_TYPE_UNKNOWN 5
#define SSH_FILEXFER_TYPE_SOCKET 6
#define SSH_FILEXFER_TYPE_CHAR_DEVICE 7
#define SSH_FILEXFER_TYPE_BLOCK_DEVICE 8
#define SSH_FILEXFER_TYPE_FIFO 9

namespace SFTP {

class Attributes;
class Connection;

/** @brief Handle for reading files and directories */
class Handle {
public:
  /** @brief Destructor */
  inline ~Handle();

  /** @brief Raw SFTP handle string */
  std::string handle;

  /** @brief Owning connection */
  Connection *connection;

  /** @brief Filename */
  std::string name;

  /** @brief Close the file */
  inline void close();
};

class FileHandle : public Handle {
public:
  /** @brief Initiate a read
   * @param offset Offset within file
   * @param len Length to read
   * @return ID for @ref finish_read
   */
  inline uint32_t begin_read(uint64_t offset, uint32_t len);

  /** @brief Complete a read
   * @param id ID from begin_read
   * @return Bytes read
   *
   * On EOF, returns an empty string.
   */
  inline std::string finish_read(uint32_t id);
};

class DirectoryHandle : public Handle {
public:
  /** @brief Initiate a directory read
   * @param handle Handle as returned by @ref open
   * @return ID for @ref finish_readdir
   */
  inline uint32_t begin_readdir();

  /** @brief Complete a directory read
   * @param id ID from begin_read
   * @param names Names read (append, not set)
   * @return Number of names read
   *
   * The name/attribute data read is *appended* to @p names,
   * i.e. extending it compared to its value on entry.
   */
  inline uint32_t finish_readdir(uint32_t id,
                                 std::vector<SFTP::Attributes> &names);
};

/** @brief Connection to an SFTP server
 *
 * After construction you must call @ref connect to establish a
 * connection. Thereafter you can use @ref open and the other
 * methods to access files.
 *
 * It is not necessary to explicitly call @ref disconnect; the
 * session will be automatically disconnected by the destructor.
 */
class Connection {
public:
  /** @brief Construct a disconnected SFTP server
   * @param name Hostname (must be acceptable to @c ssh)
   *
   * The object is initially not connected; you must called @ref
   * connect.
   */
  Connection(const std::string &name);

  /** @brief Destruct the SFTP connection */
  ~Connection();

  /** @brief Connect the SFTP session
   *
   * If already connected, does nothing.
   */
  void connect();

  /** @brief Disconnect the SFTP session
   *
   * If not connected, does nothing.
   */
  void disconnect();

  /** @brief Open a remote file
   * @param handle Handle for later access
   * @param path Remote filename
   * @param mode Open mode
   *
   * @p mode can be @ref SSH_FXF_READ, etc.
   */
  inline void open(FileHandle &handle, const std::string &path, uint32_t mode);

  /** @brief Open a remote directory
   * @param path Remote filename
   * @return Handle for later access
   */
  inline void opendir(DirectoryHandle &handle, const std::string &path);

  /** @brief Close a remote file
   * @param handle Handle as returned by @ref open
   */
  void close(const std::string &handle);

  /** @brief Get remote file information
   * @param handle Handle as returned by @ref open
   * @param attrs Attributes of remote file
   */
  void fstat(const std::string &handle, Attributes &attrs);

  /** @brief Get remote file information
   * @param path Filename
   * @param attrs Attributes of remote file
   */
  void stat(const std::string &path, Attributes &attrs);

  /** @brief Get remote file information
   * @param path Filename
   * @param attrs Attributes of remote file
   */
  void lstat(const std::string &path, Attributes &attrs);

  /** @brief Get full path
   * @param path Filename
   * @return Full filename
   */
  std::string realpath(const std::string &path);

  /** @brief Get a possibly shared connection to a host
   * @param host Hostname
   * @return Connection
   */
  static Connection *connection(const std::string &host);

private:
  /** @brief Open a remote file
   * @param path Remote filename
   * @param mode Open mode
   * @return Handle for later access
   *
   * @p mode can be @ref SSH_FXF_READ, etc.
   */
  std::string open(const std::string &path, uint32_t mode);

  /** @brief Open a remote directory
   * @param path Remote filename
   * @return Handle for later access
   */
  std::string opendir(const std::string &path);

  /** @brief Initiate a read
   * @param handle Handle as returned by @ref open
   * @param offset Offset within file
   * @param len Length to read
   * @return ID for @ref finish_read
   */
  uint32_t begin_read(const std::string &handle, uint64_t offset, uint32_t len);

  /** @brief Complete a read
   * @param id ID from begin_read
   * @return Bytes read
   *
   * On EOF, returns an empty string.
   */
  std::string finish_read(uint32_t id);

  /** @brief Initiate a directory read
   * @param handle Handle as returned by @ref open
   * @return ID for @ref finish_readdir
   */
  uint32_t begin_readdir(const std::string &handle);

  /** @brief Complete a directory read
   * @param id ID from begin_read
   * @param names Names read (append, not set)
   * @return Number of names read
   *
   * The name/attribute data read is *appended* to @p names,
   * i.e. extending it compared to its value on entry.
   */
  uint32_t finish_readdir(uint32_t id, std::vector<SFTP::Attributes> &names,
                          const std::string &dirname);

  /** @brief Hostname */
  std::string name;

  /** @brief Home directory */
  std::string home;

  /** @brief Read pipe */
  int rfd = -1;

  /** @brief Write pipe */
  int wfd = -1;

  /** @brief Child process */
  pid_t pid = -1;

  /** @brief Input buffer */
  char input[4096];

  /** @brief Total bytes in @c input */
  size_t input_total;

  /** @brief Bytes cobnsumed from @c input */
  size_t input_ptr;

  /** @brief Start a new command
   * @param s Packet being constructed
   * @param type Packet type
   *
   * Stores a placeholder for the length, and the packet type.
   *
   * Subsequently the caller should use @ref pack32, @ref pack64 and @ref
   * packstr to construct the packet. To get the ID field that appears at the
   * start of almost all packet times, use @ref newid. Finally, use
   * @ref send to transmit the packet to the server.
   */
  inline void newpacket(std::string &s, uint8_t type);

  /** @brief Get a new ID
   *
   * Registers the ID in @c waiting,
   * both to present re-use and so that the poll knows to look for a reply.
   */
  uint32_t newid();

  /** @brief Append a uint32
   * @param s Packet being constructed
   * @param n Value to append
   */
  static inline void pack32(std::string &s, uint32_t n);

  /** @brief Append a uint64
   * @param s Packet being constructed
   * @param n Value to append
   */
  static inline void pack64(std::string &s, uint64_t n);

  /** @brief Append a string
   * @param s Packet being constructed
   * @param str Value to append
   */
  static inline void packstr(std::string &s, const std::string &str);

  /** @brief Send a packet
   * @param s Complete packet
   */
  void send(std::string &s);

  /** @brief Wait for a reply packet
   * @param id Packet ID
   * @param body Reply body (excluding type and length)
   * @return Reply type
   *
   * To parse the packet, declare a parsing position:
   *
   * @code
   * size_t pos = 4;
   * @endcode
   *
   * Then use @ref unpack32, @ref unpack64 and @ref unpackstr to parse the
   * packet. You can start parsing at 0 but then you must parse the ID field.
   *
   * If the type is @ref SSH_FXP_STATUS then @ref error can be used
   * to parse it.
   */
  int await_reply(uint32_t id, std::string &body);

  /** @brief Get remote file information
   * @param s Filename or handle
   * @param attrs Attributes of remote file
   * @param type Packet type
   */
  void gstat(const std::string &s, Attributes &attrs, uint32_t type);

  /** @brief Unpack a uint32
   * @param s Packet being parsed
   * @param pos Position in packet
   * @return Valued unpacked
   */
  inline uint32_t unpack32(const std::string &s, size_t &pos) const;

  /** @brief Unpack a uint64
   * @param s Packet being parsed
   * @param pos Position in packet
   * @return Valued unpacked
   */
  inline uint64_t unpack64(const std::string &s, size_t &pos) const;

  /** @brief Unpack a string
   * @param s Packet being parsed
   * @param pos Position in packet
   * @return Valued unpacked
   */
  inline std::string unpackstr(const std::string &s, size_t &pos) const;

  /** @brief Parse a @ref SSH_FXP_STATUS packet
   * @param body Reply body (excluding type and length)
   * @param context Context string for exceptions
   *
   * Raises an exception if the status is nonzero.
   */
  void error(const std::string &body, const std::string &context = "");

  /** @brief Parse the body (after @c id) of a @ref SSH_FXP_NAME packet
   * @param s Packet being parsed
   * @param pos Position in packet
   * @param names Names and attributes
   * @param dirname Directory name
   * @return Number of names returned
   */
  uint32_t unpacknames(const std::string &s, size_t &pos,
                       std::vector<Attributes> names,
                       const std::string &dirname);

  /** @brief Lock guarding static fields */
  static std::mutex static_lock;

  /** @brief Hostnames to SFTP connections */
  static std::map<std::string, SFTP::Connection *> conns;

  /** @brief Lock guarding all later fields */
  std::mutex lock;

  /** @brief Condition variable signalling changes to state */
  std::condition_variable cond;

  /** @brief Set to terminate the poll thread */
  bool poll_quit = false;

  /** @brief Next ID to use */
  uint32_t nextid = 0;

  /** @brief A stashed reply */
  struct reply {
    /** @brief Packet type */
    int type;
    /** @brief Packet body */
    std::string body;
  };

  /** @brief Replies recieved but not yet consumed */
  std::map<uint32_t, reply> replies;

  /** @brief IDs outstanding */
  std::set<uint32_t> waiting;

  /** @brief Read bytes
   * @param buffer Buffer for bytes
   * @param n Bytes to read
   * */
  void recv(char *buffer, size_t n);

  /** @brief Read a raw reply
   * @param reply Reply body
   * @return Packet type
   */
  int recv_reply(std::string &reply);

  /** @brief Poll thread
   * @param self Connection object
   */
  static void poll(SFTP::Connection *self);

  /** @brief Thread ID for poll thread */
  std::thread poller;

  friend class Attributes;
  friend class FileHandle;
  friend class DirectoryHandle;
};

/** @brief SFTP file attributes */
class Attributes {
public:
  /** @brief Flags controlling the other fields */
  uint32_t flags = 0;

  /** @brief File size (if @ref  SSH_FILEXFER_ATTR_SIZE is set in @c flags) */
  uint64_t size = 0;

  /** @brief Owning UID (if @ref  SSH_FILEXFER_ATTR_UIDGID is set in @c flags)
   */
  uint32_t uid = 0;

  /** @brief Owning GID (if @ref  SSH_FILEXFER_ATTR_UIDGID is set in @c flags)
   */
  uint32_t gid = 0;

  /** @brief File permissions (if @ref  SSH_FILEXFER_ATTR_PERMISSIONS is set
   * in
   * @c flags)
   */
  uint32_t permissions = 0;

  /** @brief Access time (if @ref  SSH_FILEXFER_ACMODTIME is set in
   * @c flags)
   */
  uint32_t atime = 0;

  /** @brief Modification time (if @ref  SSH_FILEXFER_ACMODTIME is set in
   * @c flags)
   */
  uint32_t mtime = 0;

  /** @brief Extended attributes */
  std::vector<std::pair<std::string, std::string>> extended;

  /** @brief Filename, if unpacked from @ref SSH_FXP_NAME */
  std::string filename;

  /** @brief Expanded filename if unpacked from @ref SSH_FXP_NAME */
  std::string longname;

  /** @brief Origin of this attribute structure */
  Connection *connection;

  /** @brief The full name for the file, excluding remote hostname */
  std::string localname;

  /** @brief The full name for the file, including remote hostname */
  std::string fullname;

  inline bool operator<(const Attributes &that) const {
    return filename < that.filename;
  }

private:
  /** @brief Unpack a serialized attributes object
   * @param c Connection from which serialized attributes came
   * @param reply Serialized attributes
   * @param pos Unpack position
   */
  void unpack(SFTP::Connection &c, const std::string &reply, size_t &pos);

  friend class Connection;
};

/** @brief Exception representing an SFTP error */
class Error : public std::runtime_error {
public:
  /** @brief Construct an SFTP error
   * @param status SFTP error code
   * @param message Human-readable SFTP error string
   */
  Error(uint32_t status, const std::string &message) :
    std::runtime_error(message), status(status) {}

  /** @brief SFTP error code */
  uint32_t status;
};

// Inlines

Handle::~Handle() {
  close();
}

void Handle::close() {
  if(connection) {
    connection->close(handle);
    connection = nullptr;
  }
}

uint32_t FileHandle::begin_read(uint64_t offset, uint32_t len) {
  return connection->begin_read(handle, offset, len);
}

std::string FileHandle::finish_read(uint32_t id) {
  return connection->finish_read(id);
}

uint32_t DirectoryHandle::begin_readdir() {
  return connection->begin_readdir(handle);
}

uint32_t DirectoryHandle::finish_readdir(uint32_t id,
                                         std::vector<SFTP::Attributes> &names) {
  return connection->finish_readdir(id, names, name);
}

void Connection::open(FileHandle &handle, const std::string &path,
                      uint32_t mode) {
  handle.connection = this;
  handle.name = path;
  handle.handle = open(path, mode);
}

void Connection::opendir(DirectoryHandle &handle, const std::string &path) {
  handle.connection = this;
  handle.name = path;
  handle.handle = opendir(path);
}

} // namespace SFTP

#endif
