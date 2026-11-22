# CANGuard

CANGuard is a C++20 intrusion detector and concurrency benchmark for recorded in-vehicle Controller Area Network (CAN) traffic. It learns normal behavior for each CAN identifier, flags anomalous time windows, and scores its decisions against labeled vehicle captures. The same detection rules run sequentially, in two thread-pool configurations, and in a bounded streaming pipeline.

## Detection and execution

The reader validates timestamps, CAN identifiers, and payload lengths. The first accepted frame anchors fixed-duration windows grouped by CAN ID. For each populated window, the detector checks message count, mean inter-arrival time, mean payload bit changes, and data length code changes against a normal profile. An ID absent from training also triggers an alert. Each alert records its reason.

Labels are used for validation and evaluation. A CAN-ID window is labeled as attacked if any of its frames carries an attack label. A window with missing labels and no confirmed attack is excluded from the confusion counts.

| Mode | Execution model |
| --- | --- |
| `sequential` | Processes windows one at a time and provides reference predictions. |
| `pool-id` | Schedules different CAN IDs concurrently and preserves window order within each ID. |
| `pool-window` | Schedules work concurrently within each time slice and completes it before starting the next slice. |
| `pipeline` | Streams frames through semaphore-bounded frame and window queues. A condition variable coordinates the alert manager's result queue. |

## Build and test

Use CMake 3.20 or later and a C++20 toolchain with thread and semaphore support. The following commands use MinGW on Windows.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For a Visual Studio generator, add `--config Release` to the build command and `-C Release` to CTest. The executable is then under `build/Release`.

## Run the included example

```powershell
./build/canguard.exe run-auto data/demo_train.csv data/demo_validation.csv data/demo_test.csv results/demo 100 sequential
./build/canguard.exe run-auto data/demo_train.csv data/demo_validation.csv data/demo_test.csv results/demo-pool 100 pool-id 4 16
```

The arguments after the output directory are the window duration in whole milliseconds, the mode, and, for parallel modes, the worker count and queue capacity. `run-auto` fits a profile for the selected duration before running detection. To reuse a saved profile, use `train` followed by `run` with the same duration. Run `./build/canguard.exe --help` for command syntax.

## Run the HCRL assessment

Obtain the HCRL Car-Hacking Dataset archive. The importer uses Python 3 and extracts the raw captures to `data/hcrl`. It prepares the first 200,000 Fuzzy frames for validation and the first 200,000 DoS frames for the performance benchmark. The full normal recording is used for training. The full DoS, RPM, and gear captures are held out for detection evaluation. [HCRL Car-Hacking Dataset and citation](https://ocslab.hksecurity.net/Datasets/car-hacking-dataset/)

```powershell
python scripts/import_hcrl.py "$env:USERPROFILE/Downloads/9) Car-Hacking Dataset.zip"

./build/canguard.exe run-auto data/hcrl/normal_run_data.txt data/hcrl/validation.csv data/hcrl/DoS_dataset.csv results/hcrl/dos 100 pool-id 4 16
./build/canguard.exe run-auto data/hcrl/normal_run_data.txt data/hcrl/validation.csv data/hcrl/RPM_dataset.csv results/hcrl/rpm 100 sequential
./build/canguard.exe run-auto data/hcrl/normal_run_data.txt data/hcrl/validation.csv data/hcrl/gear_dataset.csv results/hcrl/gear 100 sequential

./build/canguard.exe bench data/hcrl/normal_run_data.txt data/hcrl/validation.csv data/hcrl/benchmark-test.csv results/hcrl/benchmark 3
```

The benchmark compares 50, 100, and 200 ms windows, 1, 2, and 4 workers, and queue capacities of 1 and 16. It checks predictions and labels against the sequential reference. Compute-only measurements use prepared windows. End-to-end measurements cover capture parsing through evaluation and exclude profile fitting and CSV writing. The streaming pipeline is measured end-to-end.

### Reference results

Detection quality on the full held-out captures at 100 ms:

| Capture | Frames | Attack-window recall | False-positive rate |
| --- | ---: | ---: | ---: |
| DoS | 3,665,771 | 100.00% | 33.02% |
| RPM spoofing | 4,621,702 | 99.02% | 5.45% |
| Gear spoofing | 4,443,142 | 95.67% | 7.76% |

One benchmark run produced 288 measurements. At 100 ms, median end-to-end throughput across three trials was 895,186 messages/s sequentially, 582,628 for the CAN-ID pool, 518,284 for the time-window pool, and 353,136 for the pipeline. The parallel configurations used four workers and queue capacity 16. The CAN-ID pool was about 12% faster than the time-window pool in that comparison. The high DoS false-positive rate limits alert precision, and the throughput figures depend on the hardware and workload.

## Reports

| File | Contents |
| --- | --- |
| `predictions.csv` | All window decisions and their reasons |
| `alerts.csv` | Windows flagged as suspicious |
| `metrics.csv` | Confusion counts, precision, recall, F1, and false-positive rate |
| `benchmarks.csv` | Runtime and throughput by mode, configuration, and trial |
| `data_quality.csv` | Input row numbers and rejection reasons |

## Repository structure

| Path | Contents |
| --- | --- |
| `src/` and `include/` | C++ implementation and interfaces |
| `tests/` | Automated checks and parser fixtures |
| `data/` | Bundled demo and [HCRL dataset layout](data/hcrl/README.md) |
| `scripts/` | Dataset import and excerpt preparation |
| `build/` and `results/` | Local build output and generated reports |

The repository tracks source code, documentation, and small fixtures. Raw HCRL captures, build output, and generated reports are ignored.
