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

namespace SFTP {

class Attributes;

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
   * @param path Remote filename
   * @param mode Open mode
   * @return Handle for later access
   *
   * @p mode can be @ref SSH_FXF_READ, etc.
   */
  std::string open(const std::string &path, uint32_t mode);

  /** @brief Close a remote file
   * @param handle Handle as returned by @ref open
   */
  void close(const std::string &handle);

  /** @brief Get remote file information
   * @param handle Handle as returned by @ref open
   */
  void fstat(const std::string &handle, Attributes &attrs);

  /** @brief Get full path
   * @param path Filename
   * @return Full filename
   */
  std::string realpath(const std::string &path);

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

private:
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
   *
   * Raises an exception if the status is nonzero.
   */
  void error(const std::string &body, const std::string &context = "");

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
};

/** @brief SFTP file attributes */
class Attributes {
public:
  uint32_t flags = 0;
  uint64_t size = 0;
  uint32_t uid = 0;
  uint32_t gid = 0;
  uint32_t permissions = 0;
  uint32_t atime = 0;
  uint32_t mtime = 0;
  std::vector<std::pair<std::string, std::string>> extended;

private:
  void unpack(const SFTP::Connection &c, std::string &reply, size_t &pos);
  friend class Connection;
};

/** @brief Exception representing an SFTP error */
class Error : public std::runtime_error {
public:
  Error(uint32_t status, const std::string &message) :
    std::runtime_error(message), status(status) {}

private:
  uint32_t status;
};

} // namespace SFTP

#endif
