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
 * @file query_protocol.h
 * @brief Query protocol message structures for database gateway
 *
 * Defines the message structures for communication between clients and
 * the database gateway server. Supports serialization/deserialization
 * using container_system.
 *
 * Protocol Design:
 * - Binary protocol using container_system for efficient serialization
 * - Request/response model for query execution
 * - Support for parameterized queries
 * - Token-based authentication
 *
 * ## Thread Safety
 * All message structs (`message_header`, `auth_token`, `query_request`,
 * `query_response`, etc.) are plain data structures with no internal
 * synchronization. Individual instances should not be shared across threads
 * without external synchronization. Serialization and deserialization create
 * independent copies and are safe to call concurrently on different instances.
 *
 * @code
 * using namespace database_server::gateway;
 *
 * // Create a query request
 * query_request request("SELECT * FROM users WHERE id = ?",
 *                       query_type::select);
 * request.params.emplace_back("id", int64_t{42});
 * request.options.timeout_ms = 5000;
 *
 * // Serialize for transmission
 * auto container = request.serialize();
 *
 * // Deserialize on the receiving end
 * auto result = query_request::deserialize(container);
 * if (result) {
 *     const auto& req = result.value();
 *     // Process request...
 * }
 * @endcode
 */

#pragma once

#include "query_types.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Common system integration
#include <kcenon/common/patterns/result.h>

// Forward declaration for container_system
// When migrating to kcenon::container namespace, update this and container_compat.h
namespace container_module
{
class value_container;
}

namespace database_server::gateway
{

/**
 * @struct message_header
 * @brief Common header for all protocol messages
 */
struct message_header
{
	uint32_t version = 1;       ///< Protocol version
	uint64_t message_id = 0;    ///< Unique message identifier
	uint64_t timestamp = 0;     ///< Message timestamp (Unix epoch ms)
	std::string correlation_id; ///< For request/response correlation

	message_header() = default;
	message_header(uint64_t id);
};

/**
 * @struct auth_token
 * @brief Authentication token for client identification
 */
struct auth_token
{
	std::string token;        ///< JWT or session token
	std::string client_id;    ///< Client identifier
	uint64_t expires_at = 0;  ///< Token expiration (Unix epoch ms)

	[[nodiscard]] bool is_expired() const noexcept;
	[[nodiscard]] bool is_valid() const noexcept;
};

/**
 * @struct query_param
 * @brief Parameter for prepared statements
 *
 * Supports various data types for query parameters.
 */
struct query_param
{
	using param_value = std::variant<
		std::monostate,          ///< NULL value
		bool,                    ///< Boolean
		int64_t,                 ///< Integer
		double,                  ///< Floating point
		std::string,             ///< String
		std::vector<uint8_t>     ///< Binary data
	>;

	std::string name;      ///< Parameter name (for named parameters)
	param_value value;     ///< Parameter value

	query_param() = default;
	query_param(std::string n, param_value v);

	template<typename T>
	explicit query_param(std::string n, T&& val)
		: name(std::move(n))
		, value(std::forward<T>(val))
	{
	}
};

/**
 * @struct query_options
 * @brief Options for query execution
 */
struct query_options
{
	uint32_t timeout_ms = 30000;  ///< Query timeout in milliseconds
	bool read_only = false;       ///< Hint for read-only queries
	std::string isolation_level;  ///< Transaction isolation level
	uint32_t max_rows = 0;        ///< Maximum rows to return (0 = unlimited)
	bool include_metadata = true; ///< Include column metadata in response
};

/**
 * @struct query_request
 * @brief Request message for database queries
 *
 * Contains all information needed to execute a database query,
 * including authentication, the query itself, and execution options.
 */
struct query_request
{
	message_header header;            ///< Message header
	auth_token token;                 ///< Client authentication
	query_type type = query_type::unknown;  ///< Query type
	std::string sql;                  ///< Query string or prepared statement ID
	std::vector<query_param> params;  ///< Query parameters
	query_options options;            ///< Execution options

