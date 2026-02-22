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
 * @file query_types.h
 * @brief Query protocol type definitions for database gateway
 *
 * Defines enumerations for query types and status codes used in the
 * gateway protocol for communication between clients and the database server.
 *
 * ## Thread Safety
 * All types are enumerations and constexpr/inline functions operating on
 * immutable data. Fully thread-safe for concurrent reads; no mutable
 * shared state exists.
 *
 * @code
 * using namespace database_server::gateway;
 *
 * // Convert between enum and string
 * auto type = query_type::select;
 * std::string_view name = to_string(type);  // "SELECT"
 *
 * // Parse from string
 * auto parsed = parse_query_type("INSERT");  // query_type::insert
 *
 * // Check status codes
 * auto code = status_code::ok;
 * std::string_view status = to_string(code);  // "OK"
 * @endcode
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace database_server::gateway
{

/**
 * @enum query_type
 * @brief Types of database queries that can be processed
 */
enum class query_type : uint8_t
{
	unknown = 0,  ///< Unknown or invalid query type
	select = 1,   ///< SELECT query - retrieves data
	insert = 2,   ///< INSERT query - adds new data
	update = 3,   ///< UPDATE query - modifies existing data
	del = 4,      ///< DELETE query - removes data
	execute = 5,  ///< EXECUTE query - runs stored procedure
	batch = 6,    ///< BATCH query - multiple queries in one request
	ping = 7,     ///< PING - health check request
};

/**
 * @enum status_code
 * @brief Status codes for query responses
 */
enum class status_code : uint16_t
{
	ok = 0,                    ///< Query executed successfully
	error = 1,                 ///< General error occurred
	timeout = 2,               ///< Query execution timed out
	connection_failed = 3,     ///< Database connection failed
	authentication_failed = 4, ///< Authentication token invalid
	invalid_query = 5,         ///< Query syntax or structure invalid
	no_connection = 6,         ///< No available connection in pool
	rate_limited = 7,          ///< Request rate limit exceeded
	server_busy = 8,           ///< Server is too busy to process
	not_found = 9,             ///< Requested resource not found
	permission_denied = 10,    ///< Insufficient permissions
};

/**
 * @brief Convert query_type to string representation
 * @param type The query type to convert
 * @return String representation of the query type
 */
constexpr std::string_view to_string(query_type type) noexcept
{
	switch (type)
	{
	case query_type::select:
		return "SELECT";
	case query_type::insert:
		return "INSERT";
	case query_type::update:
		return "UPDATE";
	case query_type::del:
		return "DELETE";
	case query_type::execute:
		return "EXECUTE";
	case query_type::batch:
		return "BATCH";
	case query_type::ping:
		return "PING";
	default:
		return "UNKNOWN";
	}
}

/**
 * @brief Convert status_code to string representation
 * @param code The status code to convert
 * @return String representation of the status code
 */
constexpr std::string_view to_string(status_code code) noexcept
{
	switch (code)
	{
	case status_code::ok:
		return "OK";
	case status_code::error:
		return "ERROR";
	case status_code::timeout:
		return "TIMEOUT";
	case status_code::connection_failed:
		return "CONNECTION_FAILED";
	case status_code::authentication_failed:
		return "AUTHENTICATION_FAILED";
	case status_code::invalid_query:
		return "INVALID_QUERY";
	case status_code::no_connection:
		return "NO_CONNECTION";
	case status_code::rate_limited:
		return "RATE_LIMITED";
	case status_code::server_busy:
		return "SERVER_BUSY";
	case status_code::not_found:
		return "NOT_FOUND";
	case status_code::permission_denied:
		return "PERMISSION_DENIED";
	default:
		return "UNKNOWN";
	}
}

/**
 * @brief Parse query_type from string
 * @param str String representation of query type
 * @return Corresponding query_type enum value
 */
inline query_type parse_query_type(std::string_view str) noexcept
{
	if (str == "SELECT" || str == "select")
		return query_type::select;
	if (str == "INSERT" || str == "insert")
		return query_type::insert;
	if (str == "UPDATE" || str == "update")
		return query_type::update;
	if (str == "DELETE" || str == "delete")
		return query_type::del;
	if (str == "EXECUTE" || str == "execute")
		return query_type::execute;
	if (str == "BATCH" || str == "batch")
		return query_type::batch;
	if (str == "PING" || str == "ping")
		return query_type::ping;
	return query_type::unknown;
}

} // namespace database_server::gateway
