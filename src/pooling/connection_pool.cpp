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

namespace database_server::pooling
{

connection_pool::connection_pool(
	database::database_types db_type,
	const database::connection_pool_config& config,
	std::function<std::unique_ptr<database::database_base>()> factory,
	size_t thread_count,
	kcenon::thread::priority_aging_config aging_config)
	: underlying_pool_(
		  std::make_shared<database::connection_pool>(db_type, config, std::move(factory)))
	, aging_queue_(
		  std::make_shared<
			  kcenon::thread::aging_typed_job_queue_t<connection_priority>>(
			  aging_config))
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
		if (logger_)
		{
			logger_->log(kcenon::common::interfaces::log_level::error,
						 "Failed to start worker pool: " + start_result.error().message);
		}
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
	auto enqueue_result = aging_queue_->enqueue(std::move(base_job));
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
				= aging_queue_->dequeue(std::vector<connection_priority>{ priority });
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
	[[maybe_unused]] auto result = aging_queue_->enqueue(std::move(base_job));
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
	if (aging_queue_)
	{
		aging_queue_->clear();
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

kcenon::thread::aging_stats connection_pool::get_aging_stats() const
{
	return aging_queue_ ? aging_queue_->get_aging_stats() : kcenon::thread::aging_stats{};
}

uint64_t connection_pool::get_total_priority_boosts() const
{
	if (!aging_queue_)
	{
		return 0;
	}

	auto stats = aging_queue_->get_aging_stats();
	return stats.total_boosts_applied;
}

std::chrono::milliseconds connection_pool::get_max_wait_time() const
{
	if (!aging_queue_)
	{
		return std::chrono::milliseconds{0};
	}

	auto stats = aging_queue_->get_aging_stats();
	return stats.max_wait_time;
}

std::chrono::milliseconds connection_pool::get_average_wait_time() const
{
	if (!aging_queue_)
	{
		return std::chrono::milliseconds{0};
	}

	auto stats = aging_queue_->get_aging_stats();
	return stats.avg_wait_time;
}

std::shared_ptr<priority_metrics<connection_priority>> connection_pool::get_metrics() const
{
	return metrics_;
}

std::shared_ptr<kcenon::common::interfaces::ILogger> connection_pool::get_logger() const
{
	return logger_;
}

void connection_pool::set_logger(std::shared_ptr<kcenon::common::interfaces::ILogger> logger)
{
	logger_ = std::move(logger);
}

} // namespace database_server::pooling
