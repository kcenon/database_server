// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file database_server.cppm
 * @brief Primary C++20 module for database_server.
 *
 * This is the main module interface for the database_server library.
 * It aggregates all module partitions to provide a single import point.
 *
 * Usage:
 * @code
 * import kcenon.database_server;
 *
 * using namespace database_server;
 *
 * // Create server application
 * server_app app;
 * if (auto result = app.initialize("config.yaml"); !result) {
 *     return 1;
 * }
 * return app.run();
 * @endcode
 *
 * Module Structure:
 * - kcenon.database_server:core - Server app and configuration
 * - kcenon.database_server:gateway - Query protocol and routing
 * - kcenon.database_server:pooling - Connection pool management
 * - kcenon.database_server:resilience - Health monitoring and recovery
 *
 * Dependencies:
 * - kcenon.common (Tier 0) - Result<T>, IExecutor
 * - kcenon.database (Tier 2) - Database interfaces
 * - kcenon.network (Tier 4) - TCP server
 * - kcenon.thread (Tier 1) - Thread pool
 * - kcenon.container (Tier 1) - Serialization
 */

export module kcenon.database_server;

import kcenon.common;

// Tier 1: Core server components
export import :core;

// Tier 2: Gateway and protocol handling
export import :gateway;

// Tier 3: Connection pooling
export import :pooling;

// Tier 4: Resilience and health monitoring
export import :resilience;

// Tier 5: Metrics collection
export import :metrics;

export namespace database_server {

/**
 * @brief Version information for database_server module.
 */
struct module_version {
    static constexpr int major = 0;
    static constexpr int minor = 1;
    static constexpr int patch = 0;
    static constexpr int tweak = 0;
    static constexpr const char* string = "0.1.0.0";
    static constexpr const char* module_name = "kcenon.database_server";
};

} // namespace database_server
