// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file database_server-resilience.cppm
 * @brief C++20 module partition for database_server resilience components.
 *
 * This module partition exports resilience components:
 * - health_status, health_check_config: Health monitoring configuration
 * - connection_health_monitor: Heartbeat-based health tracking
 * - reconnection_config, connection_state: Reconnection settings
 * - resilient_database_connection: Auto-reconnecting connection wrapper
 *
 * Part of the kcenon.database_server module.
 */

module;

// Standard library includes needed before module declaration
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Include existing headers in the global module fragment
#include "kcenon/database_server/resilience/connection_health_monitor.h"
#include "kcenon/database_server/resilience/resilient_database_connection.h"

export module kcenon.database_server:resilience;

import kcenon.common;

// ============================================================================
// Health Monitoring
// ============================================================================

export namespace database_server::resilience {

// Re-export health status
using ::database_server::resilience::health_status;

// Re-export health check configuration
using ::database_server::resilience::health_check_config;

// Re-export connection health monitor
using ::database_server::resilience::connection_health_monitor;

} // namespace database_server::resilience

// ============================================================================
// Resilient Connection
// ============================================================================

export namespace database_server::resilience {

// Re-export reconnection configuration
using ::database_server::resilience::reconnection_config;

// Re-export connection state enumeration
using ::database_server::resilience::connection_state;

// Re-export to_string for connection_state
using ::database_server::resilience::to_string;

// Re-export resilient database connection
using ::database_server::resilience::resilient_database_connection;

} // namespace database_server::resilience
