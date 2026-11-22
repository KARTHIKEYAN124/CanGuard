#include "profile_builder.hpp"
#include "feature_extractor.hpp"
#include "window_builder.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <stdexcept>

NormalProfile build_profile(const std::vector<CanFrame>& normal_frames, std::int64_t window_ns) {
    NormalProfile profile;
    WindowBuilder builder(window_ns, [&](Window window) {
        const auto f = extract_features(window);
        auto& id = profile[window.can_id];
        id.max_messages_per_window = std::max(id.max_messages_per_window, double(f.message_count));
        id.max_dlc_changes = std::max(id.max_dlc_changes, double(f.dlc_changes));
        if (f.mean_interval_ns) {
            const auto interval = *f.mean_interval_ns;
            id.min_interval_ns = id.has_comparisons ? std::min(id.min_interval_ns, interval) : interval;
            id.max_interval_ns = std::max(id.max_interval_ns, interval);
            id.max_payload_bit_changes = std::max(id.max_payload_bit_changes, *f.mean_payload_bit_changes);
            id.has_comparisons = true;
        }
    });
    for (const auto& frame : normal_frames) builder.push(frame);
    builder.finish();
    return profile;
}
NormalProfile widen_profile(NormalProfile profile, double factor) {
    for (auto& [id, p] : profile) {
        p.min_interval_ns /= factor;
        p.max_interval_ns *= factor;
        p.max_messages_per_window = std::floor(p.max_messages_per_window * factor);
        p.max_payload_bit_changes *= factor;
        p.max_dlc_changes = std::floor(p.max_dlc_changes * factor);
    }
    return profile;
}
void save_profile(const std::string& path, const NormalProfile& profile, std::int64_t window_ns) {
    std::ofstream out(path);
    out.imbue(std::locale::classic());
    out << "CANGUARD_PROFILE_V1 " << window_ns << '\n'
        << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& [id, p] : std::map<std::uint32_t, IdProfile>(profile.begin(), profile.end())) {
        out << id << ' ' << p.min_interval_ns << ' ' << p.max_interval_ns << ' '
            << p.max_messages_per_window << ' ' << p.max_payload_bit_changes << ' '
            << p.max_dlc_changes << ' ' << p.has_comparisons << '\n';
    }
    out.close();
    if (!out) throw std::runtime_error("Cannot write profile: " + path);
}
NormalProfile load_profile(const std::string& path, std::int64_t window_ns) {
    std::ifstream in(path);
    in.imbue(std::locale::classic());
    std::string format;
    std::int64_t duration{};
    if (!(in >> format >> duration) || format != "CANGUARD_PROFILE_V1" || duration != window_ns) {
        throw std::runtime_error("Invalid profile or mismatched window duration: " + path);
    }
    NormalProfile profile;
    while (in >> std::ws && in.peek() != std::char_traits<char>::eof()) {
        std::uint32_t id;
        IdProfile p;
        if (!(in >> id >> p.min_interval_ns >> p.max_interval_ns >> p.max_messages_per_window
                 >> p.max_payload_bit_changes >> p.max_dlc_changes >> p.has_comparisons)
            || id > 0x1fffffff || !std::isfinite(p.max_interval_ns)
            || !std::isfinite(p.min_interval_ns) || !std::isfinite(p.max_messages_per_window)
            || !std::isfinite(p.max_payload_bit_changes) || !std::isfinite(p.max_dlc_changes)
            || p.min_interval_ns < 0 || p.max_interval_ns < p.min_interval_ns
            || p.max_messages_per_window < 1 || p.max_payload_bit_changes < 0 || p.max_dlc_changes < 0
            || !profile.emplace(id, p).second) {
            throw std::runtime_error("Invalid profile entry: " + path);
        }
    }
    if (in.bad() || profile.empty()) throw std::runtime_error("Unreadable or empty profile: " + path);
    return profile;
}
