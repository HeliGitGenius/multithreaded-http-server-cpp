#ifndef HTTP_LOAD_BALANCER_H
#define HTTP_LOAD_BALANCER_H

#include <atomic>
#include <csignal>
#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "logger.h"
#include "socket.h"
#include "thread_pool.h"

// A round-robin TCP reverse proxy. Each accepted frontend connection is assigned
// the next backend port (round-robin) and its bytes are relayed in both
// directions until either side closes or the connection is idle. Relaying runs
// on the thread pool so the accept loop is never blocked.
class LoadBalancer {
public:
    LoadBalancer(Config cfg, Logger& log, Metrics& metrics);

    bool init();
    void run(const volatile std::sig_atomic_t& stop);

private:
    void handle_connection(Socket client, std::string client_ip);
    uint16_t next_backend();

    Config cfg_;
    Logger& log_;
    Metrics& metrics_;
    Socket listener_;
    ThreadPool pool_;
    std::atomic<size_t> rr_{0};
};

#endif // HTTP_LOAD_BALANCER_H
