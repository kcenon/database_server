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
 * @file connection_pool.h
 * @brief High-performance connection pool with priority aging and scheduling
 *
 * Provides priority-based connection pool management with:
 * - Priority aging queue to prevent starvation of low-priority jobs
 * - Cancellation token for graceful shutdown
 * - Enhanced metrics for performance monitoring
 */

#pragma once

#include "connection_priority.h"
#include "pool_metrics.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>

// Common system interfaces
#include <kcenon/common/interfaces/logger_interface.h>

// Database server pooling types (server-side implementation)
#include "connection_types.h"

// Thread system integration
#include <kcenon/thread/core/cancellation_token.h>
#include <kcenon/thread/core/error_handling.h>
#include <kcenon/thread/core/typed_thread_pool.h>
#include <kcenon/thread/core/typed_thread_worker.h>
#include <kcenon/thread/impl/typed_pool/aging_typed_job_queue.h>

namespace database_server::pooling
{

/**
 * @class connection_acquisition_job
 * @brief Typed job for adaptive priority-based connection acquisition
 *
 * This job implements connection acquisition logic integrated with
 * thread_system's adaptive_typed_job_queue for optimal performance
 * under varying load conditions.
 */
class connection_acquisition_job
	: public kcenon::thread::typed_job_t<connection_priority>
{
public:
	using completion_callback = std::function<void(
		kcenon::common::Result<std::shared_ptr<database::connection_wrapper>>)>;

	/**
	 * @brief Constructs a connection acquisition job
	 * @param priority Priority level for this request
	 * @param pool_ref Reference to the underlying connection pool
	 * @param callback Callback to invoke with the result
	 */
	explicit connection_acquisition_job(
		connection_priority priority,
		std::shared_ptr<database::connection_pool_base> pool_ref,
		completion_callback callback)
		: kcenon::thread::typed_job_t<connection_priority>(
			  priority, "connection_acquisition")
		, pool_ref_(std::move(pool_ref))
		, callback_(std::move(callback))
	{
	}

	kcenon::common::VoidResult do_work() override
	{
		try
		{
			// Acquire connection from the underlying pool
			auto result = pool_ref_->acquire_connection();

			// Invoke callback with result
			if (callback_)
			{
				callback_(std::move(result));
			}

			return kcenon::common::ok();
		}
		catch (const std::exception& e)
		{
			// On exception, invoke callback with error
			if (callback_)
			{
				callback_(kcenon::common::error_info{
					-599,
					std::string("Exception in connection acquisition: ") + e.what(),
					"connection_pool" });
			}

			return kcenon::common::error_info{
				static_cast<int>(kcenon::thread::error_code::job_execution_failed),
				std::string("Exception in connection_acquisition_job: ") + e.what(),
				"connection_pool"
			};
		}
	}

private:
	std::shared_ptr<database::connection_pool_base> pool_ref_;
	completion_callback callback_;
};

/**
 * @class connection_pool
 * @brief High-performance connection pool with priority aging and cancellation support
 *
 * connection_pool provides priority-based connection pooling with:
 * - **Priority Aging Queue**: Prevents starvation by gradually boosting priorities
 *   of waiting jobs based on configurable aging curves
 * - **Cancellation Token**: Graceful shutdown with cooperative cancellation
 * - **Enhanced Metrics**: Detailed performance monitoring with aging statistics
 * - **Ultra-Low Latency**: Target < 100ns connection acquisition latency
 * - **High Throughput**: Target > 1M ops/s
 *
 * ### Architecture
 * 1. **Priority Aging**: Prevents low-priority job starvation
 *    - Configurable aging intervals and boost amounts
 *    - Linear, exponential, or logarithmic aging curves
 *    - Starvation detection and alerting
 * 2. **Cooperative Cancellation**: Clean shutdown without forceful thread termination
 * 3. **Performance Metrics**: Track priority boosts, wait times, starvation alerts
 *
 * ### Priority Levels
 * - CRITICAL: Time-critical operations that cannot be delayed
 * - TRANSACTION: Active transactions requiring immediate response
 * - NORMAL_QUERY: Standard database queries (default priority)
 * - HEALTH_CHECK: Background health monitoring (lowest priority)
 *
 * ### Thread Safety
 * All methods are thread-safe and can be called from multiple threads concurrently.
 *
 * ### Example Usage
 * @code
 * database::connection_pool_config config;
 * config.min_connections = 5;
 * config.max_connections = 50;
 *
 * auto factory = [&]() {
 *     return std::make_unique<postgres_database>(config.connection_string);
 * };
 *
 * database_server::pooling::connection_pool pool(
 *     database::database_types::postgres,
 *     config,
 *     factory
 * );
 *
 * if (!pool.initialize()) {
 *     std::cerr << "Failed to initialize pool\n";
 * }
 *
 * // Acquire connection with priority
 * auto future = pool.acquire_connection(PRIORITY_CRITICAL);
 * auto result = future.get();
 * if (result.is_ok()) {
 *     auto conn = result.value();
 *     conn->get()->execute_query("SELECT ...");
 * }
 * @endcode
 */
class connection_pool
{
public:
	/**
	 * @brief Constructs connection pool with aging-based priority queue
	 * @param db_type Database type for this pool
	 * @param config Pool configuration
	 * @param factory Function to create new database connections
	 * @param thread_count Number of worker threads (default: hardware_concurrency)
	 * @param aging_config Priority aging configuration (optional)
	 *
	 * ### Priority Aging
	 * The aging queue prevents starvation by gradually boosting priorities
	 * of waiting jobs. Configure aging behavior via priority_aging_config.
	 */
	connection_pool(
		database::database_types db_type,
		const database::connection_pool_config& config,
		std::function<std::unique_ptr<database::core::database_backend>()> factory,
		size_t thread_count = std::thread::hardware_concurrency(),
		kcenon::thread::priority_aging_config aging_config = {});

