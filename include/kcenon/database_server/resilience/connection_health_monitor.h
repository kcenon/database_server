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
 * @file connection_health_monitor.h
 * @brief Health monitoring for database connections
 *
 * Provides heartbeat-based health tracking, quality scoring, and
 * predictive failure detection for database connections.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Database system interfaces
#include <kcenon/database/core/database_backend.h>

namespace database_server::resilience
{

/**
 * @struct health_status
 * @brief Current health status of a database connection
 */
struct health_status
{
	bool is_healthy{ false };
	uint32_t health_score{ 0 }; // 0-100 scale
	std::chrono::milliseconds latency{ 0 };
	uint64_t successful_queries{ 0 };
	uint64_t failed_queries{ 0 };
	std::chrono::system_clock::time_point last_check_time;
	std::string status_message;

	[[nodiscard]] double success_rate() const
	{
		uint64_t total = successful_queries + failed_queries;
		return total > 0
				   ? (static_cast<double>(successful_queries) / total) * 100.0
				   : 100.0;
	}
};

/**
 * @struct health_check_config
 * @brief Configuration for health monitoring
 */
struct health_check_config
{
	std::chrono::milliseconds heartbeat_interval{ 5000 };
	std::chrono::milliseconds timeout{ 2000 };
	uint32_t failure_threshold{ 3 }; // Consecutive failures before marking unhealthy
	uint32_t min_health_score{ 50 }; // Minimum acceptable health score
	bool enable_heartbeat{ true };
};

/**
 * @class connection_health_monitor
 * @brief Monitors database connection health with heartbeat
 *
 * Features:
 * - Periodic heartbeat queries to verify connectivity
 * - Latency tracking with moving average
 * - Health score calculation (0-100)
 * - Predictive failure detection
 * - Query success/failure rate tracking
 *
 * Health Score Calculation:
 * - 40%: Success rate (successful queries / total queries)
 * - 30%: Latency performance (faster = higher score)
 * - 20%: Consecutive success streak
 * - 10%: Connection uptime
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Internal state protected by mutex
 * - Atomic counters for performance metrics
 *
 * Example Usage:
 * @code
 *   health_check_config config;
 *   config.heartbeat_interval = std::chrono::seconds(5);
 *   config.failure_threshold = 3;
 *
 *   auto monitor = std::make_unique<connection_health_monitor>(backend, config);
 *   monitor->start_monitoring();
 *
 *   // Later, check health
 *   auto status = monitor->get_health_status();
 *   if (status.health_score < 50) {
 *       // Consider reconnecting
 *   }
 * @endcode
 */
class connection_health_monitor
{
public:
	/**
	 * @brief Construct health monitor for database backend
	 * @param backend Database backend to monitor
	 * @param config Health check configuration
	 */
	explicit connection_health_monitor(
		database::core::database_backend* backend,
		health_check_config config = health_check_config{});

	~connection_health_monitor();

	// Disable copy and move (due to mutex and monitoring thread)
	connection_health_monitor(const connection_health_monitor&) = delete;
	connection_health_monitor& operator=(const connection_health_monitor&) = delete;
	connection_health_monitor(connection_health_monitor&&) = delete;
	connection_health_monitor& operator=(connection_health_monitor&&) = delete;

	/**
	 * @brief Start periodic health monitoring
	 * Launches background thread for heartbeat checks
	 */
	void start_monitoring();

	/**
	 * @brief Stop health monitoring
	 * Stops background thread gracefully
	 */
	void stop_monitoring();

	/**
	 * @brief Perform immediate health check
	 * @return Current health status
	 */
	kcenon::common::Result<health_status> check_now();

	/**
	 * @brief Get current health status (cached)
	 * @return Latest health status from last check
	 */
	[[nodiscard]] health_status get_health_status() const;

	/**
	 * @brief Record successful query execution
	 * @param query_latency Query execution time
	 */
	void record_success(std::chrono::milliseconds query_latency);

	/**
	 * @brief Record failed query execution
	 * @param error_message Error description
	 */
	void record_failure(const std::string& error_message);

	/**
	 * @brief Check if connection is currently healthy
	 * @return true if health score >= min_health_score
	 */
	[[nodiscard]] bool is_healthy() const noexcept;

	/**
	 * @brief Get current health score (0-100)
	 * @return Health score
	 */
	[[nodiscard]] uint32_t get_health_score() const noexcept;

	/**
	 * @brief Predict if connection is likely to fail soon
	 * @return true if failure predicted based on trends
	 */
	[[nodiscard]] bool predict_failure() const;

	/**
	 * @brief Reset health statistics
	 * Clears all counters and history
	 */
	void reset_statistics();

private:
	/**
	 * @brief Execute heartbeat query to check connectivity
	 * @return result<void>::ok() if heartbeat successful
	 */
	kcenon::common::VoidResult execute_heartbeat();

	/**
	 * @brief Calculate health score from current metrics
	 * @return Health score (0-100)
	 */
	[[nodiscard]] uint32_t calculate_health_score() const;

	/**
	 * @brief Update moving average of query latency
	 * @param new_latency Latest latency measurement
	 */
	void update_latency_average(std::chrono::milliseconds new_latency);

	/**
	 * @brief Background monitoring loop (runs in separate thread)
	 */
	void monitoring_loop();

private:
	database::core::database_backend* backend_;
	health_check_config config_;

	std::atomic<bool> is_monitoring_{ false };
	std::atomic<bool> stop_requested_{ false };
	std::unique_ptr<std::thread> monitoring_thread_;

	mutable std::mutex mutex_;
	health_status current_status_;

	// Performance metrics
	std::atomic<uint64_t> total_queries_{ 0 };
	std::atomic<uint64_t> successful_queries_{ 0 };
	std::atomic<uint64_t> failed_queries_{ 0 };
	std::atomic<uint32_t> consecutive_failures_{ 0 };
	std::atomic<uint32_t> consecutive_successes_{ 0 };

	// Latency tracking (moving average, last 10 samples)
	std::vector<std::chrono::milliseconds> latency_history_;
	static constexpr size_t MAX_LATENCY_SAMPLES = 10;

	std::chrono::system_clock::time_point connection_start_time_;
};

} // namespace database_server::resilience
