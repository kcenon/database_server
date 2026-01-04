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
 * @file query_collector_base.h
 * @brief CRTP base class for query metrics collectors
 *
 * This file provides a CRTP (Curiously Recurring Template Pattern) base class
 * for database query metrics collection. Following the monitoring_system's
 * collector pattern, it enables efficient, zero-overhead metric collection.
 *
 * Usage:
 * @code
 * class my_collector : public query_collector_base<my_collector> {
 * public:
 *     static constexpr const char* collector_name = "my_collector";
 *
 *     bool do_initialize(const collector_config& config) {
 *         // Collector-specific initialization
 *         return true;
 *     }
 *
 *     void do_collect_query(const query_execution& exec) {
 *         // Collector-specific query metric collection
 *     }
 *
 *     bool is_available() const {
 *         return true;
 *     }
 * };
 * @endcode
 */

#pragma once

#include "query_metrics.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace database_server::metrics
{

/**
 * @brief Type alias for collector configuration map
 */
using collector_config = std::unordered_map<std::string, std::string>;

/**
 * @brief Type alias for statistics map
 */
using stats_map = std::unordered_map<std::string, double>;

/**
 * @struct query_execution
 * @brief Information about a single query execution
 */
struct query_execution
{
	std::string query_type;     ///< Type of query (select, insert, etc.)
	uint64_t latency_ns{0};     ///< Query latency in nanoseconds
	bool success{false};        ///< Whether query succeeded
	bool timeout{false};        ///< Whether query timed out
	size_t rows_affected{0};    ///< Number of rows affected/returned
	std::string error_message;  ///< Error message if failed
};

/**
 * @struct pool_stats
 * @brief Connection pool statistics snapshot
 */
struct pool_stats
{
	uint64_t active_connections{0};   ///< Currently active connections
	uint64_t idle_connections{0};     ///< Currently idle connections
	uint64_t total_connections{0};    ///< Total connections in pool
	uint64_t acquisition_time_ns{0};  ///< Last acquisition time
	bool acquisition_success{false};  ///< Last acquisition result
	bool pool_exhausted{false};       ///< Pool exhaustion state
};

/**
 * @struct cache_stats
 * @brief Cache statistics snapshot
 */
struct cache_stats
{
	bool hit{false};          ///< Whether operation was a hit
	bool eviction{false};     ///< Whether entry was evicted
	bool expiration{false};   ///< Whether entry expired
	uint64_t size_bytes{0};   ///< Current cache size
	uint64_t entry_count{0};  ///< Current entry count
};

/**
 * @struct session_stats
 * @brief Session statistics snapshot
 */
struct session_stats
{
	bool session_start{false};   ///< Session started
	bool session_end{false};     ///< Session ended
	uint64_t duration_ns{0};     ///< Session duration (if ended)
	bool auth_success{false};    ///< Authentication result
	bool auth_attempted{false};  ///< Whether auth was attempted
};

/**
 * @class query_collector_base
 * @brief CRTP base class for query metrics collectors
 *
 * This template class implements common functionality shared by all
 * query metrics collectors:
 * - Configuration parsing (enabled state)
 * - Collection with error handling and statistics
 * - Health monitoring
 * - Statistics tracking (collection count, error count)
 *
 * @tparam Derived The derived collector class (CRTP pattern)
 */
template <typename Derived>
class query_collector_base
{
public:
	query_collector_base() = default;
	virtual ~query_collector_base() = default;

	// Non-copyable, non-moveable
	query_collector_base(const query_collector_base&) = delete;
	query_collector_base& operator=(const query_collector_base&) = delete;
	query_collector_base(query_collector_base&&) = delete;
	query_collector_base& operator=(query_collector_base&&) = delete;

	/**
	 * @brief Initialize the collector with configuration
	 * @param config Configuration options (common: "enabled")
	 * @return true if initialization successful
	 */
	bool initialize(const collector_config& config)
	{
		// Parse common configuration
		if (auto it = config.find("enabled"); it != config.end())
		{
			enabled_ = (it->second == "true" || it->second == "1");
		}

		// Delegate to derived class for specific initialization
		return derived().do_initialize(config);
	}

	/**
	 * @brief Collect query execution metrics
	 * @param exec Query execution information
	 */
	void collect_query_metrics(const query_execution& exec)
	{
		if (!enabled_)
		{
			return;
		}

		try
		{
			derived().do_collect_query(exec);
			++collection_count_;
		}
		catch (...)
		{
			++collection_errors_;
		}
	}

	/**
	 * @brief Collect connection pool metrics
	 * @param stats Pool statistics snapshot
	 */
	void collect_pool_metrics(const pool_stats& stats)
	{
		if (!enabled_)
		{
			return;
		}

		try
		{
			derived().do_collect_pool(stats);
			++collection_count_;
		}
		catch (...)
		{
			++collection_errors_;
		}
	}

	/**
	 * @brief Collect cache metrics
	 * @param stats Cache statistics snapshot
	 */
	void collect_cache_metrics(const cache_stats& stats)
	{
		if (!enabled_)
		{
			return;
		}

		try
		{
			derived().do_collect_cache(stats);
			++collection_count_;
		}
		catch (...)
		{
			++collection_errors_;
		}
	}

	/**
	 * @brief Collect session metrics
	 * @param stats Session statistics snapshot
	 */
	void collect_session_metrics(const session_stats& stats)
	{
		if (!enabled_)
		{
			return;
		}

		try
		{
			derived().do_collect_session(stats);
			++collection_count_;
		}
		catch (...)
		{
			++collection_errors_;
		}
	}

	/**
	 * @brief Get the name of this collector
	 * @return Collector name from Derived::collector_name
	 */
	[[nodiscard]] std::string get_name() const
	{
		return Derived::collector_name;
	}

	/**
	 * @brief Check if the collector is healthy
	 * @return true if collector is operational
	 */
	[[nodiscard]] bool is_healthy() const
	{
		if (!enabled_)
		{
			return true; // Disabled collectors are considered healthy
		}
		return derived().is_available();
	}

	/**
	 * @brief Get collector statistics
	 * @return Map of statistic name to value
	 */
	[[nodiscard]] stats_map get_statistics() const
	{
		std::lock_guard<std::mutex> lock(stats_mutex_);
		stats_map stats;

		// Common statistics
		stats["enabled"] = enabled_ ? 1.0 : 0.0;
		stats["collection_count"] = static_cast<double>(collection_count_.load());
		stats["collection_errors"] = static_cast<double>(collection_errors_.load());

		// Let derived class add specific statistics
		derived().do_add_statistics(stats);

		return stats;
	}

	/**
	 * @brief Check if collector is enabled
	 * @return true if enabled
	 */
	[[nodiscard]] bool is_enabled() const noexcept
	{
		return enabled_;
	}

	/**
	 * @brief Get collection count
	 * @return Number of successful collections
	 */
	[[nodiscard]] size_t get_collection_count() const noexcept
	{
		return collection_count_.load();
	}

	/**
	 * @brief Get error count
	 * @return Number of failed collections
	 */
	[[nodiscard]] size_t get_collection_errors() const noexcept
	{
		return collection_errors_.load();
	}

	/**
	 * @brief Get aggregated metrics
	 * @return Current server metrics snapshot
	 */
	[[nodiscard]] const query_server_metrics& get_metrics() const noexcept
	{
		return derived().do_get_metrics();
	}

protected:
	// Configuration
	bool enabled_{true};

	// Statistics
	mutable std::mutex stats_mutex_;
	std::atomic<size_t> collection_count_{0};
	std::atomic<size_t> collection_errors_{0};

private:
	/**
	 * @brief Get reference to derived class (CRTP helper)
	 */
	Derived& derived()
	{
		return static_cast<Derived&>(*this);
	}

	const Derived& derived() const
	{
		return static_cast<const Derived&>(*this);
	}
};

/**
 * @concept QueryCollector
 * @brief Concept for validating query collector implementations
 *
 * Ensures derived classes implement required methods.
 */
template <typename T>
concept QueryCollector = requires(T t, const collector_config& config,
								  const query_execution& exec,
								  const pool_stats& pool,
								  const cache_stats& cache,
								  const session_stats& session,
								  stats_map& stats) {
	{ T::collector_name } -> std::convertible_to<const char*>;
	{ t.do_initialize(config) } -> std::same_as<bool>;
	{ t.do_collect_query(exec) } -> std::same_as<void>;
	{ t.do_collect_pool(pool) } -> std::same_as<void>;
	{ t.do_collect_cache(cache) } -> std::same_as<void>;
	{ t.do_collect_session(session) } -> std::same_as<void>;
	{ t.is_available() } -> std::same_as<bool>;
	{ t.do_add_statistics(stats) } -> std::same_as<void>;
	{ t.do_get_metrics() } -> std::same_as<const query_server_metrics&>;
};

} // namespace database_server::metrics
