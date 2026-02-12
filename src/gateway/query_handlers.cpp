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

#include <kcenon/database_server/gateway/query_handlers.h>

#include <algorithm>
#include <cctype>
#include <regex>

namespace database_server::gateway
{

namespace detail
{

std::unordered_set<std::string> extract_table_names(const std::string& sql,
													query_type type)
{
	std::unordered_set<std::string> tables;

	// Convert SQL to uppercase for case-insensitive matching
	std::string sql_upper = sql;
	std::transform(sql_upper.begin(), sql_upper.end(), sql_upper.begin(),
				   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

	// Regular expression patterns for different query types
	std::regex from_pattern(R"(\bFROM\s+([A-Z_][A-Z0-9_]*(?:\.[A-Z_][A-Z0-9_]*)?))",
							std::regex::icase);
	std::regex join_pattern(R"(\bJOIN\s+([A-Z_][A-Z0-9_]*(?:\.[A-Z_][A-Z0-9_]*)?))",
							std::regex::icase);
	std::regex into_pattern(R"(\bINTO\s+([A-Z_][A-Z0-9_]*(?:\.[A-Z_][A-Z0-9_]*)?))",
							std::regex::icase);
	std::regex update_pattern(R"(\bUPDATE\s+([A-Z_][A-Z0-9_]*(?:\.[A-Z_][A-Z0-9_]*)?))",
							  std::regex::icase);

	std::smatch match;
	std::string::const_iterator search_start = sql.cbegin();

	// Extract FROM tables
	while (std::regex_search(search_start, sql.cend(), match, from_pattern))
	{
		tables.insert(match[1].str());
		search_start = match.suffix().first;
	}

	// Extract JOIN tables
	search_start = sql.cbegin();
	while (std::regex_search(search_start, sql.cend(), match, join_pattern))
	{
		tables.insert(match[1].str());
		search_start = match.suffix().first;
	}

	// For INSERT queries
	if (type == query_type::insert)
	{
		search_start = sql.cbegin();
		while (std::regex_search(search_start, sql.cend(), match, into_pattern))
		{
			tables.insert(match[1].str());
			search_start = match.suffix().first;
		}
	}

	// For UPDATE queries
	if (type == query_type::update)
	{
		search_start = sql.cbegin();
		while (std::regex_search(search_start, sql.cend(), match, update_pattern))
		{
			tables.insert(match[1].str());
			search_start = match.suffix().first;
		}
	}

	return tables;
}

} // namespace detail

// ============================================================================
// select_handler
// ============================================================================

query_response select_handler::handle_impl(const query_request& request,
										   const handler_context& context)
{
	// Try cache lookup first
	std::string cache_key;
	auto cache = context.cache;

	if (cache && cache->is_enabled())
	{
		cache_key = query_cache::make_key(request);
		auto cached = cache->get(cache_key);
		if (cached.is_ok())
		{
			auto response = std::move(cached.value());
			response.header.message_id = request.header.message_id;
			response.header.correlation_id = request.header.correlation_id;
			return response;
		}
		// Cache miss is not an error, continue with query execution
	}

	auto pool = context.pool;
	if (!pool)
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Connection pool not available");
	}

	// Get priority based on query type
	auto priority = detail::get_priority_for_query_type(request.type);

	// Acquire connection with timeout
	auto timeout_ms = request.options.timeout_ms > 0
						  ? request.options.timeout_ms
						  : context.default_timeout_ms;

	auto future = pool->acquire_connection(priority);

	auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
	if (status == std::future_status::timeout)
	{
		return query_response(request.header.message_id, status_code::timeout,
							  "Connection acquisition timeout");
	}

