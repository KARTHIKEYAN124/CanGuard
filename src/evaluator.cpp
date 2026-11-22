#include "evaluator.hpp"
#include <stdexcept>

namespace {
double ratio(double numerator, double denominator) {
    return denominator ? numerator / denominator : 0;
}
}
double Evaluation::precision() const { return ratio(tp, double(tp) + fp); }
double Evaluation::recall() const { return ratio(tp, double(tp) + fn); }
double Evaluation::f1() const { return ratio(2.0 * tp, 2.0 * tp + fp + fn); }
double Evaluation::false_positive_rate() const { return ratio(fp, double(fp) + tn); }

WindowLabels build_window_labels(const std::vector<Window>& windows,
    const std::vector<FrameLabel>& labels, std::size_t frame_count) {
    WindowLabels by_sequence(frame_count);
    for (const auto& label : labels) {
        auto& value = by_sequence.at(label.sequence);
        if (value.has_value()) throw std::invalid_argument("Duplicate frame label");
        value = label.attack;
    }
    WindowLabels truth(windows.size());
    for (const auto& window : windows) {
        bool unknown = false;
        bool attacked = false;
        for (const auto& frame : window.frames) {
            const auto label = by_sequence.at(frame.sequence);
            unknown |= !label.has_value();
            attacked |= label.value_or(false);
        }
        if (attacked || !unknown) truth.at(window.task_id) = attacked;
    }
    return truth;
}
Evaluation evaluate(const std::vector<DetectionResult>& results, const WindowLabels& labels) {
    if (results.size() != labels.size()) throw std::invalid_argument("Prediction count mismatch");
    Evaluation result;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& prediction = results[i];
        if (prediction.task_id != i) throw std::invalid_argument("Predictions must be in task order");
        if (!labels[i]) ++result.unlabeled;
        else if (*labels[i] && prediction.suspicious) ++result.tp;
        else if (*labels[i]) ++result.fn;
        else if (prediction.suspicious) ++result.fp;
        else ++result.tn;
    }
    return result;
}
