#include "ingestor/ingestor.hpp"

#include <chrono>
#include <iostream>
#include <span>
#include <thread>
#include <utility>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h> // For _mm_pause()
#endif

namespace apollonian::core {

namespace {

// Spin-wait helper to minimize CPU latency without burning power aggressively.
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

}

Ingestor::Ingestor(const Config& config)
    : m_config(config) {
    if (auto err = m_config.validate()) {
        std::cerr << "[Ingestor] Warning: Invalid config parameters: " << *err << std::endl;
    }
}

Ingestor::~Ingestor() {
    stop();
}

void Ingestor::start() {
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return; // Already running.
    }

    // Launch single consumer background jthread (C++20 auto-join thread).
    m_consumer_thread = std::jthread([this]() { consumer_loop(); });

    std::cout << "[Ingestor] Engine started gracefully."
              << " Buffer Capacity: " << m_config.ring_buffer_capacity
              << " | Batch Size: " << m_config.batch_size
              << " | Flush Interval: " << m_config.flush_interval.count() << "ms"
              << std::endl;
}

void Ingestor::stop() noexcept {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return; // Already stopped.
    }

    if (m_consumer_thread.joinable()) {
        m_consumer_thread.join();
    }

    const auto stats = m_metrics.snapshot();
    std::cout << "[Ingestor] Stopped."
              << " Ingested: " << stats.ingested
              << " | Dropped: " << stats.dropped
              << " | Batches: " << stats.batches_sent
              << std::endl;
}

void Ingestor::set_batch_callback(BatchCallback callback) {
    m_callback = std::move(callback);
}

bool Ingestor::ingest_sample(const TelemetrySample& sample) noexcept {
    if (m_ring_buffer.push(sample)) {
        m_metrics.record_ingested();
        return true;
    }
    m_metrics.record_dropped();
    return false;
}

bool Ingestor::ingest_sample(TelemetrySample&& sample) noexcept {
    if (m_ring_buffer.push(std::move(sample))) {
        m_metrics.record_ingested();
        return true;
    }
    m_metrics.record_dropped();
    return false;
}

void Ingestor::process_batch(std::vector<TelemetrySample>& batch_scratchpad, 
                             std::vector<uint8_t>& serialization_buffer) {
    if (batch_scratchpad.empty()) {
        return;
    }

    const std::size_t required_bytes = Serializer::required_buffer_size(batch_scratchpad.size());
    if (serialization_buffer.size() < required_bytes) {
        serialization_buffer.resize(required_bytes);
    }

    const std::size_t bytes_written = Serializer::serialize(batch_scratchpad, serialization_buffer);

    if (bytes_written > 0 && m_callback) {
        // Pass non-owning view std::span to eliminate copy overhead.
        m_callback(std::span<const uint8_t>(serialization_buffer.data(), bytes_written));
        m_metrics.record_batch_sent();
    }

    batch_scratchpad.clear();
}

void Ingestor::consumer_loop() {
    // Pre-allocate scratchpad vectors outside hot path loop to achieve zero dynamic allocations.
    std::vector<TelemetrySample> batch_scratchpad;
    batch_scratchpad.reserve(m_config.batch_size);

    std::vector<uint8_t> serialization_buffer;
    serialization_buffer.resize(Serializer::required_buffer_size(m_config.batch_size));

    auto last_flush_time = std::chrono::steady_clock::now();
    uint32_t idle_spin_count = 0;

    while (m_running.load(std::memory_order_relaxed) || !m_ring_buffer.empty()) {
        TelemetrySample sample;
        
        // Drain elements from ring buffer into batch scratchpad.
        while (batch_scratchpad.size() < m_config.batch_size && m_ring_buffer.pop(sample)) {
            batch_scratchpad.push_back(sample);
        }

        const auto now = std::chrono::steady_clock::now();
        const bool timeout_reached = (now - last_flush_time) >= m_config.flush_interval;

        // Flush condition: full batch OR timer threshold reached.
        if (!batch_scratchpad.empty() && (batch_scratchpad.size() >= m_config.batch_size || timeout_reached)) {
            process_batch(batch_scratchpad, serialization_buffer);
            last_flush_time = now;
            idle_spin_count = 0;
            continue;
        }

        // Low-latency backoff mechanism when no items were processed.
        if (batch_scratchpad.empty()) {
            if (++idle_spin_count < 1000) {
                cpu_relax(); // Low-latency CPU pause instruction.
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100)); // Yield after prolonged idle
            }
        }
    }

    // Drain remaining buffered samples prior to shutdown.
    if (!batch_scratchpad.empty()) {
        process_batch(batch_scratchpad, serialization_buffer);
    }
}

}
