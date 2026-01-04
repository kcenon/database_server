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
 * @file query_metrics.h
 * @brief Metrics structures for query server performance monitoring
 *
 * Defines comprehensive metrics structures for tracking database query
 * server performance including query execution, caching, connection pool,
 * and session statistics.
 *
 * Features:
 * - Query execution metrics (latency, success rates)
 * - Cache performance metrics (hit/miss ratios)
 * - Connection pool metrics (utilization, wait times)
 * - Session management metrics (active sessions, duration)
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace database_server::metrics
{

/**
 * @struct query_execution_metrics
 * @brief Metrics for tracking query execution performance
 *
 * Provides atomic counters for tracking query execution statistics
 * including success/failure counts and latency measurements.
 */
struct query_execution_metrics
{
	std::atomic<uint64_t> total_queries{0};      ///< Total queries executed
	std::atomic<uint64_t> successful_queries{0}; ///< Successfully completed queries
	std::atomic<uint64_t> failed_queries{0};     ///< Failed queries
	std::atomic<uint64_t> timeout_queries{0};    ///< Queries that timed out

	// Latency statistics (nanoseconds)
	std::atomic<uint64_t> total_latency_ns{0}; ///< Total latency for all queries
	std::atomic<uint64_t> min_latency_ns{UINT64_MAX}; ///< Minimum query latency
	std::atomic<uint64_t> max_latency_ns{0}; ///< Maximum query latency

	// Query type breakdown
	std::atomic<uint64_t> select_queries{0}; ///< SELECT query count
	std::atomic<uint64_t> insert_queries{0}; ///< INSERT query count
	std::atomic<uint64_t> update_queries{0}; ///< UPDATE query count
	std::atomic<uint64_t> delete_queries{0}; ///< DELETE query count
	std::atomic<uint64_t> other_queries{0};  ///< Other query types

	/**
	 * @brief Record a query execution
	 * @param latency_ns Query latency in nanoseconds
	 * @param success Whether the query succeeded
	 * @param timeout Whether the query timed out
	 */
	void record_query(uint64_t latency_ns, bool success, bool timeout = false)
	{
		total_queries.fetch_add(1, std::memory_order_relaxed);
		total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);

		if (timeout)
		{
			timeout_queries.fetch_add(1, std::memory_order_relaxed);
		}
		else if (success)
		{
			successful_queries.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			failed_queries.fetch_add(1, std::memory_order_relaxed);
		}

		// Update min latency
		uint64_t current_min = min_latency_ns.load(std::memory_order_relaxed);
		while (latency_ns < current_min
			   && !min_latency_ns.compare_exchange_weak(
				   current_min, latency_ns, std::memory_order_relaxed))
		{
		}

		// Update max latency
		uint64_t current_max = max_latency_ns.load(std::memory_order_relaxed);
		while (latency_ns > current_max
			   && !max_latency_ns.compare_exchange_weak(
				   current_max, latency_ns, std::memory_order_relaxed))
		{
		}
	}

	/**
	 * @brief Calculate average query latency in milliseconds
	 * @return Average latency in milliseconds
	 */
	[[nodiscard]] double avg_query_latency_ms() const noexcept
	{
		uint64_t total = total_queries.load(std::memory_order_relaxed);
		if (total == 0)
			return 0.0;
		return static_cast<double>(total_latency_ns.load(std::memory_order_relaxed))
			   / static_cast<double>(total) / 1000000.0;
	}

	/**
	 * @brief Calculate query success rate
	 * @return Success rate as percentage (0.0 - 100.0)
	 */
	[[nodiscard]] double success_rate() const noexcept
	{
		uint64_t total = total_queries.load(std::memory_order_relaxed);
		if (total == 0)
			return 100.0;
		return static_cast<double>(successful_queries.load(std::memory_order_relaxed))
			   / static_cast<double>(total) * 100.0;
	}

	/**
	 * @brief Reset all metrics
	 */
	void reset() noexcept
	{
		total_queries.store(0, std::memory_order_relaxed);
		successful_queries.store(0, std::memory_order_relaxed);
		failed_queries.store(0, std::memory_order_relaxed);
		timeout_queries.store(0, std::memory_order_relaxed);
		total_latency_ns.store(0, std::memory_order_relaxed);
		min_latency_ns.store(UINT64_MAX, std::memory_order_relaxed);
		max_latency_ns.store(0, std::memory_order_relaxed);
		select_queries.store(0, std::memory_order_relaxed);
		insert_queries.store(0, std::memory_order_relaxed);
		update_queries.store(0, std::memory_order_relaxed);
		delete_queries.store(0, std::memory_order_relaxed);
		other_queries.store(0, std::memory_order_relaxed);
	}
};

