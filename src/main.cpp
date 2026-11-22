#include "benchmark.hpp"
#include "execution.hpp"
#include "window_builder.hpp"
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
namespace fs = std::filesystem;

constexpr auto usage = R"(Usage:
  canguard train TRAIN VALIDATION PROFILE WINDOW_MS
  canguard run PROFILE TEST RESULTS WINDOW_MS MODE [WORKERS QUEUE_CAPACITY]
  canguard run-auto TRAIN VALIDATION TEST RESULTS WINDOW_MS MODE [WORKERS QUEUE_CAPACITY]
  canguard bench TRAIN VALIDATION TEST RESULTS [TRIALS]

MODE: sequential, pool-window, pool-id, pipeline
)";

std::size_t positive(std::string_view text) {
    std::size_t value{};
    const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (status != std::errc{} || end != text.data() + text.size() || !value) {
        throw std::invalid_argument("Expected a positive integer: " + std::string(text));
    }
    return value;
}
std::int64_t nanoseconds(std::size_t ms) {
    if (ms > std::numeric_limits<std::int64_t>::max() / 1'000'000) {
        throw std::invalid_argument("Window duration out of range");
    }
    return static_cast<std::int64_t>(ms) * 1'000'000;
}
void require_distinct(const fs::path& a, const fs::path& b) {
    if (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b)) {
        throw std::invalid_argument("Expected separate files: " + a.string() + " and " + b.string());
    }
}
void show_quality(const std::string& path, const DataQuality& quality) {
    std::cout << path << '\n'
              << "Valid frames: " << quality.valid << '\n'
              << "Malformed rows: " << quality.malformed << '\n'
              << "Unique CAN IDs: " << quality.unique_ids << '\n'
              << "Timestamp regressions: " << quality.regressions << '\n';
}
struct Training {
    Dataset normal, validation;
};
Training read_training(const std::string& normal, const std::string& validation) {
    require_distinct(normal, validation);
    Training inputs{read_hcrl(normal), read_hcrl(validation)};
    show_quality(normal, inputs.normal.quality);
    show_quality(validation, inputs.validation.quality);
    for (const auto& label : inputs.normal.labels) {
        if (label.attack) throw std::invalid_argument("Training capture contains an attack label");
    }
    if (inputs.normal.frames.empty() || inputs.validation.frames.empty() ||
        inputs.validation.labels.size() != inputs.validation.frames.size()) {
        throw std::invalid_argument("Training needs normal frames and a fully labeled validation capture");
    }
    return inputs;
}
NormalProfile fit(const Training& dataset, std::int64_t duration) {
    const auto baseline = build_profile(dataset.normal.frames, duration);
    const auto windows = build_windows(dataset.validation.frames, duration);
    const auto labels = build_window_labels(windows, dataset.validation.labels, dataset.validation.frames.size());
    NormalProfile best;
    Evaluation best_score;
    for (const auto factor : {1.0, 1.05, 1.10, 1.25, 1.5, 2.0}) {
        auto candidate = widen_profile(baseline, factor);
        const auto score = evaluate(detect_sequential(windows, candidate), labels);
        if (best.empty() || score.f1() > best_score.f1() ||
            (score.f1() == best_score.f1() && score.fp < best_score.fp)) {
            best_score = score;
            best = std::move(candidate);
        }
    }
    return best;
}
struct Config {
    std::string_view mode, partition;
    std::size_t workers{}, capacity{};
};
std::vector<DetectionResult> detect(const std::vector<Window>& windows,
    const NormalProfile& profile, const Config& config) {
    if (config.mode == "sequential") return detect_sequential(windows, profile);
    return detect_parallel(windows, profile, config.workers, config.capacity,
        config.partition == "can-id" ? Partition::can_id : Partition::window);
}
struct Outcome {
    PipelineResult capture;
    Evaluation score;
};
Outcome run_capture(const std::string& path, std::int64_t duration,
    const NormalProfile& profile, const Config& config) {
    Outcome result;
    if (config.mode == "pipeline") {
        result.capture = detect_pipeline(path, duration, profile, config.workers, config.capacity);
    } else {
        auto dataset = read_hcrl(path);
        auto windows = build_windows(dataset.frames, duration);
        result.capture.predictions = detect(windows, profile, config);
        result.capture.labels = build_window_labels(windows, dataset.labels, dataset.frames.size());
        result.capture.quality = std::move(dataset.quality);
    }
    if (!result.capture.quality.valid) throw std::runtime_error("Test capture has no accepted frames");
    result.score = evaluate(result.capture.predictions, result.capture.labels);
    return result;
}
std::ofstream csv(const fs::path& path, std::string_view header = {}) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path);
    if (!header.empty()) out << header << '\n';
    return out;
}
void write_outputs(const fs::path& directory, const Outcome& outcome) {
    constexpr auto header = "task_id,can_id,start_ns,end_ns,suspicious,reasons";
    auto predictions = csv(directory / "predictions.csv", header);
    auto alerts = csv(directory / "alerts.csv", header);
    const auto write_prediction = [](auto& out, const DetectionResult& r) {
        out << r.task_id << ",0x" << std::hex << r.can_id << std::dec << ',' << r.start_ns
            << ',' << r.end_ns << ',' << r.suspicious << ',';
        for (std::size_t i = 0; i < r.reasons.size(); ++i) {
            if (i) out << '|';
            out << r.reasons[i];
        }
        out << '\n';
    };
    for (const auto& prediction : outcome.capture.predictions) {
        write_prediction(predictions, prediction);
        if (prediction.suspicious) write_prediction(alerts, prediction);
    }
    predictions.close();
    alerts.close();

    auto metrics = csv(directory / "metrics.csv",
        "tp,fp,tn,fn,unlabeled,precision,recall,f1,false_positive_rate");
    const auto& m = outcome.score;
    metrics << m.tp << ',' << m.fp << ',' << m.tn << ',' << m.fn << ',' << m.unlabeled << ',';
    if (m.tp || m.fp || m.tn || m.fn) {
        metrics << std::setprecision(12) << m.precision() << ',' << m.recall() << ','
                << m.f1() << ',' << m.false_positive_rate();
    } else {
        metrics << ",,,";
    }
    metrics << '\n';
    metrics.close();

    auto quality = csv(directory / "data_quality.csv", "line,reason");
    for (const auto& issue : outcome.capture.quality.issues) quality << issue.line << ',' << issue.reason << '\n';
    quality.close();
}
void protect_inputs(const fs::path& directory, const std::vector<fs::path>& inputs) {
    for (const auto& input : inputs) {
        for (const auto file : {"predictions.csv", "alerts.csv", "metrics.csv", "data_quality.csv", "benchmarks.csv"}) {
            require_distinct(input, directory / file);
        }
    }
}
void record(std::ostream& out, const Config& config, std::size_t ms, std::size_t frames,
    double seconds, std::string_view scope, std::size_t trial) {
    write_benchmark(out, {config.mode, config.partition, config.workers, ms, config.capacity,
        frames, seconds, scope, trial});
}
Outcome measure(std::ostream& out, const std::string& path, const NormalProfile& profile,
    const Config& config, std::size_t ms, std::size_t trial) {
    const auto start = BenchmarkClock::now();
    auto result = run_capture(path, nanoseconds(ms), profile, config);
    record(out, config, ms, result.capture.quality.valid, elapsed_seconds(start), "end-to-end", trial);
    return result;
}
void bench(const Training& training, const std::string& test, const fs::path& directory,
    std::size_t trials) {
    const auto dataset = read_hcrl(test);
    show_quality(test, dataset.quality);
    if (dataset.frames.empty()) throw std::runtime_error("Test capture has no accepted frames");
    auto out = csv(directory / "benchmarks.csv");
    write_benchmark_header(out);
    const Config sequential{"sequential", "time-window"};
    for (const std::size_t ms : {50, 100, 200}) {
        const auto duration = nanoseconds(ms);
        const auto profile = fit(training, duration);
        const auto windows = build_windows(dataset.frames, duration);
        const auto reference_labels = build_window_labels(windows, dataset.labels, dataset.frames.size());
        const auto reference = detect_sequential(windows, profile);
        if (ms == 100) {
            const auto score = evaluate(reference, reference_labels);
            write_outputs(directory, {{reference, reference_labels, dataset.quality}, score});
        }
        std::vector<Config> configs{sequential};
        for (const std::size_t workers : {1, 2, 4}) {
            for (const std::size_t capacity : {1, 16}) {
                configs.push_back({"pool", "time-window", workers, capacity});
                configs.push_back({"pool", "can-id", workers, capacity});
                configs.push_back({"pipeline", "time-window", workers, capacity});
            }
        }
        for (std::size_t trial = 0; trial < trials; ++trial) {
            for (const auto& config : configs) {
                if (config.mode != "pipeline") {
                    const auto start = BenchmarkClock::now();
                    const auto predictions = detect(windows, profile, config);
                    record(out, config, ms, dataset.frames.size(), elapsed_seconds(start), "compute-only", trial + 1);
                    if (predictions != reference) throw std::runtime_error("Compute predictions differ from sequential");
                }
                const auto result = measure(out, test, profile, config, ms, trial + 1);
                if (result.capture.predictions != reference || result.capture.labels != reference_labels) {
                    throw std::runtime_error("End-to-end predictions or labels differ from sequential");
                }
            }
        }
    }
    out.close();
}
}

