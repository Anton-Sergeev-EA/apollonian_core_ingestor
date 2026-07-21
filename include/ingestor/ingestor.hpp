#pragma once

#include "config.hpp"
#include "metrics.hpp"
#include "ring_buffer.hpp"
#include "serializer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace apollonian::core {

/**
 * @brief High-Performance Telemetry Ingestion Orchestrator.
 * 
 * Coordinates ingestion loops, batch serialization, thread lifecycles, and metric tracking.
 */
class Ingestor {
public:
    /**
     * @brief Zero-copy callback view receiving serialized telemetry bytes.
     */
    using BatchCallback = std::function<void(std::span<const uint8_t> batch_data)>;

    explicit Ingestor(const Config& config);
    ~Ingestor();

    // Prevent copying and moving to guarantee thread-safe address stability
    Ingestor(const Ingestor&) = delete;
    Ingestor& operator=(const Ingestor&) = delete;
    Ingestor(Ingestor&&) = delete;
    Ingestor& operator=(Ingestor&&) = delete;

    /**
     * @brief Starts consumer background worker processing threads.
     */
    void start();

    /**
     * @brief Gracefully halts ingestion workers and flushes remaining buffered metrics.
     */
    void stop() noexcept;

    /**
     * @brief Registers consumer callback function for processing completed batches.
     */
    void set_batch_callback(BatchCallback callback);

    /**
     * @brief Ingests a single telemetry sample into the pipeline (Producer side).
     * @return true if successfully queued into ring buffer; false if buffer overflow occurred.
     */
    bool ingest_sample(TelemetrySample&& sample) noexcept;
    bool ingest_sample(const TelemetrySample& sample) noexcept;

    /**
     * @brief Constructs and enqueues sample in-place without intermediate copies.
     */
    template <typename... Args>
    bool emplace_sample(Args&&... args) noexcept {
        if (m_ring_buffer.emplace(std::forward<Args>(args)...)) {
            m_metrics.record_ingested();
            return true;
        }
        m_metrics.record_dropped();
        return false;
    }

    /**
     * @brief Returns an atomic point-in-time snapshot of processing metrics.
     */
    [[nodiscard]] MetricsSnapshot get_metrics() const noexcept {
        return m_metrics.snapshot();
    }

    /**
     * @brief Checks whether the ingestor pipeline worker is currently running.
     */
    [[nodiscard]] bool is_running() const noexcept {
        return m_running.load(std::memory_order_relaxed);
    }

private:
    void consumer_loop();
    void process_batch(std::vector<TelemetrySample>& batch_scratchpad, std::vector<uint8_t>& serialization_buffer);

    Config m_config;
    RingBuffer<TelemetrySample> m_ring_buffer;
    Metrics m_metrics;
    BatchCallback m_callback;

    std::atomic<bool> m_running{false};
    std::jthread m_consumer_thread;
};

} // namespace apollonian::core