/**
 * @struct cache_performance_metrics
 * @brief Metrics for tracking cache performance
 *
 * Provides atomic counters for cache hit/miss tracking,
 * eviction counts, and memory usage statistics.
 */
struct cache_performance_metrics
{
	std::atomic<uint64_t> cache_hits{0};       ///< Cache hit count
	std::atomic<uint64_t> cache_misses{0};     ///< Cache miss count
	std::atomic<uint64_t> cache_evictions{0};  ///< LRU eviction count
	std::atomic<uint64_t> cache_expirations{0}; ///< TTL expiration count
	std::atomic<uint64_t> cache_size_bytes{0}; ///< Current cache size in bytes
	std::atomic<uint64_t> cache_entries{0};    ///< Current number of entries

	/**
	 * @brief Record a cache hit
	 */
	void record_hit()
	{
		cache_hits.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Record a cache miss
	 */
	void record_miss()
	{
		cache_misses.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Record a cache eviction
	 */
	void record_eviction()
	{
		cache_evictions.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Record a cache expiration
	 */
	void record_expiration()
	{
		cache_expirations.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Calculate cache hit ratio
	 * @return Hit ratio as percentage (0.0 - 100.0)
	 */
	[[nodiscard]] double cache_hit_ratio() const noexcept
	{
		uint64_t hits = cache_hits.load(std::memory_order_relaxed);
		uint64_t misses = cache_misses.load(std::memory_order_relaxed);
		uint64_t total = hits + misses;
		if (total == 0)
			return 0.0;
		return static_cast<double>(hits) / static_cast<double>(total) * 100.0;
	}

	/**
	 * @brief Reset all metrics
	 */
	void reset() noexcept
	{
		cache_hits.store(0, std::memory_order_relaxed);
		cache_misses.store(0, std::memory_order_relaxed);
		cache_evictions.store(0, std::memory_order_relaxed);
		cache_expirations.store(0, std::memory_order_relaxed);
		// Don't reset cache_size_bytes and cache_entries (they reflect current state)
	}
};

/**
 * @struct pool_performance_metrics
 * @brief Metrics for tracking connection pool performance
 *
 * Provides atomic counters for connection acquisition,
 * pool utilization, and health statistics.
 */
struct pool_performance_metrics
{
	std::atomic<uint64_t> active_connections{0}; ///< Currently active connections
	std::atomic<uint64_t> idle_connections{0};   ///< Currently idle connections
	std::atomic<uint64_t> total_connections{0};  ///< Total connections in pool

	// Acquisition statistics
	std::atomic<uint64_t> total_acquisitions{0};      ///< Total acquisition attempts
	std::atomic<uint64_t> successful_acquisitions{0}; ///< Successful acquisitions
	std::atomic<uint64_t> failed_acquisitions{0};     ///< Failed acquisitions
	std::atomic<uint64_t> pool_exhaustion_count{0};   ///< Pool exhaustion events

	// Timing statistics (nanoseconds)
	std::atomic<uint64_t> total_acquisition_time_ns{0}; ///< Total acquisition time

	/**
	 * @brief Record a connection acquisition
	 * @param acquisition_time_ns Time to acquire connection in nanoseconds
	 * @param success Whether acquisition succeeded
	 */
	void record_acquisition(uint64_t acquisition_time_ns, bool success)
	{
		total_acquisitions.fetch_add(1, std::memory_order_relaxed);
		total_acquisition_time_ns.fetch_add(acquisition_time_ns, std::memory_order_relaxed);

		if (success)
		{
			successful_acquisitions.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			failed_acquisitions.fetch_add(1, std::memory_order_relaxed);
		}
	}

	/**
	 * @brief Record a pool exhaustion event
	 */
	void record_exhaustion()
	{
		pool_exhaustion_count.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Calculate average acquisition time in milliseconds
	 * @return Average acquisition time in milliseconds
	 */
	[[nodiscard]] double avg_acquisition_time_ms() const noexcept
	{
		uint64_t total = total_acquisitions.load(std::memory_order_relaxed);
		if (total == 0)
			return 0.0;
		return static_cast<double>(
				   total_acquisition_time_ns.load(std::memory_order_relaxed))
			   / static_cast<double>(total) / 1000000.0;
	}

	/**
	 * @brief Reset all metrics
	 */
	void reset() noexcept
	{
		total_acquisitions.store(0, std::memory_order_relaxed);
		successful_acquisitions.store(0, std::memory_order_relaxed);
		failed_acquisitions.store(0, std::memory_order_relaxed);
		pool_exhaustion_count.store(0, std::memory_order_relaxed);
		total_acquisition_time_ns.store(0, std::memory_order_relaxed);
		// Don't reset connection counts (they reflect current state)
	}
};

/**
 * @struct session_performance_metrics
 * @brief Metrics for tracking session management
 *
 * Provides atomic counters for session lifecycle tracking
 * including creation, termination, and duration statistics.
 */
struct session_performance_metrics
{
	std::atomic<uint64_t> active_sessions{0};   ///< Currently active sessions
	std::atomic<uint64_t> total_sessions{0};    ///< Total sessions created
	std::atomic<uint64_t> terminated_sessions{0}; ///< Sessions terminated

	// Authentication statistics
	std::atomic<uint64_t> auth_successes{0}; ///< Successful authentications
	std::atomic<uint64_t> auth_failures{0};  ///< Failed authentications

	// Duration statistics (nanoseconds)
	std::atomic<uint64_t> total_session_duration_ns{0}; ///< Total session duration

	/**
	 * @brief Record a new session
	 */
	void record_session_start()
	{
		active_sessions.fetch_add(1, std::memory_order_relaxed);
		total_sessions.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Record session termination
	 * @param duration_ns Session duration in nanoseconds
	 */
	void record_session_end(uint64_t duration_ns)
	{
		active_sessions.fetch_sub(1, std::memory_order_relaxed);
		terminated_sessions.fetch_add(1, std::memory_order_relaxed);
		total_session_duration_ns.fetch_add(duration_ns, std::memory_order_relaxed);
	}

	/**
	 * @brief Record authentication result
	 * @param success Whether authentication succeeded
	 */
	void record_auth(bool success)
	{
		if (success)
		{
			auth_successes.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			auth_failures.fetch_add(1, std::memory_order_relaxed);
		}
	}

	/**
	 * @brief Calculate average session duration in seconds
	 * @return Average session duration in seconds
	 */
	[[nodiscard]] double avg_session_duration_sec() const noexcept
	{
		uint64_t terminated = terminated_sessions.load(std::memory_order_relaxed);
		if (terminated == 0)
			return 0.0;
		return static_cast<double>(
				   total_session_duration_ns.load(std::memory_order_relaxed))
			   / static_cast<double>(terminated) / 1000000000.0;
	}

	/**
	 * @brief Reset all metrics
	 */
	void reset() noexcept
	{
		total_sessions.store(0, std::memory_order_relaxed);
		terminated_sessions.store(0, std::memory_order_relaxed);
		auth_successes.store(0, std::memory_order_relaxed);
		auth_failures.store(0, std::memory_order_relaxed);
		total_session_duration_ns.store(0, std::memory_order_relaxed);
		// Don't reset active_sessions (it reflects current state)
	}
};

/**
 * @struct query_server_metrics
 * @brief Aggregated metrics for the entire query server
 *
 * Combines all metric categories into a single structure
 * for comprehensive server monitoring.
 */
struct query_server_metrics
{
	query_execution_metrics query_metrics;   ///< Query execution statistics
	cache_performance_metrics cache_metrics; ///< Cache performance statistics
	pool_performance_metrics pool_metrics;   ///< Connection pool statistics
	session_performance_metrics session_metrics; ///< Session management statistics

	/**
	 * @brief Reset all metrics
	 */
	void reset() noexcept
	{
		query_metrics.reset();
		cache_metrics.reset();
		pool_metrics.reset();
		session_metrics.reset();
	}
};

} // namespace database_server::metrics
