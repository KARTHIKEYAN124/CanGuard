#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
public:
    ThreadPool(std::size_t worker_count, std::size_t queue_capacity) : capacity_(queue_capacity) {
        if (!worker_count || !capacity_) throw std::invalid_argument("Pool sizes must be positive");
        workers_.reserve(worker_count);
        try {
            for (std::size_t i = 0; i < worker_count; ++i) {
                workers_.emplace_back([this] { work(); });
            }
        } catch (...) {
            shutdown();
            throw;
        }
    }
    template<class F>
    auto submit(F&& callable) {
        using Result = std::invoke_result_t<std::decay_t<F>&>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(callable));
        auto future = task->get_future();
        {
            std::unique_lock lock(mutex_);
            space_available_.wait(lock, [&] { return stopping_ || tasks_.size() < capacity_; });
            if (stopping_) throw std::runtime_error("Thread pool has stopped");
            tasks_.emplace([task] { (*task)(); });
        }
        work_available_.notify_one();
        return future;
    }
    // Called by the controlling thread. Pending tasks are drained.
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        work_available_.notify_all();
        space_available_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }
    ~ThreadPool() { shutdown(); }
private:
    void work() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                work_available_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
                if (tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            space_available_.notify_one();
            task();
        }
    }
    std::mutex mutex_;
    std::condition_variable work_available_, space_available_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    const std::size_t capacity_;
    bool stopping_{};
};
