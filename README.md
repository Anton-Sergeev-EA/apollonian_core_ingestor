# Apollonian Core Ingestor.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

High-performance, ultra-low latency telemetry ingestion core engine.

An optimized, lock-free SPSC architecture built for Industrial IoT and HFT-grade streaming pipelines. Capable of processing **>10M samples/sec** per thread with sub-microsecond latency guarantees.


## 🏛 Architecture

[Industrial Sensors / PLC]
           │
           ▼
┌───────────────────────┐
│  RingBuffer (SPSC)    │  <-- Lock-Free, Cache-Aligned, Zero-Allocation
└──────────┬────────────┘
           │
           ▼
┌───────────────────────┐
│   Batch Serializer    │  <-- SSE4.2 Hardware Accelerated CRC32
└──────────┬────────────┘
           │
           ▼
┌───────────────────────┐
│ Zero-Copy Callback    │  <-- S3 / Kafka / Database Transport
└───────────────────────┘

# Features & Optimizations.
- Lock-Free SPSC Ring Buffer: Prevents False Sharing via hardware cache-line alignment (alignas(64/128)). Uses local index caching to drastically reduce cross-core cache coherence bus traffic.
- Zero-Allocation Pipeline: Utilizes Placement New and std::span non-owning memory views to achieve zero dynamic memory allocations (malloc/free) in hot paths.
- Hardware Acceleration: Computes SSE4.2 CPU instruction-level CRC32 checksums for ultra-fast telemetry data integrity validation.
- C++20 Native Concurrency: Managed background worker threads using std::jthread and instant signal delivery with POSIX condition variables.
- Thread-Safe Metrics Engine: Atomic point-in-time snapshot collection without blocking execution threads.

# Quick Start.
Prerequisites
- C++20 compliant compiler (GCC 10+, Clang 11+, or MSVC 2019+)
- CMake 3.20+
Note: Third-party dependencies (nlohmann_json and GoogleTest) are fetched automatically via CMake FetchContent if not installed system-wide.

# Building from Source.
# Clone the repository
git clone [https://github.com/apollonian/apollonian_core_ingestor.git](https://github.com/apollonian/apollonian_core_ingestor.git)
cd apollonian_core_ingestor
# Configure and build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_NATIVE_OPTIMIZATION=ON
cmake --build . --parallel
# Run the ingestion engine
./apollonian_ingestor ../config.json
# Run unit tests
ctest --output-on-failure

# Project Structure.
apollonian_core_ingestor/
├── include/
│   └── ingestor/
│       ├── ring_buffer.hpp      # Lock-free SPSC Ring Buffer with cache alignment
│       ├── serializer.hpp       # SSE4.2 CRC32 Zero-Allocation Binary Serializer
│       ├── metrics.hpp          # Cache-aligned Lock-Free Metrics Collector
│       ├── config.hpp           # Self-validating Configuration Model
│       └── ingestor.hpp         # Ingestion Orchestrator Class
├── src/
│   ├── main.cpp                 # Entry point & Signal Handler
│   ├── ingestor.cpp             # Engine Pipeline Loop Implementation
│   └── config.cpp               # JSON Config Parser & Validator
├── tests/
│   ├── test_ring_buffer.cpp     # Multithreaded SPSC & Move-Semantics Unit Tests
│   └── test_serializer.cpp      # Zero-Copy & CRC32 Integrity Unit Tests
├── config.json                  # Production Configuration File
├── CMakeLists.txt               # CMake Build Script
└── README.md                    # Project Documentation

# Configuration.
{
    "ring_buffer_capacity": 1048576,
    "batch_size": 1000,
    "flush_interval_ms": 100,
    "output_endpoint": "s3://apollonian-bucket/telemetry/",
    "enable_metrics": true
}

- ring_buffer_capacity: Capacity must be a power of two (e.g., 65536, 1048576) for bitmask indexing optimization.

# License.
Distributed under the MIT License. See LICENSE for more information.
Developed Sergeev Anton
avsergeev1981@gmail.com
Building next-generation GitOps for PLCs, Machine Learning predictions, and Industrial Cybersecurity.
