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

#include <kcenon/database_server/resilience/connection_health_monitor.h>

#include <algorithm>
#include <numeric>
#include <thread>

namespace database_server::resilience
{

/**
 * @brief Job implementation for health monitoring loop
 */
class health_monitor_job : public kcenon::common::interfaces::IJob
{
public:
	explicit health_monitor_job(connection_health_monitor* monitor)
		: monitor_(monitor)
	{
	}

	kcenon::common::VoidResult execute() override
	{
		if (monitor_)
		{
			monitor_->monitoring_loop();
		}
		return kcenon::common::ok();
	}

	std::string get_name() const override { return "health_monitor_loop"; }
	int get_priority() const override { return 0; }

private:
	connection_health_monitor* monitor_;
};

connection_health_monitor::connection_health_monitor(
	database::core::database_backend* backend,
	health_check_config config,
	std::shared_ptr<kcenon::common::interfaces::IExecutor> executor)
	: backend_(backend)
	, config_(std::move(config))
	, executor_(std::move(executor))
	, connection_start_time_(std::chrono::system_clock::now())
{
	current_status_.last_check_time = std::chrono::system_clock::now();
}

connection_health_monitor::~connection_health_monitor()
{
	stop_monitoring();
}

void connection_health_monitor::start_monitoring()
{
	if (is_monitoring_.exchange(true))
	{
		return; // Already monitoring
	}

	stop_requested_ = false;

	if (executor_)
	{
		// Use IExecutor for background task
		auto job = std::make_unique<health_monitor_job>(this);
		auto result = executor_->execute(std::move(job));
		if (result.is_ok())
		{
			monitoring_future_ = std::move(result.unwrap());
		}
	}
	else
	{
		// Fallback to std::async if no executor provided
		monitoring_future_ = std::async(std::launch::async, [this] { monitoring_loop(); });
	}
}

void connection_health_monitor::stop_monitoring()
{
	if (!is_monitoring_.exchange(false))
	{
		return; // Not monitoring
	}

	stop_requested_ = true;

	// Wait for monitoring task to complete
	if (monitoring_future_.valid())
	{
		// Use wait_for with timeout to avoid indefinite blocking
		auto status = monitoring_future_.wait_for(std::chrono::seconds(5));
		if (status == std::future_status::timeout)
		{
			// Monitoring loop should have exited, but continue anyway
		}
	}
}

kcenon::common::Result<health_status> connection_health_monitor::check_now()
{
	if (!backend_ || !backend_->is_initialized())
	{
		std::lock_guard<std::mutex> lock(mutex_);
		current_status_.is_healthy = false;
		current_status_.health_score = 0;
		current_status_.status_message = "Backend not initialized";
		current_status_.last_check_time = std::chrono::system_clock::now();

		return kcenon::common::error_info{
			-1, "Backend not initialized", "connection_health_monitor"
		};
	}

	auto start = std::chrono::high_resolution_clock::now();
	auto result = execute_heartbeat();
	auto end = std::chrono::high_resolution_clock::now();

	auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

	std::lock_guard<std::mutex> lock(mutex_);

	if (result.is_err())
	{
		consecutive_failures_++;
		consecutive_successes_ = 0;
		failed_queries_++;
		total_queries_++;

		current_status_.is_healthy = false;
		current_status_.status_message = result.error().message;
	}
	else
	{
		consecutive_successes_++;
		consecutive_failures_ = 0;
		successful_queries_++;
		total_queries_++;

		update_latency_average(latency);

		current_status_.is_healthy = true;
		current_status_.latency = latency;
		current_status_.status_message = "Connection healthy";
	}

	current_status_.successful_queries = successful_queries_;
	current_status_.failed_queries = failed_queries_;
	current_status_.health_score = calculate_health_score();
	current_status_.last_check_time = std::chrono::system_clock::now();

	return current_status_;
}

health_status connection_health_monitor::get_health_status() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return current_status_;
}

void connection_health_monitor::record_success(std::chrono::milliseconds query_latency)
{
	consecutive_successes_++;
	consecutive_failures_ = 0;
	successful_queries_++;
	total_queries_++;

	std::lock_guard<std::mutex> lock(mutex_);
	update_latency_average(query_latency);
	current_status_.health_score = calculate_health_score();
}

void connection_health_monitor::record_failure(const std::string& error_message)
{
	consecutive_failures_++;
	consecutive_successes_ = 0;
	failed_queries_++;
	total_queries_++;

	std::lock_guard<std::mutex> lock(mutex_);
	current_status_.is_healthy = consecutive_failures_ < config_.failure_threshold;
	current_status_.status_message = error_message;
	current_status_.health_score = calculate_health_score();
}

