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
 * @file auth_middleware_test.cpp
 * @brief Unit tests for auth_middleware component
 *
 * Tests cover:
 * - Token validation success/failure cases
 * - Token expiration handling
 * - Rate limiting integration
 * - Audit event emission
 * - Metrics collection
 * - Session management
 */

#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <kcenon/database_server/gateway/auth_middleware.h>

using namespace database_server::gateway;

namespace
{

uint64_t current_timestamp_ms()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

uint64_t future_timestamp_ms(uint32_t offset_ms)
{
	return current_timestamp_ms() + offset_ms;
}

uint64_t past_timestamp_ms(uint32_t offset_ms)
{
	return current_timestamp_ms() - offset_ms;
}

} // namespace

// ============================================================================
// Auth Config Tests
// ============================================================================

class AuthConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(AuthConfigTest, DefaultConfiguration)
{
	auth_config config;

	EXPECT_TRUE(config.enabled);
	EXPECT_FALSE(config.validate_on_each_request);
	EXPECT_EQ(config.token_refresh_window_ms, 300000);
}

// ============================================================================
// Auth Event Type Tests
// ============================================================================

class AuthEventTypeTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(AuthEventTypeTest, EventTypeToString)
{
	EXPECT_STREQ(to_string(auth_event_type::auth_success), "auth_success");
	EXPECT_STREQ(to_string(auth_event_type::auth_failure), "auth_failure");
	EXPECT_STREQ(to_string(auth_event_type::token_expired), "token_expired");
	EXPECT_STREQ(to_string(auth_event_type::token_invalid), "token_invalid");
	EXPECT_STREQ(to_string(auth_event_type::rate_limited), "rate_limited");
	EXPECT_STREQ(to_string(auth_event_type::permission_denied), "permission_denied");
	EXPECT_STREQ(to_string(auth_event_type::session_created), "session_created");
	EXPECT_STREQ(to_string(auth_event_type::session_destroyed), "session_destroyed");
}

// ============================================================================
// Simple Token Validator Tests
// ============================================================================

class SimpleTokenValidatorTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(SimpleTokenValidatorTest, EmptyTokenFails)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "";

	auto result = validator.validate(token);

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::authentication_failed);
	EXPECT_FALSE(result.message.empty());
}

TEST_F(SimpleTokenValidatorTest, ValidTokenSucceeds)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "valid-test-token";
	token.client_id = "test-client";
	token.expires_at = 0; // No expiry

	auto result = validator.validate(token);

	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.code, status_code::ok);
	EXPECT_EQ(result.client_id, "test-client");
}

TEST_F(SimpleTokenValidatorTest, ExpiredTokenFails)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "expired-token";
	token.client_id = "test-client";
	token.expires_at = past_timestamp_ms(1000); // Expired 1 second ago

	auto result = validator.validate(token);

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::authentication_failed);
}

TEST_F(SimpleTokenValidatorTest, FutureExpirySucceeds)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "future-token";
	token.client_id = "test-client";
	token.expires_at = future_timestamp_ms(3600000); // Expires in 1 hour

	auto result = validator.validate(token);

	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.code, status_code::ok);
}

TEST_F(SimpleTokenValidatorTest, NeedsRefreshWithinWindow)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "refresh-token";
	token.client_id = "test-client";
	token.expires_at = future_timestamp_ms(60000); // Expires in 1 minute

	// Should need refresh if window is 5 minutes
	EXPECT_TRUE(validator.needs_refresh(token, 300000));
}

TEST_F(SimpleTokenValidatorTest, NoRefreshNeededOutsideWindow)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "no-refresh-token";
	token.client_id = "test-client";
	token.expires_at = future_timestamp_ms(600000); // Expires in 10 minutes

	// Should not need refresh if window is 5 minutes
	EXPECT_FALSE(validator.needs_refresh(token, 300000));
}

TEST_F(SimpleTokenValidatorTest, NoRefreshNeededForNoExpiry)
{
	simple_token_validator validator;
	auth_token token;
	token.token = "no-expiry-token";
	token.expires_at = 0;

	EXPECT_FALSE(validator.needs_refresh(token, 300000));
}

// ============================================================================
// Auth Metrics Tests
// ============================================================================

class AuthMetricsTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(AuthMetricsTest, InitialMetricsAreZero)
{
	auth_metrics metrics;

	EXPECT_EQ(metrics.total_auth_attempts.load(), 0u);
	EXPECT_EQ(metrics.successful_auths.load(), 0u);
	EXPECT_EQ(metrics.failed_auths.load(), 0u);
	EXPECT_EQ(metrics.expired_tokens.load(), 0u);
	EXPECT_EQ(metrics.invalid_tokens.load(), 0u);
	EXPECT_EQ(metrics.rate_limited_requests.load(), 0u);
	EXPECT_EQ(metrics.permission_denied.load(), 0u);
}

