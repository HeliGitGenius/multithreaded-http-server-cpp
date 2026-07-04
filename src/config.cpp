#include "config.h"

#include <cstdlib>
#include <sstream>

namespace {

// Parse a string into an integer within [lo, hi]. Returns false on any junk.
bool parse_int(const std::string& s, long lo, long hi, long* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0') return false;
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

// Split a comma-separated list of ports ("8081,8082,8083").
bool parse_backends(const std::string& s, std::vector<uint16_t>* out) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        long v;
        if (!parse_int(item, 1, 65535, &v)) return false;
        out->push_back(static_cast<uint16_t>(v));
    }
    return !out->empty();
}

// Fetch the argument that must follow a flag; records an error if missing.
bool need_value(int argc, char** argv, int* i, const char* flag, std::string* value, Config* cfg) {
    if (*i + 1 >= argc) {
        cfg->error = std::string("missing value for ") + flag;
        cfg->exit_code = 2;
        return false;
    }
    *value = argv[++(*i)];
    return true;
}

} // namespace

std::string usage_text(const std::string& prog) {
    std::ostringstream os;
    os << "Multithreaded HTTP Server & Load Balancer (C++17)\n\n"
       << "Usage:\n"
       << "  " << prog << " [--port N] [--root DIR] [--threads N]\n"
       << "  " << prog << " --load-balancer --frontend-port N --backends P1,P2,...\n\n"
       << "Server mode (default):\n"
       << "  --port N          Listen port (default 8080)\n"
       << "  --root DIR        Static file root (default ./public)\n"
       << "  --threads N       Worker thread count (default 4)\n\n"
       << "Load-balancer mode:\n"
       << "  --load-balancer   Run as a round-robin reverse proxy\n"
       << "  --frontend-port N Port clients connect to (default 80)\n"
       << "  --backends LIST   Comma-separated backend ports, e.g. 8081,8082,8083\n"
       << "  --threads N       Worker thread count (default 4)\n\n"
       << "Other:\n"
       << "  --help            Show this message\n\n"
       << "Examples:\n"
       << "  " << prog << " --port 8080 --root ./public --threads 4\n"
       << "  " << prog << " --load-balancer --frontend-port 8080 --backends 8081,8082,8083\n";
    return os.str();
}

Config parse_args(int argc, char** argv) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string val;

        if (arg == "--help" || arg == "-h") {
            cfg.show_help = true;
            cfg.exit_code = 0;
            return cfg;
        } else if (arg == "--load-balancer") {
            cfg.mode = Config::Mode::LoadBalancer;
        } else if (arg == "--port") {
            if (!need_value(argc, argv, &i, "--port", &val, &cfg)) return cfg;
            long v;
            if (!parse_int(val, 1, 65535, &v)) {
                cfg.error = "invalid --port: " + val;
                cfg.exit_code = 2;
                return cfg;
            }
            cfg.port = static_cast<uint16_t>(v);
        } else if (arg == "--frontend-port") {
            if (!need_value(argc, argv, &i, "--frontend-port", &val, &cfg)) return cfg;
            long v;
            if (!parse_int(val, 1, 65535, &v)) {
                cfg.error = "invalid --frontend-port: " + val;
                cfg.exit_code = 2;
                return cfg;
            }
            cfg.frontend_port = static_cast<uint16_t>(v);
        } else if (arg == "--root") {
            if (!need_value(argc, argv, &i, "--root", &val, &cfg)) return cfg;
            cfg.root = val;
        } else if (arg == "--threads") {
            if (!need_value(argc, argv, &i, "--threads", &val, &cfg)) return cfg;
            long v;
            if (!parse_int(val, 1, 1024, &v)) {
                cfg.error = "invalid --threads (1..1024): " + val;
                cfg.exit_code = 2;
                return cfg;
            }
            cfg.threads = static_cast<int>(v);
        } else if (arg == "--backends") {
            if (!need_value(argc, argv, &i, "--backends", &val, &cfg)) return cfg;
            cfg.backends.clear();
            if (!parse_backends(val, &cfg.backends)) {
                cfg.error = "invalid --backends list: " + val;
                cfg.exit_code = 2;
                return cfg;
            }
        } else {
            cfg.error = "unknown argument: " + arg;
            cfg.exit_code = 2;
            return cfg;
        }
    }

    if (cfg.mode == Config::Mode::LoadBalancer && cfg.backends.empty()) {
        cfg.error = "--load-balancer requires --backends";
        cfg.exit_code = 2;
    }

    return cfg;
}
