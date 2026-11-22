#pragma once
#include "types.hpp"

struct Evaluation {
    std::uint64_t tp{}, fp{}, tn{}, fn{}, unlabeled{};
    double precision() const;
    double recall() const;
    double f1() const;
    double false_positive_rate() const;
};
using WindowLabels = std::vector<std::optional<bool>>;
WindowLabels build_window_labels(const std::vector<Window>& windows,
    const std::vector<FrameLabel>& labels, std::size_t frame_count);
Evaluation evaluate(const std::vector<DetectionResult>& results, const WindowLabels& labels);