	auto conn_result = future.get();
	if (!conn_result.is_ok())
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Failed to acquire connection: " +
								  conn_result.error().message);
	}

	auto connection = conn_result.value();

	// Execute query
	try
	{
		auto db = connection->get();
		if (!db)
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Invalid database connection");
		}

		// Use select_query for SELECT statements
		auto select_result = db->select_query(request.sql);
		if (select_result.is_err())
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Query execution error: " +
									  select_result.error().message);
		}
		const auto& db_result = select_result.value();

		query_response response(request.header.message_id);

		if (!db_result.empty())
		{
			// Extract column metadata from first row's keys
			if (!db_result.empty())
			{
				for (const auto& [col_name, value] : db_result.front())
				{
					column_metadata meta;
					meta.name = col_name;
					// Determine type name from variant
					std::visit(
						[&meta](const auto& val)
						{
							using T = std::decay_t<decltype(val)>;
							if constexpr (std::is_same_v<T, std::string>)
								meta.type_name = "string";
							else if constexpr (std::is_same_v<T, int64_t>)
								meta.type_name = "integer";
							else if constexpr (std::is_same_v<T, double>)
								meta.type_name = "double";
							else if constexpr (std::is_same_v<T, bool>)
								meta.type_name = "boolean";
							else
								meta.type_name = "null";
						},
						value);
					response.columns.push_back(std::move(meta));
				}
			}

			// Convert rows
			for (const auto& db_row : db_result)
			{
				result_row row;
				for (const auto& [col_name, cell] : db_row)
				{
					std::visit(
						[&row](const auto& val)
						{
							using T = std::decay_t<decltype(val)>;
							if constexpr (std::is_same_v<T, std::nullptr_t>)
								row.cells.push_back(std::monostate{});
							else if constexpr (std::is_same_v<T, int64_t>)
								row.cells.push_back(val);
							else if constexpr (std::is_same_v<T, double>)
								row.cells.push_back(val);
							else if constexpr (std::is_same_v<T, bool>)
								row.cells.push_back(val);
							else if constexpr (std::is_same_v<T, std::string>)
								row.cells.push_back(val);
							else
								row.cells.push_back(std::monostate{});
						},
						cell);
				}
				response.rows.push_back(std::move(row));
			}
		}

		pool->release_connection(connection);

		// Cache the successful response
		if (cache && cache->is_enabled() && !cache_key.empty())
		{
			auto tables = detail::extract_table_names(request.sql, request.type);
			// Ignore cache put failures - they don't affect query success
			(void)cache->put(cache_key, response, tables);
		}

		return response;
	}
	catch (const std::exception& e)
	{
		pool->release_connection(connection);
		return query_response(request.header.message_id, status_code::error,
							  std::string("Query execution error: ") + e.what());
	}
}

// ============================================================================
// insert_handler
// ============================================================================

query_response insert_handler::handle_impl(const query_request& request,
										   const handler_context& context)
{
	auto pool = context.pool;
	if (!pool)
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Connection pool not available");
	}

	auto priority = detail::get_priority_for_query_type(request.type);
	auto timeout_ms = request.options.timeout_ms > 0
						  ? request.options.timeout_ms
						  : context.default_timeout_ms;

	auto future = pool->acquire_connection(priority);

	auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
	if (status == std::future_status::timeout)
	{
		return query_response(request.header.message_id, status_code::timeout,
							  "Connection acquisition timeout");
	}

	auto conn_result = future.get();
	if (!conn_result.is_ok())
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Failed to acquire connection: " +
								  conn_result.error().message);
	}

	auto connection = conn_result.value();

	try
	{
		auto db = connection->get();
		if (!db)
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Invalid database connection");
		}

		query_response response(request.header.message_id);
		auto insert_result = db->insert_query(request.sql);
		if (insert_result.is_err())
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Query execution error: " +
									  insert_result.error().message);
		}
		response.affected_rows = insert_result.value();

		pool->release_connection(connection);

		// Invalidate cache for affected tables
		auto cache = context.cache;
		if (cache && cache->is_enabled())
		{
			auto tables = detail::extract_table_names(request.sql, request.type);
			for (const auto& table : tables)
			{
				(void)cache->invalidate(table);
			}
		}

		return response;
	}
	catch (const std::exception& e)
	{
		pool->release_connection(connection);
		return query_response(request.header.message_id, status_code::error,
							  std::string("Query execution error: ") + e.what());
	}
}

// ============================================================================
// update_handler
// ============================================================================

