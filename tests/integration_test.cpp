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
 * @file integration_test.cpp
 * @brief Integration tests for database gateway components (Phase 3.5)
 *
 * Tests cover:
 * - Complete query flow through router and auth middleware
 * - Error handling scenarios
 * - Rate limiting behavior
 * - Concurrent request handling
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <thread>
#include <vector>

#include <kcenon/database_server/gateway/auth_middleware.h>
#include <kcenon/database_server/gateway/query_protocol.h>
#include <kcenon/database_server/gateway/query_router.h>
#include <kcenon/database_server/gateway/query_types.h>

#include <kcenon/common/config/feature_flags.h>

using namespace database_server::gateway;
using namespace std::chrono_literals;

// ============================================================================
// Test Fixtures
// ============================================================================

class IntegrationTestFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auth_config_.enabled = true;
		auth_config_.validate_on_each_request = true;

		rate_config_.enabled = true;
		rate_config_.requests_per_second = 100;
		rate_config_.burst_size = 200;
		rate_config_.window_size_ms = 1000;
		rate_config_.block_duration_ms = 1000;

		router_config_.default_timeout_ms = 5000;
		router_config_.max_concurrent_queries = 100;
		router_config_.enable_metrics = true;
	}

	void TearDown() override {}

	auth_token create_valid_token(const std::string& client_id = "test-client",
								  uint32_t expires_in_ms = 3600000)
	{
		auth_token token;
		token.token = "valid-token-" + std::to_string(++token_counter_);
		token.client_id = client_id;

		auto future = std::chrono::system_clock::now()
					  + std::chrono::milliseconds(expires_in_ms);
		token.expires_at = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				future.time_since_epoch())
				.count());

		return token;
	}

	auth_token create_expired_token(const std::string& client_id = "test-client")
	{
		auth_token token;
		token.token = "expired-token-" + std::to_string(++token_counter_);
		token.client_id = client_id;

		auto past = std::chrono::system_clock::now() - std::chrono::hours(1);
		token.expires_at = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				past.time_since_epoch())
				.count());

		return token;
	}

	query_request create_select_request(const std::string& sql = "SELECT 1")
	{
		query_request request(sql, query_type::select);
		request.header.message_id = ++message_counter_;
		request.token = create_valid_token();
		return request;
	}

	auth_config auth_config_;
	rate_limit_config rate_config_;
	router_config router_config_;

private:
	std::atomic<uint64_t> token_counter_{0};
	std::atomic<uint64_t> message_counter_{0};
};

// ============================================================================
// Auth Middleware Integration Tests
// ============================================================================

class AuthMiddlewareIntegrationTest : public IntegrationTestFixture
{
protected:
	void SetUp() override
	{
		IntegrationTestFixture::SetUp();
		middleware_ = std::make_unique<auth_middleware>(auth_config_, rate_config_);
	}

	std::unique_ptr<auth_middleware> middleware_;
};

TEST_F(AuthMiddlewareIntegrationTest, ValidTokenPassesAuthentication)
{
	auto token = create_valid_token("client-001");
	auto result = middleware_->authenticate("session-001", token);

	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.code, status_code::ok);
	EXPECT_EQ(result.client_id, "client-001");
}

TEST_F(AuthMiddlewareIntegrationTest, ExpiredTokenFailsAuthentication)
{
	auto token = create_expired_token("client-001");
	auto result = middleware_->authenticate("session-001", token);

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::authentication_failed);
}

TEST_F(AuthMiddlewareIntegrationTest, EmptyTokenFailsAuthentication)
{
	auth_token token;
	auto result = middleware_->authenticate("session-001", token);

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.code, status_code::authentication_failed);
}

TEST_F(AuthMiddlewareIntegrationTest, RateLimitAllowsNormalTraffic)
{
	auto token = create_valid_token("client-001");

	for (int i = 0; i < 50; ++i)
	{
		auto result = middleware_->check("session-001", token);
		EXPECT_TRUE(result.success)
			<< "Request " << i << " should be allowed under rate limit";
	}
}

