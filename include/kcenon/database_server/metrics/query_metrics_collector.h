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
 * @file query_metrics_collector.h
 * @brief CRTP-based query metrics collector implementation
 *
 * Provides comprehensive metrics collection for database query server
 * including query execution, caching, connection pool, and session
 * statistics. Uses CRTP pattern for zero-overhead collection.
 *
 * Features:
 * - Query execution tracking (latency, success/failure, types)
 * - Cache performance monitoring (hit/miss, evictions)
 * - Connection pool utilization metrics
 * - Session lifecycle tracking
 * - Thread-safe atomic operations
 * - Zero virtual dispatch overhead via CRTP
 *
 * Usage Example:
 * @code
 * query_metrics_collector collector;
 * collector.initialize({{"enabled", "true"}});
 *
 * // Record query execution
 * query_execution exec;
 * exec.query_type = "select";
 * exec.latency_ns = 1500000;
 * exec.success = true;
 * collector.collect_query_metrics(exec);
 *
 * // Get aggregated metrics
 * const auto& metrics = collector.get_metrics();
 * double avg_latency = metrics.query_metrics.avg_query_latency_ms();
 * @endcode
 */

#pragma once

#include "query_collector_base.h"
#include "query_metrics.h"

#include <memory>
#include <mutex>
#include <shared_mutex>

namespace database_server::metrics
{

/**
 * @struct collector_options
 * @brief Configuration options for query metrics collector
 */
struct collector_options
{
	bool enabled = true;              ///< Enable/disable collection
	bool track_query_types = true;    ///< Track metrics per query type
	bool track_latency_histogram = false; ///< Track latency distribution
	uint32_t histogram_buckets = 10;  ///< Number of histogram buckets
};

/**
 * @class query_metrics_collector
 * @brief CRTP-based collector for database query metrics
 *
 * Implements the query_collector_base interface for comprehensive
 * metrics collection. All operations are thread-safe using atomic
 * operations and minimal locking.
 *
 * Thread Safety:
 * - All collection methods are thread-safe
 * - Statistics retrieval uses shared locks
 * - Metrics storage uses atomic operations
 */
class query_metrics_collector
	: public query_collector_base<query_metrics_collector>
{
	friend class query_collector_base<query_metrics_collector>;

public:
	/**
	 * @brief Static collector name for CRTP base
	 */
	static constexpr const char* collector_name = "query_metrics_collector";

	/**
	 * @brief Constructs a query metrics collector
	 * @param options Collector configuration options
	 */
	explicit query_metrics_collector(
		const collector_options& options = collector_options{});

	/**
	 * @brief Destructor
	 */
	~query_metrics_collector() = default;

	/**
	 * @brief Get current query execution metrics
	 * @return Reference to query execution metrics
	 */
	[[nodiscard]] const query_execution_metrics& query_metrics() const noexcept;

	/**
	 * @brief Get current cache performance metrics
	 * @return Reference to cache performance metrics
	 */
	[[nodiscard]] const cache_performance_metrics& cache_metrics() const noexcept;

	/**
	 * @brief Get current pool performance metrics
	 * @return Reference to pool performance metrics
	 */
	[[nodiscard]] const pool_performance_metrics& pool_metrics() const noexcept;

	/**
	 * @brief Get current session performance metrics
	 * @return Reference to session performance metrics
	 */
	[[nodiscard]] const session_performance_metrics& session_metrics() const noexcept;

	/**
	 * @brief Reset all collected metrics
	 */
	void reset_metrics();

	/**
	 * @brief Get collector options
	 * @return Current collector options
	 */
	[[nodiscard]] const collector_options& options() const noexcept;

protected:
	// CRTP interface implementations
	bool do_initialize(const collector_config& config);
	void do_collect_query(const query_execution& exec);
	void do_collect_pool(const pool_stats& stats);
	void do_collect_cache(const cache_stats& stats);
	void do_collect_session(const session_stats& stats);
	[[nodiscard]] bool is_available() const;
	void do_add_statistics(stats_map& stats) const;
	[[nodiscard]] const query_server_metrics& do_get_metrics() const noexcept;

private:
	/**
	 * @brief Update query type counters
	 * @param query_type Type of query executed
	 */
	void update_query_type_counter(const std::string& query_type);

private:
	collector_options options_;
	query_server_metrics metrics_;
	mutable std::shared_mutex metrics_mutex_;
};

/**
 * @brief Global query metrics collector instance
 *
 * Provides a singleton-like access to the query metrics collector
 * for easy integration throughout the codebase.
 *
 * Usage:
 * @code
 * auto& collector = get_query_metrics_collector();
 * collector.collect_query_metrics(exec);
 * @endcode
 */
query_metrics_collector& get_query_metrics_collector();

/**
 * @brief Set the global query metrics collector instance
 * @param collector Shared pointer to collector instance
 *
 * Allows injection of a custom collector for testing or
 * advanced configurations.
 */
void set_query_metrics_collector(std::shared_ptr<query_metrics_collector> collector);

} // namespace database_server::metrics
