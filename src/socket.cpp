#include "socket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int Socket::release() noexcept {
    int fd = fd_;
    fd_ = -1;
    return fd;
}

void Socket::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

namespace net {

Socket create_listener(uint16_t port, int backlog, std::string* err) {
    Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
    if (!sock.valid()) {
        if (err) *err = std::string("socket: ") + std::strerror(errno);
        return Socket{};
    }

    int on = 1;
    if (::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        if (err) *err = std::string("setsockopt(SO_REUSEADDR): ") + std::strerror(errno);
        return Socket{};
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (err) *err = std::string("bind(port ") + std::to_string(port) + "): " + std::strerror(errno);
        return Socket{};
    }

    if (::listen(sock.get(), backlog) < 0) {
        if (err) *err = std::string("listen: ") + std::strerror(errno);
        return Socket{};
    }

    return sock;
}

Socket connect_local(uint16_t port, std::string* err) {
    Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
    if (!sock.valid()) {
        if (err) *err = std::string("socket: ") + std::strerror(errno);
        return Socket{};
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rc;
    do {
        rc = ::connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        if (err) *err = std::string("connect(127.0.0.1:") + std::to_string(port) + "): " + std::strerror(errno);
        return Socket{};
    }

    return sock;
}

Socket accept_conn(int listen_fd, std::string* client_ip) {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);

    int fd;
    do {
        fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) return Socket{};

    if (client_ip) {
        char buf[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf))) {
            *client_ip = buf;
        } else {
            *client_ip = "?.?.?.?";
        }
    }

    return Socket(fd);
}

bool set_timeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return false;
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) return false;
    return true;
}

ssize_t read_some(int fd, char* buf, size_t buf_len) {
    ssize_t n;
    do {
        n = ::recv(fd, buf, buf_len, 0);
    } while (n < 0 && errno == EINTR);
    return n;
}

bool write_all(int fd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

} // namespace net
