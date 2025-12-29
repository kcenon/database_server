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
 * @file query_cache.h
 * @brief Query result cache with LRU eviction and TTL expiration
 *
 * Provides an optional in-memory cache for SELECT query results to improve
 * performance for frequently executed queries. Features include:
 * - LRU (Least Recently Used) eviction policy
 * - TTL (Time-To-Live) based expiration
 * - Automatic invalidation on write operations
 * - Thread-safe operation with reader/writer locks
 * - Configurable size limits and expiration times
 */

#pragma once

#include "query_protocol.h"
#include "query_types.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace database_server::gateway
{

/**
 * @struct cache_config
 * @brief Configuration for query cache behavior
 */
struct cache_config
{
	bool enabled = false;                      ///< Enable/disable cache
	size_t max_entries = 10000;                ///< Maximum number of cached entries
	uint32_t ttl_seconds = 300;                ///< Time-to-live in seconds (0 = no expiration)
	size_t max_result_size_bytes = 1024 * 1024; ///< Max size of single result (1MB default)
	bool enable_lru = true;                    ///< Enable LRU eviction (vs random)
};

/**
 * @struct cache_metrics
 * @brief Statistics for cache performance monitoring
 */
struct cache_metrics
{
	std::atomic<uint64_t> hits{0};           ///< Cache hit count
	std::atomic<uint64_t> misses{0};         ///< Cache miss count
	std::atomic<uint64_t> evictions{0};      ///< LRU eviction count
	std::atomic<uint64_t> expirations{0};    ///< TTL expiration count
	std::atomic<uint64_t> invalidations{0};  ///< Manual invalidation count
	std::atomic<uint64_t> puts{0};           ///< Total put operations
	std::atomic<uint64_t> skipped_too_large{0}; ///< Entries skipped due to size

	/**
	 * @brief Calculate cache hit rate
	 * @return Hit rate as percentage (0.0 - 100.0)
	 */
	[[nodiscard]] double hit_rate() const noexcept
	{
		auto total = hits.load() + misses.load();
		if (total == 0) return 0.0;
		return static_cast<double>(hits.load()) / static_cast<double>(total) * 100.0;
	}

	/**
	 * @brief Reset all metrics counters
	 */
	void reset() noexcept
	{
		hits.store(0);
		misses.store(0);
		evictions.store(0);
		expirations.store(0);
		invalidations.store(0);
		puts.store(0);
		skipped_too_large.store(0);
	}
};

/**
 * @class query_cache
 * @brief Thread-safe LRU cache for query results with TTL support
 *
 * Implements an efficient cache using a combination of:
 * - std::list for O(1) LRU ordering
 * - std::unordered_map for O(1) key lookup
 * - std::shared_mutex for concurrent read access
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Read operations use shared locks for concurrency
 * - Write operations use exclusive locks
 *
 * Usage Example:
 * @code
 * cache_config config;
 * config.enabled = true;
 * config.max_entries = 1000;
 * config.ttl_seconds = 60;
 *
 * query_cache cache(config);
 *
 * // Cache a query result
 * std::string key = query_cache::make_key(request);
 * cache.put(key, response, {"users"});
 *
 * // Retrieve from cache
 * auto cached = cache.get(key);
 * if (cached) {
 *     // Use cached response
 * }
 *
 * // Invalidate on write
 * cache.invalidate("users");
 * @endcode
 */
class query_cache
{
public:
	/**
	 * @brief Constructs a query cache with configuration
	 * @param config Cache configuration
	 */
	explicit query_cache(const cache_config& config = cache_config{});

	/**
	 * @brief Destructor
	 */
	~query_cache() = default;

	// Non-copyable, non-movable
	query_cache(const query_cache&) = delete;
	query_cache& operator=(const query_cache&) = delete;
	query_cache(query_cache&&) = delete;
	query_cache& operator=(query_cache&&) = delete;

	/**
	 * @brief Get a cached query response
	 * @param cache_key The cache key to look up
	 * @return Cached response if found and valid, std::nullopt otherwise
	 *
	 * Returns std::nullopt if:
	 * - Cache is disabled
	 * - Key not found
	 * - Entry has expired (TTL exceeded)
	 */
	[[nodiscard]] std::optional<query_response> get(const std::string& cache_key);

	/**
	 * @brief Store a query response in the cache
	 * @param cache_key The cache key
	 * @param response The response to cache
	 * @param table_names Tables referenced by this query (for invalidation)
	 *
	 * The entry will not be cached if:
	 * - Cache is disabled
	 * - Response size exceeds max_result_size_bytes
	 * - Response indicates an error (non-OK status)
	 */
	void put(const std::string& cache_key,
			 const query_response& response,
			 const std::unordered_set<std::string>& table_names = {});

	/**
	 * @brief Generate a cache key from a query request
	 * @param request The query request
	 * @return Unique cache key based on SQL and parameters
	 *
	 * The key is generated by hashing:
	 * - SQL query string
	 * - Parameter values in order
	 * - Query options that affect results
	 */
	[[nodiscard]] static std::string make_key(const query_request& request);

	/**
	 * @brief Invalidate all cache entries for a table
	 * @param table_name Name of the table to invalidate
	 *
	 * Called when a write operation (INSERT/UPDATE/DELETE) occurs
	 * on a table to ensure cached SELECT results are refreshed.
	 */
	void invalidate(const std::string& table_name);

	/**
	 * @brief Invalidate a specific cache entry
	 * @param cache_key The cache key to invalidate
	 */
	void invalidate_key(const std::string& cache_key);

	/**
	 * @brief Clear all cached entries
	 */
	void clear();

	/**
	 * @brief Get current cache metrics
	 * @return Reference to metrics structure
	 */
	[[nodiscard]] const cache_metrics& metrics() const noexcept;

	/**
	 * @brief Reset cache metrics
	 */
	void reset_metrics();

	/**
	 * @brief Get current number of cached entries
	 * @return Number of entries in cache
	 */
	[[nodiscard]] size_t size() const noexcept;

	/**
	 * @brief Check if cache is enabled
	 * @return true if caching is enabled
	 */
	[[nodiscard]] bool is_enabled() const noexcept;

	/**
	 * @brief Get cache configuration
	 * @return Current configuration
	 */
	[[nodiscard]] const cache_config& config() const noexcept;

private:
	/**
	 * @struct cache_entry
	 * @brief Internal structure for cached items
	 */
	struct cache_entry
	{
		std::string key;                        ///< Cache key
		query_response response;                ///< Cached response
		std::chrono::steady_clock::time_point expires_at; ///< Expiration time
		std::unordered_set<std::string> tables; ///< Referenced tables
		size_t estimated_size = 0;              ///< Estimated memory size
	};

	using cache_list = std::list<cache_entry>;
	using cache_map = std::unordered_map<std::string, cache_list::iterator>;
	using table_map = std::unordered_map<std::string, std::unordered_set<std::string>>;

	/**
	 * @brief Check if an entry has expired
	 */
	[[nodiscard]] bool is_expired(const cache_entry& entry) const noexcept;

	/**
	 * @brief Evict the least recently used entry
	 */
	void evict_lru();

	/**
	 * @brief Estimate the size of a response in bytes
	 */
	[[nodiscard]] static size_t estimate_size(const query_response& response);

	/**
	 * @brief Remove an entry from the cache (internal)
	 */
	void remove_entry(cache_list::iterator it);

private:
	cache_config config_;
	mutable std::shared_mutex mutex_;

	cache_list lru_list_;   ///< LRU ordering (front = most recent)
	cache_map cache_map_;   ///< Key to list iterator mapping
	table_map table_map_;   ///< Table to cache keys mapping

	cache_metrics metrics_;
};

} // namespace database_server::gateway
