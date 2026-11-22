#include "detector.hpp"
#include "feature_extractor.hpp"

DetectionResult analyze_window(const Window& window, const NormalProfile& profile) {
    DetectionResult result{window.task_id, window.can_id, window.start_ns, window.end_ns, false, {}};
    const auto found = profile.find(window.can_id);
    if (found == profile.end()) {
        result.reasons.emplace_back("UNKNOWN_ID");
    } else {
        const auto features = extract_features(window);
        const auto& p = found->second;
        if (features.message_count > p.max_messages_per_window) {
            result.reasons.emplace_back("MESSAGE_COUNT");
        }
        if (p.has_comparisons && features.mean_interval_ns) {
            if (*features.mean_interval_ns < p.min_interval_ns ||
                *features.mean_interval_ns > p.max_interval_ns) {
                result.reasons.emplace_back("TIMING");
            }
            if (*features.mean_payload_bit_changes > p.max_payload_bit_changes) {
                result.reasons.emplace_back("PAYLOAD_CHANGES");
            }
        }
        if (features.dlc_changes > p.max_dlc_changes) result.reasons.emplace_back("DLC_CHANGES");
    }
    result.suspicious = !result.reasons.empty();
    return result;
}

std::vector<DetectionResult> detect_sequential(const std::vector<Window>& windows,
                                               const NormalProfile& profile) {
    std::vector<DetectionResult> results;
    results.reserve(windows.size());
    for (const auto& window : windows) results.push_back(analyze_window(window, profile));
    return results;
}
