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
 * - metrics_utils: Atomic metrics utility functions
 * - query_execution_metrics, cache_performance_metrics, etc.: Metrics structures
 * - query_collector_base: CRTP base class for collectors
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
 *
 * Part of the kcenon.database_server module.
 */

module;

// Standard library includes needed before module declaration
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

// Include existing headers in the global module fragment
#include "kcenon/database_server/metrics/query_metrics.h"
#include "kcenon/database_server/metrics/query_collector_base.h"
#include "kcenon/database_server/metrics/query_metrics_collector.h"

export module kcenon.database_server:metrics;

import kcenon.common;

// ============================================================================
// Metrics Utilities
// ============================================================================

export namespace database_server::metrics {

// Re-export metrics utility functions
using ::database_server::metrics::metrics_utils;

} // namespace database_server::metrics

// ============================================================================
// Metrics Structures
// ============================================================================

export namespace database_server::metrics {

// Re-export query execution metrics
using ::database_server::metrics::query_execution_metrics;

// Re-export cache performance metrics
using ::database_server::metrics::cache_performance_metrics;

// Re-export pool performance metrics
using ::database_server::metrics::pool_performance_metrics;

// Re-export session performance metrics
using ::database_server::metrics::session_performance_metrics;

// Re-export aggregated server metrics
using ::database_server::metrics::query_server_metrics;

} // namespace database_server::metrics

// ============================================================================
// Collector Base Types
// ============================================================================

export namespace database_server::metrics {

// Re-export type aliases
using ::database_server::metrics::collector_config;
using ::database_server::metrics::stats_map;

// Re-export collector input types
using ::database_server::metrics::query_execution;
using ::database_server::metrics::pool_stats;
using ::database_server::metrics::cache_stats;
using ::database_server::metrics::session_stats;

// Re-export CRTP base template
using ::database_server::metrics::query_collector_base;

} // namespace database_server::metrics

// ============================================================================
// Query Metrics Collector
// ============================================================================

export namespace database_server::metrics {

// Re-export collector configuration
using ::database_server::metrics::collector_options;

// Re-export metrics collector class
using ::database_server::metrics::query_metrics_collector;

// Re-export global collector access
using ::database_server::metrics::get_query_metrics_collector;
using ::database_server::metrics::set_query_metrics_collector;

} // namespace database_server::metrics

// ============================================================================
// Monitoring System Integration
// ============================================================================

export namespace database_server::metrics {

/**
 * @brief Initialize monitoring system integration
 * @param collector_name Name to use for this collector in monitoring system
 * @return true if initialization successful
 */
bool initialize_monitoring_integration(const std::string& collector_name);

/**
 * @brief Export current metrics to monitoring system
 */
void export_metrics_to_monitoring();

/**
 * @brief Check if monitoring integration is initialized
 * @return true if initialized
 */
bool is_monitoring_integration_initialized();

/**
 * @brief Shutdown monitoring integration
 */
void shutdown_monitoring_integration();

} // namespace database_server::metrics
