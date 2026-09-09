#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <nmmintrin.h>  // Hardware SSE4.2 CRC32 instructions.
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace apollonian::core {

/**
 * @brief Memory-aligned Telemetry Sample payload.
 * Ordered by field alignment sizes to prevent compiler padding bytes (8 -> 8 -> 4 -> 1 -> +3 padding).
 */
struct alignas(8) TelemetrySample {
    uint64_t timestamp_ms;
    double value;
    uint32_t tag_id;
    uint8_t quality;
    uint8_t reserved[3]{0};  // Explicit padding for 8-byte boundary alignment
};

/**
 * @brief Header for binary telemetry batches.
 */
struct alignas(4) BatchHeader {
    uint32_t magic = 0x41504C43;  // "APLC"
    uint32_t count{0};
    uint32_t payload_crc32{0};
};

/**
 * @brief Zero-allocation, High-Performance Binary Serializer for Telemetry Data.
 */
class Serializer {
  public:
    static constexpr uint32_t kMagicNumber = 0x41504C43;

    /**
     * @brief Serializes telemetry samples into a pre-allocated output buffer (Zero-Allocation).
     *
     * @param samples Input span of telemetry samples.
     * @param out_buffer Destination byte buffer (must have capacity >= required_size(samples.size())).
     * @return std::size_t Total number of bytes written to buffer, or 0 if buffer capacity is insufficient.
     */
    static std::size_t serialize(std::span<const TelemetrySample> samples, std::span<uint8_t> out_buffer) noexcept {
        const std::size_t total_required = required_buffer_size(samples.size());
        if (out_buffer.size() < total_required) {
            return 0;  // Buffer overflow safety check
        }

        const uint32_t count = static_cast<uint32_t>(samples.size());
        const std::size_t payload_bytes = count * sizeof(TelemetrySample);

        // Calculate payload checksum before writing.
        const uint32_t crc = calculate_crc32(reinterpret_cast<const uint8_t*>(samples.data()), payload_bytes);

        BatchHeader header{.magic = kMagicNumber, .count = count, .payload_crc32 = crc};

        // Efficient memcpy writes avoiding alignment traps.
        std::memcpy(out_buffer.data(), &header, sizeof(BatchHeader));
        std::memcpy(out_buffer.data() + sizeof(BatchHeader), samples.data(), payload_bytes);

        return total_required;
    }

    /**
     * @brief Fallback vector-allocating serialize for convenience where latency is non-critical.
     */
    [[nodiscard]] static std::vector<uint8_t> serialize(std::span<const TelemetrySample> samples) {
        std::vector<uint8_t> buffer(required_buffer_size(samples.size()));
        serialize(samples, buffer);
        return buffer;
    }

    /**
     * @brief Zero-Copy view validator and deserializer.
     *
     * @param data Raw binary input buffer.
     * @return Optional span pointing directly to the TelemetrySample array inside the input buffer.
     */
    [[nodiscard]] static std::optional<std::span<const TelemetrySample>> deserialize_zero_copy(
        std::span<const uint8_t> data) noexcept {
        if (data.size() < sizeof(BatchHeader)) {
            return std::nullopt;
        }

        BatchHeader header;
        std::memcpy(&header, data.data(), sizeof(BatchHeader));

        if (header.magic != kMagicNumber) {
            return std::nullopt;
        }

        const std::size_t expected_payload_bytes = header.count * sizeof(TelemetrySample);
        if (data.size() < sizeof(BatchHeader) + expected_payload_bytes) {
            return std::nullopt;
        }

        const uint8_t* payload_ptr = data.data() + sizeof(BatchHeader);
        const uint32_t calculated_crc = calculate_crc32(payload_ptr, expected_payload_bytes);

        if (calculated_crc != header.payload_crc32) {
            return std::nullopt;  // Corrupted payload checksum match.
        }

        return std::span<const TelemetrySample>(reinterpret_cast<const TelemetrySample*>(payload_ptr), header.count);
    }

    /**
     * @brief Calculates exact byte capacity required for a given batch size.
     */
    [[nodiscard]] static constexpr std::size_t required_buffer_size(std::size_t sample_count) noexcept {
        return sizeof(BatchHeader) + (sample_count * sizeof(TelemetrySample));
    }

  private:
    /**
     * @brief Hardware-accelerated CRC32 calculation with a portable software fallback.
     *
     * SSE4.2 is a runtime CPU feature, not something the compiler can assume just
     * because the target architecture is x86-64 - plenty of real x86-64 hardware
     * (older Atom cores, some VMs/hypervisors, certain embedded targets) lacks it.
     * Calling the intrinsic unconditionally either fails to build without
     * -march=native/-msse4.2, or - worse - builds fine and then raises SIGILL the
     * first time it runs on hardware without the instruction. To stay both correct
     * and portable, the accelerated path is compiled as its own function tagged
     * with a target attribute (so it doesn't require global -march flags) and is
     * only invoked after a runtime CPUID feature check.
     */
    static uint32_t calculate_crc32(const uint8_t* data, std::size_t length) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        if (has_sse42()) {
            return crc32_hw(data, length);
        }
#endif
        return crc32_sw(data, length);
    }

#if defined(__x86_64__) || defined(_M_X64)
    [[nodiscard]] static bool has_sse42() noexcept {
        static const bool supported = detect_sse42();
        return supported;
    }

    [[nodiscard]] static bool detect_sse42() noexcept {
#if defined(_MSC_VER)
        int cpu_info[4] = {0, 0, 0, 0};
        __cpuid(cpu_info, 1);
        return (cpu_info[2] & (1 << 20)) != 0;  // ECX bit 20 = SSE4.2.
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_cpu_supports("sse4.2");
#else
        return false;
#endif
    }

    // Compiled with the sse4.2 target attribute so this translation unit does not
    // need to be built with -march=native/-msse4.2 as a whole; only ever entered
    // after has_sse42() has confirmed the running CPU actually supports it.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((target("sse4.2")))
#endif
    static uint32_t
    crc32_hw(const uint8_t* data, std::size_t length) noexcept {
        uint32_t crc = 0xFFFFFFFF;

        while (length >= 8) {
            uint64_t chunk;
            std::memcpy(&chunk, data, sizeof(chunk));
            crc = static_cast<uint32_t>(_mm_crc32_u64(crc, chunk));
            data += 8;
            length -= 8;
        }
        while (length >= 4) {
            uint32_t chunk;
            std::memcpy(&chunk, data, sizeof(chunk));
            crc = _mm_crc32_u32(crc, chunk);
            data += 4;
            length -= 4;
        }
        while (length > 0) {
            crc = _mm_crc32_u8(crc, *data);
            ++data;
            --length;
        }

        return crc ^ 0xFFFFFFFF;
    }
#endif  // x86_64

    // Portable bitwise CRC32 (IEEE 802.3 polynomial), used on non-x86 targets and
    // whenever SSE4.2 is unavailable at runtime.
    static uint32_t crc32_sw(const uint8_t* data, std::size_t length) noexcept {
        uint32_t crc = 0xFFFFFFFF;
        while (length > 0) {
            crc ^= *data;
            for (int bit = 0; bit < 8; ++bit) {
                const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
                crc = (crc >> 1) ^ (0xEDB88320U & mask);
            }
            ++data;
            --length;
        }
        return crc ^ 0xFFFFFFFF;
    }
};

}  // namespace apollonian::core.