	query_request() = default;

	/**
	 * @brief Create a simple query request
	 * @param query_sql The SQL query string
	 * @param qtype The query type
	 */
	query_request(std::string query_sql, query_type qtype);

	/**
	 * @brief Serialize to container for network transmission
	 * @return Shared pointer to serialized container
	 */
	[[nodiscard]] std::shared_ptr<container_module::value_container> serialize() const;

	/**
	 * @brief Deserialize from container
	 * @param container The container to deserialize from
	 * @return Result containing deserialized request or error
	 */
	static kcenon::common::Result<query_request>
	deserialize(std::shared_ptr<container_module::value_container> container);

	/**
	 * @brief Deserialize from byte array
	 * @param data The byte array to deserialize from
	 * @return Result containing deserialized request or error
	 */
	static kcenon::common::Result<query_request>
	deserialize(const std::vector<uint8_t>& data);

	/**
	 * @brief Validate the request
	 * @return true if the request is valid
	 */
	[[nodiscard]] bool is_valid() const noexcept;
};

/**
 * @struct column_metadata
 * @brief Metadata for a result column
 */
struct column_metadata
{
	std::string name;          ///< Column name
	std::string type_name;     ///< Database type name
	uint32_t type_id = 0;      ///< Database type ID
	bool nullable = true;      ///< Whether column can be NULL
	uint32_t precision = 0;    ///< For numeric types
	uint32_t scale = 0;        ///< For numeric types
};

/**
 * @struct result_row
 * @brief A single row of query results
 */
struct result_row
{
	using cell_value = std::variant<
		std::monostate,          ///< NULL value
		bool,                    ///< Boolean
		int64_t,                 ///< Integer
		double,                  ///< Floating point
		std::string,             ///< String
		std::vector<uint8_t>     ///< Binary data
	>;

	std::vector<cell_value> cells;  ///< Cell values in column order
};

/**
 * @struct query_response
 * @brief Response message for database queries
 *
 * Contains the results of query execution, including status,
 * result rows (for SELECT), affected row count, and any errors.
 */
struct query_response
{
	message_header header;                    ///< Message header
	status_code status = status_code::ok;     ///< Response status
	std::vector<column_metadata> columns;     ///< Column metadata (for SELECT)
	std::vector<result_row> rows;             ///< Result rows (for SELECT)
	uint64_t affected_rows = 0;               ///< Affected count (for INSERT/UPDATE/DELETE)
	std::string error_message;                ///< Error details if status != OK
	uint64_t execution_time_us = 0;           ///< Query execution time in microseconds

	query_response() = default;

	/**
	 * @brief Create a success response
	 * @param request_id The original request's message ID
	 */
	explicit query_response(uint64_t request_id);

	/**
	 * @brief Create an error response
	 * @param request_id The original request's message ID
	 * @param error_status The error status code
	 * @param error_msg The error message
	 */
	query_response(uint64_t request_id, status_code error_status, std::string error_msg);

	/**
	 * @brief Serialize to container for network transmission
	 * @return Shared pointer to serialized container
	 */
	[[nodiscard]] std::shared_ptr<container_module::value_container> serialize() const;

	/**
	 * @brief Deserialize from container
	 * @param container The container to deserialize from
	 * @return Result containing deserialized response or error
	 */
	static kcenon::common::Result<query_response>
	deserialize(std::shared_ptr<container_module::value_container> container);

	/**
	 * @brief Deserialize from byte array
	 * @param data The byte array to deserialize from
	 * @return Result containing deserialized response or error
	 */
	static kcenon::common::Result<query_response>
	deserialize(const std::vector<uint8_t>& data);

	/**
	 * @brief Check if response indicates success
	 * @return true if status is OK
	 */
	[[nodiscard]] bool is_success() const noexcept;
};

} // namespace database_server::gateway
