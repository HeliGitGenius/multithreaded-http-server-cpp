#include <csignal>
#include <cstring>
#include <iostream>

#include "config.h"
#include "load_balancer.h"
#include "logger.h"
#include "server.h"

namespace {

// Set by the signal handler; polled by the accept loops. sig_atomic_t is the
// only type guaranteed safe to touch from a signal handler.
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handle_stop(int) { g_stop = 1; }

// Install SIGINT/SIGTERM -> graceful stop, and ignore SIGPIPE so a write to a
// closed peer returns EPIPE instead of killing the process.
void install_signals() {
    struct sigaction sa {};
    sa.sa_handler = handle_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART: let blocking calls return EINTR
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    struct sigaction ign {};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;
    sigaction(SIGPIPE, &ign, nullptr);
}

} // namespace

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);
    const std::string prog = (argc > 0 && argv[0]) ? argv[0] : "http_server";

    if (cfg.show_help) {
        std::cout << usage_text(prog);
        return 0;
    }
    if (!cfg.error.empty()) {
        std::cerr << "error: " << cfg.error << "\n\n" << usage_text(prog);
        return cfg.exit_code;
    }

    install_signals();

    Logger log;
    Metrics metrics;

    int rc = 0;
    if (cfg.mode == Config::Mode::LoadBalancer) {
        LoadBalancer lb(cfg, log, metrics);
        if (!lb.init()) {
            rc = 1;
        } else {
            lb.run(g_stop);
        }
    } else {
        HttpServer server(std::move(cfg), log, metrics);
        if (!server.init()) {
            rc = 1;
        } else {
            server.run(g_stop);
        }
    }

    log.print_metrics(metrics);
    return rc;
}
