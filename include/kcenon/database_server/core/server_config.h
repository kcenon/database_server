// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/**
 * @file server_config.h
 * @brief Server configuration structures
 *
 * Defines configuration structures for the database server.
 * Configuration can be loaded from YAML files or constructed programmatically.
 *
 * ## Thread Safety
 * Configuration structs are plain data structures with no internal
 * synchronization. They are intended to be created and populated before
 * server startup, then read concurrently. Concurrent modification is
 * NOT thread-safe; use external synchronization if runtime changes are needed.
 *
 * @code
 * using namespace database_server;
 *
 * // Load from file
 * auto config = server_config::load_from_file("config.conf");
 * if (config.has_value()) {
 *     if (!config->validate()) {
 *         for (const auto& err : config->validation_errors()) {
 *             std::cerr << "Config error: " << err << std::endl;
 *         }
 *     }
 * }
 *
 * // Or construct programmatically
 * auto cfg = server_config::default_config();
 * cfg.network.port = 5433;
 * cfg.pool.max_connections = 100;
 * cfg.cache.enabled = true;
 * @endcode
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace database_server
{

/**
 * @struct network_config
 * @brief Network listener configuration
 */
struct network_config
{
	std::string host = "0.0.0.0";   ///< Listen address
	uint16_t port = 5432;           ///< Listen port
	bool enable_tls = false;        ///< Enable TLS/SSL
	std::string cert_file;          ///< TLS certificate file path
	std::string key_file;           ///< TLS private key file path
	uint32_t max_connections = 100; ///< Maximum concurrent connections
	uint32_t connection_timeout_ms = 30000; ///< Connection timeout in milliseconds
};

/**
 * @struct logging_config
 * @brief Logging configuration
 */
struct logging_config
{
	std::string level = "info";       ///< Log level (debug, info, warn, error)
	std::string log_file;             ///< Log file path (empty for stdout)
	bool enable_console = true;       ///< Enable console output
	uint32_t max_file_size_mb = 100;  ///< Max log file size before rotation
	uint32_t max_backup_files = 5;    ///< Number of backup files to keep
};

/**
 * @struct pool_config
 * @brief Connection pool configuration (for Phase 2)
 */
struct pool_config
{
	uint32_t min_connections = 5;    ///< Minimum pool size
	uint32_t max_connections = 50;   ///< Maximum pool size
	uint32_t idle_timeout_ms = 60000; ///< Idle connection timeout
	uint32_t health_check_interval_ms = 30000; ///< Health check interval
};

/**
 * @struct query_cache_config
 * @brief Query cache configuration (for Phase 3)
 */
struct query_cache_config
{
	bool enabled = false;                      ///< Enable/disable query cache
	size_t max_entries = 10000;                ///< Maximum cached entries
	uint32_t ttl_seconds = 300;                ///< Time-to-live in seconds (0 = no expiration)
	size_t max_result_size_bytes = 1024 * 1024; ///< Max size of single result (1MB)
	bool enable_lru = true;                    ///< Enable LRU eviction policy
};

/**
 * @struct server_config
 * @brief Main server configuration
 *
 * Contains all configuration for the database server including
 * network settings, logging, and connection pooling.
 */
struct server_config
{
	std::string name = "database_server"; ///< Server instance name
	network_config network;               ///< Network configuration
	logging_config logging;               ///< Logging configuration
	pool_config pool;                     ///< Connection pool configuration
	query_cache_config cache;             ///< Query cache configuration

	/**
	 * @brief Load configuration from a YAML file
	 * @param path Path to the configuration file
	 * @return Loaded configuration, or std::nullopt on error
	 */
	static std::optional<server_config> load_from_file(const std::string& path);

	/**
	 * @brief Create a default configuration
	 * @return Default server configuration
	 */
	static server_config default_config();

	/**
	 * @brief Validate the configuration
	 * @return true if configuration is valid
	 */
	bool validate() const;

	/**
	 * @brief Get validation error messages
	 * @return Vector of validation error messages
	 */
	std::vector<std::string> validation_errors() const;
};

} // namespace database_server