TEST_F(AuthMiddlewareIntegrationTest, RateLimitBlocksExcessiveTraffic)
{
	rate_config_.requests_per_second = 10;
	rate_config_.burst_size = 15;
	middleware_ = std::make_unique<auth_middleware>(auth_config_, rate_config_);

	auto token = create_valid_token("client-001");
	int blocked_count = 0;

	for (int i = 0; i < 50; ++i)
	{
		auto result = middleware_->check("session-001", token);
		if (!result.success && result.code == status_code::rate_limited)
		{
			++blocked_count;
		}
	}

	EXPECT_GT(blocked_count, 0) << "Some requests should be rate limited";
}

TEST_F(AuthMiddlewareIntegrationTest, MetricsTrackAuthAttempts)
{
	auto valid_token = create_valid_token("client-001");
	auto expired_token = create_expired_token("client-002");

	for (int i = 0; i < 5; ++i)
	{
		(void)middleware_->authenticate("session-001", valid_token);
	}
	for (int i = 0; i < 3; ++i)
	{
		(void)middleware_->authenticate("session-002", expired_token);
	}

	const auto& metrics = middleware_->metrics();
	EXPECT_EQ(metrics.total_auth_attempts.load(), 8);
	EXPECT_EQ(metrics.successful_auths.load(), 5);
	EXPECT_EQ(metrics.failed_auths.load(), 3);
}

TEST_F(AuthMiddlewareIntegrationTest, AuditCallbackReceivesEvents)
{
	std::vector<auth_event> captured_events;
	std::mutex events_mutex;

	middleware_->set_audit_callback([&](const auth_event& event) {
		std::lock_guard<std::mutex> lock(events_mutex);
		captured_events.push_back(event);
	});

	auto valid_token = create_valid_token("client-001");
	auto expired_token = create_expired_token("client-002");

	(void)middleware_->authenticate("session-001", valid_token);
	(void)middleware_->authenticate("session-002", expired_token);

	std::lock_guard<std::mutex> lock(events_mutex);
	EXPECT_GE(captured_events.size(), 2);

	bool found_success = false;
	bool found_failure = false;
	for (const auto& event : captured_events)
	{
		if (event.type == auth_event_type::auth_success)
		{
			found_success = true;
		}
		if (event.type == auth_event_type::token_expired
			|| event.type == auth_event_type::auth_failure)
		{
			found_failure = true;
		}
	}

	EXPECT_TRUE(found_success);
	EXPECT_TRUE(found_failure);
}

TEST_F(AuthMiddlewareIntegrationTest, SessionLifecycleTracking)
{
	middleware_->on_session_created("session-001", "client-001");

	auto token = create_valid_token("client-001");
	auto result = middleware_->authenticate("session-001", token);

	EXPECT_TRUE(result.success);

	middleware_->on_session_destroyed("session-001");
}

TEST_F(AuthMiddlewareIntegrationTest, ConcurrentAuthenticationRequests)
{
	const int num_threads = 10;
	const int requests_per_thread = 100;
	std::atomic<int> success_count{0};
	std::atomic<int> failure_count{0};

	std::vector<std::thread> threads;
	threads.reserve(num_threads);

	for (int t = 0; t < num_threads; ++t)
	{
		threads.emplace_back([&, t]() {
			auto token = create_valid_token("client-" + std::to_string(t));
			std::string session_id = "session-" + std::to_string(t);

			for (int i = 0; i < requests_per_thread; ++i)
			{
				auto result = middleware_->authenticate(session_id, token);
				if (result.success)
				{
					success_count.fetch_add(1);
				}
				else
				{
					failure_count.fetch_add(1);
				}
			}
		});
	}

	for (auto& thread : threads)
	{
		thread.join();
	}

	int total = success_count.load() + failure_count.load();
	EXPECT_EQ(total, num_threads * requests_per_thread);
	EXPECT_EQ(success_count.load(), num_threads * requests_per_thread);
}

