#include "ingestor/config.hpp"
#include "ingestor/ingestor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <span>
#include <thread>

namespace {

// Lock-free flag and condition variable for instant signal delivery.
std::atomic<bool> g_shutdown_requested{false};
std::mutex g_signal_mutex;
std::condition_variable g_signal_cv;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_shutdown_requested.store(true, std::memory_order_relaxed);
        g_signal_cv.notify_all();
    }
}

/**
 * @brief Simulates real-time telemetry source (e.g., Industrial PLC / Sensors).
 */
void telemetry_producer_worker(apollonian::core::Ingestor& ingestor) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> value_dist(10.0, 90.0);
    uint32_t tag_id = 1;

    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        apollonian::core::TelemetrySample sample{
            .timestamp_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            ),
            .value = value_dist(rng),
            .tag_id = tag_id++,
            .quality = 0 // Good status.
        };

        if (tag_id > 500) {
            tag_id = 1;
        }

        // Push to ingestor ring buffer without allocations.
        ingestor.ingest_sample(sample);

        // High-frequency telemetry sleep simulation (e.g., ~1000 samples/sec).
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
}

} // namespace.

int main(int argc, char** argv) {
    // Install signal handlers for POSIX graceful shutdown.
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << R"(

)" << std::endl;

    // Load configuration.
    const std::filesystem::path config_path = (argc > 1) ? argv[1] : "config.json";
    
    auto config_opt = apollonian::core::Config::from_file(config_path);
    apollonian::core::Config config;

    if (config_opt) {
        config = *config_opt;
        std::cout << "Loaded configuration from file: " << config_path << std::endl;
    } else {
        std::cout << "Failed to load config from " << config_path 
                  << "Using default production configuration." << std::endl;
    }

    std::cout << "Buffer capacity: " << config.ring_buffer_capacity << " elements\n"
              << "Batch size: " << config.batch_size << " samples\n"
              << "Flush interval: " << config.flush_interval.count() << " ms\n"
              << "Output endpoint: " << config.output_endpoint << "\n\n";

    // Initialize core orchestrator.
    apollonian::core::Ingestor ingestor(config);

    // Set callback receiving zero-copy byte buffers.
    ingestor.set_batch_callback([](std::span<const uint8_t> batch_data) {
        static std::atomic<uint64_t> batch_counter{0};
        const uint64_t count = ++batch_counter;

        // Thread-safe console log.
        std::cout << "[Batch #" << std::setw(6) << count << "] "
                  << std::setw(8) << batch_data.size() << " bytes serialized" << std::endl;
    });

    // Start background processing pipeline.
    ingestor.start();

    // Launch worker thread feeding telemetry samples.
    std::thread producer_thread(telemetry_producer_worker, std::ref(ingestor));

    std::cout << "Ingestor engine running. Press Ctrl+C to terminate." << std::endl << std::endl;

    // Monitoring loop: Wait on condition variable with 5-second interval
    std::unique_lock<std::mutex> lock(g_signal_mutex);
    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        if (g_signal_cv.wait_for(lock, std::chrono::seconds(5), [] { 
            return g_shutdown_requested.load(std::memory_order_relaxed); 
        })) {
            break; // Immediately exit loop on shutdown signal.
        }

        const auto stats = ingestor.get_metrics();
        std::cout << "[Metrics] Ingested: " << stats.ingested
                  << "Dropped: " << stats.dropped
                  << "Batches Sent: " << stats.batches_sent
                  << std::endl;
    }

    std::cout << "\nShutdown signal received. Stopping services." << std::endl;

    // Join producer worker.
    if (producer_thread.joinable()) {
        producer_thread.join();
    }

    // Gracefully stop core engine.
    ingestor.stop();

    std::cout << "Ingestor terminated gracefully." << std::endl;
    return 0;
}
