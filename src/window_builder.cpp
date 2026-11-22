#include "window_builder.hpp"
#include <limits>
#include <stdexcept>
#include <utility>

WindowBuilder::WindowBuilder(std::int64_t window_ns, Emit emit)
    : duration_(window_ns), emit_(std::move(emit)) {
    if (duration_ <= 0) throw std::invalid_argument("Window duration must be positive");
}
void WindowBuilder::push(CanFrame frame) {
    if (frame.timestamp_ns < 0 || (last_ && frame.timestamp_ns < *last_)) {
        throw std::invalid_argument("Windows require nonnegative, chronological timestamps");
    }
    last_ = frame.timestamp_ns;
    if (!origin_) origin_ = frame.timestamp_ns;
    const auto offset = frame.timestamp_ns - *origin_;
    const auto start = frame.timestamp_ns - offset % duration_;
    if (start > std::numeric_limits<std::int64_t>::max() - duration_) {
        throw std::overflow_error("Window end exceeds timestamp range");
    }
    if (start != start_) {
        flush();
        start_ = start;
    }
    auto [entry, inserted] = buckets_.try_emplace(frame.can_id);
    auto& window = entry->second;
    if (inserted) {
        window.can_id = frame.can_id;
        window.start_ns = start;
        window.end_ns = start + duration_;
        if (const auto previous = previous_.find(frame.can_id); previous != previous_.end()) {
            window.has_previous = true;
            window.previous = previous->second;
        }
    }
    window.frames.push_back(frame);
    previous_.insert_or_assign(frame.can_id, frame);
}
void WindowBuilder::flush() {
    for (auto& [id, window] : buckets_) {
        window.task_id = next_task_++;
        emit_(std::move(window));
    }
    buckets_.clear();
}
void WindowBuilder::finish() { flush(); }

std::vector<Window> build_windows(const std::vector<CanFrame>& frames, std::int64_t window_ns) {
    std::vector<Window> windows;
    WindowBuilder builder(window_ns, [&](Window window) { windows.push_back(std::move(window)); });
    for (const auto& frame : frames) builder.push(frame);
    builder.finish();
    return windows;
}
