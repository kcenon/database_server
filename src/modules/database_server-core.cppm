// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file database_server-core.cppm
 * @brief C++20 module partition for database_server core components.
 *
 * This module partition exports core server components:
 * - server_config: Configuration structures
 * - server_app: Main server application class
 * - server_state: Server state enumeration
 *
 * Part of the kcenon.database_server module.
 */

module;

// Standard library includes needed before module declaration
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Include existing headers in the global module fragment
#include "kcenon/database_server/core/server_config.h"
#include "kcenon/database_server/server_app.h"

export module kcenon.database_server:core;

import kcenon.common;

// ============================================================================
// Configuration Structures
// ============================================================================

export namespace database_server {

// Re-export configuration structures
using ::database_server::network_config;
using ::database_server::logging_config;
using ::database_server::pool_config;
using ::database_server::query_cache_config;
using ::database_server::server_config;

} // namespace database_server

// ============================================================================
// Server Application
// ============================================================================

export namespace database_server {

// Re-export server state enumeration
using ::database_server::server_state;

// Re-export server application class
using ::database_server::server_app;

} // namespace database_server
