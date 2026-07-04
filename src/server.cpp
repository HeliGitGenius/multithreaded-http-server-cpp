#include "server.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <poll.h>
#include <sstream>

namespace {

constexpr int kListenBacklog = 1024;
constexpr size_t kMaxRequestBytes = 64 * 1024; // header+body cap to bound memory
constexpr int kMaxKeepAliveReqs = 100;

// Read an entire file into a string. Returns false if it cannot be opened.
bool read_file(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

uint64_t micros_since(const std::chrono::steady_clock::time_point& t0) {
    auto dt = std::chrono::steady_clock::now() - t0;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(dt).count());
}

} // namespace

HttpServer::HttpServer(Config cfg, Logger& log, Metrics& metrics)
    : cfg_(std::move(cfg)), log_(log), metrics_(metrics),
      pool_(static_cast<size_t>(cfg_.threads)) {}

bool HttpServer::init() {
    // Canonicalise the root so we can reject traversal attempts by prefix check.
    char resolved[PATH_MAX];
    if (::realpath(cfg_.root.c_str(), resolved) == nullptr) {
        log_.error("cannot resolve root directory '" + cfg_.root + "': " + std::strerror(errno));
        return false;
    }
    real_root_ = resolved;

    std::string err;
    listener_ = net::create_listener(cfg_.port, kListenBacklog, &err);
    if (!listener_.valid()) {
        log_.error(err);
        return false;
    }

    log_.info("HTTP server listening on port " + std::to_string(cfg_.port) +
              ", root=" + real_root_ + ", threads=" + std::to_string(cfg_.threads));
    return true;
}

int HttpServer::resolve_file(const std::string& req_path, std::string* out_path) const {
    std::string rel = req_path;
    if (rel.empty() || rel == "/") rel = "/index.html";

    std::string candidate = real_root_ + rel;

    char resolved[PATH_MAX];
    if (::realpath(candidate.c_str(), resolved) == nullptr) {
        return 404; // does not exist
    }
    std::string real = resolved;

    // Must live inside the (canonical) root. Compare with a trailing separator
    // so "/rootevil" is not accepted as being under "/root".
    std::string root_prefix = real_root_;
    if (root_prefix.back() != '/') root_prefix.push_back('/');
    if (real != real_root_ && real.compare(0, root_prefix.size(), root_prefix) != 0) {
        return 403; // traversal outside root
    }

    // Reject directories.
    std::ifstream test(real, std::ios::binary);
    if (!test) return 404;

    *out_path = real;
    return 200;
}

HttpResponse HttpServer::build_response(const HttpRequest& req) {
    // Only GET and HEAD are served; POST is accepted and echoed minimally so the
    // bonus body-parsing path is exercised, everything else is 405.
    if (req.method == "POST") {
        HttpResponse resp(200);
        std::ostringstream body;
        body << "Received POST " << req.path << " (" << req.body.size() << " bytes)\n";
        resp.set_body(body.str());
        resp.set_header("Content-Type", "text/plain; charset=utf-8");
        resp.set_header("Connection", req.keep_alive ? "keep-alive" : "close");
        return resp;
    }

    if (req.method != "GET" && req.method != "HEAD") {
        HttpResponse resp = HttpResponse::make_error(405, req.keep_alive);
        resp.set_header("Allow", "GET, HEAD, POST");
        return resp;
    }

    std::string file_path;
    int status = resolve_file(req.path, &file_path);
    if (status != 200) {
        return HttpResponse::make_error(status, req.keep_alive);
    }

    std::string contents;
    if (!read_file(file_path, &contents)) {
        return HttpResponse::make_error(500, req.keep_alive);
    }

    HttpResponse resp(200);
    resp.set_body(std::move(contents));
    resp.set_header("Content-Type", mime_type(file_path));
    resp.set_header("Connection", req.keep_alive ? "keep-alive" : "close");
    return resp;
}

void HttpServer::handle_connection(Socket sock, std::string client_ip) {
    std::string buffer;
    int served = 0;

    for (;;) {
        // Try to parse whatever is buffered before reading more.
        HttpRequest req;
        size_t consumed = 0;
        ParseStatus st = parse_request(buffer, &req, &consumed);

        if (st == ParseStatus::Incomplete) {
            if (buffer.size() > kMaxRequestBytes) {
                // Oversized headers: refuse rather than buffer unbounded.
                auto resp = HttpResponse::make_error(400, false);
                std::string wire = resp.serialize(true);
                net::write_all(sock.get(), wire.data(), wire.size());
                metrics_.record(400, 0);
                log_.access(client_ip, "-", "-", 400, 0);
                return;
            }
            char tmp[8192];
            ssize_t n = net::read_some(sock.get(), tmp, sizeof(tmp));
            if (n > 0) {
                buffer.append(tmp, static_cast<size_t>(n));
                continue;
            }
            // n == 0: peer closed. n < 0: timeout/error -> close connection.
            return;
        }

        auto t0 = std::chrono::steady_clock::now();

        if (st == ParseStatus::BadRequest) {
            HttpResponse resp = HttpResponse::make_error(400, false);
            std::string wire = resp.serialize(true);
            net::write_all(sock.get(), wire.data(), wire.size());
            uint64_t us = micros_since(t0);
            metrics_.record(400, us);
            log_.access(client_ip, "-", "-", 400, us);
            return; // cannot trust the stream after a malformed request
        }

        // st == Ok. If this connection has reached its keep-alive budget, tell
        // the client to close so it reconnects cleanly instead of racing a
        // server-side close on a socket it believes is reusable.
        if (served + 1 >= kMaxKeepAliveReqs) req.keep_alive = false;

        HttpResponse resp = build_response(req);
        bool include_body = (req.method != "HEAD");
        std::string wire = resp.serialize(include_body);
        bool ok = net::write_all(sock.get(), wire.data(), wire.size());

        uint64_t us = micros_since(t0);
        metrics_.record(resp.status(), us);
        log_.access(client_ip, req.method, req.path, resp.status(), us);

        // Remove the consumed request from the buffer (pipelining-safe).
        if (consumed <= buffer.size()) buffer.erase(0, consumed);
        else buffer.clear();

        ++served;
        if (!ok || !req.keep_alive) return;
    }
}

void HttpServer::run(const volatile std::sig_atomic_t& stop) {
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
        if (r == 0) continue; // timeout: re-check stop flag

        if (pfd.revents & POLLIN) {
            std::string ip;
            Socket client = net::accept_conn(listener_.get(), &ip);
            if (!client.valid()) continue;

            net::set_timeout(client.get(), 5); // 5s idle timeout

            // std::function must be copyable, so hand ownership to a shared_ptr.
            auto held = std::make_shared<Socket>(std::move(client));
            pool_.submit([this, held, ip]() mutable {
                handle_connection(std::move(*held), ip);
            });
        }
    }

    log_.info("shutting down: draining worker queue...");
    pool_.shutdown(); // drains queued connections and joins workers
}
