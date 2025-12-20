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

#include <kcenon/database_server/gateway/auth_middleware.h>

#include <algorithm>

namespace database_server::gateway
{

namespace
{

uint64_t current_timestamp_ms()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

} // namespace

// ============================================================================
// auth_validator
// ============================================================================

bool auth_validator::needs_refresh(const auth_token& token,
								   uint32_t refresh_window_ms) const
{
	if (token.expires_at == 0)
	{
		return false;
	}

	auto now = current_timestamp_ms();
	auto refresh_threshold = token.expires_at - refresh_window_ms;
	return now >= refresh_threshold;
}

// ============================================================================
// simple_token_validator
// ============================================================================

auth_result simple_token_validator::validate(const auth_token& token)
{
	auth_result result;

	if (token.token.empty())
	{
		result.success = false;
		result.code = status_code::authentication_failed;
		result.message = "Token is empty";
		return result;
	}

	if (token.is_expired())
	{
		result.success = false;
		result.code = status_code::authentication_failed;
		result.message = "Token has expired";
		return result;
	}

	result.success = true;
	result.code = status_code::ok;
	result.client_id = token.client_id;
	return result;
}

// ============================================================================
// rate_limiter
// ============================================================================

rate_limiter::rate_limiter(const rate_limit_config& config)
	: config_(config)
{
}

bool rate_limiter::allow_request(const std::string& client_id)
{
	if (!config_.enabled)
	{
		return true;
	}

	std::lock_guard<std::mutex> lock(entries_mutex_);

	auto now = current_timestamp_ms();
	auto& entry = entries_[client_id];

	entry.total_requests.fetch_add(1, std::memory_order_relaxed);

	// Check if blocked
	if (entry.blocked_until > now)
	{
		entry.rejected_requests.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	// Clean old timestamps outside the window
	auto window_start = now - config_.window_size_ms;
	entry.request_timestamps.erase(
		std::remove_if(entry.request_timestamps.begin(),
					   entry.request_timestamps.end(),
					   [window_start](uint64_t ts) { return ts < window_start; }),
		entry.request_timestamps.end());

	// Check if within limits
	auto requests_in_window = static_cast<uint32_t>(entry.request_timestamps.size());
	auto max_requests = config_.requests_per_second * config_.window_size_ms / 1000;

	// Allow burst
	if (requests_in_window >= std::max(max_requests, config_.burst_size))
	{
		entry.blocked_until = now + config_.block_duration_ms;
		entry.rejected_requests.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	entry.request_timestamps.push_back(now);
	return true;
}

uint32_t rate_limiter::remaining_requests(const std::string& client_id) const
{
	if (!config_.enabled)
	{
		return UINT32_MAX;
	}

	std::lock_guard<std::mutex> lock(entries_mutex_);

	auto it = entries_.find(client_id);
	if (it == entries_.end())
	{
		return config_.requests_per_second * config_.window_size_ms / 1000;
	}

	auto now = current_timestamp_ms();
	auto window_start = now - config_.window_size_ms;

	auto requests_in_window = std::count_if(
		it->second.request_timestamps.begin(),
		it->second.request_timestamps.end(),
		[window_start](uint64_t ts) { return ts >= window_start; });

	auto max_requests = config_.requests_per_second * config_.window_size_ms / 1000;
	if (static_cast<uint32_t>(requests_in_window) >= max_requests)
	{
		return 0;
	}

	return max_requests - static_cast<uint32_t>(requests_in_window);
}

bool rate_limiter::is_blocked(const std::string& client_id) const
{
	if (!config_.enabled)
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(entries_mutex_);

	auto it = entries_.find(client_id);
	if (it == entries_.end())
	{
		return false;
	}

	return it->second.blocked_until > current_timestamp_ms();
}

uint64_t rate_limiter::block_expires_at(const std::string& client_id) const
{
	std::lock_guard<std::mutex> lock(entries_mutex_);

	auto it = entries_.find(client_id);
	if (it == entries_.end())
	{
		return 0;
	}

	auto now = current_timestamp_ms();
	if (it->second.blocked_until <= now)
	{
		return 0;
	}

	return it->second.blocked_until;
}

void rate_limiter::reset(const std::string& client_id)
{
	std::lock_guard<std::mutex> lock(entries_mutex_);
	entries_.erase(client_id);
}

void rate_limiter::cleanup()
{
	std::lock_guard<std::mutex> lock(entries_mutex_);

	auto now = current_timestamp_ms();
	auto window_start = now - config_.window_size_ms;

	for (auto it = entries_.begin(); it != entries_.end();)
	{
		// Remove entry if not blocked and no recent requests
		bool has_recent_requests = std::any_of(
			it->second.request_timestamps.begin(),
			it->second.request_timestamps.end(),
			[window_start](uint64_t ts) { return ts >= window_start; });

		bool is_blocked = it->second.blocked_until > now;

		if (!has_recent_requests && !is_blocked)
		{
			it = entries_.erase(it);
		}
		else
		{
			++it;
		}
	}
}

const rate_limit_config& rate_limiter::config() const noexcept
{
	return config_;
}

// ============================================================================
// auth_metrics
// ============================================================================

double auth_metrics::success_rate() const noexcept
{
	auto total = total_auth_attempts.load(std::memory_order_relaxed);
	if (total == 0)
	{
		return 100.0;
	}

	auto successful = successful_auths.load(std::memory_order_relaxed);
	return (static_cast<double>(successful) / static_cast<double>(total)) * 100.0;
}

double auth_metrics::rate_limit_rejection_rate() const noexcept
{
	auto total = total_auth_attempts.load(std::memory_order_relaxed);
	if (total == 0)
	{
		return 0.0;
	}

	auto rejected = rate_limited_requests.load(std::memory_order_relaxed);
	return (static_cast<double>(rejected) / static_cast<double>(total)) * 100.0;
}

void auth_metrics::reset() noexcept
{
	total_auth_attempts.store(0, std::memory_order_relaxed);
	successful_auths.store(0, std::memory_order_relaxed);
	failed_auths.store(0, std::memory_order_relaxed);
	expired_tokens.store(0, std::memory_order_relaxed);
	invalid_tokens.store(0, std::memory_order_relaxed);
	rate_limited_requests.store(0, std::memory_order_relaxed);
	permission_denied.store(0, std::memory_order_relaxed);
}

// ============================================================================
// auth_middleware
// ============================================================================

auth_middleware::auth_middleware(const auth_config& auth_config,
								 const rate_limit_config& rate_config)
	: auth_config_(auth_config)
	, validator_(std::make_shared<simple_token_validator>())
	, rate_limiter_(rate_config)
{
}

auth_middleware::auth_middleware(const auth_config& auth_config,
								 const rate_limit_config& rate_config,
								 std::shared_ptr<auth_validator> validator)
	: auth_config_(auth_config)
	, validator_(validator ? std::move(validator) : std::make_shared<simple_token_validator>())
	, rate_limiter_(rate_config)
{
}

auth_result auth_middleware::authenticate(const std::string& session_id,
										  const auth_token& token)
{
	metrics_.total_auth_attempts.fetch_add(1, std::memory_order_relaxed);

	if (!auth_config_.enabled)
	{
		auth_result result;
		result.success = true;
		result.code = status_code::ok;
		result.client_id = token.client_id;
		metrics_.successful_auths.fetch_add(1, std::memory_order_relaxed);
		return result;
	}

	auto result = validator_->validate(token);

	if (result.success)
	{
		metrics_.successful_auths.fetch_add(1, std::memory_order_relaxed);
		emit_event(auth_event_type::auth_success, token.client_id, session_id);

		// Update session-client mapping
		{
			std::lock_guard<std::mutex> lock(sessions_mutex_);
			session_client_map_[session_id] = token.client_id;
		}
	}
	else
	{
		metrics_.failed_auths.fetch_add(1, std::memory_order_relaxed);

		if (token.is_expired())
		{
			metrics_.expired_tokens.fetch_add(1, std::memory_order_relaxed);
			emit_event(auth_event_type::token_expired, token.client_id, session_id,
					   result.message);
		}
		else
		{
			metrics_.invalid_tokens.fetch_add(1, std::memory_order_relaxed);
			emit_event(auth_event_type::token_invalid, token.client_id, session_id,
					   result.message);
		}
	}

	return result;
}

bool auth_middleware::check_rate_limit(const std::string& client_id)
{
	bool allowed = rate_limiter_.allow_request(client_id);

	if (!allowed)
	{
		metrics_.rate_limited_requests.fetch_add(1, std::memory_order_relaxed);
		emit_event(auth_event_type::rate_limited, client_id, "",
				   "Rate limit exceeded");
	}

	return allowed;
}

auth_result auth_middleware::check(const std::string& session_id,
								   const auth_token& token)
{
	// First check rate limit
	if (!check_rate_limit(token.client_id.empty() ? session_id : token.client_id))
	{
		auth_result result;
		result.success = false;
		result.code = status_code::rate_limited;
		result.message = "Rate limit exceeded";
		return result;
	}

	// Then authenticate
	return authenticate(session_id, token);
}

void auth_middleware::on_session_created(const std::string& session_id,
										 const std::string& client_id)
{
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		session_client_map_[session_id] = client_id;
	}

	emit_event(auth_event_type::session_created, client_id, session_id);
}

void auth_middleware::on_session_destroyed(const std::string& session_id)
{
	std::string client_id;
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		if (auto it = session_client_map_.find(session_id);
			it != session_client_map_.end())
		{
			client_id = it->second;
			session_client_map_.erase(it);
		}
	}

	emit_event(auth_event_type::session_destroyed, client_id, session_id);
}

void auth_middleware::set_audit_callback(audit_callback_t callback)
{
	std::lock_guard<std::mutex> lock(callback_mutex_);
	audit_callback_ = std::move(callback);
}

const auth_metrics& auth_middleware::metrics() const noexcept
{
	return metrics_;
}

rate_limiter& auth_middleware::get_rate_limiter() noexcept
{
	return rate_limiter_;
}

const auth_config& auth_middleware::get_auth_config() const noexcept
{
	return auth_config_;
}

bool auth_middleware::is_enabled() const noexcept
{
	return auth_config_.enabled;
}

void auth_middleware::emit_event(auth_event_type type,
								 const std::string& client_id,
								 const std::string& session_id,
								 const std::string& details)
{
	audit_callback_t callback;
	{
		std::lock_guard<std::mutex> lock(callback_mutex_);
		callback = audit_callback_;
	}

	if (callback)
	{
		auth_event event;
		event.type = type;
		event.client_id = client_id;
		event.session_id = session_id;
		event.details = details;
		event.timestamp = current_timestamp_ms();

		callback(event);
	}
}

} // namespace database_server::gateway
