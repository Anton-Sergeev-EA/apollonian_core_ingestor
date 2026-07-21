#include "ingestor/config.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace apollonian::core {

std::optional<Config> Config::from_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Config] Error: Unable to open configuration file at " << path << std::endl;
        return std::nullopt;
    }

    try {
        json j;
        file >> j;

        Config cfg;
        cfg.ring_buffer_capacity = j.value("ring_buffer_capacity", cfg.ring_buffer_capacity);
        cfg.batch_size = j.value("batch_size", cfg.batch_size);
        
        if (j.contains("flush_interval_ms") && j["flush_interval_ms"].is_number()) {
            cfg.flush_interval = std::chrono::milliseconds(j["flush_interval_ms"].get<uint64_t>());
        }

        cfg.output_endpoint = j.value("output_endpoint", cfg.output_endpoint);
        cfg.enable_metrics = j.value("enable_metrics", cfg.enable_metrics);

        // Enforce runtime configuration safety rules.
        if (auto validation_error = cfg.validate()) {
            std::cerr << "[Config] Validation error in " << path << ": " << *validation_error << std::endl;
            return std::nullopt;
        }

        return cfg;
    } catch (const json::exception& e) {
        std::cerr << "[Config] JSON parsing error in " << path << ": " << e.what() << std::endl;
        return std::nullopt;
    } catch (const std::exception& e) {
        std::cerr << "[Config] Unexpected exception reading " << path << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

}