// ============================================================================
// Query Router Integration Tests
// ============================================================================

class QueryRouterIntegrationTest : public IntegrationTestFixture
{
protected:
	void SetUp() override
	{
		IntegrationTestFixture::SetUp();
		router_ = std::make_unique<query_router>(router_config_);
	}

	std::unique_ptr<query_router> router_;
};

TEST_F(QueryRouterIntegrationTest, RouterIsNotReadyWithoutPool)
{
	EXPECT_FALSE(router_->is_ready());
}

TEST_F(QueryRouterIntegrationTest, ExecuteWithoutPoolReturnsError)
{
	auto request = create_select_request();
	auto result = router_->execute(request);

	EXPECT_TRUE(result.is_err());
}

TEST_F(QueryRouterIntegrationTest, MetricsInitializedToZero)
{
	const auto& metrics = router_->metrics();

	EXPECT_EQ(metrics.total_queries.load(), 0);
	EXPECT_EQ(metrics.successful_queries.load(), 0);
	EXPECT_EQ(metrics.failed_queries.load(), 0);
	EXPECT_EQ(metrics.timeout_queries.load(), 0);
}

TEST_F(QueryRouterIntegrationTest, MetricsTrackFailedQueries)
{
	for (int i = 0; i < 5; ++i)
	{
		auto request = create_select_request();
		(void)router_->execute(request);
	}

	const auto& metrics = router_->metrics();
	EXPECT_EQ(metrics.total_queries.load(), 5);
	EXPECT_EQ(metrics.failed_queries.load(), 5);
	EXPECT_EQ(metrics.successful_queries.load(), 0);
}

TEST_F(QueryRouterIntegrationTest, ResetMetricsClearsCounters)
{
	for (int i = 0; i < 5; ++i)
	{
		auto request = create_select_request();
		(void)router_->execute(request);
	}

	router_->reset_metrics();

	const auto& metrics = router_->metrics();
	EXPECT_EQ(metrics.total_queries.load(), 0);
	EXPECT_EQ(metrics.failed_queries.load(), 0);
}

TEST_F(QueryRouterIntegrationTest, ConfigIsAccessible)
{
	const auto& config = router_->config();

	EXPECT_EQ(config.default_timeout_ms, 5000);
	EXPECT_EQ(config.max_concurrent_queries, 100);
	EXPECT_TRUE(config.enable_metrics);
}

