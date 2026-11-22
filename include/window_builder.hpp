#pragma once
#include "types.hpp"
#include <functional>
#include <map>
#include <unordered_map>

class WindowBuilder {
public:
    using Emit = std::function<void(Window)>;
    WindowBuilder(std::int64_t window_ns, Emit emit);
    void push(CanFrame frame);
    void finish();
private:
    void flush();
    std::int64_t duration_;
    Emit emit_;
    std::optional<std::int64_t> origin_, last_;
    std::int64_t start_{};
    std::uint64_t next_task_{};
    std::map<std::uint32_t, Window> buckets_;
    std::unordered_map<std::uint32_t, CanFrame> previous_;
};
std::vector<Window> build_windows(const std::vector<CanFrame>& frames, std::int64_t window_ns);