TEST_F(AuthMetricsTest, SuccessRateWithNoAttempts)
{
	auth_metrics metrics;

	EXPECT_DOUBLE_EQ(metrics.success_rate(), 100.0);
}

TEST_F(AuthMetricsTest, SuccessRateCalculation)
{
	auth_metrics metrics;
	metrics.total_auth_attempts.store(100);
	metrics.successful_auths.store(75);

	EXPECT_DOUBLE_EQ(metrics.success_rate(), 75.0);
}

TEST_F(AuthMetricsTest, RateLimitRejectionRateCalculation)
{
	auth_metrics metrics;
	metrics.total_auth_attempts.store(100);
	metrics.rate_limited_requests.store(10);

	EXPECT_DOUBLE_EQ(metrics.rate_limit_rejection_rate(), 10.0);
}

TEST_F(AuthMetricsTest, RateLimitRejectionRateWithNoAttempts)
{
	auth_metrics metrics;

	EXPECT_DOUBLE_EQ(metrics.rate_limit_rejection_rate(), 0.0);
}

TEST_F(AuthMetricsTest, ResetClearsAllMetrics)
{
	auth_metrics metrics;
	metrics.total_auth_attempts.store(100);
	metrics.successful_auths.store(50);
	metrics.failed_auths.store(50);
	metrics.expired_tokens.store(10);
	metrics.invalid_tokens.store(40);
	metrics.rate_limited_requests.store(5);
	metrics.permission_denied.store(3);

	metrics.reset();

	EXPECT_EQ(metrics.total_auth_attempts.load(), 0u);
	EXPECT_EQ(metrics.successful_auths.load(), 0u);
	EXPECT_EQ(metrics.failed_auths.load(), 0u);
	EXPECT_EQ(metrics.expired_tokens.load(), 0u);
	EXPECT_EQ(metrics.invalid_tokens.load(), 0u);
	EXPECT_EQ(metrics.rate_limited_requests.load(), 0u);
	EXPECT_EQ(metrics.permission_denied.load(), 0u);
}

// ============================================================================
// Auth Middleware Basic Tests
// ============================================================================

class AuthMiddlewareTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auth_cfg_.enabled = true;
		auth_cfg_.validate_on_each_request = false;

		rate_cfg_.enabled = true;
		rate_cfg_.requests_per_second = 100;
		rate_cfg_.burst_size = 200;
		rate_cfg_.window_size_ms = 1000;
		rate_cfg_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	auth_config auth_cfg_;
	rate_limit_config rate_cfg_;
};

TEST_F(AuthMiddlewareTest, ConstructionWithDefaultValidator)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	EXPECT_TRUE(middleware.is_enabled());
}

TEST_F(AuthMiddlewareTest, ConstructionWithCustomValidator)
{
	auto validator = std::make_shared<simple_token_validator>();
	auth_middleware middleware(auth_cfg_, rate_cfg_, validator);

	EXPECT_TRUE(middleware.is_enabled());
}

TEST_F(AuthMiddlewareTest, DisabledMiddlewareAlwaysSucceeds)
{
	auth_cfg_.enabled = false;
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = ""; // Empty token would normally fail

	auto result = middleware.authenticate("session1", token);

	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.code, status_code::ok);
}

TEST_F(AuthMiddlewareTest, AuthenticateValidToken)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = "valid-token";
	token.client_id = "client1";
	token.expires_at = 0;

	auto result = middleware.authenticate("session1", token);

	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.code, status_code::ok);
	EXPECT_EQ(result.client_id, "client1");
}

TEST_F(AuthMiddlewareTest, AuthenticateEmptyTokenFails)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = "";
	token.client_id = "client1";

	auto result = middleware.authenticate("session1", token);

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::authentication_failed);
}

TEST_F(AuthMiddlewareTest, AuthenticateExpiredTokenFails)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = "expired-token";
	token.client_id = "client1";
	token.expires_at = past_timestamp_ms(1000);

	auto result = middleware.authenticate("session1", token);

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::authentication_failed);
}

// ============================================================================
// Auth Middleware Rate Limiting Integration Tests
// ============================================================================

class AuthMiddlewareRateLimitTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auth_cfg_.enabled = true;

		rate_cfg_.enabled = true;
		rate_cfg_.requests_per_second = 5;
		rate_cfg_.burst_size = 5;
		rate_cfg_.window_size_ms = 1000;
		rate_cfg_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	auth_config auth_cfg_;
	rate_limit_config rate_cfg_;
};

