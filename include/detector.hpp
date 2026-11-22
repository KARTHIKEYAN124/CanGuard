#pragma once
#include "profile_builder.hpp"
DetectionResult analyze_window(const Window& window, const NormalProfile& profile);
std::vector<DetectionResult> detect_sequential(const std::vector<Window>& windows,
                                               const NormalProfile& profile);
