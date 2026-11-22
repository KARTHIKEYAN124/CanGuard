#pragma once
#include <chrono>
#include <cstddef>
#include <iosfwd>
#include <string_view>

using BenchmarkClock = std::chrono::steady_clock;
double elapsed_seconds(BenchmarkClock::time_point start);
struct BenchmarkRow {
    std::string_view mode, partition;
    std::size_t workers{}, window_ms{}, queue_capacity{}, frames{};
    double runtime_s{};
    std::string_view scope;
    std::size_t trial{};
};
void write_benchmark_header(std::ostream& out);
void write_benchmark(std::ostream& out, const BenchmarkRow& row);
