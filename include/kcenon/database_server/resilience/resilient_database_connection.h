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
 * @file resilient_database_connection.h
 * @brief Resilient database connection with automatic reconnection
 *
 * Provides automatic reconnection with exponential backoff, connection
 * health monitoring, and self-healing capabilities. Integrates patterns
 * from network_system's resilient_client for robust database connectivity.
 */

#pragma once

#include "connection_health_monitor.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// Database system interfaces
#include <database/core/database_backend.h>
#include <database/database_types.h>

namespace database_server::resilience
{

/**
 * @struct reconnection_config
 * @brief Configuration for automatic reconnection behavior
 */
struct reconnection_config
{
	std::chrono::milliseconds initial_delay{ 100 };
	std::chrono::milliseconds max_delay{ 30000 };
	double backoff_multiplier{ 2.0 };
	uint32_t max_retries{ 10 };
	bool enable_auto_reconnect{ true };
};

/**
 * @enum connection_state
 * @brief Current state of the resilient connection
 */
enum class connection_state
{
	disconnected,
	connecting,
	connected,
	reconnecting,
	failed
};

/**
 * @brief Converts connection_state enum to string representation.
 * @param state The connection state to convert.
 * @return String representation of the connection state.
 */
constexpr const char* to_string(connection_state state) noexcept
{
	switch (state)
	{
	case connection_state::disconnected:
		return "disconnected";
	case connection_state::connecting:
		return "connecting";
	case connection_state::connected:
		return "connected";
	case connection_state::reconnecting:
		return "reconnecting";
	case connection_state::failed:
		return "failed";
	default:
		return "unknown";
	}
}

/**
 * @class resilient_database_connection
 * @brief Database connection wrapper with automatic reconnection
 *
 * Wraps any database_backend implementation and adds:
 * - Automatic reconnection with exponential backoff
 * - Connection health monitoring with heartbeat
 * - Graceful degradation on connection failures
 * - Configurable retry policies
 *
 * Design Pattern:
 * - Decorator pattern: Wraps database_backend interface
 * - Strategy pattern: Configurable reconnection strategy
 * - Observer pattern: Health status notifications
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Internal state protected by mutex
 * - Atomic state tracking for lock-free reads
 *
 * Example Usage:
 * @code
 *   auto backend = backend_registry::create("postgresql", config);
 *
 *   reconnection_config recon_config;
 *   recon_config.initial_delay = std::chrono::milliseconds(100);
 *   recon_config.max_retries = 5;
 *
 *   auto resilient = std::make_shared<resilient_database_connection>(
 *       std::move(backend), recon_config);
 *
 *   // Automatically reconnects on connection loss
 *   auto result = resilient->select_query("SELECT * FROM users");
 * @endcode
 */
class resilient_database_connection : public database::core::database_backend
{
public:
	/**
	 * @brief Construct resilient connection wrapper
	 * @param backend Underlying database backend to wrap
	 * @param config Reconnection configuration
	 */
	explicit resilient_database_connection(
		std::unique_ptr<database::core::database_backend> backend,
		reconnection_config config = reconnection_config{});

	~resilient_database_connection() override;

	// Disable copy and move (due to mutex and monitoring thread)
	resilient_database_connection(const resilient_database_connection&) = delete;
	resilient_database_connection& operator=(const resilient_database_connection&)
		= delete;
	resilient_database_connection(resilient_database_connection&&) = delete;
	resilient_database_connection& operator=(resilient_database_connection&&) = delete;

	/**
	 * @brief Get database type of underlying backend
	 * @return Database type identifier
	 */
	[[nodiscard]] database::database_types type() const override;

	/**
	 * @brief Initialize connection with automatic retry
	 * @param config Connection configuration
	 * @return result<void>::ok() on success, error on failure
	 */
	kcenon::common::VoidResult initialize(
		const database::core::connection_config& config) override;

	/**
	 * @brief Shutdown connection gracefully
	 * @return result<void>::ok() on success, error on failure
	 */
	kcenon::common::VoidResult shutdown() override;

	/**
	 * @brief Check if connection is active and healthy
	 * @return true if connected and passing health checks
	 */
	[[nodiscard]] bool is_initialized() const override;

	/**
	 * @brief Execute INSERT with automatic reconnection
	 * @param query_string SQL INSERT statement
	 * @return Number of rows inserted, or error
	 */
	kcenon::common::Result<uint64_t> insert_query(
		const std::string& query_string) override;

