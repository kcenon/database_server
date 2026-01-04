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
 * @file collector_integration.cpp
 * @brief Integration layer between query_metrics_collector and monitoring_system
 *
 * Provides bridge functionality to expose database_server metrics through
 * the monitoring_system infrastructure. This enables unified metrics
 * collection and reporting across the entire system.
 */

#include <kcenon/database_server/metrics/query_metrics_collector.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace database_server::metrics
{

namespace
{

/**
 * @brief Metric data structure for monitoring system integration
 */
struct monitoring_metric
{
	std::string name;
	double value;
	std::chrono::system_clock::time_point timestamp;
	std::unordered_map<std::string, std::string> tags;
};

/**
 * @brief Integration state
 */
struct integration_state
{
	bool initialized{false};
	std::string collector_name{"database_server"};
	std::function<void(const std::vector<monitoring_metric>&)> export_callback;
	std::mutex mutex;
};

integration_state g_integration_state;

} // namespace

/**
 * @brief Initialize monitoring system integration
 * @param collector_name Name to use for this collector in monitoring system
 * @return true if initialization successful
 */
bool initialize_monitoring_integration(const std::string& collector_name)
{
	std::lock_guard<std::mutex> lock(g_integration_state.mutex);

	if (g_integration_state.initialized)
	{
		return true;
	}

	g_integration_state.collector_name = collector_name;
	g_integration_state.initialized = true;

	return true;
}

/**
 * @brief Set callback for exporting metrics to monitoring system
 * @param callback Function to call with collected metrics
 */
void set_metrics_export_callback(
	std::function<void(const std::vector<monitoring_metric>&)> callback)
{
	std::lock_guard<std::mutex> lock(g_integration_state.mutex);
	g_integration_state.export_callback = std::move(callback);
}

/**
 * @brief Export current metrics to monitoring system
 *
 * Collects all current metrics from the query_metrics_collector
 * and exports them through the configured callback.
 */
void export_metrics_to_monitoring()
{
	std::lock_guard<std::mutex> lock(g_integration_state.mutex);

	if (!g_integration_state.initialized || !g_integration_state.export_callback)
	{
		return;
	}

	auto& collector = get_query_metrics_collector();
	const auto& metrics = collector.get_metrics();
	const auto now = std::chrono::system_clock::now();

	std::vector<monitoring_metric> exported_metrics;
	exported_metrics.reserve(20);

	std::unordered_map<std::string, std::string> base_tags = {
		{"collector", g_integration_state.collector_name},
		{"component", "database_server"}
	};

	// Query execution metrics
	exported_metrics.push_back({
		"database_server.queries.total",
		static_cast<double>(
			metrics.query_metrics.total_queries.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.queries.successful",
		static_cast<double>(
			metrics.query_metrics.successful_queries.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.queries.failed",
		static_cast<double>(
			metrics.query_metrics.failed_queries.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.queries.timeout",
		static_cast<double>(
			metrics.query_metrics.timeout_queries.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.queries.latency_avg_ms",
		metrics.query_metrics.avg_query_latency_ms(),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.queries.success_rate",
		metrics.query_metrics.success_rate(),
		now,
		base_tags
	});

	// Cache metrics
	exported_metrics.push_back({
		"database_server.cache.hits",
		static_cast<double>(
			metrics.cache_metrics.cache_hits.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.cache.misses",
		static_cast<double>(
			metrics.cache_metrics.cache_misses.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.cache.hit_ratio",
		metrics.cache_metrics.cache_hit_ratio(),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.cache.evictions",
		static_cast<double>(
			metrics.cache_metrics.cache_evictions.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.cache.size_bytes",
		static_cast<double>(
			metrics.cache_metrics.cache_size_bytes.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	// Pool metrics
	exported_metrics.push_back({
		"database_server.pool.active_connections",
		static_cast<double>(
			metrics.pool_metrics.active_connections.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.pool.idle_connections",
		static_cast<double>(
			metrics.pool_metrics.idle_connections.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.pool.total_connections",
		static_cast<double>(
			metrics.pool_metrics.total_connections.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.pool.exhaustion_count",
		static_cast<double>(
			metrics.pool_metrics.pool_exhaustion_count.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.pool.acquisition_time_avg_ms",
		metrics.pool_metrics.avg_acquisition_time_ms(),
		now,
		base_tags
	});

	// Session metrics
	exported_metrics.push_back({
		"database_server.sessions.active",
		static_cast<double>(
			metrics.session_metrics.active_sessions.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.sessions.total",
		static_cast<double>(
			metrics.session_metrics.total_sessions.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.sessions.duration_avg_sec",
		metrics.session_metrics.avg_session_duration_sec(),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.sessions.auth_successes",
		static_cast<double>(
			metrics.session_metrics.auth_successes.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	exported_metrics.push_back({
		"database_server.sessions.auth_failures",
		static_cast<double>(
			metrics.session_metrics.auth_failures.load(std::memory_order_relaxed)),
		now,
		base_tags
	});

	// Export through callback
	g_integration_state.export_callback(exported_metrics);
}

/**
 * @brief Get all metrics as a map for health endpoint
 * @return Map of metric name to value
 */
std::unordered_map<std::string, double> get_metrics_for_health_endpoint()
{
	auto& collector = get_query_metrics_collector();
	return collector.get_statistics();
}

/**
 * @brief Check if monitoring integration is initialized
 * @return true if initialized
 */
bool is_monitoring_integration_initialized()
{
	std::lock_guard<std::mutex> lock(g_integration_state.mutex);
	return g_integration_state.initialized;
}

/**
 * @brief Shutdown monitoring integration
 */
void shutdown_monitoring_integration()
{
	std::lock_guard<std::mutex> lock(g_integration_state.mutex);
	g_integration_state.initialized = false;
	g_integration_state.export_callback = nullptr;
}

} // namespace database_server::metrics
