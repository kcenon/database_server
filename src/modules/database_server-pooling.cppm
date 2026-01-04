// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file database_server-pooling.cppm
 * @brief C++20 module partition for database_server pooling components.
 *
 * This module partition exports connection pooling components:
 * - connection_pool_config, connection_stats: Configuration and statistics
 * - connection_wrapper: Connection wrapper with metadata
 * - connection_pool_base, connection_pool: Pool implementations
 * - connection_priority: Priority levels for connection acquisition
 * - pool_metrics, priority_metrics: Performance metrics
 * - connection_acquisition_job: Typed job for priority-based scheduling
 *
 * Part of the kcenon.database_server module.
 */

module;

// Standard library includes needed before module declaration
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

// Include existing headers in the global module fragment
#include "kcenon/database_server/pooling/connection_types.h"
#include "kcenon/database_server/pooling/connection_priority.h"
#include "kcenon/database_server/pooling/pool_metrics.h"
#include "kcenon/database_server/pooling/connection_pool.h"

export module kcenon.database_server:pooling;

import kcenon.common;

// ============================================================================
// Connection Types (from database namespace)
// ============================================================================

export namespace database {

// Re-export connection pool configuration
using ::database::connection_pool_config;

// Re-export connection statistics
using ::database::connection_stats;

// Re-export connection wrapper
using ::database::connection_wrapper;

// Re-export connection pool base interface
using ::database::connection_pool_base;

// Re-export connection pool implementation
using ::database::connection_pool;

} // namespace database

// ============================================================================
// Connection Priority
// ============================================================================

export namespace database_server::pooling {

// Re-export connection priority type alias
using ::database_server::pooling::connection_priority;

// Re-export priority constants
using ::database_server::pooling::PRIORITY_CRITICAL;
using ::database_server::pooling::PRIORITY_TRANSACTION;
using ::database_server::pooling::PRIORITY_NORMAL_QUERY;
using ::database_server::pooling::PRIORITY_HEALTH_CHECK;

// Re-export priority to string function
using ::database_server::pooling::priority_to_string;

} // namespace database_server::pooling

// ============================================================================
// Pool Metrics
// ============================================================================

export namespace database_server::pooling {

// Re-export pool metrics
using ::database_server::pooling::pool_metrics;

// Re-export priority metrics template
using ::database_server::pooling::priority_metrics;

} // namespace database_server::pooling

// ============================================================================
// Connection Pool (Server-side with adaptive queue)
// ============================================================================

export namespace database_server::pooling {

// Re-export connection acquisition job
using ::database_server::pooling::connection_acquisition_job;

// Re-export connection pool (server-side implementation)
using ::database_server::pooling::connection_pool;

} // namespace database_server::pooling
