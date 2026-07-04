#include "logger.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

// ISO-8601-ish local timestamp: "2026-07-04 15:04:05".
std::string now_stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

} // namespace

void Logger::write_line(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mu_);
    std::cerr << now_stamp() << " [" << level << "] " << msg << '\n';
}

void Logger::info(const std::string& msg) { write_line("INFO", msg); }
void Logger::error(const std::string& msg) { write_line("ERROR", msg); }

void Logger::access(const std::string& client_ip, const std::string& method,
                    const std::string& path, int status, uint64_t elapsed_us) {
    std::ostringstream os;
    os << client_ip << " \"" << method << ' ' << path << "\" " << status << ' '
       << std::fixed << std::setprecision(2) << (static_cast<double>(elapsed_us) / 1000.0) << "ms";
    write_line("ACCESS", os.str());
}

void Logger::print_metrics(const Metrics& m) {
    const uint64_t total = m.total_requests.load();
    const uint64_t errors = m.error_count.load();
    const uint64_t sum_us = m.total_response_us.load();
    const double avg_ms = total ? (static_cast<double>(sum_us) / static_cast<double>(total)) / 1000.0 : 0.0;

    std::lock_guard<std::mutex> lock(mu_);
    std::cerr << "\n==================== Metrics ====================\n";
    std::cerr << "Total requests served : " << total << '\n';
    std::cerr << "Error responses (>=400): " << errors << '\n';
    std::cerr << "Average response time  : " << std::fixed << std::setprecision(3) << avg_ms << " ms\n";
    std::cerr << "=================================================\n";
}
