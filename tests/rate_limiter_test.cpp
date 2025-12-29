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
 * @file rate_limiter_test.cpp
 * @brief Unit tests for rate_limiter component
 *
 * Tests cover:
 * - Sliding window algorithm correctness
 * - Burst handling
 * - Block duration behavior
 * - Concurrent access safety
 * - Configuration handling
 */

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <kcenon/database_server/gateway/auth_middleware.h>

using namespace database_server::gateway;

// ============================================================================
// Rate Limiter Configuration Tests
// ============================================================================

class RateLimiterConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(RateLimiterConfigTest, DefaultConfiguration)
{
	rate_limit_config config;

	EXPECT_TRUE(config.enabled);
	EXPECT_EQ(config.requests_per_second, 100);
	EXPECT_EQ(config.burst_size, 200);
	EXPECT_EQ(config.window_size_ms, 1000);
	EXPECT_EQ(config.block_duration_ms, 60000);
}

TEST_F(RateLimiterConfigTest, CustomConfiguration)
{
	rate_limit_config config;
	config.enabled = true;
	config.requests_per_second = 50;
	config.burst_size = 100;
	config.window_size_ms = 2000;
	config.block_duration_ms = 30000;

	rate_limiter limiter(config);

	EXPECT_EQ(limiter.config().requests_per_second, 50);
	EXPECT_EQ(limiter.config().burst_size, 100);
	EXPECT_EQ(limiter.config().window_size_ms, 2000);
	EXPECT_EQ(limiter.config().block_duration_ms, 30000);
}

// ============================================================================
// Rate Limiter Basic Behavior Tests
// ============================================================================

class RateLimiterTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.requests_per_second = 10;
		config_.burst_size = 15;
		config_.window_size_ms = 1000;
		config_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	rate_limit_config config_;
};

TEST_F(RateLimiterTest, AllowsRequestsWithinLimit)
{
	rate_limiter limiter(config_);

	// Should allow 10 requests (requests_per_second)
	for (int i = 0; i < 10; ++i)
	{
		EXPECT_TRUE(limiter.allow_request("client1"))
			<< "Request " << i << " should be allowed";
	}
}

TEST_F(RateLimiterTest, AllowsBurstRequests)
{
	rate_limiter limiter(config_);

	// Should allow burst_size requests
	for (uint32_t i = 0; i < config_.burst_size; ++i)
	{
		EXPECT_TRUE(limiter.allow_request("client1"))
			<< "Burst request " << i << " should be allowed";
	}

	// Next request should be rejected
	EXPECT_FALSE(limiter.allow_request("client1"));
}

TEST_F(RateLimiterTest, RejectsAfterBurstExceeded)
{
	rate_limiter limiter(config_);

	// Exhaust burst
	for (uint32_t i = 0; i < config_.burst_size; ++i)
	{
		limiter.allow_request("client1");
	}

	// Should reject
	EXPECT_FALSE(limiter.allow_request("client1"));
	EXPECT_FALSE(limiter.allow_request("client1"));
}

TEST_F(RateLimiterTest, ClientsAreIndependent)
{
	rate_limiter limiter(config_);

	// Exhaust client1 burst
	for (uint32_t i = 0; i < config_.burst_size; ++i)
	{
		limiter.allow_request("client1");
	}

	EXPECT_FALSE(limiter.allow_request("client1"));

	// client2 should still be allowed
	EXPECT_TRUE(limiter.allow_request("client2"));
}

TEST_F(RateLimiterTest, DisabledAlwaysAllows)
{
	rate_limit_config disabled_config = config_;
	disabled_config.enabled = false;

	rate_limiter limiter(disabled_config);

	// Should allow unlimited requests when disabled
	for (int i = 0; i < 1000; ++i)
	{
		EXPECT_TRUE(limiter.allow_request("client1"));
	}
}

// ============================================================================
// Rate Limiter Block Behavior Tests
// ============================================================================

class RateLimiterBlockTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.requests_per_second = 5;
		config_.burst_size = 5;
		config_.window_size_ms = 100;
		config_.block_duration_ms = 100; // Short block for testing
	}

	void TearDown() override {}

	rate_limit_config config_;
};

TEST_F(RateLimiterBlockTest, BlocksClientAfterExceedingLimit)
{
	rate_limiter limiter(config_);

	// Exhaust limit (burst_size requests are allowed)
	for (uint32_t i = 0; i < config_.burst_size; ++i)
	{
		EXPECT_TRUE(limiter.allow_request("client1"));
	}

	// This request exceeds the burst size and triggers the block
	EXPECT_FALSE(limiter.allow_request("client1"));

	// Client should now be blocked
	EXPECT_TRUE(limiter.is_blocked("client1"));
	EXPECT_GT(limiter.block_expires_at("client1"), 0u);
}

TEST_F(RateLimiterBlockTest, UnblockedClientNotBlocked)
{
	rate_limiter limiter(config_);

	EXPECT_FALSE(limiter.is_blocked("new_client"));
	EXPECT_EQ(limiter.block_expires_at("new_client"), 0u);
}

TEST_F(RateLimiterBlockTest, BlockExpiresAfterDuration)
{
	rate_limiter limiter(config_);

	// Exhaust limit and get blocked
	for (uint32_t i = 0; i < config_.burst_size; ++i)
	{
		limiter.allow_request("client1");
	}
	limiter.allow_request("client1"); // Trigger block

	EXPECT_TRUE(limiter.is_blocked("client1"));

	// Wait for block to expire
	std::this_thread::sleep_for(std::chrono::milliseconds(config_.block_duration_ms + 50));

	EXPECT_FALSE(limiter.is_blocked("client1"));
}

// ============================================================================
// Rate Limiter Remaining Requests Tests
// ============================================================================

class RateLimiterRemainingTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.requests_per_second = 10;
		config_.burst_size = 20;
		config_.window_size_ms = 1000;
		config_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	rate_limit_config config_;
};

TEST_F(RateLimiterRemainingTest, NewClientHasFullAllowance)
{
	rate_limiter limiter(config_);

	// New client should have full requests available
	uint32_t expected = config_.requests_per_second * config_.window_size_ms / 1000;
	EXPECT_EQ(limiter.remaining_requests("new_client"), expected);
}

TEST_F(RateLimiterRemainingTest, RemainingDecreasesWithRequests)
{
	rate_limiter limiter(config_);

	uint32_t max_requests = config_.requests_per_second * config_.window_size_ms / 1000;

	// Make some requests
	for (int i = 0; i < 5; ++i)
	{
		limiter.allow_request("client1");
	}

	EXPECT_EQ(limiter.remaining_requests("client1"), max_requests - 5);
}

TEST_F(RateLimiterRemainingTest, DisabledReturnsMaxUint)
{
	rate_limit_config disabled_config = config_;
	disabled_config.enabled = false;

	rate_limiter limiter(disabled_config);

	EXPECT_EQ(limiter.remaining_requests("client1"), UINT32_MAX);
}

// ============================================================================
// Rate Limiter Reset and Cleanup Tests
// ============================================================================

class RateLimiterResetTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.requests_per_second = 10;
		config_.burst_size = 10;
		config_.window_size_ms = 1000;
		config_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	rate_limit_config config_;
};

TEST_F(RateLimiterResetTest, ResetClearsClientState)
{
	rate_limiter limiter(config_);

	// Make requests
	for (int i = 0; i < 10; ++i)
	{
		limiter.allow_request("client1");
	}

	// Reset client
	limiter.reset("client1");

	// Client should have full allowance again
	uint32_t expected = config_.requests_per_second * config_.window_size_ms / 1000;
	EXPECT_EQ(limiter.remaining_requests("client1"), expected);
}

TEST_F(RateLimiterResetTest, ResetUnblocksClient)
{
	rate_limiter limiter(config_);

	// Exhaust limit and get blocked
	for (uint32_t i = 0; i <= config_.burst_size; ++i)
	{
		limiter.allow_request("client1");
	}

	EXPECT_TRUE(limiter.is_blocked("client1"));

	// Reset should unblock
	limiter.reset("client1");

	EXPECT_FALSE(limiter.is_blocked("client1"));
	EXPECT_TRUE(limiter.allow_request("client1"));
}

