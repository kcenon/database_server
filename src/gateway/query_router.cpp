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

} // namespace

// ============================================================================
// query_router
// ============================================================================

query_router::query_router(const router_config& config)
	: config_(config)
{
	initialize_handlers();
}

void query_router::initialize_handlers()
{
	// Built-in handlers are stored as member variables for CRTP optimization
	// No dynamic allocation needed for built-in handlers
}

void query_router::register_handler(std::unique_ptr<i_query_handler> handler)
{
	std::lock_guard<std::mutex> lock(handlers_mutex_);
	handlers_.push_back(std::move(handler));
}

handler_context query_router::get_handler_context() const
{
	handler_context ctx;
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		ctx.pool = pool_;
	}
	{
		std::lock_guard<std::mutex> lock(cache_mutex_);
		ctx.cache = cache_;
	}
	ctx.default_timeout_ms = config_.default_timeout_ms;
	return ctx;
}

i_query_handler* query_router::find_handler(query_type type) const
{
	// First check custom handlers
	{
		std::lock_guard<std::mutex> lock(handlers_mutex_);
		for (const auto& handler : handlers_)
		{
			if (handler->can_handle(type))
			{
				return handler.get();
			}
		}
	}

	// Return nullptr - built-in handlers are used directly in execute_with_handler
	return nullptr;
}

query_response query_router::execute_with_handler(const query_request& request)
{
	auto ctx = get_handler_context();

	// Check for custom handler first
	auto* custom_handler = find_handler(request.type);
	if (custom_handler != nullptr)
	{
		return custom_handler->handle(request, ctx);
	}

	// Use built-in CRTP handlers (zero virtual dispatch overhead)
	switch (request.type)
	{
	case query_type::select:
		return select_handler_.handle(request, ctx);

	case query_type::insert:
		return insert_handler_.handle(request, ctx);

	case query_type::update:
		return update_handler_.handle(request, ctx);

	case query_type::del:
		return delete_handler_.handle(request, ctx);

	case query_type::execute:
		return execute_handler_.handle(request, ctx);

	case query_type::ping:
		return ping_handler_.handle(request, ctx);

	case query_type::batch:
		return batch_handler_.handle(request, ctx);

	default:
		return query_response(request.header.message_id, status_code::invalid_query,
							  "Unknown query type");
	}
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

	// Execute using CRTP handlers
	query_response response(request.header.message_id);

	try
	{
		response = execute_with_handler(request);
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
	// Delegate to detail namespace implementation
	return detail::extract_table_names(sql, type);
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
