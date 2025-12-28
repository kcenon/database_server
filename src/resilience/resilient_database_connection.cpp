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

#include <kcenon/database_server/resilience/resilient_database_connection.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>

namespace database_server::resilience
{

resilient_database_connection::resilient_database_connection(
	std::unique_ptr<database::core::database_backend> backend, reconnection_config config)
	: backend_(std::move(backend))
	, config_(std::move(config))
{
	if (backend_)
	{
		health_monitor_ = std::make_unique<connection_health_monitor>(backend_.get());
	}
}

resilient_database_connection::~resilient_database_connection()
{
	stop_auto_recovery();
	if (backend_ && backend_->is_initialized())
	{
		backend_->shutdown();
	}
}

database::database_types resilient_database_connection::type() const
{
	return backend_ ? backend_->type() : database::database_types::none;
}

kcenon::common::VoidResult resilient_database_connection::initialize(
	const database::core::connection_config& config)
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (!backend_)
	{
		return kcenon::common::error_info{
			-1,
			"Backend is null",
			"resilient_database_connection"
		};
	}

	connection_config_ = config;
	set_state(connection_state::connecting);

	auto result = backend_->initialize(config);

	if (result.is_err())
	{
		set_state(connection_state::failed);
		last_error_message_ = result.error().message;
		return result;
	}

	set_state(connection_state::connected);
	reset_retry_state();

	if (health_monitor_)
	{
		health_monitor_->start_monitoring();
	}

	return kcenon::common::ok();
}

kcenon::common::VoidResult resilient_database_connection::shutdown()
{
	stop_auto_recovery();

	if (health_monitor_)
	{
		health_monitor_->stop_monitoring();
	}

	std::lock_guard<std::mutex> lock(mutex_);

	if (!backend_)
	{
		return kcenon::common::ok();
	}

	auto result = backend_->shutdown();
	set_state(connection_state::disconnected);

	return result;
}

bool resilient_database_connection::is_initialized() const
{
	return backend_ && backend_->is_initialized()
		   && state_.load() == connection_state::connected;
}

kcenon::common::Result<uint64_t> resilient_database_connection::insert_query(
	const std::string& query_string)
{
	return execute_with_retry(
		[this, &query_string]() { return backend_->insert_query(query_string); });
}

kcenon::common::Result<uint64_t> resilient_database_connection::update_query(
	const std::string& query_string)
{
	return execute_with_retry(
		[this, &query_string]() { return backend_->update_query(query_string); });
}

kcenon::common::Result<uint64_t> resilient_database_connection::delete_query(
	const std::string& query_string)
{
	return execute_with_retry(
		[this, &query_string]() { return backend_->delete_query(query_string); });
}

kcenon::common::Result<database::core::database_result>
resilient_database_connection::select_query(const std::string& query_string)
{
	return execute_with_retry(
		[this, &query_string]() { return backend_->select_query(query_string); });
}

kcenon::common::VoidResult resilient_database_connection::execute_query(
	const std::string& query_string)
{
	return execute_with_retry(
		[this, &query_string]() { return backend_->execute_query(query_string); });
}

kcenon::common::VoidResult resilient_database_connection::begin_transaction()
{
	// Transactions require stable connection - don't retry during transaction
	auto ensure_result = ensure_connected();
	if (ensure_result.is_err())
	{
		return ensure_result;
	}

	return backend_->begin_transaction();
}

kcenon::common::VoidResult resilient_database_connection::commit_transaction()
{
	// Don't retry commit - could lead to double commit
	if (!backend_)
	{
		return kcenon::common::error_info{
			-1,
			"Backend is null",
			"resilient_database_connection"
		};
	}

	return backend_->commit_transaction();
}

kcenon::common::VoidResult resilient_database_connection::rollback_transaction()
{
	// Don't retry rollback - idempotent operation
	if (!backend_)
	{
		return kcenon::common::error_info{
			-1,
			"Backend is null",
			"resilient_database_connection"
		};
	}

	return backend_->rollback_transaction();
}

bool resilient_database_connection::in_transaction() const
{
	return backend_ && backend_->in_transaction();
}

std::string resilient_database_connection::last_error() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return last_error_message_;
}

std::map<std::string, std::string> resilient_database_connection::connection_info() const
{
	auto info
		= backend_ ? backend_->connection_info() : std::map<std::string, std::string>{};

	// Add resilience info
	info["resilience_enabled"] = "true";
	info["connection_state"] = to_string(state_.load());
	info["retry_count"] = std::to_string(retry_count_.load());
	info["auto_recovery_enabled"] = auto_recovery_enabled_.load() ? "true" : "false";

	if (health_monitor_)
	{
		auto health = health_monitor_->get_health_status();
		info["health_score"] = std::to_string(health.health_score);
		info["is_healthy"] = health.is_healthy ? "true" : "false";
	}

	return info;
}

kcenon::common::VoidResult resilient_database_connection::ensure_connected()
{
	if (is_initialized())
	{
		return kcenon::common::ok();
	}

	return attempt_reconnect();
}

