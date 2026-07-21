#pragma once

#include <atomic>
#include <cstddef>
#include <concepts>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace apollonian::core {

// Hardware cache line size for avoiding false sharing.
#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_destructive_interference_size;
#else
    // Default cache line size for modern x86-64 / ARM64 processors.
    constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

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
        // Clear all remaining unconsumed elements to prevent resource leaks.
        T dummy;
        while (pop(dummy)) {}

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
                return false; // Buffer is full
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
    bool push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace(std::move(item));
    }

    bool push(const T& item) {
        return emplace(item);
    }

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
                return false; // Buffer is empty.
            }
        }

        auto* ptr = reinterpret_cast<T*>(std::addressof(m_buffer[current_head & kMask].storage));
        value = std::move(*ptr);
        ptr->~T(); // Explicitly destroy the element.

        // Release order guarantees producer sees free slot after head update.
        m_head.store(current_head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Estimates current number of items in the ring buffer.
     * @note Lock-free state snapshot; exact value may fluctuate concurrently.
     */
    [[nodiscard]] size_type size() const noexcept { loop_again:
        const size_type head = m_head.load(std::memory_order_relaxed);
        const size_type tail = m_tail.load(std::memory_order_relaxed);
        
        if (tail >= head) {
            return tail - head;
        }
        // Handle wrap-around edge case safely if state shifted during load.
        return (Capacity - (head - tail));
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_head.load(std::memory_order_relaxed) == m_tail.load(std::memory_order_relaxed);
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept {
        return Capacity;
    }

private:
    static constexpr size_type kMask = Capacity - 1;

    // Properly aligned uninitialized storage wrapper.
    struct alignas(alignof(T)) Storage {
        std::byte storage[sizeof(T)];
    };

    // Pointer to heap-allocated raw memory buffer.
    Storage* const m_buffer;

    // PRODUCER STATE (Written by Producer).
    alignas(hardware_destructive_interference_size) std::atomic<size_type> m_tail;
    size_type m_head_cached{0}; // Read-only copy of head maintained by Producer

    // CONSUMER STATE (Written by Consumer).
    alignas(hardware_destructive_interference_size) std::atomic<size_type> m_head;
    size_type m_tail_cached{0}; // Read-only copy of tail maintained by Consumer.

    // Padding to ensure no trailing variables leak into the last cache line.
    alignas(hardware_destructive_interference_size) char m_padding[1];
};

} // namespace apollonian::core
