#include "execution.hpp"
#include "feature_extractor.hpp"
#include "thread_pool.hpp"
#include "window_builder.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected demo and parser fixture directories");
        const std::filesystem::path data(argv[1]);
        const std::filesystem::path fixtures(argv[2]);
        const auto training = read_hcrl((data / "demo_train.csv").string());
        const auto normal_log = read_hcrl((fixtures / "normal_log_fixture.txt").string());
        check(normal_log.quality.valid == 2 && normal_log.quality.malformed == 1 &&
            normal_log.labels.empty() && normal_log.frames[0].timestamp_ns == 1000000001 &&
            normal_log.frames[0].can_id == 0x350 && normal_log.frames[0].payload[1] == 255 &&
            normal_log.frames[1].dlc == 0, "Native HCRL attack-free log");
        const auto packed = read_hcrl((fixtures / "packed_fixture.csv").string());
        check(packed.quality.valid == 2 && packed.labels.size() == 2 &&
            packed.frames[0].payload[1] == 255 && packed.labels[1].attack, "Packed CSV");
        const auto invalid = read_hcrl((fixtures / "invalid_records.csv").string());
        constexpr const char* reasons[]{
            "MISSING_FIELDS", "MISSING_FIELDS", "MISSING_FIELDS", "MISSING_FIELDS",
            "INVALID_CAN_ID", "INVALID_CAN_ID",
            "INVALID_PAYLOAD_LENGTH", "INVALID_PAYLOAD_LENGTH", "INVALID_PAYLOAD_LENGTH",
            "INVALID_PAYLOAD_LENGTH"
        };
        check(invalid.quality.valid == 1 && invalid.quality.malformed == std::size(reasons) &&
            invalid.quality.issues.size() == std::size(reasons) &&
            invalid.frames[0].can_id == 0x1fffffff && invalid.frames[0].dlc == 1,
            "Reject invalid records and accept a padded payload with a valid extended ID");
        for (std::size_t i = 0; i < std::size(reasons); ++i) {
            const auto& issue = invalid.quality.issues[i];
            check(issue.line == i + 1 && issue.reason == reasons[i], "Invalid record diagnostic");
        }
        const auto path = (data / "demo_test.csv").string();
        const auto test = read_hcrl(path);
        check(test.quality.valid == 10 && test.quality.malformed == 1 &&
              test.quality.regressions == 1 && test.quality.unique_ids == 3, "Quality counters");
        check(training.quality.valid == 9 && training.frames[0].timestamp_ns == 1000000001,
              "Padded rows and nanosecond timestamp parsing");

        constexpr std::int64_t duration = 100'000'000;
        const auto windows = build_windows(test.frames, duration);
        const auto count = std::accumulate(windows.begin(), windows.end(), std::size_t{},
            [](auto sum, const auto& window) { return sum + window.frames.size(); });
        check(count == test.frames.size() && windows.size() == 7, "Each accepted frame appears once");
        check(windows[2].start_ns == windows[0].end_ns && windows[2].has_previous &&
              windows[2].previous.sequence == 1, "Half-open boundaries and context");
        check(!extract_features(windows[1]).mean_interval_ns, "Missing interval");
        Window dlc_window;
        dlc_window.has_previous = true;
        dlc_window.previous.dlc = 1;
        CanFrame next;
        next.timestamp_ns = 12;
        next.dlc = 2;
        next.payload[0] = 1;
        next.payload[1] = 255;
        dlc_window.frames.push_back(next);
        const auto features = extract_features(dlc_window);
        check(features.mean_payload_bit_changes == 1 && features.dlc_changes == 1, "DLC and shared payload bytes");

        const auto profile = build_profile(training.frames, duration);
        const auto predictions = detect_sequential(windows, profile);
        const auto labels = build_window_labels(windows, test.labels, test.frames.size());
        check(labels == WindowLabels{false, false, true, true, false, true, false}, "Ground truth");
        const auto score = evaluate(predictions, labels);
        check(score.tp == 1 && score.fp == 1 && score.tn == 3 && score.fn == 2 &&
              score.precision() == 0.5 && score.recall() == 1.0 / 3 && score.f1() == 0.4 &&
              score.false_positive_rate() == 0.25, "Hand-labeled confusion matrix and metrics");
        auto partial_labels = test.labels;
        partial_labels.erase(partial_labels.begin(), partial_labels.begin() + 2);
        const auto partial = build_window_labels(windows, partial_labels, test.frames.size());
        check(!partial[0].has_value() && evaluate(predictions, partial).unlabeled == 1, "Missing labels");

        for (const auto workers : {1, 2, 4}) {
            for (const auto partition : {Partition::window, Partition::can_id}) {
                check(detect_parallel(windows, profile, workers, 1, partition) == predictions,
                    "Pool parity");
            }
            const auto pipeline = detect_pipeline(path, duration, profile, workers, 1);
            check(pipeline.predictions == predictions && pipeline.labels == labels &&
                pipeline.quality.valid == test.quality.valid, "Pipeline parity");
        }
        bool cancelled = false;
        try {
            detect_pipeline((data / "missing.csv").string(), duration, profile, 2, 1);
        } catch (const std::runtime_error&) {
            cancelled = true;
        }
        check(cancelled, "Reader failure cancels pipeline");
        cancelled = false;
        try {
            detect_pipeline(path, std::numeric_limits<std::int64_t>::max(), profile, 2, 1);
        } catch (const std::overflow_error&) {
            cancelled = true;
        }
        check(cancelled, "Builder failure cancels pipeline");
        cancelled = false;
        try {
            detect_pipeline(path, duration, profile, 0, 1);
        } catch (const std::invalid_argument&) {
            cancelled = true;
        }
        check(cancelled, "Dispatcher failure cancels both pipeline channels");
        ThreadPool pool(2, 1);
        auto failed = pool.submit([] { throw std::runtime_error("task"); });
        auto completed = pool.submit([] { return 42; });
        pool.shutdown();
        check(completed.get() == 42, "Pool drains on shutdown");
        bool propagated = false;
        try { failed.get(); } catch (const std::runtime_error&) { propagated = true; }
        check(propagated, "Pool propagates task errors");
        bool rejected = false;
        try { pool.submit([] {}); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Pool rejects submissions after shutdown");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