kcenon::common::Result<health_status> resilient_database_connection::check_health()
{
	if (!health_monitor_)
	{
		return kcenon::common::error_info{
			-1,
			"Health monitor not initialized",
			"resilient_database_connection"
		};
	}

	return health_monitor_->check_now();
}

void resilient_database_connection::start_auto_recovery()
{
	auto_recovery_enabled_ = true;
	if (health_monitor_)
	{
		health_monitor_->start_monitoring();
	}
}

void resilient_database_connection::stop_auto_recovery()
{
	auto_recovery_enabled_ = false;
}

connection_state resilient_database_connection::get_state() const noexcept
{
	return state_.load();
}

uint32_t resilient_database_connection::get_retry_count() const noexcept
{
	return retry_count_.load();
}

kcenon::common::VoidResult resilient_database_connection::attempt_reconnect()
{
	if (!config_.enable_auto_reconnect)
	{
		return kcenon::common::error_info{
			-2,
			"Auto reconnect disabled",
			"resilient_database_connection"
		};
	}

	std::lock_guard<std::mutex> lock(mutex_);

	if (retry_count_ >= config_.max_retries)
	{
		set_state(connection_state::failed);
		last_error_message_ = "Max retries exceeded";
		return kcenon::common::error_info{
			-3,
			"Max retries exceeded",
			"resilient_database_connection"
		};
	}

	set_state(connection_state::reconnecting);

	// Calculate and apply backoff delay
	auto delay = calculate_next_delay();
	std::this_thread::sleep_for(delay);

	// Attempt reconnection
	if (!backend_)
	{
		set_state(connection_state::failed);
		last_error_message_ = "Backend is null";
		return kcenon::common::error_info{
			-1,
			"Backend is null",
			"resilient_database_connection"
		};
	}

	backend_->shutdown();

	auto result = backend_->initialize(connection_config_);

	if (result.is_err())
	{
		retry_count_++;
		last_error_message_ = result.error().message;
		set_state(connection_state::failed);
		return result;
	}

	set_state(connection_state::connected);
	reset_retry_state();

	if (health_monitor_)
	{
		health_monitor_->reset_statistics();
		health_monitor_->start_monitoring();
	}

	return kcenon::common::ok();
}

template <typename Func>
auto resilient_database_connection::execute_with_retry(Func&& operation)
	-> decltype(operation())
{
	using result_type = decltype(operation());

	if (!backend_)
	{
		return kcenon::common::error_info{
			-1,
			"Backend is null",
			"resilient_database_connection"
		};
	}

	// Don't retry if in transaction
	if (in_transaction())
	{
		return operation();
	}

	auto start_time = std::chrono::high_resolution_clock::now();

	// Try operation
	auto result = operation();

	auto end_time = std::chrono::high_resolution_clock::now();
	auto latency
		= std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

	if (result.is_ok())
	{
		if (health_monitor_)
		{
			health_monitor_->record_success(latency);
		}
		return result;
	}

	// Operation failed - record and attempt reconnect if enabled
	if (health_monitor_)
	{
		health_monitor_->record_failure(result.error().message);
	}

	if (!config_.enable_auto_reconnect)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		last_error_message_ = result.error().message;
		return result;
	}

	// Attempt reconnection
	auto reconnect_result = attempt_reconnect();
	if (reconnect_result.is_err())
	{
		return result; // Return original error
	}

	// Retry operation after successful reconnection
	auto retry_result = operation();

	if (retry_result.is_ok() && health_monitor_)
	{
		health_monitor_->record_success(latency);
	}

	return retry_result;
}

std::chrono::milliseconds resilient_database_connection::calculate_next_delay()
{
	uint32_t attempts = retry_count_.load();
	double delay_ms = config_.initial_delay.count()
					  * std::pow(config_.backoff_multiplier, attempts);

	delay_ms = std::min(delay_ms, static_cast<double>(config_.max_delay.count()));

	return std::chrono::milliseconds(static_cast<int64_t>(delay_ms));
}

void resilient_database_connection::reset_retry_state()
{
	retry_count_ = 0;
}

void resilient_database_connection::set_state(connection_state new_state) noexcept
{
	state_.store(new_state);
}

// Explicit template instantiations for common query result types
template kcenon::common::Result<uint64_t>
resilient_database_connection::execute_with_retry<
	std::function<kcenon::common::Result<uint64_t>()>>(
	std::function<kcenon::common::Result<uint64_t>()>&&);

template kcenon::common::Result<database::core::database_result>
resilient_database_connection::execute_with_retry<
	std::function<kcenon::common::Result<database::core::database_result>()>>(
	std::function<kcenon::common::Result<database::core::database_result>()>&&);

template kcenon::common::VoidResult resilient_database_connection::execute_with_retry<
	std::function<kcenon::common::VoidResult()>>(
	std::function<kcenon::common::VoidResult()>&&);

} // namespace database_server::resilience
