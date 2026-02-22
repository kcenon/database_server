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
 * @file metrics_base.h
 * @brief Common utilities for atomic metrics operations
 *
 * Provides reusable utility functions for metrics structs to eliminate
 * code duplication across query_metrics.h and pool_metrics.h.
 *
 * Utility categories:
 * - Atomic reset operations
 * - Compare-and-swap min/max updates
 * - Average and rate calculations
 *
 * Related to: #58 Extract common base class for metrics structs
 *
 * ## Thread Safety
 * All functions are static and operate on `std::atomic` parameters passed
 * by the caller. The CAS-based `update_min` / `update_max` functions are
 * lock-free and safe for concurrent calls. Pure calculation functions
 * (`average_ns_to_ms`, `calculate_rate`, etc.) are stateless and thread-safe.
 *
 * @code
 * using namespace database_server::metrics;
 *
 * std::atomic<uint64_t> min_latency{UINT64_MAX};
 * std::atomic<uint64_t> max_latency{0};
 *
 * // Thread-safe min/max update (CAS loop)
 * metrics_utils::update_min_max(min_latency, max_latency, 1500);
 *
 * // Calculate average (stateless)
 * double avg_ms = metrics_utils::average_ns_to_ms(total_ns, count);
 * double rate = metrics_utils::calculate_rate(successes, total);
 * @endcode
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace database_server::metrics
{

/**
 * @struct metrics_utils
 * @brief Static utility functions for atomic metrics operations
 *
 * Provides common operations used across all metrics structs:
 * - Atomic counter reset
 * - Thread-safe min/max value updates using CAS loops
 * - Average and rate calculations with divide-by-zero protection
 */
struct metrics_utils
{
	/**
	 * @brief Reset an atomic counter to zero
	 * @param counter The atomic counter to reset
	 * @param order Memory ordering (default: relaxed)
	 */
	static void reset_counter(std::atomic<uint64_t>& counter,
							  std::memory_order order = std::memory_order_relaxed) noexcept
	{
		counter.store(0, order);
	}

	/**
	 * @brief Reset a minimum latency tracker to max value
	 * @param min_value The atomic minimum value to reset
	 * @param order Memory ordering (default: relaxed)
	 */
	static void reset_min(std::atomic<uint64_t>& min_value,
						  std::memory_order order = std::memory_order_relaxed) noexcept
	{
		min_value.store(std::numeric_limits<uint64_t>::max(), order);
	}

	/**
	 * @brief Update minimum value using compare-and-swap
	 *
	 * Thread-safe update that only stores the new value if it's smaller
	 * than the current minimum.
	 *
	 * @param min_value The atomic minimum value to update
	 * @param new_value The candidate new minimum
	 * @param order Memory ordering for CAS success (default: relaxed)
	 */
	static void update_min(std::atomic<uint64_t>& min_value, uint64_t new_value,
						   std::memory_order order = std::memory_order_relaxed) noexcept
	{
		uint64_t current = min_value.load(std::memory_order_relaxed);
		while (new_value < current
			   && !min_value.compare_exchange_weak(current, new_value, order,
												   std::memory_order_relaxed))
		{
		}
	}

	/**
	 * @brief Update maximum value using compare-and-swap
	 *
	 * Thread-safe update that only stores the new value if it's larger
	 * than the current maximum.
	 *
	 * @param max_value The atomic maximum value to update
	 * @param new_value The candidate new maximum
	 * @param order Memory ordering for CAS success (default: relaxed)
	 */
	static void update_max(std::atomic<uint64_t>& max_value, uint64_t new_value,
						   std::memory_order order = std::memory_order_relaxed) noexcept
	{
		uint64_t current = max_value.load(std::memory_order_relaxed);
		while (new_value > current
			   && !max_value.compare_exchange_weak(current, new_value, order,
												   std::memory_order_relaxed))
		{
		}
	}

	/**
	 * @brief Update both min and max values atomically
	 *
	 * Convenience function that updates both minimum and maximum
	 * tracking values with a single new sample.
	 *
	 * @param min_value The atomic minimum value to update
	 * @param max_value The atomic maximum value to update
	 * @param new_value The new sample value
	 */
	static void update_min_max(std::atomic<uint64_t>& min_value,
							   std::atomic<uint64_t>& max_value,
							   uint64_t new_value) noexcept
	{
		update_min(min_value, new_value);
		update_max(max_value, new_value);
	}

	/**
	 * @brief Calculate average in nanoseconds converted to milliseconds
	 * @param total_ns Total accumulated nanoseconds
	 * @param count Number of samples
	 * @return Average in milliseconds, or 0.0 if count is zero
	 */
	[[nodiscard]] static double average_ns_to_ms(uint64_t total_ns,
												 uint64_t count) noexcept
	{
		if (count == 0)
		{
			return 0.0;
		}
		return static_cast<double>(total_ns) / static_cast<double>(count) / 1000000.0;
	}

	/**
	 * @brief Calculate average in nanoseconds converted to seconds
	 * @param total_ns Total accumulated nanoseconds
	 * @param count Number of samples
	 * @return Average in seconds, or 0.0 if count is zero
	 */
	[[nodiscard]] static double average_ns_to_sec(uint64_t total_ns,
												  uint64_t count) noexcept
	{
		if (count == 0)
		{
			return 0.0;
		}
		return static_cast<double>(total_ns) / static_cast<double>(count) / 1000000000.0;
	}

	/**
	 * @brief Calculate average in microseconds
	 * @param total_us Total accumulated microseconds
	 * @param count Number of samples
	 * @return Average in microseconds, or 0.0 if count is zero
	 */
	[[nodiscard]] static double average_us(uint64_t total_us, uint64_t count) noexcept
	{
		if (count == 0)
		{
			return 0.0;
		}
		return static_cast<double>(total_us) / static_cast<double>(count);
	}

	/**
	 * @brief Calculate percentage rate
	 * @param numerator The count of successful/target items
	 * @param denominator The total count
	 * @param default_value Value to return when denominator is zero (default: 100.0)
	 * @return Rate as percentage (0.0 - 100.0)
	 */
	[[nodiscard]] static double calculate_rate(uint64_t numerator, uint64_t denominator,
											   double default_value = 100.0) noexcept
	{
		if (denominator == 0)
		{
			return default_value;
		}
		return static_cast<double>(numerator) / static_cast<double>(denominator) * 100.0;
	}

	/**
	 * @brief Calculate ratio (not percentage)
	 * @param numerator The count of successful/target items
	 * @param denominator The total count
	 * @return Ratio (0.0 - 1.0), or 0.0 if denominator is zero
	 */
	[[nodiscard]] static double calculate_ratio(uint64_t numerator,
												uint64_t denominator) noexcept
	{
		if (denominator == 0)
		{
			return 0.0;
		}
		return static_cast<double>(numerator) / static_cast<double>(denominator);
	}
};

} // namespace database_server::metrics
