#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace apollonian::core {

/**
 * @brief Application configuration settings with runtime validation.
 */
struct Config {
    // Default configuration constants.
    static constexpr std::size_t kDefaultCapacity = 1024 * 1024; // 1M elements.
    static constexpr std::size_t kDefaultBatchSize = 1000;
    static constexpr std::chrono::milliseconds kDefaultFlushInterval{100};

    std::size_t ring_buffer_capacity{kDefaultCapacity}; // Must be a power of two
    std::size_t batch_size{kDefaultBatchSize};           // Maximum items per telemetry batch
    std::chrono::milliseconds flush_interval{kDefaultFlushInterval};
    std::string output_endpoint{"s3://apollonian-bucket/telemetry/"};
    bool enable_metrics{true};

    /**
     * @brief Validates internal configuration parameters.
     * @return std::nullopt if configuration is valid, or an error description string if invalid.
     */
    [[nodiscard]] std::optional<std::string> validate() const noexcept {
        // 1. Capacity must be a non-zero power of two for RingBuffer mask optimizations.
        if (ring_buffer_capacity == 0 || (ring_buffer_capacity & (ring_buffer_capacity - 1)) != 0) {
            return "ring_buffer_capacity must be a positive power of two (e.g., 1024, 65536, 1048576)";
        }

        // 2. Batch size constraints.
        if (batch_size == 0) {
            return "batch_size must be greater than 0";
        }

        if (batch_size > ring_buffer_capacity) {
            return "batch_size cannot exceed ring_buffer_capacity";
        }

        // 3. Flush interval constraint.
        if (flush_interval.count() <= 0) {
            return "flush_interval must be greater than 0 ms";
        }

        // 4. Output endpoint validation.
        if (output_endpoint.empty()) {
            return "output_endpoint URI cannot be empty";
        }

        return std::nullopt; // Configuration is completely valid
    }

    /**
     * @brief Parses and loads configuration parameters from a JSON file path.
     * 
     * @param path Path to the JSON configuration file.
     * @return Config object on success, or std::nullopt if reading/parsing/validation failed.
     */
    [[nodiscard]] static std::optional<Config> from_file(const std::filesystem::path& path);
};

} // namespace apollonian::core
