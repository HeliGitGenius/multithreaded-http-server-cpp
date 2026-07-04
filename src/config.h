#ifndef HTTP_CONFIG_H
#define HTTP_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

// Parsed program configuration. One of two modes is selected:
//   - server        (default): serve static files
//   - load_balancer (--load-balancer): round-robin proxy to backends
struct Config {
    enum class Mode { Server, LoadBalancer };

    Mode mode = Mode::Server;

    // Server mode.
    uint16_t port = 8080;
    std::string root = "./public";
    int threads = 4;

    // Load-balancer mode.
    uint16_t frontend_port = 80;
    std::vector<uint16_t> backends;

    // Set when --help was requested or parsing failed. When true the program
    // should print help/usage and exit with exit_code.
    bool show_help = false;
    int exit_code = 0;
    std::string error; // non-empty on parse failure
};

// Parse argv into a Config. Never throws. On error, sets error/exit_code and
// returns the (partially filled) config; the caller inspects .error.
Config parse_args(int argc, char** argv);

// Human-readable usage string.
std::string usage_text(const std::string& prog);

#endif // HTTP_CONFIG_H
