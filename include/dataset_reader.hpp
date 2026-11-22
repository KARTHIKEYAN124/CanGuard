#pragma once
#include "types.hpp"
#include <functional>

struct RowIssue {
    std::size_t line{};
    std::string reason;
};
struct DataQuality {
    std::size_t valid{}, malformed{}, unique_ids{}, regressions{};
    std::vector<RowIssue> issues;
};
struct Dataset {
    std::vector<CanFrame> frames;
    std::vector<FrameLabel> labels;
    DataQuality quality;
};
DataQuality stream_hcrl(const std::string& path,
    const std::function<void(CanFrame, std::optional<FrameLabel>)>& consume);
Dataset read_hcrl(const std::string& path);
