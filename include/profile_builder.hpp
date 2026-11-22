#pragma once
#include "types.hpp"
#include <unordered_map>

struct IdProfile {
    double min_interval_ns{}, max_interval_ns{};
    double max_messages_per_window{}, max_payload_bit_changes{};
    double max_dlc_changes{};
    bool has_comparisons{};
};
using NormalProfile = std::unordered_map<std::uint32_t, IdProfile>;

NormalProfile build_profile(const std::vector<CanFrame>& normal_frames, std::int64_t window_ns);
NormalProfile widen_profile(NormalProfile profile, double factor);
void save_profile(const std::string& path, const NormalProfile& profile, std::int64_t window_ns);
NormalProfile load_profile(const std::string& path, std::int64_t window_ns);