bool connection_health_monitor::is_healthy() const noexcept
{
	return consecutive_failures_ < config_.failure_threshold
		   && get_health_score() >= config_.min_health_score;
}

uint32_t connection_health_monitor::get_health_score() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex_);
	return current_status_.health_score;
}

bool connection_health_monitor::predict_failure() const
{
	std::lock_guard<std::mutex> lock(mutex_);

	// Predict failure if:
	// 1. Consecutive failures approaching threshold
	if (consecutive_failures_ >= config_.failure_threshold - 1)
	{
		return true;
	}

	// 2. Health score declining trend (below 60)
	if (current_status_.health_score < 60)
	{
		return true;
	}

	// 3. Recent latency spike (> 2x average)
	if (!latency_history_.empty())
	{
		auto avg_latency = std::accumulate(
							   latency_history_.begin(),
							   latency_history_.end(),
							   std::chrono::milliseconds(0))
						   / latency_history_.size();

		if (current_status_.latency > avg_latency * 2)
		{
			return true;
		}
	}

	return false;
}

void connection_health_monitor::reset_statistics()
{
	total_queries_ = 0;
	successful_queries_ = 0;
	failed_queries_ = 0;
	consecutive_failures_ = 0;
	consecutive_successes_ = 0;

	std::lock_guard<std::mutex> lock(mutex_);
	latency_history_.clear();
	current_status_ = health_status{};
	current_status_.last_check_time = std::chrono::system_clock::now();
	connection_start_time_ = std::chrono::system_clock::now();
}

kcenon::common::VoidResult connection_health_monitor::execute_heartbeat()
{
	if (!backend_)
	{
		return kcenon::common::error_info{
			-1, "Backend is null", "connection_health_monitor"
		};
	}

	// Execute simple SELECT query as heartbeat
	// Most databases support "SELECT 1" as a connectivity check
	auto result = backend_->select_query("SELECT 1");

	if (result.is_err())
	{
		return result.error();
	}

	return kcenon::common::ok();
}

uint32_t connection_health_monitor::calculate_health_score() const
{
	// Calculate health score (0-100) based on multiple factors

	// Factor 1: Success rate (40% weight)
	uint64_t total = total_queries_.load();
	double success_rate
		= total > 0 ? (static_cast<double>(successful_queries_) / total) : 1.0;
	uint32_t success_score = static_cast<uint32_t>(success_rate * 40);

	// Factor 2: Latency performance (30% weight)
	// Assume < 10ms = excellent, 10-50ms = good, 50-100ms = fair, > 100ms = poor
	uint32_t latency_score = 30;
	if (!latency_history_.empty())
	{
		auto avg_latency = std::accumulate(
							   latency_history_.begin(),
							   latency_history_.end(),
							   std::chrono::milliseconds(0))
						   / latency_history_.size();

		if (avg_latency.count() < 10)
		{
			latency_score = 30;
		}
		else if (avg_latency.count() < 50)
		{
			latency_score = 25;
		}
		else if (avg_latency.count() < 100)
		{
			latency_score = 15;
		}
		else
		{
			latency_score = 5;
		}
	}

	// Factor 3: Consecutive success streak (20% weight)
	uint32_t streak_score = std::min(consecutive_successes_.load(), 10u) * 2;

	// Factor 4: Connection uptime (10% weight)
	auto uptime = std::chrono::system_clock::now() - connection_start_time_;
	auto uptime_minutes
		= std::chrono::duration_cast<std::chrono::minutes>(uptime).count();
	uint32_t uptime_score
		= std::min(static_cast<uint32_t>(uptime_minutes / 6), 10u); // Max at 1 hour

	// Penalty for consecutive failures
	uint32_t failure_penalty = consecutive_failures_.load() * 10;

	uint32_t total_score = success_score + latency_score + streak_score + uptime_score;
	total_score = (total_score > failure_penalty) ? (total_score - failure_penalty) : 0;

	return std::min(total_score, 100u);
}

void connection_health_monitor::update_latency_average(
	std::chrono::milliseconds new_latency)
{
	latency_history_.push_back(new_latency);
	if (latency_history_.size() > MAX_LATENCY_SAMPLES)
	{
		latency_history_.erase(latency_history_.begin());
	}
}

void connection_health_monitor::monitoring_loop()
{
	while (!stop_requested_)
	{
		if (config_.enable_heartbeat)
		{
			check_now();
		}

		// Sleep for heartbeat interval, checking stop_requested frequently
		auto sleep_until
			= std::chrono::steady_clock::now() + config_.heartbeat_interval;
		while (std::chrono::steady_clock::now() < sleep_until && !stop_requested_)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

} // namespace database_server::resilience
