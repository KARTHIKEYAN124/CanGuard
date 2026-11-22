#include "execution.hpp"
#include "bounded_queue.hpp"
#include "thread_pool.hpp"
#include "window_builder.hpp"
#include <algorithm>
#include <deque>

namespace {
// Each time slice contains at most one window per CAN ID. Finish the slice
// before starting its successor so no ID can run ahead of its own history.
class TimeSlices {
public:
    void enter(std::int64_t start) {
        if (start_ && start < *start_) throw std::invalid_argument("Windows must be chronological");
        if (start_ != start) {
            finish();
            start_ = start;
        }
    }
    void add(std::future<void> task) { tasks_.push_back(std::move(task)); }
    void finish() {
        for (auto& task : tasks_) task.get();
        tasks_.clear();
    }
private:
    std::optional<std::int64_t> start_;
    std::vector<std::future<void>> tasks_;
};
}

std::vector<DetectionResult> detect_parallel(const std::vector<Window>& windows,
    const NormalProfile& profile, std::size_t workers, std::size_t capacity, Partition partition) {
    std::vector<DetectionResult> results(windows.size());
    struct Pending {
        std::deque<std::size_t> windows;
        bool active{};
    };
    std::mutex mutex;
    std::unordered_map<std::uint32_t, Pending> pending;
    // The pool is destroyed before the state its tasks reference.
    std::vector<std::future<void>> futures;
    TimeSlices slices;
    ThreadPool pool(workers, capacity);
    for (std::size_t i = 0; i < windows.size(); ++i) {
        if (partition == Partition::window) {
            slices.enter(windows[i].start_ns);
            slices.add(pool.submit([&, i] { results[i] = analyze_window(windows[i], profile); }));
            continue;
        }
        const auto id = windows[i].can_id;
        {
            std::lock_guard lock(mutex);
            auto& entry = pending[id];
            entry.windows.push_back(i);
            if (entry.active) continue;
            entry.active = true;
        }
        futures.push_back(pool.submit([&, id] {
            for (;;) {
                std::size_t index;
                {
                    std::lock_guard lock(mutex);
                    auto& entry = pending.at(id);
                    if (entry.windows.empty()) {
                        entry.active = false;
                        return;
                    }
                    index = entry.windows.front();
                    entry.windows.pop_front();
                }
                results[index] = analyze_window(windows[index], profile);
            }
        }));
    }
    slices.finish();
    for (auto& future : futures) future.get();
    return results;
}

PipelineResult detect_pipeline(const std::string& path, std::int64_t window_ns,
    const NormalProfile& profile, std::size_t workers, std::size_t capacity) {
    using FrameMessage = std::optional<CanFrame>;
    using WindowMessage = std::optional<Window>;
    using ResultMessage = std::optional<DetectionResult>;
    BoundedQueue<FrameMessage> frames(capacity);
    BoundedQueue<WindowMessage> ready_windows(capacity);
    ResultQueue<ResultMessage> results(capacity);
    PipelineResult output;
    std::vector<FrameLabel> labels;
    std::vector<std::uint64_t> frame_tasks;
    std::size_t task_count = 0;
    std::mutex error_mutex;
    std::exception_ptr error;
    const auto cancel = [&](std::exception_ptr cause) {
        {
            std::lock_guard lock(error_mutex);
            if (!error) error = cause;
        }
        frames.cancel();
        ready_windows.cancel();
        results.cancel();
    };
    const auto guard = [&](auto action) {
        return [&, action = std::move(action)] {
            try {
                action();
            } catch (...) {
                cancel(std::current_exception());
            }
        };
    };

    std::jthread reader, builder, dispatcher, alerts;
    try {
        alerts = std::jthread(guard([&] {
            std::unordered_map<std::uint32_t, std::int64_t> last_start;
            while (auto message = results.pop()) {
                if (!*message) break;
                const auto& result = **message;
                auto [previous, inserted] = last_start.try_emplace(result.can_id, result.start_ns);
                if (!inserted && result.start_ns <= previous->second) {
                    throw std::logic_error("Pipeline results arrived out of order for a CAN ID");
                }
                previous->second = result.start_ns;
                output.predictions.push_back(std::move(**message));
            }
        }));
        dispatcher = std::jthread(guard([&] {
            TimeSlices slices;
            ThreadPool pool(workers, capacity);
            while (auto message = ready_windows.pop()) {
                if (!*message) {
                    slices.finish();
                    pool.shutdown();
                    results.push(std::nullopt);
                    return;
                }
                slices.enter((*message)->start_ns);
                slices.add(pool.submit(guard([&, window = std::move(**message)] {
                    results.push(analyze_window(window, profile));
                })));
            }
        }));
        builder = std::jthread(guard([&] {
            WindowBuilder windows(window_ns, [&](Window window) {
                for (const auto& frame : window.frames) {
                    if (frame.sequence >= frame_tasks.size()) frame_tasks.resize(frame.sequence + 1);
                    frame_tasks[frame.sequence] = window.task_id;
                }
                ++task_count;
                ready_windows.push(std::move(window));
            });
            while (auto message = frames.pop()) {
                if (!*message) {
                    windows.finish();
                    ready_windows.push(std::nullopt);
                    return;
                }
                windows.push(**message);
            }
        }));
        reader = std::jthread(guard([&] {
            struct Cancelled {};
            try {
                output.quality = stream_hcrl(path, [&](CanFrame frame, std::optional<FrameLabel> label) {
                    if (label) labels.push_back(*label);
                    if (!frames.push(frame)) throw Cancelled{};
                });
                frames.push(std::nullopt);
            } catch (const Cancelled&) {
                // The initiating stage has already recorded its error.
            }
        }));
    } catch (...) {
        cancel(std::current_exception());
    }
    if (reader.joinable()) reader.join();
    if (builder.joinable()) builder.join();
    if (dispatcher.joinable()) dispatcher.join();
    if (alerts.joinable()) alerts.join();
    if (error) std::rethrow_exception(error);

    std::sort(output.predictions.begin(), output.predictions.end(),
        [](const auto& a, const auto& b) { return a.task_id < b.task_id; });
    if (frame_tasks.size() != output.quality.valid || output.predictions.size() != task_count) {
        throw std::runtime_error("Pipeline did not account for every frame and task");
    }
    output.labels.resize(task_count);
    std::vector<std::size_t> missing(task_count);
    for (const auto task : frame_tasks) ++missing[task];
    for (const auto& label : labels) {
        const auto task = frame_tasks.at(label.sequence);
        --missing[task];
        if (label.attack) output.labels[task] = true;
    }
    for (std::size_t task = 0; task < task_count; ++task) {
        if (!missing[task] && !output.labels[task]) output.labels[task] = false;
    }
    return output;
}
