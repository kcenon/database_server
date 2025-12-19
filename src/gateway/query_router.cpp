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

#include <chrono>
#include <future>
#include <thread>

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
		return pooling::connection_priority::NORMAL_QUERY;
	case query_type::insert:
	case query_type::update:
	case query_type::del:
		return pooling::connection_priority::TRANSACTION;
	case query_type::execute:
		return pooling::connection_priority::TRANSACTION;
	case query_type::batch:
		return pooling::connection_priority::TRANSACTION;
	case query_type::ping:
		return pooling::connection_priority::HEALTH_CHECK;
	default:
		return pooling::connection_priority::NORMAL_QUERY;
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

query_response query_router::execute(const query_request& request)
{
	auto start_time = current_timestamp_us();

	// Check if router is ready
	if (!is_ready())
	{
		record_metrics(false, false, 0);
		return query_response(request.header.message_id, status_code::no_connection,
							  "No connection pool available");
	}

	// Check concurrent query limit
	auto current = active_queries_.fetch_add(1, std::memory_order_relaxed);
	if (current >= config_.max_concurrent_queries)
	{
		active_queries_.fetch_sub(1, std::memory_order_relaxed);
		record_metrics(false, false, 0);
		return query_response(request.header.message_id, status_code::server_busy,
							  "Maximum concurrent queries exceeded");
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
		response = query_response(request.header.message_id, status_code::error,
								  std::string("Exception: ") + e.what());
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

	return response;
}

void query_router::execute_async(const query_request& request,
								 std::function<void(query_response)> callback)
{
	// Launch async execution
	std::thread(
		[this, request, callback = std::move(callback)]()
		{
			auto response = execute(request);
			if (callback)
			{
				callback(std::move(response));
			}
		})
		.detach();
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

query_response query_router::execute_select(const query_request& request)
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

		auto result = db->execute_query(request.sql);

		query_response response(request.header.message_id);

		if (result.is_ok())
		{
			// Convert database result to response rows
			auto& db_result = result.value();

			// Add column metadata
			for (const auto& col : db_result.columns())
			{
				column_metadata meta;
				meta.name = col.name;
				meta.type_name = col.type_name;
				response.columns.push_back(std::move(meta));
			}

			// Add rows
			for (const auto& db_row : db_result.rows())
			{
				result_row row;
				for (size_t i = 0; i < db_row.size(); ++i)
				{
					const auto& cell = db_row[i];
					if (cell.is_null())
					{
						row.cells.push_back(std::monostate{});
					}
					else if (cell.is_int())
					{
						row.cells.push_back(static_cast<int64_t>(cell.as_int()));
					}
					else if (cell.is_double())
					{
						row.cells.push_back(cell.as_double());
					}
					else if (cell.is_bool())
					{
						row.cells.push_back(cell.as_bool());
					}
					else
					{
						row.cells.push_back(cell.as_string());
					}
				}
				response.rows.push_back(std::move(row));
			}
		}
		else
		{
			response = query_response(request.header.message_id, status_code::error,
									  result.error().message);
		}

		pool->release_connection(connection);
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

		auto result = db->execute_non_query(request.sql);

		query_response response(request.header.message_id);

		if (result.is_ok())
		{
			response.affected_rows = static_cast<uint64_t>(result.value());
		}
		else
		{
			response = query_response(request.header.message_id, status_code::error,
									  result.error().message);
		}

		pool->release_connection(connection);
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
