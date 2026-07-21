#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>

namespace apollonian::core {

#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_destructive_interference_size;
#else
    constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

/**
 * @brief Thread-safe Metrics Snapshot for reporting and telemetry export.
 */
struct MetricsSnapshot {
    uint64_t ingested{0};
    uint64_t dropped{0};
    uint64_t batches_sent{0};
    uint64_t total_latency_ns{0};
};

/**
 * @brief High-Performance, Low-Overhead Lock-Free Metrics Collector.
 * 
 * Cache-line aligned to eliminate false sharing between concurrent worker threads.
 * Uses relaxed memory ordering for non-blocking counters in hot paths.
 */
class Metrics {
public:
    Metrics() noexcept = default;

    // Fast-path counter increments (Lock-free, Relaxed ordering).
    void record_ingested(uint64_t count = 1) noexcept { 
        m_ingested.fetch_add(count, std::memory_order_relaxed); 
    }

    void record_dropped(uint64_t count = 1) noexcept { 
        m_dropped.fetch_add(count, std::memory_order_relaxed); 
    }

    void record_batch_sent(uint64_t count = 1) noexcept { 
        m_batches_sent.fetch_add(count, std::memory_order_relaxed); 
    }

    void record_latency(std::chrono::nanoseconds latency) noexcept {
        m_total_latency_ns.fetch_add(static_cast<uint64_t>(latency.count()), std::memory_order_relaxed);
    }

    // Read counter values
    [[nodiscard]] uint64_t ingested() const noexcept { 
        return m_ingested.load(std::memory_order_relaxed); 
    }

    [[nodiscard]] uint64_t dropped() const noexcept { 
        return m_dropped.load(std::memory_order_relaxed); 
    }

    [[nodiscard]] uint64_t batches() const noexcept { 
        return m_batches_sent.load(std::memory_order_relaxed); 
    }

    [[nodiscard]] uint64_t total_latency_ns() const noexcept { 
        return m_total_latency_ns.load(std::memory_order_relaxed); 
    }

    /**
     * @brief Atomically creates a point-in-time snapshot of current metric states.
     */
    [[nodiscard]] MetricsSnapshot snapshot() const noexcept {
        return MetricsSnapshot{
            .ingested = m_ingested.load(std::memory_order_relaxed),
            .dropped = m_dropped.load(std::memory_order_relaxed),
            .batches_sent = m_batches_sent.load(std::memory_order_relaxed),
            .total_latency_ns = m_total_latency_ns.load(std::memory_order_relaxed)
        };
    }

    /**
     * @brief Atomically fetches current values and resets all internal counters to zero.
     * @return MetricsSnapshot containing counter values prior to reset.
     */
    MetricsSnapshot exchange_and_reset() noexcept {
        return MetricsSnapshot{
            .ingested = m_ingested.exchange(0, std::memory_order_relaxed),
            .dropped = m_dropped.exchange(0, std::memory_order_relaxed),
            .batches_sent = m_batches_sent.exchange(0, std::memory_order_relaxed),
            .total_latency_ns = m_total_latency_ns.exchange(0, std::memory_order_relaxed)
        };
    }

    /**
     * @brief Resets all atomic counters to zero.
     */
    void reset() noexcept {
        m_ingested.store(0, std::memory_order_relaxed);
        m_dropped.store(0, std::memory_order_relaxed);
        m_batches_sent.store(0, std::memory_order_relaxed);
        m_total_latency_ns.store(0, std::memory_order_relaxed);
    }

private:
    // Isolated atomic counters on independent cache lines to prevent cache thrashing
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> m_ingested{0};
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> m_dropped{0};
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> m_batches_sent{0};
    alignas(hardware_destructive_interference_size) std::atomic<uint64_t> m_total_latency_ns{0};
};

} // namespace apollonian::core
