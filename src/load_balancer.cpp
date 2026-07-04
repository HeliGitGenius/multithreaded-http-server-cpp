#include "load_balancer.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>

namespace {

constexpr int kListenBacklog = 1024;
constexpr int kIdleTimeoutMs = 5000; // 5s idle -> tear down the relay

uint64_t micros_since(const std::chrono::steady_clock::time_point& t0) {
    auto dt = std::chrono::steady_clock::now() - t0;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(dt).count());
}

// Copy readable bytes from src_fd to dst_fd once. Returns:
//   >0  bytes forwarded
//    0  src closed (EOF)
//   -1  error
ssize_t pump(int src_fd, int dst_fd) {
    char buf[16384];
    ssize_t n = net::read_some(src_fd, buf, sizeof(buf));
    if (n <= 0) return n;
    return net::write_all(dst_fd, buf, static_cast<size_t>(n)) ? n : -1;
}

} // namespace

LoadBalancer::LoadBalancer(Config cfg, Logger& log, Metrics& metrics)
    : cfg_(std::move(cfg)), log_(log), metrics_(metrics),
      pool_(static_cast<size_t>(cfg_.threads)) {}

uint16_t LoadBalancer::next_backend() {
    size_t idx = rr_.fetch_add(1, std::memory_order_relaxed) % cfg_.backends.size();
    return cfg_.backends[idx];
}

bool LoadBalancer::init() {
    std::string err;
    listener_ = net::create_listener(cfg_.frontend_port, kListenBacklog, &err);
    if (!listener_.valid()) {
        log_.error(err);
        return false;
    }

    std::string list;
    for (size_t i = 0; i < cfg_.backends.size(); ++i) {
        if (i) list += ",";
        list += std::to_string(cfg_.backends[i]);
    }
    log_.info("load balancer listening on port " + std::to_string(cfg_.frontend_port) +
              " -> backends [" + list + "], threads=" + std::to_string(cfg_.threads));
    return true;
}

void LoadBalancer::handle_connection(Socket client, std::string client_ip) {
    auto t0 = std::chrono::steady_clock::now();
    uint16_t backend_port = next_backend();

    std::string err;
    Socket backend = net::connect_local(backend_port, &err);
    if (!backend.valid()) {
        log_.error("backend " + std::to_string(backend_port) + " unreachable: " + err);
        metrics_.record(502, micros_since(t0));
        log_.access(client_ip, "PROXY", "->" + std::to_string(backend_port), 502, micros_since(t0));
        return;
    }

    int cfd = client.get();
    int bfd = backend.get();

    bool client_open = true; // still readable
    bool backend_open = true;

    while (client_open || backend_open) {
        pollfd fds[2];
        fds[0].fd = cfd;
        fds[0].events = client_open ? POLLIN : 0;
        fds[0].revents = 0;
        fds[1].fd = bfd;
        fds[1].events = backend_open ? POLLIN : 0;
        fds[1].revents = 0;

        int r = ::poll(fds, 2, kIdleTimeoutMs);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break; // idle timeout

        if (client_open && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = pump(cfd, bfd);
            if (n <= 0) {
                client_open = false;
                ::shutdown(bfd, SHUT_WR); // signal EOF downstream
            }
        }
        if (backend_open && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = pump(bfd, cfd);
            if (n <= 0) {
                backend_open = false;
                ::shutdown(cfd, SHUT_WR); // signal EOF upstream
            }
        }
    }

    uint64_t us = micros_since(t0);
    metrics_.record(200, us);
    log_.access(client_ip, "PROXY", "->" + std::to_string(backend_port), 200, us);
}

void LoadBalancer::run(const volatile std::sig_atomic_t& stop) {
    pollfd pfd{};
    pfd.fd = listener_.get();
    pfd.events = POLLIN;

    while (stop == 0) {
        int r = ::poll(&pfd, 1, 500);
        if (r < 0) {
            if (errno == EINTR) continue;
            log_.error(std::string("poll: ") + std::strerror(errno));
            break;
        }
        if (r == 0) continue;

        if (pfd.revents & POLLIN) {
            std::string ip;
            Socket client = net::accept_conn(listener_.get(), &ip);
            if (!client.valid()) continue;

            net::set_timeout(client.get(), 5);

            auto held = std::make_shared<Socket>(std::move(client));
            pool_.submit([this, held, ip]() mutable {
                handle_connection(std::move(*held), ip);
            });
        }
    }

    log_.info("shutting down: draining worker queue...");
    pool_.shutdown();
}
