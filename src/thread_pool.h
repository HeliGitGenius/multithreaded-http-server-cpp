#ifndef HTTP_THREAD_POOL_H
#define HTTP_THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// A fixed-size pool of worker threads that pull tasks from a shared queue.
//
// Tasks are std::function<void()>. Enqueued tasks run on some worker thread.
// shutdown() drains the queue (running every queued task) and joins all workers,
// which is required for a graceful stop on SIGINT/SIGTERM.
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a task. Ignored (returns false) after shutdown has begun.
    bool submit(std::function<void()> task);

    // Signal workers to stop, let them finish every queued task, then join.
    // Idempotent.
    void shutdown();

    size_t size() const { return workers_.size(); }

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex mu_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

#endif // HTTP_THREAD_POOL_H
