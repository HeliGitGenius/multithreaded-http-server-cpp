#ifndef HTTP_SOCKET_H
#define HTTP_SOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h> // ssize_t

// A minimal RAII wrapper around a POSIX socket file descriptor.
//
// The wrapper owns exactly one fd. It is move-only so ownership is explicit and
// the descriptor is closed exactly once, in the destructor. Copying is deleted
// to prevent double-close.
class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(int fd) noexcept : fd_(fd) {}

    ~Socket();

    // Move-only semantics.
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    // Relinquish ownership of the fd without closing it.
    int release() noexcept;

    // Close the fd now (also called by the destructor).
    void close() noexcept;

private:
    int fd_ = -1;
};

namespace net {

// Create, bind and listen on a TCP socket for the given port on all
// interfaces. SO_REUSEADDR is set. Returns an invalid Socket on failure and
// writes a human-readable reason into *err.
Socket create_listener(uint16_t port, int backlog, std::string* err);

// Connect a fresh TCP socket to 127.0.0.1:port. Returns an invalid Socket on
// failure.
Socket connect_local(uint16_t port, std::string* err);

// Accept one connection, retrying on EINTR. On success fills client_ip with the
// dotted-quad address of the peer. Returns an invalid Socket if the listener
// was shut down or a non-recoverable error occurred.
Socket accept_conn(int listen_fd, std::string* client_ip);

// Apply a receive/send timeout (idle-connection timeout) to a socket.
bool set_timeout(int fd, int seconds);

// Read up to buf_len bytes. Returns bytes read (>0), 0 on orderly close,
// or -1 on error/timeout. Retries on EINTR.
ssize_t read_some(int fd, char* buf, size_t buf_len);

// Write the entire buffer, looping over partial writes and retrying on EINTR.
// Uses MSG_NOSIGNAL so a broken pipe reports EPIPE instead of raising SIGPIPE.
// Returns true if all bytes were written.
bool write_all(int fd, const char* buf, size_t len);

} // namespace net

#endif // HTTP_SOCKET_H
