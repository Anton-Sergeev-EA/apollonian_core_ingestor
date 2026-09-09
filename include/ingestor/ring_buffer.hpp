#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "ingestor/cache_line.hpp"

namespace apollonian::core {

/**
 * @brief High-Performance, Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 *
 * Optimized for ultra-low latency scenarios using cache-line alignment to prevent false sharing
 * and local index caching to minimize cross-core bus traffic.
 *
 * @tparam T Element type stored in the buffer.
 * @tparam Capacity Buffer size (MUST be a power of two).
 */
template <typename T, std::size_t Capacity = 1024 * 1024>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

  public:
    using value_type = T;
    using size_type = std::size_t;

    RingBuffer() : m_buffer(static_cast<Storage*>(::operator new[](sizeof(Storage) * Capacity))) {
        // Initialize atomic counters.
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
        m_head_cached = 0;
        m_tail_cached = 0;
    }

    ~RingBuffer() {
        // Destroy all remaining unconsumed elements in place. This is intentionally
        // NOT implemented as "T dummy; while (pop(dummy)) {}" - that would require T
        // to be default-constructible, which contradicts this container's own
        // stated support for move-only types (see MoveOnlySemanticsAndEmplace in
        // tests/test_ring_buffer.cpp, which has no default constructor at all).
        // By the time the destructor runs there is no concurrent producer/consumer
        // access, so plain relaxed loads of the indices are safe here.
        if constexpr (!std::is_trivially_destructible_v<T>) {
            size_type head = m_head.load(std::memory_order_relaxed);
            const size_type tail = m_tail.load(std::memory_order_relaxed);
            while (head != tail) {
                std::destroy_at(std::launder(reinterpret_cast<T*>(std::addressof(m_buffer[head & kMask].storage))));
                ++head;
            }
        }

        // Free dynamically allocated raw memory block.
        ::operator delete[](m_buffer);
    }

    // Disable copy and assignment to preserve SPSC semantics and internal state integrity.
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * @brief Constructs an element in-place at the end of the buffer (Producer thread only).
     * @tparam Args Argument types for T constructor.
     * @param args Arguments forwarded to construct T.
     * @return true if successful, false if the buffer is full.
     */
    template <typename... Args>
    bool emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const size_type current_tail = m_tail.load(std::memory_order_relaxed);

        // Optimization: Check cached head first to avoid costly cross-core atomic acquire load.
        if ((current_tail - m_head_cached) >= Capacity) {
            m_head_cached = m_head.load(std::memory_order_acquire);
            if ((current_tail - m_head_cached) >= Capacity) {
                return false;  // Buffer is full
            }
        }

        // Construct object directly in allocated uninitialized memory block.
        new (std::addressof(m_buffer[current_tail & kMask].storage)) T(std::forward<Args>(args)...);

        // Release order guarantees consumer sees initialized memory after tail update.
        m_tail.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pushes item into the buffer via move/copy (Producer thread only).
     */
    bool push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) { return emplace(std::move(item)); }

    bool push(const T& item) { return emplace(item); }

    /**
     * @brief Pops an element from the buffer (Consumer thread only).
     * @param value Output parameter where extracted element is moved.
     * @return true if an element was extracted, false if buffer is empty.
     */
    bool pop(T& value) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const size_type current_head = m_head.load(std::memory_order_relaxed);

        // Optimization: Check cached tail first to prevent acquire loads on every pop call.
        if (current_head == m_tail_cached) {
            m_tail_cached = m_tail.load(std::memory_order_acquire);
            if (current_head == m_tail_cached) {
                return false;  // Buffer is empty.
            }
        }

        auto* ptr = std::launder(reinterpret_cast<T*>(std::addressof(m_buffer[current_head & kMask].storage)));
        value = std::move(*ptr);
        ptr->~T();  // Explicitly destroy the element.

        // Release order guarantees producer sees free slot after head update.
        m_head.store(current_head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Estimates current number of items in the ring buffer.
     * @note Lock-free state snapshot; exact value may fluctuate concurrently.
     */
    [[nodiscard]] size_type size() const noexcept {
        // Indices only ever increase (they wrap via unsigned overflow, never via a
        // modulo reset), so tail is always >= head from a single consistent
        // snapshot; no separate "wrap-around" branch is needed or correct here.
        const size_type head = m_head.load(std::memory_order_relaxed);
        const size_type tail = m_tail.load(std::memory_order_relaxed);
        return tail - head;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_head.load(std::memory_order_relaxed) == m_tail.load(std::memory_order_relaxed);
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept { return Capacity; }

  private:
    static constexpr size_type kMask = Capacity - 1;

    // Properly aligned uninitialized storage wrapper.
    struct alignas(alignof(T)) Storage {
        std::byte storage[sizeof(T)];
    };

    // Pointer to heap-allocated raw memory buffer.
    Storage* const m_buffer;

    // PRODUCER STATE (Written by Producer).
    alignas(kCacheLineSize) std::atomic<size_type> m_tail;
    size_type m_head_cached{0};  // Read-only copy of head maintained by Producer

    // CONSUMER STATE (Written by Consumer).
    alignas(kCacheLineSize) std::atomic<size_type> m_head;
    size_type m_tail_cached{0};  // Read-only copy of tail maintained by Consumer.

    // Padding to ensure no trailing variables leak into the last cache line.
    alignas(kCacheLineSize) char m_padding[1];
};

}  // namespace apollonian::core
