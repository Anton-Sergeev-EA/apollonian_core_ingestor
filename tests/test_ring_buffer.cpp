#include <gtest/gtest.h>
#include "ingestor/ring_buffer.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace apollonian::core;

namespace {

// Helper move-only struct for testing zero-copy guarantees.
struct MoveOnlyType {
    int id;
    explicit MoveOnlyType(int value) : id(value) {}
    
    MoveOnlyType(const MoveOnlyType&) = delete;
    MoveOnlyType& operator=(const MoveOnlyType&) = delete;
    
    MoveOnlyType(MoveOnlyType&&) noexcept = default;
    MoveOnlyType& operator=(MoveOnlyType&&) noexcept = default;
};

} // namespace.

TEST(RingBufferTest, BasicPushPop) {
    RingBuffer<int, 4> rb;

    EXPECT_EQ(rb.capacity(), 4);
    EXPECT_TRUE(rb.empty());

    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_TRUE(rb.push(3));
    
    // Capacity logic check (3 items inserted out of 4 capacity slots available).
    EXPECT_FALSE(rb.empty());

    int val = 0;
    EXPECT_TRUE(rb.pop(val)); 
    EXPECT_EQ(val, 1);
    
    EXPECT_TRUE(rb.pop(val)); 
    EXPECT_EQ(val, 2);
    
    EXPECT_TRUE(rb.pop(val)); 
    EXPECT_EQ(val, 3);
    
    EXPECT_FALSE(rb.pop(val)); // Buffer is now empty.
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, MoveOnlySemanticsAndEmplace) {
    RingBuffer<MoveOnlyType, 8> rb;

    // Test emplace-construction in-place.
    EXPECT_TRUE(rb.emplace(100));
    EXPECT_TRUE(rb.emplace(200));

    // Test pushing rvalue reference.
    EXPECT_TRUE(rb.push(MoveOnlyType(300)));

    MoveOnlyType item(0);
    EXPECT_TRUE(rb.pop(item));
    EXPECT_EQ(item.id, 100);

    EXPECT_TRUE(rb.pop(item));
    EXPECT_EQ(item.id, 200);

    EXPECT_TRUE(rb.pop(item));
    EXPECT_EQ(item.id, 300);
}

TEST(RingBufferTest, WraparoundBehavior) {
    RingBuffer<int, 4> rb;

    for (int cycle = 0; cycle < 10; ++cycle) {
        EXPECT_TRUE(rb.push(cycle));
        int val = -1;
        EXPECT_TRUE(rb.pop(val));
        EXPECT_EQ(val, cycle);
    }
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, ConcurrentSPSCStressTest) {
    constexpr std::size_t kIterations = 1'000'000;
    RingBuffer<std::size_t, 1024> rb;

    std::atomic<bool> start_flag{false};

    // Producer Thread.
    std::thread producer([&]() {
        while (!start_flag.load(std::memory_order_relaxed)) {}

        for (std::size_t i = 0; i < kIterations; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield(); // Backoff on full buffer.
            }
        }
    });

    // Consumer Thread.
    std::vector<std::size_t> consumed_items;
    consumed_items.reserve(kIterations);

    std::thread consumer([&]() {
        while (!start_flag.load(std::memory_order_relaxed)) {}

        std::size_t value = 0;
        for (std::size_t i = 0; i < kIterations; ++i) {
            while (!rb.pop(value)) {
                std::this_thread::yield(); // Backoff on empty buffer.
            }
            consumed_items.push_back(value);
        }
    });

    // Release threads simultaneously.
    start_flag.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    // Verify ordering and completeness.
    ASSERT_EQ(consumed_items.size(), kIterations);
    for (std::size_t i = 0; i < kIterations; ++i) {
        EXPECT_EQ(consumed_items[i], i);
    }
}
