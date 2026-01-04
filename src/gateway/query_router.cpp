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

#include <kcenon/database_server/gateway/query_router.h>
#include <kcenon/database_server/pooling/connection_pool.h>
#include <kcenon/database_server/pooling/connection_priority.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <regex>

namespace database_server::gateway
{

namespace
{

uint64_t current_timestamp_us()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count());
}

pooling::connection_priority get_priority_for_query_type(query_type type)
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

} // namespace

// ============================================================================
// query_router
// ============================================================================

query_router::query_router(const router_config& config)
	: config_(config)
{
}

void query_router::set_connection_pool(std::shared_ptr<pooling::connection_pool> pool)
{
	std::lock_guard<std::mutex> lock(pool_mutex_);
	pool_ = std::move(pool);
}

kcenon::common::Result<query_response> query_router::execute(const query_request& request)
{
	auto start_time = current_timestamp_us();

	// Check if router is ready
	if (!is_ready())
	{
		record_metrics(false, false, 0);
		return kcenon::common::error_info{
			kcenon::common::error_codes::NOT_INITIALIZED,
			"No connection pool available",
			"query_router"};
	}

	// Check concurrent query limit
	auto current = active_queries_.fetch_add(1, std::memory_order_relaxed);
	if (current >= config_.max_concurrent_queries)
	{
		active_queries_.fetch_sub(1, std::memory_order_relaxed);
		record_metrics(false, false, 0);
		return kcenon::common::error_info{
			kcenon::common::error_codes::INTERNAL_ERROR,
			"Maximum concurrent queries exceeded",
			"query_router"};
	}

	// Execute based on query type
	query_response response(request.header.message_id);

	try
	{
		switch (request.type)
		{
		case query_type::select:
			response = execute_select(request);
			break;

		case query_type::insert:
		case query_type::update:
		case query_type::del:
			response = execute_modify(request);
			break;

		case query_type::execute:
			response = execute_procedure(request);
			break;

		case query_type::batch:
			// Batch queries not yet implemented
			response = query_response(request.header.message_id, status_code::error,
									  "Batch queries not yet implemented");
			break;

		case query_type::ping:
			// Ping is handled at gateway level
			response = query_response(request.header.message_id);
			break;

		default:
			response = query_response(request.header.message_id, status_code::invalid_query,
									  "Unknown query type");
			break;
		}
	}
	catch (const std::exception& e)
	{
		active_queries_.fetch_sub(1, std::memory_order_relaxed);
		record_metrics(false, false, 0);
		return kcenon::common::error_info{
			kcenon::common::error_codes::INTERNAL_ERROR,
			std::string("Exception: ") + e.what(),
			"query_router"};
	}

	active_queries_.fetch_sub(1, std::memory_order_relaxed);

	// Calculate execution time
	auto end_time = current_timestamp_us();
	auto execution_time = end_time - start_time;
	response.execution_time_us = execution_time;

	// Record metrics
	bool is_success = response.is_success();
	bool is_timeout = response.status == status_code::timeout;
	record_metrics(is_success, is_timeout, execution_time);

	return kcenon::common::ok(std::move(response));
}

/**
 * @brief Job implementation for async query execution
 */
class async_query_job : public kcenon::common::interfaces::IJob
{
public:
	async_query_job(query_router* router,
					query_request request,
					std::function<void(query_response)> callback)
		: router_(router)
		, request_(std::move(request))
		, callback_(std::move(callback))
	{
	}

	kcenon::common::VoidResult execute() override
	{
		if (router_)
		{
			auto result = router_->execute(request_);
			if (callback_)
			{
				if (result.is_ok())
				{
					callback_(std::move(result.value()));
				}
				else
				{
					// Create error response for callback
					query_response error_response(
						request_.header.message_id,
						status_code::error,
						result.error().message);
					callback_(std::move(error_response));
				}
			}
		}
		return kcenon::common::ok();
	}

	std::string get_name() const override { return "async_query"; }
	int get_priority() const override { return 0; }

private:
	query_router* router_;
	query_request request_;
	std::function<void(query_response)> callback_;
};

void query_router::execute_async(const query_request& request,
								 std::function<void(query_response)> callback)
{
	std::shared_ptr<kcenon::common::interfaces::IExecutor> exec;
	{
		std::lock_guard<std::mutex> lock(executor_mutex_);
		exec = executor_;
	}

	if (exec)
	{
		// Use IExecutor for async execution
		auto job = std::make_unique<async_query_job>(this, request, std::move(callback));
		auto result = exec->execute(std::move(job));
		// Fire and forget - the callback will be invoked when done
		(void)result;
	}
	else
	{
		// Fallback to std::async if no executor provided
		std::async(std::launch::async,
				   [this, request, callback = std::move(callback)]()
				   {
					   auto result = execute(request);
					   if (callback)
					   {
						   if (result.is_ok())
						   {
							   callback(std::move(result.value()));
						   }
						   else
						   {
							   query_response error_response(
								   request.header.message_id,
								   status_code::error,
								   result.error().message);
							   callback(std::move(error_response));
						   }
					   }
				   });
	}
}

