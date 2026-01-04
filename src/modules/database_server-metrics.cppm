// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file database_server-metrics.cppm
 * @brief Metrics module partition for database_server.
 *
 * This module partition provides CRTP-based metrics collection for
 * database query server performance monitoring.
 *
 * Key Components:
 * - query_metrics_collector: CRTP-based collector implementation
 * - query_server_metrics: Aggregated metrics structure
 * - collector_integration: Monitoring system integration
 *
 * Usage:
 * @code
 * import kcenon.database_server:metrics;
 *
 * using namespace database_server::metrics;
 *
 * // Get global collector
 * auto& collector = get_query_metrics_collector();
 *
 * // Record query execution
 * query_execution exec;
 * exec.query_type = "select";
 * exec.latency_ns = 1500000;
 * exec.success = true;
 * collector.collect_query_metrics(exec);
 *
 * // Get metrics
 * const auto& metrics = collector.get_metrics();
 * @endcode
 */

export module kcenon.database_server:metrics;

import kcenon.common;

// Re-export metrics types
export namespace database_server::metrics {

/**
 * @brief Forward declarations for metrics types
 * @note Full implementations are in header files
 */

// Core metrics structures
struct query_execution_metrics;
struct cache_performance_metrics;
struct pool_performance_metrics;
struct session_performance_metrics;
struct query_server_metrics;

// Collector types
struct collector_options;
struct query_execution;
struct pool_stats;
struct cache_stats;
struct session_stats;

class query_metrics_collector;

// Global collector access
query_metrics_collector& get_query_metrics_collector();
void set_query_metrics_collector(std::shared_ptr<query_metrics_collector> collector);

// Monitoring integration
bool initialize_monitoring_integration(const std::string& collector_name);
void export_metrics_to_monitoring();
bool is_monitoring_integration_initialized();
void shutdown_monitoring_integration();

} // namespace database_server::metrics
