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
 * @file query_router.h
 * @brief Query router with load balancing for database gateway
 *
 * Routes incoming queries to appropriate connection pools with
 * load balancing support. Integrates with Phase 2 connection pool
 * for actual query execution.
 *
 * Features:
 * - Round-robin load balancing across connections
 * - Priority-based query scheduling
 * - Query execution with timeout support
 * - Metrics collection for monitoring
 */

#pragma once

#include "query_protocol.h"
#include "query_types.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Common system integration
#include <kcenon/common/patterns/result.h>

// Forward declarations
namespace database_server::pooling
{
class connection_pool;
}

namespace database_server::gateway
{

/**
 * @struct router_config
 * @brief Configuration for the query router
 */
struct router_config
{
	uint32_t default_timeout_ms = 30000;   ///< Default query timeout
	uint32_t max_concurrent_queries = 100; ///< Maximum concurrent queries
	bool enable_metrics = true;            ///< Enable metrics collection
};

/**
 * @struct router_metrics
 * @brief Metrics for query router performance monitoring
 */
struct router_metrics
{
	std::atomic<uint64_t> total_queries{0};    ///< Total queries processed
	std::atomic<uint64_t> successful_queries{0}; ///< Successful queries
	std::atomic<uint64_t> failed_queries{0};     ///< Failed queries
	std::atomic<uint64_t> timeout_queries{0};    ///< Timed out queries
	std::atomic<uint64_t> total_execution_time_us{0}; ///< Total execution time

	double average_execution_time_us() const noexcept
	{
		auto total = total_queries.load();
		if (total == 0) return 0.0;
		return static_cast<double>(total_execution_time_us.load()) / total;
	}

	double success_rate() const noexcept
	{
		auto total = total_queries.load();
		if (total == 0) return 0.0;
		return static_cast<double>(successful_queries.load()) / total * 100.0;
	}
};

/**
 * @class query_router
 * @brief Routes and executes queries using connection pool
 *
 * Provides the core query execution logic for the gateway:
 * - Routes queries to connection pool
 * - Handles different query types (SELECT, INSERT, etc.)
 * - Manages timeouts and retries
 * - Collects execution metrics
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Queries are executed concurrently
 *
 * Usage Example:
 * @code
 * // Create connection pool (from Phase 2)
 * auto pool = std::make_shared<connection_pool>(...);
 * pool->initialize();
 *
 * // Create router
 * router_config config;
 * query_router router(config);
 * router.set_connection_pool(pool);
 *
 * // Execute query
 * query_request request("SELECT * FROM users", query_type::select);
 * auto response = router.execute(request);
 * @endcode
 */
class query_router
{
public:
	/**
	 * @brief Constructs a query router with configuration
	 * @param config Router configuration
	 */
	explicit query_router(const router_config& config = router_config{});

	/**
	 * @brief Destructor
	 */
	~query_router() = default;

	// Non-copyable, non-movable
	query_router(const query_router&) = delete;
	query_router& operator=(const query_router&) = delete;
	query_router(query_router&&) = delete;
	query_router& operator=(query_router&&) = delete;

	/**
	 * @brief Set the connection pool for query execution
	 * @param pool Shared pointer to connection pool
	 */
	void set_connection_pool(std::shared_ptr<pooling::connection_pool> pool);

	/**
	 * @brief Execute a query request
	 * @param request The query request to execute
	 * @return Query response with results or error
	 *
	 * This is the main entry point for query execution.
	 * Handles all query types and returns appropriate response.
	 */
	[[nodiscard]] query_response execute(const query_request& request);

	/**
	 * @brief Execute a query request asynchronously
	 * @param request The query request to execute
	 * @param callback Callback to invoke with response
	 *
	 * The callback is invoked on a worker thread when
	 * the query completes.
	 */
	void execute_async(const query_request& request,
					   std::function<void(query_response)> callback);

	/**
	 * @brief Get current router metrics
	 * @return Reference to metrics structure
	 */
	[[nodiscard]] const router_metrics& metrics() const noexcept;

	/**
	 * @brief Reset metrics counters
	 */
	void reset_metrics();

	/**
	 * @brief Get router configuration
	 * @return Current configuration
	 */
	[[nodiscard]] const router_config& config() const noexcept;

	/**
	 * @brief Check if router is ready for queries
	 * @return true if connection pool is set and available
	 */
	[[nodiscard]] bool is_ready() const noexcept;

private:
	/**
	 * @brief Execute SELECT query
	 */
	[[nodiscard]] query_response execute_select(const query_request& request);

	/**
	 * @brief Execute INSERT/UPDATE/DELETE query
	 */
	[[nodiscard]] query_response execute_modify(const query_request& request);

	/**
	 * @brief Execute stored procedure
	 */
	[[nodiscard]] query_response execute_procedure(const query_request& request);

	/**
	 * @brief Record execution metrics
	 */
	void record_metrics(bool success, bool timeout, uint64_t execution_time_us);

private:
	router_config config_;
	std::shared_ptr<pooling::connection_pool> pool_;
	router_metrics metrics_;

	std::atomic<uint64_t> active_queries_{0};
	mutable std::mutex pool_mutex_;
};

} // namespace database_server::gateway