int main(int argc, char** argv) {
    try {
        const std::string_view command = argc > 1 ? argv[1] : "";
        if (command == "train" && argc == 6) {
            require_distinct(argv[2], argv[4]);
            require_distinct(argv[3], argv[4]);
            const auto training = read_training(argv[2], argv[3]);
            const auto duration = nanoseconds(positive(argv[5]));
            const auto profile = fit(training, duration);
            save_profile(argv[4], profile, duration);
            std::cout << "Saved profile for " << profile.size() << " CAN IDs\n";
            return 0;
        }
        if (command == "run" || command == "run-auto") {
            const bool auto_train = command == "run-auto";
            const int test_arg = auto_train ? 4 : 3;
            const int mode_arg = test_arg + 3;
            const bool has_worker_args = argc == mode_arg + 3;
            if (argc != mode_arg + 1 && !has_worker_args) throw std::invalid_argument(usage);
            const std::string_view mode = argv[mode_arg];
            Config config{"sequential", "time-window"};
            if (mode != "sequential") {
                if (!has_worker_args || (mode != "pool-window" && mode != "pool-id" && mode != "pipeline")) {
                    throw std::invalid_argument(usage);
                }
                config = {mode == "pipeline" ? "pipeline" : "pool",
                    mode == "pool-id" ? "can-id" : "time-window",
                    positive(argv[mode_arg + 1]), positive(argv[mode_arg + 2])};
            } else if (has_worker_args) {
                throw std::invalid_argument(usage);
            }
            const auto ms = positive(argv[test_arg + 2]);
            const auto duration = nanoseconds(ms);
            const std::string test = argv[test_arg];
            const fs::path directory = argv[test_arg + 1];
            std::vector<fs::path> inputs{argv[2], test};
            if (auto_train) {
                require_distinct(argv[2], test);
                require_distinct(argv[3], test);
                inputs.emplace_back(argv[3]);
            }
            protect_inputs(directory, inputs);
            const auto profile = auto_train
                ? fit(read_training(argv[2], argv[3]), duration)
                : load_profile(argv[2], duration);
            fs::create_directories(directory);
            // Preserve existing output until the capture has been processed successfully.
            const auto start = BenchmarkClock::now();
            const auto outcome = run_capture(test, duration, profile, config);
            const auto seconds = elapsed_seconds(start);
            auto out = csv(directory / "benchmarks.csv");
            write_benchmark_header(out);
            record(out, config, ms, outcome.capture.quality.valid, seconds, "end-to-end", 1);
            out.close();
            show_quality(test, outcome.capture.quality);
            write_outputs(directory, outcome);
            return 0;
        }
        if (command == "bench" && (argc == 6 || argc == 7)) {
            require_distinct(argv[2], argv[4]);
            require_distinct(argv[3], argv[4]);
            const fs::path directory = argv[5];
            protect_inputs(directory, {argv[2], argv[3], argv[4]});
            const auto trials = argc == 7 ? positive(argv[6]) : 3;
            const auto training = read_training(argv[2], argv[3]);
            fs::create_directories(directory);
            bench(training, argv[4], directory, trials);
            return 0;
        }
        std::cout << usage;
        return command == "--help" || command == "-h" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
