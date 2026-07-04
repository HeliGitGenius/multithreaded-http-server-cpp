#ifndef HTTP_LOGGER_H
#define HTTP_LOGGER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// Aggregate counters printed once on shutdown. All members are atomic so worker
// threads can update them without holding a lock.
struct Metrics {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> error_count{0};       // responses with status >= 400
    std::atomic<uint64_t> total_response_us{0};  // summed latency, microseconds

    void record(int status, uint64_t elapsed_us) {
        total_requests.fetch_add(1, std::memory_order_relaxed);
        total_response_us.fetch_add(elapsed_us, std::memory_order_relaxed);
        if (status >= 400) error_count.fetch_add(1, std::memory_order_relaxed);
    }
};

// Simple thread-safe logger writing to stderr. A single mutex serialises whole
// lines so concurrent worker output is never interleaved.
class Logger {
public:
    // Structured per-request access log line.
    void access(const std::string& client_ip, const std::string& method,
                const std::string& path, int status, uint64_t elapsed_us);

    // Free-form informational / error line.
    void info(const std::string& msg);
    void error(const std::string& msg);

    // Print the accumulated metrics summary.
    void print_metrics(const Metrics& m);

private:
    void write_line(const std::string& level, const std::string& msg);

    std::mutex mu_;
};

#endif // HTTP_LOGGER_H