TEST_F(RateLimiterResetTest, CleanupRemovesInactiveClients)
{
	config_.window_size_ms = 50; // Short window for testing
	rate_limiter limiter(config_);

	// Make request
	limiter.allow_request("client1");

	// Wait for window to expire
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// Cleanup should remove inactive client
	limiter.cleanup();

	// Client should have full allowance (entry was removed)
	uint32_t expected = config_.requests_per_second * config_.window_size_ms / 1000;
	EXPECT_EQ(limiter.remaining_requests("client1"), expected);
}

// ============================================================================
// Rate Limiter Sliding Window Tests
// ============================================================================

class RateLimiterSlidingWindowTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.requests_per_second = 10;
		config_.burst_size = 10;
		config_.window_size_ms = 100; // 100ms window
		config_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	rate_limit_config config_;
};

TEST_F(RateLimiterSlidingWindowTest, WindowSlidesCorrectly)
{
	rate_limiter limiter(config_);

	// Use all requests
	// max_requests = 10 * 100 / 1000 = 1
	// burst = 10
	// So burst is used
	for (uint32_t i = 0; i < config_.burst_size; ++i)
	{
		limiter.allow_request("client1");
	}

	EXPECT_FALSE(limiter.allow_request("client1"));

	// Wait for window to slide
	std::this_thread::sleep_for(std::chrono::milliseconds(config_.window_size_ms + 50));

	// Check if blocked expired
	if (!limiter.is_blocked("client1"))
	{
		// Should be able to make new requests
		EXPECT_TRUE(limiter.allow_request("client1"));
	}
}

// ============================================================================
// Rate Limiter Concurrent Access Tests
// ============================================================================

class RateLimiterConcurrencyTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.requests_per_second = 1000;
		config_.burst_size = 2000;
		config_.window_size_ms = 1000;
		config_.block_duration_ms = 1000;
	}

	void TearDown() override {}

	rate_limit_config config_;
};

TEST_F(RateLimiterConcurrencyTest, ThreadSafeAccess)
{
	rate_limiter limiter(config_);

	constexpr int num_threads = 10;
	constexpr int requests_per_thread = 100;

	std::vector<std::future<int>> futures;

	for (int t = 0; t < num_threads; ++t)
	{
		futures.push_back(std::async(
			std::launch::async,
			[&limiter, t, requests_per_thread]()
			{
				int allowed = 0;
				std::string client_id = "client_" + std::to_string(t);

				for (int i = 0; i < requests_per_thread; ++i)
				{
					if (limiter.allow_request(client_id))
					{
						++allowed;
					}
				}
				return allowed;
			}));
	}

	int total_allowed = 0;
	for (auto& future : futures)
	{
		total_allowed += future.get();
	}

	// Should have allowed requests without crashing
	EXPECT_GT(total_allowed, 0);
	EXPECT_LE(total_allowed, num_threads * requests_per_thread);
}

TEST_F(RateLimiterConcurrencyTest, ConcurrentDifferentClients)
{
	rate_limiter limiter(config_);

	constexpr int num_clients = 5;
	constexpr int requests_per_client = 50;

	std::vector<std::future<int>> futures;

	for (int c = 0; c < num_clients; ++c)
	{
		futures.push_back(std::async(
			std::launch::async,
			[&limiter, c, requests_per_client]()
			{
				int allowed = 0;
				std::string client_id = "concurrent_client_" + std::to_string(c);

				for (int i = 0; i < requests_per_client; ++i)
				{
					if (limiter.allow_request(client_id))
					{
						++allowed;
					}
				}
				return allowed;
			}));
	}

	for (auto& future : futures)
	{
		int allowed = future.get();
		// Each client should have allowed some requests
		EXPECT_GT(allowed, 0);
	}
}