	/**
	 * @brief Execute UPDATE with automatic reconnection
	 * @param query_string SQL UPDATE statement
	 * @return Number of rows updated, or error
	 */
	kcenon::common::Result<uint64_t> update_query(
		const std::string& query_string) override;

	/**
	 * @brief Execute DELETE with automatic reconnection
	 * @param query_string SQL DELETE statement
	 * @return Number of rows deleted, or error
	 */
	kcenon::common::Result<uint64_t> delete_query(
		const std::string& query_string) override;

	/**
	 * @brief Execute SELECT with automatic reconnection
	 * @param query_string SQL SELECT statement
	 * @return Query results, or error
	 */
	kcenon::common::Result<database::core::database_result> select_query(
		const std::string& query_string) override;

	/**
	 * @brief Execute general query with automatic reconnection
	 * @param query_string SQL statement
	 * @return result<void>::ok() on success, error on failure
	 */
	kcenon::common::VoidResult execute_query(const std::string& query_string) override;

	/**
	 * @brief Begin transaction (requires stable connection)
	 * @return result<void>::ok() on success, error on failure
	 */
	kcenon::common::VoidResult begin_transaction() override;

	/**
	 * @brief Commit transaction
	 * @return result<void>::ok() on success, error on failure
	 */
	kcenon::common::VoidResult commit_transaction() override;

	/**
	 * @brief Rollback transaction
	 * @return result<void>::ok() on success, error on failure
	 */
	kcenon::common::VoidResult rollback_transaction() override;

	/**
	 * @brief Check if in transaction
	 * @return true if transaction is active
	 */
	[[nodiscard]] bool in_transaction() const override;

	/**
	 * @brief Get last error message
	 * @return Error message string
	 */
	[[nodiscard]] std::string last_error() const override;

	/**
	 * @brief Get connection information
	 * @return Map of connection properties
	 */
	[[nodiscard]] std::map<std::string, std::string> connection_info() const override;

	/**
	 * @brief Ensure connection is established (with retry)
	 * @return result<void>::ok() if connected, error otherwise
	 */
	kcenon::common::VoidResult ensure_connected();

	/**
	 * @brief Check connection health status
	 * @return Health status information
	 */
	kcenon::common::Result<health_status> check_health();

	/**
	 * @brief Start automatic health monitoring
	 * Launches background thread for periodic health checks
	 */
	void start_auto_recovery();

	/**
	 * @brief Stop automatic health monitoring
	 */
	void stop_auto_recovery();

	/**
	 * @brief Get current connection state
	 * @return Current state enum value
	 */
	[[nodiscard]] connection_state get_state() const noexcept;

	/**
	 * @brief Get number of reconnection attempts since last success
	 * @return Reconnection attempt count
	 */
	[[nodiscard]] uint32_t get_retry_count() const noexcept;

private:
	/**
	 * @brief Attempt to reconnect with exponential backoff
	 * @return result<void>::ok() on successful reconnection
	 */
	kcenon::common::VoidResult attempt_reconnect();

	/**
	 * @brief Execute query operation with automatic retry
	 * @tparam Func Query function type
	 * @param operation Query operation to execute
	 * @return Query result
	 */
	template <typename Func>
	auto execute_with_retry(Func&& operation) -> decltype(operation());

	/**
	 * @brief Calculate next retry delay using exponential backoff
	 * @return Delay duration for next retry
	 */
	[[nodiscard]] std::chrono::milliseconds calculate_next_delay();

	/**
	 * @brief Reset retry counter after successful operation
	 */
	void reset_retry_state();

	/**
	 * @brief Update connection state atomically
	 * @param new_state New state to set
	 */
	void set_state(connection_state new_state) noexcept;

private:
	std::unique_ptr<database::core::database_backend> backend_;
	reconnection_config config_;
	std::unique_ptr<connection_health_monitor> health_monitor_;

	database::core::connection_config connection_config_;
	std::atomic<connection_state> state_{ connection_state::disconnected };
	std::atomic<uint32_t> retry_count_{ 0 };
	std::atomic<bool> auto_recovery_enabled_{ false };

	mutable std::mutex mutex_;
	std::string last_error_message_;
};

} // namespace database_server::resilience
