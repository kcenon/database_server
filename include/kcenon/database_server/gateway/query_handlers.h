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
 * @file query_handlers.h
 * @brief CRTP-based query handlers for database gateway
 *
 * Implements query handlers using the Curiously Recurring Template Pattern
 * (CRTP) for zero virtual dispatch overhead in query processing.
 *
 * Handlers:
 * - select_handler: Handles SELECT queries with caching support
 * - insert_handler: Handles INSERT queries with cache invalidation
 * - update_handler: Handles UPDATE queries with cache invalidation
 * - delete_handler: Handles DELETE queries with cache invalidation
 * - execute_handler: Handles stored procedure execution
 * - ping_handler: Handles health check pings
 *
 * @see https://github.com/kcenon/database_server/issues/48
 */

#pragma once

#include "query_cache.h"
#include "query_handler_base.h"
#include "query_protocol.h"
#include "query_types.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <string>
#include <unordered_set>

#include <kcenon/database_server/pooling/connection_pool.h>
#include <kcenon/database_server/pooling/connection_priority.h>

namespace database_server::gateway
{

namespace detail
{

/**
 * @brief Get current timestamp in microseconds
 */
inline uint64_t current_timestamp_us()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count());
}

/**
 * @brief Map query type to connection priority
 */
inline pooling::connection_priority get_priority_for_query_type(query_type type)
{
	switch (type)
	{
	case query_type::select:
		return pooling::PRIORITY_NORMAL_QUERY;
	case query_type::insert:
	case query_type::update:
	case query_type::del:
		return pooling::PRIORITY_TRANSACTION;
	case query_type::execute:
		return pooling::PRIORITY_TRANSACTION;
	case query_type::batch:
		return pooling::PRIORITY_TRANSACTION;
	case query_type::ping:
		return pooling::PRIORITY_HEALTH_CHECK;
	default:
		return pooling::PRIORITY_NORMAL_QUERY;
	}
}

/**
 * @brief Extract table names from SQL query
 * @param sql The SQL query string
 * @param type The query type
 * @return Set of table names found in the query
 */
std::unordered_set<std::string> extract_table_names(const std::string& sql,
													query_type type);

} // namespace detail

/**
 * @class select_handler
 * @brief CRTP handler for SELECT queries
 *
 * Handles SELECT query execution with optional result caching.
 * Results are cached based on query text and parameters.
 *
 * Thread Safety:
 * - Thread-safe when using thread-safe connection pool and cache
 */
class select_handler : public query_handler_base<select_handler>
{
	friend class query_handler_base<select_handler>;

public:
	select_handler() = default;
	~select_handler() = default;

private:
	/**
	 * @brief Handle SELECT query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::select;
	}
};

/**
 * @class insert_handler
 * @brief CRTP handler for INSERT queries
 *
 * Handles INSERT query execution with cache invalidation for
 * affected tables.
 */
class insert_handler : public query_handler_base<insert_handler>
{
	friend class query_handler_base<insert_handler>;

public:
	insert_handler() = default;
	~insert_handler() = default;

private:
	/**
	 * @brief Handle INSERT query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::insert;
	}
};

/**
 * @class update_handler
 * @brief CRTP handler for UPDATE queries
 *
 * Handles UPDATE query execution with cache invalidation for
 * affected tables.
 */
class update_handler : public query_handler_base<update_handler>
{
	friend class query_handler_base<update_handler>;

public:
	update_handler() = default;
	~update_handler() = default;

private:
	/**
	 * @brief Handle UPDATE query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::update;
	}
};

/**
 * @class delete_handler
 * @brief CRTP handler for DELETE queries
 *
 * Handles DELETE query execution with cache invalidation for
 * affected tables.
 */
class delete_handler : public query_handler_base<delete_handler>
{
	friend class query_handler_base<delete_handler>;

public:
	delete_handler() = default;
	~delete_handler() = default;

private:
	/**
	 * @brief Handle DELETE query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::del;
	}
};

/**
 * @class execute_handler
 * @brief CRTP handler for stored procedure execution
 *
 * Handles EXECUTE query type for stored procedure calls.
 */
class execute_handler : public query_handler_base<execute_handler>
{
	friend class query_handler_base<execute_handler>;

public:
	execute_handler() = default;
	~execute_handler() = default;

private:
	/**
	 * @brief Handle EXECUTE query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::execute;
	}
};

/**
 * @class ping_handler
 * @brief CRTP handler for health check pings
 *
 * Handles PING query type for connection health verification.
 */
class ping_handler : public query_handler_base<ping_handler>
{
	friend class query_handler_base<ping_handler>;

public:
	ping_handler() = default;
	~ping_handler() = default;

private:
	/**
	 * @brief Handle PING query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::ping;
	}
};

/**
 * @class batch_handler
 * @brief CRTP handler for batch query execution
 *
 * Handles BATCH query type for executing multiple queries
 * in a single request.
 */
class batch_handler : public query_handler_base<batch_handler>
{
	friend class query_handler_base<batch_handler>;

public:
	batch_handler() = default;
	~batch_handler() = default;

private:
	/**
	 * @brief Handle BATCH query implementation
	 */
	[[nodiscard]] query_response handle_impl(const query_request& request,
											 const handler_context& context);

	/**
	 * @brief Check if handler can handle query type
	 */
	[[nodiscard]] bool can_handle_impl(query_type type) const noexcept
	{
		return type == query_type::batch;
	}
};

} // namespace database_server::gateway
