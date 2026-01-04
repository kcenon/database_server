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

#include <kcenon/database_server/metrics/query_metrics_collector.h>

#include <algorithm>
#include <memory>
#include <mutex>

namespace database_server::metrics
{

namespace
{
// Global collector instance
std::shared_ptr<query_metrics_collector> g_collector;
std::mutex g_collector_mutex;
} // namespace

query_metrics_collector::query_metrics_collector(const collector_options& options)
	: options_(options)
{
}

const query_execution_metrics& query_metrics_collector::query_metrics() const noexcept
{
	return metrics_.query_metrics;
}

const cache_performance_metrics& query_metrics_collector::cache_metrics() const noexcept
{
	return metrics_.cache_metrics;
}

const pool_performance_metrics& query_metrics_collector::pool_metrics() const noexcept
{
	return metrics_.pool_metrics;
}

const session_performance_metrics& query_metrics_collector::session_metrics() const noexcept
{
	return metrics_.session_metrics;
}

void query_metrics_collector::reset_metrics()
{
	std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
	metrics_.reset();
}

const collector_options& query_metrics_collector::options() const noexcept
{
	return options_;
}

bool query_metrics_collector::do_initialize(const collector_config& config)
{
	// Parse collector-specific configuration
	if (auto it = config.find("track_query_types"); it != config.end())
	{
		options_.track_query_types = (it->second == "true" || it->second == "1");
	}

	if (auto it = config.find("track_latency_histogram"); it != config.end())
	{
		options_.track_latency_histogram = (it->second == "true" || it->second == "1");
	}

	if (auto it = config.find("histogram_buckets"); it != config.end())
	{
		try
		{
			options_.histogram_buckets = static_cast<uint32_t>(std::stoul(it->second));
		}
		catch (...)
		{
			// Keep default value on parse error
		}
	}

	return true;
}

void query_metrics_collector::do_collect_query(const query_execution& exec)
{
	// Record basic query metrics (atomic operations, no lock needed)
	metrics_.query_metrics.record_query(exec.latency_ns, exec.success, exec.timeout);

	// Update query type counters if enabled
	if (options_.track_query_types)
	{
		update_query_type_counter(exec.query_type);
	}
}

void query_metrics_collector::do_collect_pool(const pool_stats& stats)
{
	// Update connection counts (atomic operations)
	metrics_.pool_metrics.active_connections.store(
		stats.active_connections, std::memory_order_relaxed);
	metrics_.pool_metrics.idle_connections.store(
		stats.idle_connections, std::memory_order_relaxed);
	metrics_.pool_metrics.total_connections.store(
		stats.total_connections, std::memory_order_relaxed);

	// Record acquisition if applicable
	if (stats.acquisition_time_ns > 0)
	{
		metrics_.pool_metrics.record_acquisition(
			stats.acquisition_time_ns, stats.acquisition_success);
	}

	// Record pool exhaustion
	if (stats.pool_exhausted)
	{
		metrics_.pool_metrics.record_exhaustion();
	}
}

void query_metrics_collector::do_collect_cache(const cache_stats& stats)
{
	// Record hit or miss
	if (stats.hit)
	{
		metrics_.cache_metrics.record_hit();
	}
	else
	{
		metrics_.cache_metrics.record_miss();
	}

	// Record eviction
	if (stats.eviction)
	{
		metrics_.cache_metrics.record_eviction();
	}

	// Record expiration
	if (stats.expiration)
	{
		metrics_.cache_metrics.record_expiration();
	}

	// Update cache size
	metrics_.cache_metrics.cache_size_bytes.store(
		stats.size_bytes, std::memory_order_relaxed);
	metrics_.cache_metrics.cache_entries.store(
		stats.entry_count, std::memory_order_relaxed);
}

void query_metrics_collector::do_collect_session(const session_stats& stats)
{
	// Record session start
	if (stats.session_start)
	{
		metrics_.session_metrics.record_session_start();
	}

	// Record session end
	if (stats.session_end)
	{
		metrics_.session_metrics.record_session_end(stats.duration_ns);
	}

	// Record authentication
	if (stats.auth_attempted)
	{
		metrics_.session_metrics.record_auth(stats.auth_success);
	}
}

bool query_metrics_collector::is_available() const
{
	// Collector is always available once initialized
	return true;
}

void query_metrics_collector::do_add_statistics(stats_map& stats) const
{
	std::shared_lock<std::shared_mutex> lock(metrics_mutex_);

	// Query metrics
	stats["total_queries"] = static_cast<double>(
		metrics_.query_metrics.total_queries.load(std::memory_order_relaxed));
	stats["successful_queries"] = static_cast<double>(
		metrics_.query_metrics.successful_queries.load(std::memory_order_relaxed));
	stats["failed_queries"] = static_cast<double>(
		metrics_.query_metrics.failed_queries.load(std::memory_order_relaxed));
	stats["timeout_queries"] = static_cast<double>(
		metrics_.query_metrics.timeout_queries.load(std::memory_order_relaxed));
	stats["avg_query_latency_ms"] = metrics_.query_metrics.avg_query_latency_ms();
	stats["query_success_rate"] = metrics_.query_metrics.success_rate();

	// Cache metrics
	stats["cache_hits"] = static_cast<double>(
		metrics_.cache_metrics.cache_hits.load(std::memory_order_relaxed));
	stats["cache_misses"] = static_cast<double>(
		metrics_.cache_metrics.cache_misses.load(std::memory_order_relaxed));
	stats["cache_hit_ratio"] = metrics_.cache_metrics.cache_hit_ratio();
	stats["cache_evictions"] = static_cast<double>(
		metrics_.cache_metrics.cache_evictions.load(std::memory_order_relaxed));

	// Pool metrics
	stats["active_connections"] = static_cast<double>(
		metrics_.pool_metrics.active_connections.load(std::memory_order_relaxed));
	stats["idle_connections"] = static_cast<double>(
		metrics_.pool_metrics.idle_connections.load(std::memory_order_relaxed));
	stats["pool_exhaustion_count"] = static_cast<double>(
		metrics_.pool_metrics.pool_exhaustion_count.load(std::memory_order_relaxed));
	stats["avg_acquisition_time_ms"] = metrics_.pool_metrics.avg_acquisition_time_ms();

	// Session metrics
	stats["active_sessions"] = static_cast<double>(
		metrics_.session_metrics.active_sessions.load(std::memory_order_relaxed));
	stats["total_sessions"] = static_cast<double>(
		metrics_.session_metrics.total_sessions.load(std::memory_order_relaxed));
	stats["avg_session_duration_sec"] = metrics_.session_metrics.avg_session_duration_sec();
}

const query_server_metrics& query_metrics_collector::do_get_metrics() const noexcept
{
	return metrics_;
}

void query_metrics_collector::update_query_type_counter(const std::string& query_type)
{
	// Convert to lowercase for comparison
	std::string type_lower = query_type;
	std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
				   [](unsigned char c) { return std::tolower(c); });

	if (type_lower == "select")
	{
		metrics_.query_metrics.select_queries.fetch_add(1, std::memory_order_relaxed);
	}
	else if (type_lower == "insert")
	{
		metrics_.query_metrics.insert_queries.fetch_add(1, std::memory_order_relaxed);
	}
	else if (type_lower == "update")
	{
		metrics_.query_metrics.update_queries.fetch_add(1, std::memory_order_relaxed);
	}
	else if (type_lower == "delete")
	{
		metrics_.query_metrics.delete_queries.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		metrics_.query_metrics.other_queries.fetch_add(1, std::memory_order_relaxed);
	}
}

query_metrics_collector& get_query_metrics_collector()
{
	std::lock_guard<std::mutex> lock(g_collector_mutex);
	if (!g_collector)
	{
		g_collector = std::make_shared<query_metrics_collector>();
	}
	return *g_collector;
}

void set_query_metrics_collector(std::shared_ptr<query_metrics_collector> collector)
{
	std::lock_guard<std::mutex> lock(g_collector_mutex);
	g_collector = std::move(collector);
}

} // namespace database_server::metrics