TEST_F(QueryRouterIntegrationTest, ConcurrentQueryExecution)
{
	const int num_threads = 10;
	const int queries_per_thread = 50;
	std::atomic<int> executed_count{0};

	std::vector<std::thread> threads;
	threads.reserve(num_threads);

	for (int t = 0; t < num_threads; ++t)
	{
		threads.emplace_back([&, t]() {
			for (int i = 0; i < queries_per_thread; ++i)
			{
				query_request request(
					"SELECT " + std::to_string(t * queries_per_thread + i),
					query_type::select);
				request.header.message_id = t * queries_per_thread + i;

				auto response = router_->execute(request);
				executed_count.fetch_add(1);
			}
		});
	}

	for (auto& thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(executed_count.load(), num_threads * queries_per_thread);
	EXPECT_EQ(router_->metrics().total_queries.load(),
			  static_cast<uint64_t>(num_threads * queries_per_thread));
}

// ============================================================================
// Combined Auth + Router Integration Tests
// ============================================================================

class FullPipelineIntegrationTest : public IntegrationTestFixture
{
protected:
	void SetUp() override
	{
		IntegrationTestFixture::SetUp();
		middleware_ = std::make_unique<auth_middleware>(auth_config_, rate_config_);
		router_ = std::make_unique<query_router>(router_config_);
	}

	query_response execute_authenticated_query(const std::string& session_id,
											   const auth_token& token,
											   const query_request& request)
	{
		auto auth_result = middleware_->check(session_id, token);
		if (!auth_result.success)
		{
			return query_response(request.header.message_id, auth_result.code,
								  auth_result.message);
		}

		auto result = router_->execute(request);
		if (result.is_ok())
		{
			return std::move(result.value());
		}
		return query_response(request.header.message_id, status_code::error,
							  result.error().message);
	}

	std::unique_ptr<auth_middleware> middleware_;
	std::unique_ptr<query_router> router_;
};

TEST_F(FullPipelineIntegrationTest, ValidAuthAndQueryFlow)
{
	auto token = create_valid_token("client-001");
	auto request = create_select_request("SELECT * FROM users");

	auto response = execute_authenticated_query("session-001", token, request);

	// When no connection pool is available, router returns error Result
	// which is converted to status_code::error in execute_authenticated_query
	EXPECT_EQ(response.status, status_code::error);
	EXPECT_EQ(middleware_->metrics().successful_auths.load(), 1);
	EXPECT_EQ(router_->metrics().total_queries.load(), 1);
}

TEST_F(FullPipelineIntegrationTest, ExpiredTokenBlocksQuery)
{
	auto token = create_expired_token("client-001");
	auto request = create_select_request("SELECT * FROM users");

	auto response = execute_authenticated_query("session-001", token, request);

	EXPECT_EQ(response.status, status_code::authentication_failed);
	EXPECT_EQ(router_->metrics().total_queries.load(), 0);
}

TEST_F(FullPipelineIntegrationTest, RateLimitedClientCantExecuteQuery)
{
	rate_config_.requests_per_second = 5;
	rate_config_.burst_size = 10;
	middleware_ = std::make_unique<auth_middleware>(auth_config_, rate_config_);

	auto token = create_valid_token("client-001");
	int rate_limited_count = 0;

	for (int i = 0; i < 50; ++i)
	{
		query_request request("SELECT " + std::to_string(i), query_type::select);
		request.header.message_id = i;

		auto response = execute_authenticated_query("session-001", token, request);
		if (response.status == status_code::rate_limited)
		{
			++rate_limited_count;
		}
	}

	EXPECT_GT(rate_limited_count, 0);
}

TEST_F(FullPipelineIntegrationTest, MultipleClientsConcurrentAccess)
{
	const int num_clients = 5;
	const int requests_per_client = 20;

	std::vector<std::future<std::pair<int, int>>> futures;
	futures.reserve(num_clients);

	for (int c = 0; c < num_clients; ++c)
	{
		futures.push_back(std::async(std::launch::async, [&, c]() {
			auto token = create_valid_token("client-" + std::to_string(c));
			std::string session_id = "session-" + std::to_string(c);

			int success = 0;
			int failure = 0;

			for (int i = 0; i < requests_per_client; ++i)
			{
				query_request request(
					"SELECT " + std::to_string(c * requests_per_client + i),
					query_type::select);
				request.header.message_id = c * requests_per_client + i;

				auto response = execute_authenticated_query(session_id, token, request);
				if (response.status == status_code::no_connection)
				{
					++success;
				}
				else
				{
					++failure;
				}
			}

			return std::make_pair(success, failure);
		}));
	}

	int total_success = 0;
	int total_failure = 0;
	for (auto& future : futures)
	{
		auto [s, f] = future.get();
		total_success += s;
		total_failure += f;
	}

	EXPECT_EQ(total_success + total_failure, num_clients * requests_per_client);
}

// ============================================================================
// Error Handling Scenario Tests
// ============================================================================

class ErrorHandlingTest : public IntegrationTestFixture
{
protected:
	void SetUp() override
	{
		IntegrationTestFixture::SetUp();
		middleware_ = std::make_unique<auth_middleware>(auth_config_, rate_config_);
		router_ = std::make_unique<query_router>(router_config_);
	}

	std::unique_ptr<auth_middleware> middleware_;
	std::unique_ptr<query_router> router_;
};

TEST_F(ErrorHandlingTest, InvalidQueryTypeReturnsError)
{
	query_request request("", query_type::unknown);
	request.header.message_id = 1;

	EXPECT_FALSE(request.is_valid());
}

TEST_F(ErrorHandlingTest, EmptySqlForNonPingReturnsError)
{
	query_request request("", query_type::select);
	request.header.message_id = 1;

	EXPECT_FALSE(request.is_valid());
}

TEST_F(ErrorHandlingTest, PingWithEmptySqlIsValid)
{
	query_request request("", query_type::ping);
	request.header.message_id = 1;

	EXPECT_TRUE(request.is_valid());
}

TEST_F(ErrorHandlingTest, ResponseErrorCodesAreCorrect)
{
	EXPECT_FALSE(
		query_response(1, status_code::error, "Generic error").is_success());
	EXPECT_FALSE(
		query_response(1, status_code::timeout, "Query timeout").is_success());
	EXPECT_FALSE(query_response(1, status_code::connection_failed, "Connection lost")
					 .is_success());
	EXPECT_FALSE(query_response(1, status_code::authentication_failed, "Auth failed")
					 .is_success());
	EXPECT_FALSE(
		query_response(1, status_code::invalid_query, "Bad SQL").is_success());
	EXPECT_FALSE(query_response(1, status_code::no_connection, "No pool")
					 .is_success());
	EXPECT_FALSE(query_response(1, status_code::rate_limited, "Too many requests")
					 .is_success());
	EXPECT_FALSE(
		query_response(1, status_code::server_busy, "Server overloaded").is_success());
	EXPECT_FALSE(
		query_response(1, status_code::not_found, "Not found").is_success());
	EXPECT_FALSE(query_response(1, status_code::permission_denied, "Access denied")
					 .is_success());
}

TEST_F(ErrorHandlingTest, MetricsAccuracyUnderLoad)
{
	const int num_queries = 1000;
	std::atomic<int> executed{0};

	std::vector<std::thread> threads;
	const int num_threads = 4;
	threads.reserve(num_threads);

	for (int t = 0; t < num_threads; ++t)
	{
		threads.emplace_back([&, t]() {
			for (int i = t; i < num_queries; i += num_threads)
			{
				query_request request("SELECT " + std::to_string(i),
									  query_type::select);
				request.header.message_id = i;
				(void)router_->execute(request);
				executed.fetch_add(1);
			}
		});
	}

	for (auto& thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(executed.load(), num_queries);
	EXPECT_EQ(router_->metrics().total_queries.load(),
			  static_cast<uint64_t>(num_queries));
}

// ============================================================================
// Rate Limiter Edge Cases
// ============================================================================

class RateLimiterEdgeCaseTest : public IntegrationTestFixture
{
protected:
	void SetUp() override { IntegrationTestFixture::SetUp(); }
};

TEST_F(RateLimiterEdgeCaseTest, RateLimiterResetAllowsNewRequests)
{
	rate_config_.requests_per_second = 5;
	rate_config_.burst_size = 5;
	rate_limiter limiter(rate_config_);

	for (int i = 0; i < 10; ++i)
	{
		(void)limiter.allow_request("client-001");
	}

	limiter.reset("client-001");

	EXPECT_TRUE(limiter.allow_request("client-001"));
	EXPECT_GT(limiter.remaining_requests("client-001"), 0);
}

TEST_F(RateLimiterEdgeCaseTest, BlockExpirationAllowsNewRequests)
{
	rate_config_.requests_per_second = 1;
	rate_config_.burst_size = 1;
	rate_config_.block_duration_ms = 50;
	rate_limiter limiter(rate_config_);

	(void)limiter.allow_request("client-001");
	(void)limiter.allow_request("client-001");

	if (limiter.is_blocked("client-001"))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	EXPECT_FALSE(limiter.is_blocked("client-001"));
}

TEST_F(RateLimiterEdgeCaseTest, DifferentClientsHaveIndependentLimits)
{
	rate_config_.requests_per_second = 5;
	rate_config_.burst_size = 5;
	rate_limiter limiter(rate_config_);

	for (int i = 0; i < 10; ++i)
	{
		(void)limiter.allow_request("client-001");
	}

	EXPECT_TRUE(limiter.allow_request("client-002"));
	EXPECT_GT(limiter.remaining_requests("client-002"), 0);
}

// ============================================================================
// Query Protocol Integration Tests
// ============================================================================

class QueryProtocolIntegrationTest : public IntegrationTestFixture
{
protected:
	void SetUp() override { IntegrationTestFixture::SetUp(); }
};

TEST_F(QueryProtocolIntegrationTest, RequestWithAllParamTypes)
{
	query_request request("INSERT INTO test (?, ?, ?, ?, ?, ?)", query_type::insert);
	request.header.message_id = 12345;
	request.params.emplace_back("null_val", std::monostate{});
	request.params.emplace_back("bool_val", true);
	request.params.emplace_back("int_val", static_cast<int64_t>(42));
	request.params.emplace_back("double_val", 3.14159);
	request.params.emplace_back("string_val", std::string("hello world"));
	request.params.emplace_back("bytes_val",
								std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});

	EXPECT_TRUE(request.is_valid());
	EXPECT_EQ(request.params.size(), 6);
}

TEST_F(QueryProtocolIntegrationTest, ResponseWithResultSet)
{
	query_response response(12345);
	response.status = status_code::ok;
	response.execution_time_us = 1500;

	column_metadata col1;
	col1.name = "id";
	col1.type_name = "INTEGER";
	col1.type_id = 1;
	col1.nullable = false;

	column_metadata col2;
	col2.name = "name";
	col2.type_name = "VARCHAR";
	col2.type_id = 2;
	col2.nullable = true;
	col2.precision = 255;

	response.columns.push_back(col1);
	response.columns.push_back(col2);

	for (int i = 1; i <= 100; ++i)
	{
		result_row row;
		row.cells.emplace_back(static_cast<int64_t>(i));
		row.cells.emplace_back(std::string("User " + std::to_string(i)));
		response.rows.push_back(row);
	}

	EXPECT_TRUE(response.is_success());
	EXPECT_EQ(response.columns.size(), 2);
	EXPECT_EQ(response.rows.size(), 100);
	EXPECT_EQ(std::get<int64_t>(response.rows[0].cells[0]), 1);
	EXPECT_EQ(std::get<std::string>(response.rows[99].cells[1]), "User 100");
}

#if KCENON_WITH_CONTAINER_SYSTEM

TEST_F(QueryProtocolIntegrationTest, LargeRequestSerialization)
{
	query_request request("INSERT INTO test VALUES (?, ?, ?)", query_type::insert);
	request.header.message_id = 99999;

	for (int i = 0; i < 100; ++i)
	{
		request.params.emplace_back("param_" + std::to_string(i),
									std::string(1000, 'x'));
	}

	auto container = request.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_request::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	EXPECT_EQ(deserialized.params.size(), 100);
}

TEST_F(QueryProtocolIntegrationTest, LargeResponseSerialization)
{
	query_response original(12345);
	original.status = status_code::ok;

	for (int c = 0; c < 10; ++c)
	{
		column_metadata col;
		col.name = "col_" + std::to_string(c);
		col.type_name = "VARCHAR";
		original.columns.push_back(col);
	}

	for (int r = 0; r < 1000; ++r)
	{
		result_row row;
		for (int c = 0; c < 10; ++c)
		{
			row.cells.emplace_back(std::string("row_" + std::to_string(r) + "_col_"
											   + std::to_string(c)));
		}
		original.rows.push_back(row);
	}

	auto container = original.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_response::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	EXPECT_EQ(deserialized.columns.size(), 10);
	EXPECT_EQ(deserialized.rows.size(), 1000);
}

#endif // KCENON_WITH_CONTAINER_SYSTEM
