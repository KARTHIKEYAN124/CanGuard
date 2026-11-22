#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <utility>

// One producer and one consumer. One extra permit per semaphore is reserved for cancellation.
template<class T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : slots_(checked_capacity(capacity)) {}
    bool push(T value) {
        slots_.acquire();
        std::lock_guard lock(mutex_);
        if (cancelled_) return false;
        items_.push_back(std::move(value));
        ready_.release();
        return true;
    }
    std::optional<T> pop() {
        ready_.acquire();
        std::lock_guard lock(mutex_);
        if (cancelled_) return std::nullopt;
        auto item = std::move(items_.front());
        items_.pop_front();
        slots_.release();
        return item;
    }
    void cancel() {
        std::lock_guard lock(mutex_);
        if (cancelled_) return;
        cancelled_ = true;
        slots_.release();
        ready_.release();
    }
private:
    static std::ptrdiff_t checked_capacity(std::size_t capacity) {
        if (!capacity || capacity >= static_cast<std::size_t>(std::counting_semaphore<>::max())) {
            throw std::invalid_argument("Frame queue capacity out of range");
        }
        return static_cast<std::ptrdiff_t>(capacity);
    }
    std::mutex mutex_;
    std::deque<T> items_;
    std::counting_semaphore<> slots_, ready_{0};
    bool cancelled_{};
};

// Multiple producers publish results to one reader.
template<class T>
class ResultQueue {
public:
    explicit ResultQueue(std::size_t capacity) : capacity_(capacity) {
        if (!capacity_) throw std::invalid_argument("Result queue capacity must be positive");
    }
    bool push(T value) {
        std::unique_lock lock(mutex_);
        space_.wait(lock, [&] { return cancelled_ || queue_.size() < capacity_; });
        if (cancelled_) return false;
        queue_.push_back(std::move(value));
        ready_.notify_one();
        return true;
    }
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [&] { return cancelled_ || !queue_.empty(); });
        if (cancelled_) return std::nullopt;
        auto value = std::move(queue_.front());
        queue_.pop_front();
        space_.notify_one();
        return value;
    }
    void cancel() {
        std::lock_guard lock(mutex_);
        cancelled_ = true;
        ready_.notify_all();
        space_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_, space_;
    std::deque<T> queue_;
    const std::size_t capacity_;
    bool cancelled_{};
};
