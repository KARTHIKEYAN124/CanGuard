#include "feature_extractor.hpp"
#include <algorithm>
#include <bit>

Features extract_features(const Window& window) {
    Features features;
    features.message_count = window.frames.size();
    const CanFrame* previous = window.has_previous ? &window.previous : nullptr;
    double intervals = 0, bit_changes = 0;
    std::size_t comparisons = 0;
    for (const auto& frame : window.frames) {
        if (previous) {
            intervals += static_cast<double>(frame.timestamp_ns - previous->timestamp_ns);
            features.dlc_changes += frame.dlc != previous->dlc;
            for (std::size_t i = 0; i < std::min(frame.dlc, previous->dlc); ++i) {
                bit_changes += std::popcount(static_cast<unsigned>(frame.payload[i] ^ previous->payload[i]));
            }
            ++comparisons;
        }
        previous = &frame;
    }
    if (comparisons) {
        features.mean_interval_ns = intervals / comparisons;
        features.mean_payload_bit_changes = bit_changes / comparisons;
    }
    return features;
}
