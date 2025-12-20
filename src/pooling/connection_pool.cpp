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

#include <kcenon/database_server/pooling/connection_pool.h>

#include <iostream>

namespace database_server::pooling
{

connection_pool::connection_pool(
	database::database_types db_type,
	const database::connection_pool_config& config,
	std::function<std::unique_ptr<database::database_base>()> factory,
	size_t thread_count,
	kcenon::thread::adaptive_typed_job_queue_t<connection_priority>::queue_strategy
		queue_strategy)
	: underlying_pool_(
		  std::make_shared<database::connection_pool>(db_type, config, std::move(factory)))
	, adaptive_queue_(
		  std::make_shared<
			  kcenon::thread::adaptive_typed_job_queue_t<connection_priority>>(
			  queue_strategy))
	, worker_pool_(nullptr) // Will be created in initialize()
	, shutdown_token_(kcenon::thread::cancellation_token::create())
	, metrics_(std::make_shared<priority_metrics<connection_priority>>())
	, thread_count_(thread_count > 0 ? thread_count : std::thread::hardware_concurrency())
	, shutdown_requested_(false)
{
}

connection_pool::~connection_pool()
{
	if (!shutdown_requested_.load())
	{
		shutdown();
	}
}

bool connection_pool::initialize()
{
	// Initialize underlying pool
	if (!underlying_pool_->initialize())
	{
		return false;
	}

	// Create worker pool with adaptive queue
	worker_pool_ = std::make_shared<kcenon::thread::typed_thread_pool_t<connection_priority>>(
		"connection_pool_workers");

	// Start the worker pool
	auto start_result = worker_pool_->start();
	if (start_result.is_err())
	{
		std::cerr << "Failed to start worker pool: " << start_result.error().message
				  << std::endl;
		return false;
	}

	// Register shutdown callback
	shutdown_token_.register_callback([this]() { shutdown_requested_.store(true); });

	return true;
}

std::future<kcenon::common::Result<std::shared_ptr<database::connection_wrapper>>>
connection_pool::acquire_connection(connection_priority priority)
{
	// Check if shutdown requested
	if (shutdown_requested_.load())
	{
		std::promise<kcenon::common::Result<std::shared_ptr<database::connection_wrapper>>>
			promise;
		promise.set_value(
			kcenon::common::error_info{ -500, "Pool is shutting down", "connection_pool" });
		return promise.get_future();
	}

	// Create promise/future pair
	auto promise = std::make_shared<
		std::promise<kcenon::common::Result<std::shared_ptr<database::connection_wrapper>>>>();
	auto future = promise->get_future();

	// Record acquisition start time
	auto start_time = std::chrono::steady_clock::now();

	// Create acquisition job with callback
	auto job = std::make_unique<connection_acquisition_job>(
		priority,
		underlying_pool_,
		[this, promise, priority, start_time](
			kcenon::common::Result<std::shared_ptr<database::connection_wrapper>> result) {
			// Calculate acquisition time
			auto end_time = std::chrono::steady_clock::now();
			auto duration
				= std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

			// Record metrics
			metrics_->record_acquisition_with_priority(
				priority, duration.count(), result.is_ok());

			// Set promise value
			promise->set_value(std::move(result));
		});

	// Enqueue job to adaptive queue (cast to base job type)
	std::unique_ptr<kcenon::thread::job> base_job = std::move(job);
	auto enqueue_result = adaptive_queue_->enqueue(std::move(base_job));
	if (enqueue_result.is_err())
	{
		std::promise<kcenon::common::Result<std::shared_ptr<database::connection_wrapper>>>
			error_promise;
		error_promise.set_value(kcenon::common::error_info{
			-598,
			"Failed to enqueue acquisition request: " + enqueue_result.error().message,
			"connection_pool" });
		return error_promise.get_future();
	}

	// Process jobs from adaptive queue in worker pool
	// Dequeue from adaptive queue and execute
	std::thread(
		[this, priority]()
		{
			if (shutdown_requested_.load())
			{
				return;
			}

			// Try to dequeue a job matching this priority
			auto dequeue_result
				= adaptive_queue_->dequeue(std::vector<connection_priority>{ priority });
			if (dequeue_result.is_ok())
			{
				auto job_ptr = std::move(dequeue_result.value());
				if (job_ptr)
				{
					[[maybe_unused]] auto work_result = job_ptr->do_work();
				}
			}
		})
		.detach();

	return future;
}

void connection_pool::release_connection(
	std::shared_ptr<database::connection_wrapper> connection)
{
	if (underlying_pool_)
	{
		underlying_pool_->release_connection(std::move(connection));
	}
}

void connection_pool::schedule_health_check()
{
	if (shutdown_requested_.load())
	{
		return;
	}

	// Create a low-priority health check job
	auto job = std::make_unique<connection_acquisition_job>(
		PRIORITY_HEALTH_CHECK,
		underlying_pool_,
		[this](
			kcenon::common::Result<std::shared_ptr<database::connection_wrapper>> result) {
			// Health check callback - just verify connection works
			if (result.is_ok())
			{
				auto conn = result.value();
				if (conn && conn->is_healthy())
				{
					// Connection is healthy, return it
					release_connection(conn);
				}
				else
				{
					// Connection unhealthy, will be destroyed
					if (conn)
					{
						conn->mark_unhealthy();
					}
				}
			}
		});

	// Enqueue health check job (cast to base job type)
	std::unique_ptr<kcenon::thread::job> base_job = std::move(job);
	[[maybe_unused]] auto result = adaptive_queue_->enqueue(std::move(base_job));
}

size_t connection_pool::active_connections() const
{
	return underlying_pool_ ? underlying_pool_->active_connections() : 0;
}

size_t connection_pool::available_connections() const
{
	return underlying_pool_ ? underlying_pool_->available_connections() : 0;
}

database::connection_stats connection_pool::get_stats() const
{
	return underlying_pool_ ? underlying_pool_->get_stats() : database::connection_stats{};
}

void connection_pool::request_shutdown()
{
	shutdown_token_.cancel();
}

void connection_pool::shutdown()
{
	if (shutdown_requested_.exchange(true))
	{
		// Already shutting down
		return;
	}

	// Signal shutdown via cancellation token
	shutdown_token_.cancel();

	// Clear adaptive queue
	if (adaptive_queue_)
	{
		adaptive_queue_->clear();
	}

	// Stop worker pool
	if (worker_pool_)
	{
		worker_pool_->stop(false); // Don't wait for remaining jobs
	}

	// Shutdown underlying pool
	if (underlying_pool_)
	{
		underlying_pool_->shutdown();
	}
}

bool connection_pool::is_shutdown_requested() const
{
	return shutdown_requested_.load();
}

std::string connection_pool::get_current_queue_type() const
{
	return adaptive_queue_ ? adaptive_queue_->get_current_type() : "UNKNOWN";
}

uint64_t connection_pool::get_queue_switch_count() const
{
	if (!adaptive_queue_)
	{
		return 0;
	}

	auto metrics = adaptive_queue_->get_metrics();
	return metrics.switch_count;
}

double connection_pool::get_contention_ratio() const
{
	if (!adaptive_queue_)
	{
		return 0.0;
	}

	auto metrics = adaptive_queue_->get_metrics();
	return metrics.get_contention_ratio() * 100.0; // Convert to percentage
}

double connection_pool::get_average_queue_latency_ns() const
{
	if (!adaptive_queue_)
	{
		return 0.0;
	}

	auto metrics = adaptive_queue_->get_metrics();
	return metrics.get_average_latency_ns();
}

std::shared_ptr<priority_metrics<connection_priority>> connection_pool::get_metrics() const
{
	return metrics_;
}

} // namespace database_server::pooling
