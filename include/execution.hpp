#pragma once
#include "dataset_reader.hpp"
#include "detector.hpp"
#include "evaluator.hpp"

enum class Partition { window, can_id };
std::vector<DetectionResult> detect_parallel(const std::vector<Window>& windows,
    const NormalProfile& profile, std::size_t workers, std::size_t capacity, Partition partition);

struct PipelineResult {
    std::vector<DetectionResult> predictions;
    WindowLabels labels;
    DataQuality quality;
};
PipelineResult detect_pipeline(const std::string& path, std::int64_t window_ns,
    const NormalProfile& profile, std::size_t workers, std::size_t capacity);
