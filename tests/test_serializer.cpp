#include <gtest/gtest.h>
#include "ingestor/serializer.hpp"

#include <array>
#include <cstring>
#include <span>
#include <vector>

using namespace apollonian::core;

TEST(SerializerTest, ZeroAllocationSerializeAndDeserialize) {
    std::vector<TelemetrySample> samples;
    for (uint32_t i = 0; i < 5; ++i) {
        samples.push_back(TelemetrySample{
            .timestamp_ms = 1000U + i,
            .value = static_cast<double>(i) * 2.5,
            .tag_id = i + 1,
            .quality = static_cast<uint8_t>(i % 2)
        });
    }

    // Allocate stack buffer for zero-allocation API.
    std::array<uint8_t, 1024> stack_buffer{};
    
    const std::size_t written_bytes = Serializer::serialize(samples, stack_buffer);
    const std::size_t expected_size = Serializer::required_buffer_size(samples.size());

    EXPECT_EQ(written_bytes, expected_size);

    // Perform Zero-Copy deserialization with automatic CRC validation.
    auto view_opt = Serializer::deserialize_zero_copy(std::span<const uint8_t>(stack_buffer.data(), written_bytes));
    
    ASSERT_TRUE(view_opt.has_value());
    auto deserialized_samples = *view_opt;

    ASSERT_EQ(deserialized_samples.size(), samples.size());

    for (std::size_t i = 0; i < samples.size(); ++i) {
        EXPECT_EQ(deserialized_samples[i].timestamp_ms, samples[i].timestamp_ms);
        EXPECT_EQ(deserialized_samples[i].tag_id, samples[i].tag_id);
        EXPECT_DOUBLE_EQ(deserialized_samples[i].value, samples[i].value);
        EXPECT_EQ(deserialized_samples[i].quality, samples[i].quality);
    }
}

TEST(SerializerTest, CRC32CorruptionDetection) {
    std::vector<TelemetrySample> samples{
        TelemetrySample{.timestamp_ms = 123456, .value = 42.0, .tag_id = 1, .quality = 0}
    };

    auto buffer = Serializer::serialize(samples);

    // Ensure valid packet passes checksum check.
    auto valid_view = Serializer::deserialize_zero_copy(buffer);
    ASSERT_TRUE(valid_view.has_value());

    // Corrupt one byte inside the payload.
    buffer[sizeof(BatchHeader) + 2] ^= 0xFF;

    // Deserializer must fail CRC validation check and return std::nullopt.
    auto corrupted_view = Serializer::deserialize_zero_copy(buffer);
    EXPECT_FALSE(corrupted_view.has_value());
}

TEST(SerializerTest, BufferTooSmallHandling) {
    std::vector<TelemetrySample> samples{
        TelemetrySample{.timestamp_ms = 100, .value = 1.0, .tag_id = 1, .quality = 0}
    };

    // Buffer smaller than required size.
    std::array<uint8_t, sizeof(BatchHeader)> small_buffer{};
    const std::size_t written_bytes = Serializer::serialize(samples, small_buffer);

    // Expect 0 bytes written (Overflow protection safeguard).
    EXPECT_EQ(written_bytes, 0);
}
