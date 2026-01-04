// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file database_server-gateway.cppm
 * @brief C++20 module partition for database_server gateway components.
 *
 * This module partition exports gateway components:
 * - query_type, status_code: Protocol type definitions
 * - message_header, auth_token: Protocol message structures
 * - query_request, query_response: Query protocol messages
 * - gateway_server, gateway_config: TCP server handling
 * - query_router, router_config: Query routing with load balancing
 * - query_cache, cache_config: Query result caching
 * - auth_middleware, auth_config: Authentication and rate limiting
 * - generate_session_id: Session ID generation
 *
 * Part of the kcenon.database_server module.
 */

module;

// Standard library includes needed before module declaration
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

// Include existing headers in the global module fragment
#include "kcenon/database_server/gateway/query_types.h"
#include "kcenon/database_server/gateway/query_protocol.h"
#include "kcenon/database_server/gateway/query_handler_base.h"
#include "kcenon/database_server/gateway/query_handlers.h"
#include "kcenon/database_server/gateway/auth_middleware.h"
#include "kcenon/database_server/gateway/query_cache.h"
#include "kcenon/database_server/gateway/query_router.h"
#include "kcenon/database_server/gateway/gateway_server.h"
#include "kcenon/database_server/gateway/session_id_generator.h"

export module kcenon.database_server:gateway;

import kcenon.common;

// ============================================================================
// Query Types and Status Codes
// ============================================================================

export namespace database_server::gateway {

// Re-export query type enumeration
using ::database_server::gateway::query_type;

// Re-export status code enumeration
using ::database_server::gateway::status_code;

// Re-export to_string functions
using ::database_server::gateway::to_string;
using ::database_server::gateway::parse_query_type;

} // namespace database_server::gateway

// ============================================================================
// Protocol Message Structures
// ============================================================================

export namespace database_server::gateway {

// Re-export message header
using ::database_server::gateway::message_header;

// Re-export authentication token
using ::database_server::gateway::auth_token;

// Re-export query parameter
using ::database_server::gateway::query_param;

// Re-export query options
using ::database_server::gateway::query_options;

// Re-export query request
using ::database_server::gateway::query_request;

// Re-export column metadata
using ::database_server::gateway::column_metadata;

// Re-export result row
using ::database_server::gateway::result_row;

// Re-export query response
using ::database_server::gateway::query_response;

} // namespace database_server::gateway

// ============================================================================
// Authentication Middleware
// ============================================================================

export namespace database_server::gateway {

// Re-export auth configuration
using ::database_server::gateway::auth_config;
using ::database_server::gateway::rate_limit_config;

// Re-export auth event types
using ::database_server::gateway::auth_event_type;
using ::database_server::gateway::auth_event;

// Re-export callback type
using ::database_server::gateway::audit_callback_t;

// Re-export auth result
using ::database_server::gateway::auth_result;

// Re-export auth validator interface
using ::database_server::gateway::auth_validator;
using ::database_server::gateway::simple_token_validator;

// Re-export rate limit entry and limiter
using ::database_server::gateway::rate_limit_entry;
using ::database_server::gateway::rate_limiter;

// Re-export auth metrics
using ::database_server::gateway::auth_metrics;

// Re-export auth middleware
using ::database_server::gateway::auth_middleware;

} // namespace database_server::gateway

// ============================================================================
// Query Cache
// ============================================================================

export namespace database_server::gateway {

// Re-export cache configuration
using ::database_server::gateway::cache_config;

// Re-export cache metrics
using ::database_server::gateway::cache_metrics;

// Re-export query cache
using ::database_server::gateway::query_cache;

} // namespace database_server::gateway

// ============================================================================
// Query Handler CRTP Infrastructure
// ============================================================================

export namespace database_server::gateway {

// Re-export handler context
using ::database_server::gateway::handler_context;

// Re-export CRTP base template
using ::database_server::gateway::query_handler_base;

// Re-export type-erased interface
using ::database_server::gateway::i_query_handler;

// Re-export wrapper template
using ::database_server::gateway::query_handler_wrapper;

// Re-export handler factory
using ::database_server::gateway::make_handler;

// Re-export CRTP handlers
using ::database_server::gateway::select_handler;
using ::database_server::gateway::insert_handler;
using ::database_server::gateway::update_handler;
using ::database_server::gateway::delete_handler;
using ::database_server::gateway::execute_handler;
using ::database_server::gateway::ping_handler;
using ::database_server::gateway::batch_handler;

} // namespace database_server::gateway

// ============================================================================
// Query Router
// ============================================================================

export namespace database_server::gateway {

// Re-export router configuration
using ::database_server::gateway::router_config;

// Re-export router metrics
using ::database_server::gateway::router_metrics;

// Re-export query router
using ::database_server::gateway::query_router;

} // namespace database_server::gateway

// ============================================================================
// Gateway Server
// ============================================================================

export namespace database_server::gateway {

// Re-export gateway configuration
using ::database_server::gateway::gateway_config;

// Re-export client session
using ::database_server::gateway::client_session;

// Re-export request handler type
using ::database_server::gateway::request_handler_t;

// Re-export gateway server
using ::database_server::gateway::gateway_server;

} // namespace database_server::gateway

// ============================================================================
// Session ID Generation
// ============================================================================

export namespace database_server::gateway {

// Re-export session ID generator function
using ::database_server::gateway::generate_session_id;

} // namespace database_server::gateway
