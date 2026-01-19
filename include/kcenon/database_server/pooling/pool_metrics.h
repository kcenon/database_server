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
 * @file pool_metrics.h
 * @brief Performance metrics for connection pools
 *
 * Provides tracking of connection pool performance including acquisition
 * latency, success rates, and priority-based statistics.
 */

#pragma once

#include "../metrics/metrics_base.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace database_server::pooling
{

using database_server::metrics::metrics_utils;

/**
 * @struct pool_metrics
 * @brief Performance metrics for connection pools
 *
 * Tracks key performance indicators for database connection management:
 * - Connection acquisition latency
 * - Queue wait times
 * - Throughput rates
 * - Priority-based statistics
 */
struct pool_metrics
{
	// Connection statistics
	std::atomic<uint64_t> total_acquisitions{ 0 };
	std::atomic<uint64_t> successful_acquisitions{ 0 };
	std::atomic<uint64_t> failed_acquisitions{ 0 };
	std::atomic<uint64_t> timeouts{ 0 };

	// Timing statistics (microseconds)
	std::atomic<uint64_t> total_wait_time_us{ 0 };
	std::atomic<uint64_t> min_wait_time_us{ UINT64_MAX };
	std::atomic<uint64_t> max_wait_time_us{ 0 };

	// Current state
	std::atomic<uint64_t> current_active{ 0 };
	std::atomic<uint64_t> current_queued{ 0 };
	std::atomic<uint64_t> peak_active{ 0 };
	std::atomic<uint64_t> peak_queued{ 0 };

	// Health check statistics
	std::atomic<uint64_t> health_checks_performed{ 0 };
	std::atomic<uint64_t> unhealthy_connections_removed{ 0 };

	/**
	 * @brief Record a connection acquisition
	 * @param wait_time_us Wait time in microseconds
	 * @param success Whether acquisition was successful
	 */
	void record_acquisition(uint64_t wait_time_us, bool success)
	{
		total_acquisitions.fetch_add(1, std::memory_order_relaxed);

		if (success)
		{
			successful_acquisitions.fetch_add(1, std::memory_order_relaxed);
			total_wait_time_us.fetch_add(wait_time_us, std::memory_order_relaxed);
			metrics_utils::update_min_max(min_wait_time_us, max_wait_time_us, wait_time_us);
		}
		else
		{
			failed_acquisitions.fetch_add(1, std::memory_order_relaxed);
		}
	}

	/**
	 * @brief Record a timeout event
	 */
	void record_timeout()
	{
		timeouts.fetch_add(1, std::memory_order_relaxed);
	}

	/**
	 * @brief Update current active connection count
	 * @param delta Change in active connections (+1 for acquire, -1 for release)
	 */
	void update_active(int delta)
	{
		uint64_t new_active
			= current_active.fetch_add(delta, std::memory_order_relaxed) + delta;
		metrics_utils::update_max(peak_active, new_active);
	}

	/**
	 * @brief Update current queued request count
	 * @param delta Change in queued requests
	 */
	void update_queued(int delta)
	{
		uint64_t new_queued
			= current_queued.fetch_add(delta, std::memory_order_relaxed) + delta;
		metrics_utils::update_max(peak_queued, new_queued);
	}

	/**
	 * @brief Record a health check operation
	 * @param removed_connections Number of unhealthy connections removed
	 */
	void record_health_check(uint64_t removed_connections = 0)
	{
		health_checks_performed.fetch_add(1, std::memory_order_relaxed);
		if (removed_connections > 0)
		{
			unhealthy_connections_removed.fetch_add(
				removed_connections, std::memory_order_relaxed);
		}
	}

	/**
	 * @brief Calculate average wait time
	 * @return Average wait time in microseconds
	 */
	[[nodiscard]] double average_wait_time_us() const
	{
		return metrics_utils::average_us(
			total_wait_time_us.load(std::memory_order_relaxed),
			total_acquisitions.load(std::memory_order_relaxed));
	}

	/**
	 * @brief Calculate success rate
	 * @return Success rate as percentage (0.0 - 100.0)
	 */
	[[nodiscard]] double success_rate() const
	{
		return metrics_utils::calculate_rate(
			successful_acquisitions.load(std::memory_order_relaxed),
			total_acquisitions.load(std::memory_order_relaxed), 100.0);
	}

	/**
	 * @brief Reset all metrics
	 */
	void reset()
	{
		metrics_utils::reset_counter(total_acquisitions);
		metrics_utils::reset_counter(successful_acquisitions);
		metrics_utils::reset_counter(failed_acquisitions);
		metrics_utils::reset_counter(timeouts);

		metrics_utils::reset_counter(total_wait_time_us);
		metrics_utils::reset_min(min_wait_time_us);
		metrics_utils::reset_counter(max_wait_time_us);

		// Don't reset current_active and current_queued (they reflect current state)
		peak_active.store(
			current_active.load(std::memory_order_relaxed), std::memory_order_relaxed);
		peak_queued.store(
			current_queued.load(std::memory_order_relaxed), std::memory_order_relaxed);

		metrics_utils::reset_counter(health_checks_performed);
		metrics_utils::reset_counter(unhealthy_connections_removed);
	}
};

/**
 * @struct priority_metrics
 * @brief Priority-specific metrics for typed thread pools
 *
 * Extends pool_metrics with priority-level statistics.
 * Tracks performance per connection_priority level.
 */
template <typename PriorityType>
struct priority_metrics : public pool_metrics
{
	using priority_type = PriorityType;

	// Per-priority statistics
	std::map<PriorityType, std::atomic<uint64_t>> acquisitions_by_priority;
	std::map<PriorityType, std::atomic<uint64_t>> wait_time_by_priority;

	mutable std::mutex map_mutex; // Protects map modifications

	/**
	 * @brief Record acquisition with priority tracking
	 * @param priority Request priority
	 * @param wait_time_us Wait time in microseconds
	 * @param success Whether acquisition was successful
	 */
	void record_acquisition_with_priority(
		PriorityType priority, uint64_t wait_time_us, bool success)
	{
		// Record base metrics
		record_acquisition(wait_time_us, success);

		if (success)
		{
			// Update priority-specific metrics
			std::lock_guard<std::mutex> lock(map_mutex);

			// Initialize if first time seeing this priority
			if (acquisitions_by_priority.find(priority)
				== acquisitions_by_priority.end())
			{
				acquisitions_by_priority[priority].store(0, std::memory_order_relaxed);
				wait_time_by_priority[priority].store(0, std::memory_order_relaxed);
			}

			acquisitions_by_priority[priority].fetch_add(1, std::memory_order_relaxed);
			wait_time_by_priority[priority].fetch_add(
				wait_time_us, std::memory_order_relaxed);
		}
	}

	/**
	 * @brief Get average wait time for a specific priority
	 * @param priority Priority level to query
	 * @return Average wait time in microseconds, or 0 if no data
	 */
	[[nodiscard]] double average_wait_time_for_priority(PriorityType priority) const
	{
		std::lock_guard<std::mutex> lock(map_mutex);

		auto acq_it = acquisitions_by_priority.find(priority);
		auto wait_it = wait_time_by_priority.find(priority);

		if (acq_it == acquisitions_by_priority.end()
			|| wait_it == wait_time_by_priority.end())
		{
			return 0.0;
		}

		return metrics_utils::average_us(
			wait_it->second.load(std::memory_order_relaxed),
			acq_it->second.load(std::memory_order_relaxed));
	}

	/**
	 * @brief Reset all metrics including priority-specific data
	 */
	void reset_all()
	{
		pool_metrics::reset();

		std::lock_guard<std::mutex> lock(map_mutex);
		for (auto& [priority, counter] : acquisitions_by_priority)
		{
			metrics_utils::reset_counter(counter);
		}
		for (auto& [priority, wait_time] : wait_time_by_priority)
		{
			metrics_utils::reset_counter(wait_time);
		}
	}
};

} // namespace database_server::pooling