const router_metrics& query_router::metrics() const noexcept
{
	return metrics_;
}

void query_router::reset_metrics()
{
	metrics_.total_queries = 0;
	metrics_.successful_queries = 0;
	metrics_.failed_queries = 0;
	metrics_.timeout_queries = 0;
	metrics_.total_execution_time_us = 0;
}

const router_config& query_router::config() const noexcept
{
	return config_;
}

bool query_router::is_ready() const noexcept
{
	std::lock_guard<std::mutex> lock(pool_mutex_);
	return pool_ != nullptr;
}

void query_router::set_query_cache(std::shared_ptr<query_cache> cache)
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	cache_ = std::move(cache);
}

std::shared_ptr<query_cache> query_router::get_query_cache() const noexcept
{
	std::lock_guard<std::mutex> lock(cache_mutex_);
	return cache_;
}

void query_router::set_executor(
	std::shared_ptr<kcenon::common::interfaces::IExecutor> executor)
{
	std::lock_guard<std::mutex> lock(executor_mutex_);
	executor_ = std::move(executor);
}

std::shared_ptr<kcenon::common::interfaces::IExecutor> query_router::get_executor()
	const noexcept
{
	std::lock_guard<std::mutex> lock(executor_mutex_);
	return executor_;
}

std::unordered_set<std::string> query_router::extract_table_names(
	const std::string& sql, query_type type)
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

query_response query_router::execute_select(const query_request& request)
{
	// Try cache lookup first
	std::shared_ptr<query_cache> cache;
	std::string cache_key;
	{
		std::lock_guard<std::mutex> lock(cache_mutex_);
		cache = cache_;
	}

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

	std::shared_ptr<pooling::connection_pool> pool;
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		pool = pool_;
	}

	if (!pool)
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Connection pool not available");
	}

	// Get priority based on query type
	auto priority = get_priority_for_query_type(request.type);

	// Acquire connection with timeout
	auto timeout_ms = request.options.timeout_ms > 0
						  ? request.options.timeout_ms
						  : config_.default_timeout_ms;

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
		auto db_result = db->select_query(request.sql);

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
			auto tables = extract_table_names(request.sql, request.type);
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

query_response query_router::execute_modify(const query_request& request)
{
	std::shared_ptr<pooling::connection_pool> pool;
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		pool = pool_;
	}

	if (!pool)
	{
		return query_response(request.header.message_id, status_code::no_connection,
							  "Connection pool not available");
	}

	auto priority = get_priority_for_query_type(request.type);
	auto timeout_ms = request.options.timeout_ms > 0
						  ? request.options.timeout_ms
						  : config_.default_timeout_ms;

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
		unsigned int affected_rows = 0;

		// Use appropriate method based on query type
		switch (request.type)
		{
		case query_type::insert:
			affected_rows = db->insert_query(request.sql);
			break;
		case query_type::update:
			affected_rows = db->update_query(request.sql);
			break;
		case query_type::del:
			affected_rows = db->delete_query(request.sql);
			break;
		default:
			// Generic execute for other types
			if (!db->execute_query(request.sql))
			{
				pool->release_connection(connection);
				return query_response(request.header.message_id, status_code::error,
									  "Query execution failed");
			}
			break;
		}

		response.affected_rows = static_cast<uint64_t>(affected_rows);

		pool->release_connection(connection);

		// Invalidate cache for affected tables
		std::shared_ptr<query_cache> cache;
		{
			std::lock_guard<std::mutex> lock(cache_mutex_);
			cache = cache_;
		}

		if (cache && cache->is_enabled())
		{
			auto tables = extract_table_names(request.sql, request.type);
			for (const auto& table : tables)
			{
				// Ignore cache invalidation failures - they don't affect query success
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

query_response query_router::execute_procedure(const query_request& request)
{
	// Stored procedure execution - similar to execute_select but may return multiple result sets
	// For now, delegate to execute_select
	return execute_select(request);
}

void query_router::record_metrics(bool success, bool timeout, uint64_t execution_time_us)
{
	if (!config_.enable_metrics)
	{
		return;
	}

	metrics_.total_queries.fetch_add(1, std::memory_order_relaxed);

	if (success)
	{
		metrics_.successful_queries.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		metrics_.failed_queries.fetch_add(1, std::memory_order_relaxed);
	}

	if (timeout)
	{
		metrics_.timeout_queries.fetch_add(1, std::memory_order_relaxed);
	}

	metrics_.total_execution_time_us.fetch_add(execution_time_us, std::memory_order_relaxed);
}

} // namespace database_server::gateway
