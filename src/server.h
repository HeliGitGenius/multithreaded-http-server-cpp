#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <csignal>
#include <string>

#include "config.h"
#include "http_request.h"
#include "http_response.h"
#include "logger.h"
#include "socket.h"
#include "thread_pool.h"

// A concurrent static-file HTTP/1.1 server. The listen socket is accepted on the
// calling thread; each accepted connection is dispatched to a worker in the
// thread pool, which reads, parses and serves requests (with keep-alive) until
// the peer closes or the connection times out.
class HttpServer {
public:
    HttpServer(Config cfg, Logger& log, Metrics& metrics);

    // Create and bind the listen socket. Returns false (and logs) on failure.
    bool init();

    // Run the accept loop until *stop becomes non-zero, then drain the pool.
    void run(const volatile std::sig_atomic_t& stop);

private:
    // Serve requests on one connection until close/timeout.
    void handle_connection(Socket sock, std::string client_ip);

    // Build the response for a single parsed request.
    HttpResponse build_response(const HttpRequest& req);

    // Resolve a request path to a real file under root_, guarding against
    // directory traversal. On success sets *out_path and returns 200; otherwise
    // returns 403/404.
    int resolve_file(const std::string& req_path, std::string* out_path) const;

    Config cfg_;
    Logger& log_;
    Metrics& metrics_;
    Socket listener_;
    ThreadPool pool_;
    std::string real_root_; // canonicalised root directory
};

#endif // HTTP_SERVER_H