TEST_F(AuthMiddlewareRateLimitTest, RateLimitAllowsRequests)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	EXPECT_TRUE(middleware.check_rate_limit("client1"));
}

TEST_F(AuthMiddlewareRateLimitTest, RateLimitBlocksAfterExceeding)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	// Exhaust rate limit
	for (uint32_t i = 0; i < rate_cfg_.burst_size; ++i)
	{
		middleware.check_rate_limit("client1");
	}

	// Next should be blocked
	EXPECT_FALSE(middleware.check_rate_limit("client1"));
}

TEST_F(AuthMiddlewareRateLimitTest, CheckCombinesRateLimitAndAuth)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = "valid-token";
	token.client_id = "client1";

	// First check should pass
	auto result = middleware.check("session1", token);
	EXPECT_TRUE(result.success);

	// Exhaust rate limit
	for (uint32_t i = 1; i < rate_cfg_.burst_size; ++i)
	{
		middleware.check("session1", token);
	}

	// Next check should fail with rate_limited
	result = middleware.check("session1", token);
	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::rate_limited);
}

// ============================================================================
// Auth Middleware Metrics Tests
// ============================================================================

class AuthMiddlewareMetricsTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auth_cfg_.enabled = true;

		rate_cfg_.enabled = true;
		rate_cfg_.requests_per_second = 100;
		rate_cfg_.burst_size = 200;
	}

	void TearDown() override {}

	auth_config auth_cfg_;
	rate_limit_config rate_cfg_;
};

TEST_F(AuthMiddlewareMetricsTest, TracksSuccessfulAuth)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = "valid-token";
	token.client_id = "client1";

	middleware.authenticate("session1", token);

	EXPECT_EQ(middleware.metrics().total_auth_attempts.load(), 1u);
	EXPECT_EQ(middleware.metrics().successful_auths.load(), 1u);
	EXPECT_EQ(middleware.metrics().failed_auths.load(), 0u);
}

TEST_F(AuthMiddlewareMetricsTest, TracksFailedAuth)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = ""; // Invalid

	middleware.authenticate("session1", token);

	EXPECT_EQ(middleware.metrics().total_auth_attempts.load(), 1u);
	EXPECT_EQ(middleware.metrics().successful_auths.load(), 0u);
	EXPECT_EQ(middleware.metrics().failed_auths.load(), 1u);
	EXPECT_EQ(middleware.metrics().invalid_tokens.load(), 1u);
}

TEST_F(AuthMiddlewareMetricsTest, TracksExpiredTokens)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	auth_token token;
	token.token = "expired-token";
	token.expires_at = past_timestamp_ms(1000);

	middleware.authenticate("session1", token);

	EXPECT_EQ(middleware.metrics().expired_tokens.load(), 1u);
}

TEST_F(AuthMiddlewareMetricsTest, TracksRateLimitedRequests)
{
	rate_limit_config rate_cfg;
	rate_cfg.enabled = true;
	rate_cfg.requests_per_second = 2;
	rate_cfg.burst_size = 2;
	rate_cfg.window_size_ms = 1000;
	rate_cfg.block_duration_ms = 1000;

	auth_middleware middleware(auth_cfg_, rate_cfg);

	// Exhaust burst (2 allowed) and trigger rate limit
	for (int i = 0; i < 5; ++i)
	{
		middleware.check_rate_limit("client1");
	}

	// At least 2 requests should be rate limited (5 - 2 burst - 1 triggers = 2)
	EXPECT_GT(middleware.metrics().rate_limited_requests.load(), 0u);
}

// ============================================================================
// Auth Middleware Audit Callback Tests
// ============================================================================

class AuthMiddlewareAuditTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auth_cfg_.enabled = true;
		rate_cfg_.enabled = true;
		rate_cfg_.burst_size = 5;
	}

	void TearDown() override {}

	auth_config auth_cfg_;
	rate_limit_config rate_cfg_;
};

TEST_F(AuthMiddlewareAuditTest, CallbackInvokedOnSuccess)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	std::vector<auth_event> events;
	std::mutex events_mutex;

	middleware.set_audit_callback(
		[&events, &events_mutex](const auth_event& event)
		{
			std::lock_guard<std::mutex> lock(events_mutex);
			events.push_back(event);
		});

	auth_token token;
	token.token = "valid-token";
	token.client_id = "client1";

	middleware.authenticate("session1", token);

	std::lock_guard<std::mutex> lock(events_mutex);
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].type, auth_event_type::auth_success);
	EXPECT_EQ(events[0].client_id, "client1");
	EXPECT_EQ(events[0].session_id, "session1");
}

