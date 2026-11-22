#include "benchmark.hpp"
#include <iomanip>
#include <ostream>

double elapsed_seconds(BenchmarkClock::time_point start) {
    return std::chrono::duration<double>(BenchmarkClock::now() - start).count();
}
void write_benchmark_header(std::ostream& out) {
    out << "mode,partition,workers,window_ms,queue_capacity,frames,runtime_s,messages_per_s,scope,trial\n";
}
void write_benchmark(std::ostream& out, const BenchmarkRow& r) {
    out << r.mode << ',' << r.partition << ',' << r.workers << ',' << r.window_ms << ','
        << r.queue_capacity << ',' << r.frames << ',' << std::setprecision(12) << r.runtime_s
        << ',' << (r.runtime_s > 0 ? r.frames / r.runtime_s : 0) << ',' << r.scope << ',' << r.trial << '\n';
}