	/**
	 * @brief Destructor - ensures graceful shutdown
	 */
	~connection_pool();

	// Prevent copying and moving
	connection_pool(const connection_pool&) = delete;
	connection_pool& operator=(const connection_pool&) = delete;
	connection_pool(connection_pool&&) = delete;
	connection_pool& operator=(connection_pool&&) = delete;

	/**
	 * @brief Initializes the connection pool
	 * @return true if initialization successful, false otherwise
	 *
	 * Must be called before acquiring connections.
	 */
	bool initialize();

	/**
	 * @brief Acquires a connection with specified priority
	 * @param priority Priority level for this request (default: NORMAL_QUERY)
	 * @return Future that resolves to Result<connection_wrapper>
	 *
	 * ### Thread Safety
	 * Thread-safe, can be called from multiple threads concurrently.
	 *
	 * ### Performance
	 * - Priority aging: Prevents starvation of low-priority requests
	 * - Target latency: < 100ns scheduling + pool acquisition time
	 * - Throughput: > 1M ops/s under high load
	 */
	std::future<kcenon::common::Result<std::shared_ptr<database::connection_wrapper>>>
	acquire_connection(connection_priority priority = PRIORITY_NORMAL_QUERY);

	/**
	 * @brief Returns a connection to the pool
	 * @param connection Connection to return
	 *
	 * Always return connections after use to avoid resource leaks.
	 */
	void release_connection(std::shared_ptr<database::connection_wrapper> connection);

	/**
	 * @brief Schedules asynchronous health check
	 *
	 * Health checks run as low-priority background jobs.
	 */
	void schedule_health_check();

	/**
	 * @brief Gets the number of active connections
	 * @return Number of active connections
	 */
	[[nodiscard]] size_t active_connections() const;

	/**
	 * @brief Gets the number of available connections
	 * @return Number of available connections
	 */
	[[nodiscard]] size_t available_connections() const;

	/**
	 * @brief Gets connection pool statistics
	 * @return Connection statistics
	 */
	[[nodiscard]] database::connection_stats get_stats() const;

	/**
	 * @brief Requests graceful shutdown via cancellation token
	 *
	 * Signals all pending operations to cancel cooperatively.
	 * Does not block; call shutdown() to wait for completion.
	 */
	void request_shutdown();

	/**
	 * @brief Shuts down the connection pool
	 *
	 * Waits for pending operations to complete before shutting down.
	 */
	void shutdown();

	/**
	 * @brief Checks if shutdown was requested
	 * @return true if shutdown requested, false otherwise
	 */
	[[nodiscard]] bool is_shutdown_requested() const;

	/**
	 * @brief Gets the aging statistics for the queue
	 * @return Aging statistics including boost counts and wait times
	 */
	[[nodiscard]] kcenon::thread::aging_stats get_aging_stats() const;

	/**
	 * @brief Gets the total number of priority boosts applied
	 * @return Number of times jobs have had their priorities boosted
	 */
	[[nodiscard]] uint64_t get_total_priority_boosts() const;

	/**
	 * @brief Gets the maximum wait time observed
	 * @return Maximum time a job has waited in milliseconds
	 */
	[[nodiscard]] std::chrono::milliseconds get_max_wait_time() const;

	/**
	 * @brief Gets the average wait time
	 * @return Average time jobs wait before being processed
	 */
	[[nodiscard]] std::chrono::milliseconds get_average_wait_time() const;

	/**
	 * @brief Gets performance metrics for this pool
	 * @return Shared pointer to priority-aware metrics
	 */
	[[nodiscard]] std::shared_ptr<priority_metrics<connection_priority>> get_metrics()
		const;

	/**
	 * @brief Get the logger used by this pool
	 * @return Shared pointer to logger, or nullptr if not set
	 */
	[[nodiscard]] std::shared_ptr<kcenon::common::interfaces::ILogger> get_logger() const;

	/**
	 * @brief Set the logger for this pool
	 * @param logger Shared pointer to logger
	 *
	 * When set, replaces stderr logging with structured logging via ILogger.
	 */
	void set_logger(std::shared_ptr<kcenon::common::interfaces::ILogger> logger);

private:
	// Underlying connection pool (actual connection management)
	std::shared_ptr<database::connection_pool> underlying_pool_;

	// Aging job queue for priority-based scheduling with starvation prevention
	std::shared_ptr<kcenon::thread::aging_typed_job_queue_t<connection_priority>>
		aging_queue_;

	// Worker thread pool
	std::shared_ptr<kcenon::thread::typed_thread_pool_t<connection_priority>>
		worker_pool_;

	// Cancellation token for graceful shutdown
	kcenon::thread::cancellation_token shutdown_token_;

	// Performance metrics with priority tracking
	std::shared_ptr<priority_metrics<connection_priority>> metrics_;

	// Thread count
	size_t thread_count_;

	// Shutdown flag
	std::atomic<bool> shutdown_requested_;

	// Logger for structured logging
	std::shared_ptr<kcenon::common::interfaces::ILogger> logger_;
};

} // namespace database_server::pooling
