#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct CanFrame {
    std::uint64_t sequence{};
    std::int64_t timestamp_ns{};
    std::uint32_t can_id{};
    std::uint8_t dlc{};
    std::array<std::uint8_t, 8> payload{};
};
struct FrameLabel {
    std::uint64_t sequence{};
    bool attack{};
};
struct Window {
    std::uint64_t task_id{};
    std::uint32_t can_id{};
    std::int64_t start_ns{}, end_ns{};
    std::vector<CanFrame> frames;
    bool has_previous{};
    CanFrame previous{};
};
struct Features {
    std::size_t message_count{};
    std::optional<double> mean_interval_ns;
    std::optional<double> mean_payload_bit_changes;
    std::size_t dlc_changes{};
};
struct DetectionResult {
    std::uint64_t task_id{};
    std::uint32_t can_id{};
    std::int64_t start_ns{}, end_ns{};
    bool suspicious{};
    std::vector<std::string> reasons;
    bool operator==(const DetectionResult&) const = default;
};