query_response update_handler::handle_impl(const query_request& request,
										   const handler_context& context)
{
	auto pool = context.pool;
	if (!pool)
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Connection pool not available");
	}

	auto priority = detail::get_priority_for_query_type(request.type);
	auto timeout_ms = request.options.timeout_ms > 0
						  ? request.options.timeout_ms
						  : context.default_timeout_ms;

	auto future = pool->acquire_connection(priority);

	auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
	if (status == std::future_status::timeout)
	{
		return query_response(request.header.message_id, status_code::timeout,
							  "Connection acquisition timeout");
	}

	auto conn_result = future.get();
	if (!conn_result.is_ok())
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Failed to acquire connection: " +
								  conn_result.error().message);
	}

	auto connection = conn_result.value();

	try
	{
		auto db = connection->get();
		if (!db)
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Invalid database connection");
		}

		query_response response(request.header.message_id);
		auto update_result = db->update_query(request.sql);
		if (update_result.is_err())
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Query execution error: " +
									  update_result.error().message);
		}
		response.affected_rows = update_result.value();

		pool->release_connection(connection);

		// Invalidate cache for affected tables
		auto cache = context.cache;
		if (cache && cache->is_enabled())
		{
			auto tables = detail::extract_table_names(request.sql, request.type);
			for (const auto& table : tables)
			{
				(void)cache->invalidate(table);
			}
		}

		return response;
	}
	catch (const std::exception& e)
	{
		pool->release_connection(connection);
		return query_response(request.header.message_id, status_code::error,
							  std::string("Query execution error: ") + e.what());
	}
}

// ============================================================================
// delete_handler
// ============================================================================

query_response delete_handler::handle_impl(const query_request& request,
										   const handler_context& context)
{
	auto pool = context.pool;
	if (!pool)
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Connection pool not available");
	}

	auto priority = detail::get_priority_for_query_type(request.type);
	auto timeout_ms = request.options.timeout_ms > 0
						  ? request.options.timeout_ms
						  : context.default_timeout_ms;

	auto future = pool->acquire_connection(priority);

	auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
	if (status == std::future_status::timeout)
	{
		return query_response(request.header.message_id, status_code::timeout,
							  "Connection acquisition timeout");
	}

	auto conn_result = future.get();
	if (!conn_result.is_ok())
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Failed to acquire connection: " +
								  conn_result.error().message);
	}

	auto connection = conn_result.value();

	try
	{
		auto db = connection->get();
		if (!db)
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Invalid database connection");
		}

		query_response response(request.header.message_id);
		auto delete_result = db->delete_query(request.sql);
		if (delete_result.is_err())
		{
			pool->release_connection(connection);
			return query_response(request.header.message_id, status_code::error,
								  "Query execution error: " +
									  delete_result.error().message);
		}
		response.affected_rows = delete_result.value();

		pool->release_connection(connection);

		// Invalidate cache for affected tables
		auto cache = context.cache;
		if (cache && cache->is_enabled())
		{
			auto tables = detail::extract_table_names(request.sql, request.type);
			for (const auto& table : tables)
			{
				(void)cache->invalidate(table);
			}
		}

		return response;
	}
	catch (const std::exception& e)
	{
		pool->release_connection(connection);
		return query_response(request.header.message_id, status_code::error,
							  std::string("Query execution error: ") + e.what());
	}
}

// ============================================================================
// execute_handler
// ============================================================================

query_response execute_handler::handle_impl(const query_request& request,
											const handler_context& context)
{
	// Stored procedure execution - reuse select handler logic
	// as procedures may return result sets
	select_handler select;
	return select.handle(request, context);
}

// ============================================================================
// ping_handler
// ============================================================================

query_response ping_handler::handle_impl(const query_request& request,
										 const handler_context& /* context */)
{
	// Simple ping response - no database interaction needed
	return query_response(request.header.message_id);
}

// ============================================================================
// batch_handler
// ============================================================================

query_response batch_handler::handle_impl(const query_request& request,
										  const handler_context& /* context */)
{
	// Batch queries not yet implemented
	return query_response(request.header.message_id, status_code::error,
						  "Batch queries not yet implemented");
}

} // namespace database_server::gateway
