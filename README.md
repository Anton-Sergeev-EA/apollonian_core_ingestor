# Apollonian Core Ingestor

[![CI](https://github.com/Anton-Sergeev-EA/apollonian_core_ingestor/actions/workflows/ci.yml/badge.svg)](https://github.com/Anton-Sergeev-EA/apollonian_core_ingestor/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

High-performance, low-latency telemetry ingestion core engine.

A lock-free SPSC (single-producer, single-consumer) pipeline for streaming
telemetry from industrial sensors / PLCs into a downstream sink (S3, Kafka, a
database, ...), built around a cache-aligned ring buffer and a zero-allocation
binary serializer.

## Architecture

```
[Industrial Sensors / PLC]
           |
           v
+------------------------+
|   RingBuffer (SPSC)     |  <-- lock-free, cache-aligned, zero-allocation
+-----------+--------------+
           |
           v
+------------------------+
|   Batch Serializer      |  <-- SSE4.2-accelerated CRC32 (with a portable
+-----------+--------------+      software fallback when unavailable)
           |
           v
+------------------------+
|   Zero-copy callback    |  <-- S3 / Kafka / database transport
+------------------------+
```

## Features

- **Lock-free SPSC ring buffer** — cache-line aligned to avoid false sharing,
  with local index caching to reduce cross-core cache-coherence traffic.
- **Zero-allocation hot path** — placement-new construction and
  `std::span`-based non-owning views mean no `malloc`/`free` once the pipeline
  is running.
- **Hardware-accelerated CRC32** — uses the SSE4.2 `crc32` instruction when the
  running CPU actually supports it (checked once at runtime via `cpuid` /
  `__builtin_cpu_supports`), and transparently falls back to a portable
  bitwise implementation everywhere else. No `-march=native` required, and no
  risk of `SIGILL` on hardware without SSE4.2.
- **C++20 native concurrency** — a `std::jthread` background worker with
  cooperative, signal-driven shutdown.
- **Lock-free metrics** — atomic, cache-line-isolated counters with
  point-in-time snapshotting.

## Quick Start

### Prerequisites

- A C++20 compiler (GCC 12+, Clang 15+, or MSVC 2022+ — `std::jthread`
  requires a reasonably recent standard library)
- CMake 3.20+
- [`nlohmann_json`](https://github.com/nlohmann/json) and
  [GoogleTest](https://github.com/google/googletest) — used if already
  installed on the system (e.g. via `apt install nlohmann-json3-dev
  libgtest-dev`), otherwise fetched automatically via CMake `FetchContent`.

### Building from source

```bash
git clone https://github.com/Anton-Sergeev-EA/apollonian_core_ingestor.git
cd apollonian_core_ingestor

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel

# Run the ingestion engine
./apollonian_ingestor ../config.json

# Run the unit test suite
ctest --output-on-failure
```

Useful CMake options:

| Option                        | Default | Description                                   |
|--------------------------------|---------|------------------------------------------------|
| `BUILD_TESTS`                  | `ON`    | Build the GoogleTest suite                     |
| `ENABLE_NATIVE_OPTIMIZATION`   | `OFF`   | Build with `-march=native -O3` (not portable — only for benchmarking on the build machine) |
| `ENABLE_SANITIZERS`            | `OFF`   | Build with ASan + UBSan                        |

## Project Structure

```
apollonian_core_ingestor/
├── include/
│   └── ingestor/
│       ├── cache_line.hpp   # Shared cache-line-size constant
│       ├── ring_buffer.hpp  # Lock-free SPSC ring buffer
│       ├── serializer.hpp   # CRC32 zero-allocation binary serializer
│       ├── metrics.hpp      # Lock-free metrics collector
│       ├── config.hpp       # Configuration model + validation
│       └── ingestor.hpp     # Ingestion orchestrator
├── src/
│   ├── main.cpp             # Entry point & signal handling
│   ├── ingestor.cpp         # Pipeline loop implementation
│   └── config.cpp           # JSON config parsing
├── tests/
│   ├── test_ring_buffer.cpp
│   └── test_serializer.cpp
├── .github/workflows/ci.yml # Build + test + sanitizer + format CI
├── config.json               # Example configuration
├── CMakeLists.txt
└── README.md
```

## Configuration

```json
{
    "ring_buffer_capacity": 1048576,
    "batch_size": 1000,
    "flush_interval_ms": 100,
    "output_endpoint": "s3://apollonian-bucket/telemetry/",
    "enable_metrics": true
}
```

- `ring_buffer_capacity` must be a power of two (e.g. `65536`, `1048576`) —
  required for bitmask-based indexing.
- `batch_size` must be greater than 0 and no larger than
  `ring_buffer_capacity`.

Configuration is validated on load; an invalid file is rejected with a
descriptive error rather than silently falling back to defaults.

## License

Distributed under the MIT License — see [LICENSE](LICENSE) for details.

---

Anton Sergeev — avsergeev1981@gmail.com