TEST_F(AuthMiddlewareAuditTest, CallbackInvokedOnFailure)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	std::vector<auth_event> events;
	std::mutex events_mutex;

	middleware.set_audit_callback(
		[&events, &events_mutex](const auth_event& event)
		{
			std::lock_guard<std::mutex> lock(events_mutex);
			events.push_back(event);
		});

	auth_token token;
	token.token = "";
	token.client_id = "client1";

	middleware.authenticate("session1", token);

	std::lock_guard<std::mutex> lock(events_mutex);
	ASSERT_GE(events.size(), 1u);
	EXPECT_EQ(events[0].type, auth_event_type::token_invalid);
}

TEST_F(AuthMiddlewareAuditTest, CallbackInvokedOnRateLimit)
{
	rate_limit_config rate_cfg;
	rate_cfg.enabled = true;
	rate_cfg.requests_per_second = 2;
	rate_cfg.burst_size = 2;
	rate_cfg.window_size_ms = 1000;
	rate_cfg.block_duration_ms = 1000;

	auth_middleware middleware(auth_cfg_, rate_cfg);

	std::vector<auth_event> events;
	std::mutex events_mutex;

	middleware.set_audit_callback(
		[&events, &events_mutex](const auth_event& event)
		{
			std::lock_guard<std::mutex> lock(events_mutex);
			events.push_back(event);
		});

	// Exhaust rate limit (2 allowed, rest will be rate limited)
	for (int i = 0; i < 5; ++i)
	{
		middleware.check_rate_limit("client1");
	}

	std::lock_guard<std::mutex> lock(events_mutex);
	bool found_rate_limited = false;
	for (const auto& event : events)
	{
		if (event.type == auth_event_type::rate_limited)
		{
			found_rate_limited = true;
			break;
		}
	}
	EXPECT_TRUE(found_rate_limited);
}

// ============================================================================
// Auth Middleware Session Management Tests
// ============================================================================

class AuthMiddlewareSessionTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auth_cfg_.enabled = true;
		rate_cfg_.enabled = true;
	}

	void TearDown() override {}

	auth_config auth_cfg_;
	rate_limit_config rate_cfg_;
};

TEST_F(AuthMiddlewareSessionTest, SessionCreatedEventEmitted)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	std::vector<auth_event> events;
	std::mutex events_mutex;

	middleware.set_audit_callback(
		[&events, &events_mutex](const auth_event& event)
		{
			std::lock_guard<std::mutex> lock(events_mutex);
			events.push_back(event);
		});

	middleware.on_session_created("session1", "client1");

	std::lock_guard<std::mutex> lock(events_mutex);
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].type, auth_event_type::session_created);
	EXPECT_EQ(events[0].session_id, "session1");
	EXPECT_EQ(events[0].client_id, "client1");
}

TEST_F(AuthMiddlewareSessionTest, SessionDestroyedEventEmitted)
{
	auth_middleware middleware(auth_cfg_, rate_cfg_);

	std::vector<auth_event> events;
	std::mutex events_mutex;

	middleware.set_audit_callback(
		[&events, &events_mutex](const auth_event& event)
		{
			std::lock_guard<std::mutex> lock(events_mutex);
			events.push_back(event);
		});

	// Create session first
	middleware.on_session_created("session1", "client1");

	// Then destroy
	middleware.on_session_destroyed("session1");

	std::lock_guard<std::mutex> lock(events_mutex);
	ASSERT_GE(events.size(), 2u);
	EXPECT_EQ(events[1].type, auth_event_type::session_destroyed);
	EXPECT_EQ(events[1].session_id, "session1");
	EXPECT_EQ(events[1].client_id, "client1");
}

// ============================================================================
// Auth Middleware Configuration Access Tests
// ============================================================================

class AuthMiddlewareConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(AuthMiddlewareConfigTest, GetAuthConfig)
{
	auth_config auth_cfg;
	auth_cfg.enabled = true;
	auth_cfg.validate_on_each_request = true;
	auth_cfg.token_refresh_window_ms = 60000;

	rate_limit_config rate_cfg;

	auth_middleware middleware(auth_cfg, rate_cfg);

	const auto& config = middleware.get_auth_config();
	EXPECT_TRUE(config.enabled);
	EXPECT_TRUE(config.validate_on_each_request);
	EXPECT_EQ(config.token_refresh_window_ms, 60000u);
}

TEST_F(AuthMiddlewareConfigTest, GetRateLimiter)
{
	auth_config auth_cfg;
	rate_limit_config rate_cfg;
	rate_cfg.requests_per_second = 50;

	auth_middleware middleware(auth_cfg, rate_cfg);

	auto& limiter = middleware.get_rate_limiter();
	EXPECT_EQ(limiter.config().requests_per_second, 50u);
}
