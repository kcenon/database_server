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
 * @file auth_middleware.h
 * @brief Authentication middleware for database gateway
 *
 * Provides authentication validation, rate limiting, and audit logging
 * for the database gateway server. Designed to be pluggable with custom
 * token validators.
 *
 * Features:
 * - Token-based authentication with pluggable validators
 * - Per-client rate limiting with sliding window
 * - Audit logging for security events
 * - Thread-safe metrics collection
 */

#pragma once

#include "query_protocol.h"
#include "query_types.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace database_server::gateway
{

/**
 * @struct auth_config
 * @brief Configuration for authentication middleware
 */
struct auth_config
{
	bool enabled = true;                  ///< Enable authentication
	bool validate_on_each_request = false; ///< Validate token on every request
	uint32_t token_refresh_window_ms = 300000; ///< Token refresh window (5 min)
};

/**
 * @struct rate_limit_config
 * @brief Configuration for rate limiting
 */
struct rate_limit_config
{
	bool enabled = true;                  ///< Enable rate limiting
	uint32_t requests_per_second = 100;   ///< Max requests per second
	uint32_t burst_size = 200;            ///< Max burst size
	uint32_t window_size_ms = 1000;       ///< Sliding window size
	uint32_t block_duration_ms = 60000;   ///< Block duration when limit exceeded
};

/**
 * @enum auth_event_type
 * @brief Types of authentication events for audit logging
 */
enum class auth_event_type : uint8_t
{
	auth_success = 1,
	auth_failure = 2,
	token_expired = 3,
	token_invalid = 4,
	rate_limited = 5,
	permission_denied = 6,
	session_created = 7,
	session_destroyed = 8
};

/**
 * @brief Convert auth_event_type to string
 */
constexpr const char* to_string(auth_event_type type) noexcept
{
	switch (type)
	{
	case auth_event_type::auth_success:
		return "auth_success";
	case auth_event_type::auth_failure:
		return "auth_failure";
	case auth_event_type::token_expired:
		return "token_expired";
	case auth_event_type::token_invalid:
		return "token_invalid";
	case auth_event_type::rate_limited:
		return "rate_limited";
	case auth_event_type::permission_denied:
		return "permission_denied";
	case auth_event_type::session_created:
		return "session_created";
	case auth_event_type::session_destroyed:
		return "session_destroyed";
	default:
		return "unknown";
	}
}

/**
 * @struct auth_event
 * @brief Authentication event for audit logging
 */
struct auth_event
{
	auth_event_type type;        ///< Event type
	std::string client_id;       ///< Client identifier
	std::string session_id;      ///< Session identifier
	std::string details;         ///< Additional details
	uint64_t timestamp = 0;      ///< Event timestamp (Unix epoch ms)
};

/**
 * @brief Callback type for audit logging
 */
using audit_callback_t = std::function<void(const auth_event&)>;

/**
 * @struct auth_result
 * @brief Result of authentication validation
 */
struct auth_result
{
	bool success = false;         ///< Whether authentication succeeded
	status_code code = status_code::ok; ///< Status code
	std::string message;          ///< Error message if failed
	std::string client_id;        ///< Validated client ID
	std::vector<std::string> permissions; ///< Client permissions (optional)
};

/**
 * @class auth_validator
 * @brief Interface for token validation
 *
 * Implement this interface to provide custom token validation logic.
 * The default implementation (simple_token_validator) only checks
 * token expiration.
 */
class auth_validator
{
public:
	virtual ~auth_validator() = default;

	/**
	 * @brief Validate an authentication token
	 * @param token The token to validate
	 * @return auth_result indicating success or failure
	 */
	[[nodiscard]] virtual auth_result validate(const auth_token& token) = 0;

	/**
	 * @brief Check if token needs refresh
	 * @param token The token to check
	 * @param refresh_window_ms Time window for refresh
	 * @return true if token should be refreshed
	 */
	[[nodiscard]] virtual bool needs_refresh(const auth_token& token,
											 uint32_t refresh_window_ms) const;
};

/**
 * @class simple_token_validator
 * @brief Simple token validator that checks expiration only
 *
 * This is the default validator that only checks if the token
 * is present and not expired. For production use, implement
 * a custom validator that verifies token signatures, claims, etc.
 */
class simple_token_validator : public auth_validator
{
public:
	[[nodiscard]] auth_result validate(const auth_token& token) override;
};

/**
 * @struct rate_limit_entry
 * @brief Rate limit tracking entry for a client
 */
struct rate_limit_entry
{
	std::vector<uint64_t> request_timestamps; ///< Timestamps of recent requests
	uint64_t blocked_until = 0;               ///< Block expiration time
	std::atomic<uint64_t> total_requests{0};  ///< Total request count
	std::atomic<uint64_t> rejected_requests{0}; ///< Rejected request count
};

/**
 * @class rate_limiter
 * @brief Sliding window rate limiter
 *
 * Implements per-client rate limiting using a sliding window algorithm.
 * Supports burst allowance and temporary blocking when limits are exceeded.
 *
 * Thread Safety:
 * - All public methods are thread-safe
 */
class rate_limiter
{
public:
	/**
	 * @brief Construct rate limiter with configuration
	 * @param config Rate limit configuration
	 */
	explicit rate_limiter(const rate_limit_config& config);

	/**
	 * @brief Check if request is allowed for client
	 * @param client_id Client identifier
	 * @return true if request is allowed
	 */
	[[nodiscard]] bool allow_request(const std::string& client_id);

	/**
	 * @brief Get remaining requests for client
	 * @param client_id Client identifier
	 * @return Number of remaining requests in current window
	 */
	[[nodiscard]] uint32_t remaining_requests(const std::string& client_id) const;

	/**
	 * @brief Check if client is currently blocked
	 * @param client_id Client identifier
	 * @return true if client is blocked
	 */
	[[nodiscard]] bool is_blocked(const std::string& client_id) const;

	/**
	 * @brief Get block expiration time for client
	 * @param client_id Client identifier
	 * @return Block expiration timestamp (0 if not blocked)
	 */
	[[nodiscard]] uint64_t block_expires_at(const std::string& client_id) const;

	/**
	 * @brief Reset rate limit for client
	 * @param client_id Client identifier
	 */
	void reset(const std::string& client_id);

	/**
	 * @brief Clean up expired entries
	 */
	void cleanup();

	/**
	 * @brief Get configuration
	 * @return Rate limit configuration
	 */
	[[nodiscard]] const rate_limit_config& config() const noexcept;

private:
	rate_limit_config config_;
	mutable std::mutex entries_mutex_;
	std::unordered_map<std::string, rate_limit_entry> entries_;
};

/**
 * @struct auth_metrics
 * @brief Metrics for authentication middleware
 *
 * Thread-safe metrics collection using atomic operations.
 */
struct auth_metrics
{
	std::atomic<uint64_t> total_auth_attempts{0};
	std::atomic<uint64_t> successful_auths{0};
	std::atomic<uint64_t> failed_auths{0};
	std::atomic<uint64_t> expired_tokens{0};
	std::atomic<uint64_t> invalid_tokens{0};
	std::atomic<uint64_t> rate_limited_requests{0};
	std::atomic<uint64_t> permission_denied{0};

	/**
	 * @brief Calculate authentication success rate
	 * @return Success rate as percentage (0.0 to 100.0)
	 */
	[[nodiscard]] double success_rate() const noexcept;

	/**
	 * @brief Calculate rate limit rejection rate
	 * @return Rejection rate as percentage (0.0 to 100.0)
	 */
	[[nodiscard]] double rate_limit_rejection_rate() const noexcept;

	/**
	 * @brief Reset all metrics
	 */
	void reset() noexcept;
};

/**
 * @class auth_middleware
 * @brief Authentication middleware for gateway server
 *
 * Provides authentication, rate limiting, and audit logging for
 * the database gateway. Can be configured with custom token validators.
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Callbacks may be invoked from multiple threads
 *
 * Usage Example:
 * @code
 * auth_config auth_cfg;
 * auth_cfg.enabled = true;
 *
 * rate_limit_config rate_cfg;
 * rate_cfg.requests_per_second = 100;
 *
 * auto validator = std::make_shared<custom_jwt_validator>();
 * auth_middleware middleware(auth_cfg, rate_cfg, validator);
 *
 * middleware.set_audit_callback([](const auth_event& event) {
 *     logger.info("Auth event: {} for {}", to_string(event.type), event.client_id);
 * });
 *
 * auto result = middleware.authenticate(session_id, request.token);
 * if (!result.success) {
 *     // Handle authentication failure
 * }
 *
 * if (!middleware.check_rate_limit(client_id)) {
 *     // Handle rate limit exceeded
 * }
 * @endcode
 */
class auth_middleware
{
public:
	/**
	 * @brief Construct auth middleware with default validator
	 * @param auth_config Authentication configuration
	 * @param rate_config Rate limit configuration
	 */
	auth_middleware(const auth_config& auth_config,
					const rate_limit_config& rate_config);

	/**
	 * @brief Construct auth middleware with custom validator
	 * @param auth_config Authentication configuration
	 * @param rate_config Rate limit configuration
	 * @param validator Custom token validator
	 */
	auth_middleware(const auth_config& auth_config,
					const rate_limit_config& rate_config,
					std::shared_ptr<auth_validator> validator);

	/**
	 * @brief Authenticate a client request
	 * @param session_id Session identifier
	 * @param token Authentication token
	 * @return auth_result indicating success or failure
	 */
	[[nodiscard]] auth_result authenticate(const std::string& session_id,
										   const auth_token& token);

	/**
	 * @brief Check rate limit for client
	 * @param client_id Client identifier
	 * @return true if request is allowed
	 */
	[[nodiscard]] bool check_rate_limit(const std::string& client_id);

	/**
	 * @brief Full authentication check including rate limiting
	 * @param session_id Session identifier
	 * @param token Authentication token
	 * @return auth_result with combined status
	 */
	[[nodiscard]] auth_result check(const std::string& session_id,
									const auth_token& token);

	/**
	 * @brief Record session creation event
	 * @param session_id Session identifier
	 * @param client_id Client identifier
	 */
	void on_session_created(const std::string& session_id,
							const std::string& client_id);

	/**
	 * @brief Record session destruction event
	 * @param session_id Session identifier
	 */
	void on_session_destroyed(const std::string& session_id);

	/**
	 * @brief Set audit callback for logging events
	 * @param callback Function to call for each auth event
	 */
	void set_audit_callback(audit_callback_t callback);

	/**
	 * @brief Get authentication metrics
	 * @return Reference to metrics structure
	 */
	[[nodiscard]] const auth_metrics& metrics() const noexcept;

	/**
	 * @brief Get rate limiter
	 * @return Reference to rate limiter
	 */
	[[nodiscard]] rate_limiter& get_rate_limiter() noexcept;

	/**
	 * @brief Get authentication configuration
	 * @return Authentication configuration
	 */
	[[nodiscard]] const auth_config& get_auth_config() const noexcept;

	/**
	 * @brief Check if authentication is enabled
	 * @return true if authentication is enabled
	 */
	[[nodiscard]] bool is_enabled() const noexcept;

private:
	/**
	 * @brief Emit audit event
	 */
	void emit_event(auth_event_type type,
					const std::string& client_id,
					const std::string& session_id,
					const std::string& details = "");

private:
	auth_config auth_config_;
	std::shared_ptr<auth_validator> validator_;
	rate_limiter rate_limiter_;
	auth_metrics metrics_;

	mutable std::mutex callback_mutex_;
	audit_callback_t audit_callback_;

	mutable std::mutex sessions_mutex_;
	std::unordered_map<std::string, std::string> session_client_map_; ///< session_id -> client_id
};

} // namespace database_server::gateway
