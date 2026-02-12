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
 * @file connection_pool_test.cpp
 * @brief Unit tests for connection pool components
 *
 * Tests cover:
 * - Connection pool configuration
 * - Priority scheduling order
 * - Graceful shutdown behavior
 * - Connection acquisition timeout
 * - Pool exhaustion handling
 * - Pool metrics
 */

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <kcenon/database_server/pooling/connection_priority.h>
#include <kcenon/database_server/pooling/connection_types.h>
#include <kcenon/database_server/pooling/pool_metrics.h>

using namespace database;
using namespace database_server::pooling;

// ============================================================================
// Mock Database for Testing
// ============================================================================

namespace
{

class mock_database : public core::database_backend
{
public:
	mock_database() : initialized_(false) {}

	~mock_database() override
	{
		if (initialized_)
		{
			(void)shutdown();
		}
	}

	database_types type() const override { return database_types::mysql; }

	kcenon::common::VoidResult initialize(const core::connection_config& /*config*/) override
	{
		initialized_ = true;
		return kcenon::common::ok();
	}

	kcenon::common::VoidResult shutdown() override
	{
		initialized_ = false;
		return kcenon::common::ok();
	}

	bool is_initialized() const override { return initialized_; }

	kcenon::common::Result<uint64_t> insert_query(const std::string& /*query_string*/) override
	{
		if (!initialized_)
		{
			return kcenon::common::error_info{-1, "Not initialized", "mock_database"};
		}
		return uint64_t{1};
	}

	kcenon::common::Result<uint64_t> update_query(const std::string& /*query_string*/) override
	{
		if (!initialized_)
		{
			return kcenon::common::error_info{-1, "Not initialized", "mock_database"};
		}
		return uint64_t{1};
	}

	kcenon::common::Result<uint64_t> delete_query(const std::string& /*query_string*/) override
	{
		if (!initialized_)
		{
			return kcenon::common::error_info{-1, "Not initialized", "mock_database"};
		}
		return uint64_t{1};
	}

	kcenon::common::Result<core::database_result> select_query(
		const std::string& /*query_string*/) override
	{
		if (!initialized_)
		{
			return kcenon::common::error_info{-1, "Not initialized", "mock_database"};
		}
		core::database_row row;
		row["result"] = std::string("mock_result");
		return core::database_result{row};
	}

	kcenon::common::VoidResult execute_query(const std::string& /*query_string*/) override
	{
		if (!initialized_)
		{
			return kcenon::common::error_info{-1, "Not initialized", "mock_database"};
		}
		return kcenon::common::ok();
	}

	kcenon::common::VoidResult begin_transaction() override
	{
		return kcenon::common::ok();
	}

	kcenon::common::VoidResult commit_transaction() override
	{
		return kcenon::common::ok();
	}

	kcenon::common::VoidResult rollback_transaction() override
	{
		return kcenon::common::ok();
	}

	bool in_transaction() const override { return false; }

	std::string last_error() const override { return {}; }

	std::map<std::string, std::string> connection_info() const override { return {}; }

private:
	bool initialized_;
};

std::unique_ptr<core::database_backend> create_mock_database()
{
	auto db = std::make_unique<mock_database>();
	(void)db->initialize(core::connection_config{});
	return db;
}

std::unique_ptr<core::database_backend> create_failing_database()
{
	return nullptr;
}

} // namespace

// ============================================================================
// Connection Pool Config Tests
// ============================================================================

class ConnectionPoolConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ConnectionPoolConfigTest, DefaultConfiguration)
{
	connection_pool_config config;

	EXPECT_EQ(config.min_connections, 2u);
	EXPECT_EQ(config.max_connections, 20u);
	EXPECT_EQ(config.acquire_timeout.count(), 5000);
	EXPECT_EQ(config.idle_timeout.count(), 30000);
	EXPECT_EQ(config.health_check_interval.count(), 60000);
	EXPECT_TRUE(config.enable_health_checks);
}

TEST_F(ConnectionPoolConfigTest, CustomConfiguration)
{
	connection_pool_config config;
	config.min_connections = 5;
	config.max_connections = 50;
	config.acquire_timeout = std::chrono::milliseconds(10000);
	config.idle_timeout = std::chrono::milliseconds(60000);
	config.health_check_interval = std::chrono::milliseconds(30000);
	config.enable_health_checks = false;
	config.connection_string = "host=localhost;dbname=test";

	EXPECT_EQ(config.min_connections, 5u);
	EXPECT_EQ(config.max_connections, 50u);
	EXPECT_EQ(config.acquire_timeout.count(), 10000);
	EXPECT_EQ(config.idle_timeout.count(), 60000);
	EXPECT_EQ(config.health_check_interval.count(), 30000);
	EXPECT_FALSE(config.enable_health_checks);
	EXPECT_EQ(config.connection_string, "host=localhost;dbname=test");
}

// ============================================================================
// Connection Wrapper Tests
// ============================================================================

class ConnectionWrapperTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ConnectionWrapperTest, InitiallyHealthy)
{
	auto db = create_mock_database();
	connection_wrapper wrapper(std::move(db));

	EXPECT_TRUE(wrapper.is_healthy());
}

TEST_F(ConnectionWrapperTest, CanMarkUnhealthy)
{
	auto db = create_mock_database();
	connection_wrapper wrapper(std::move(db));

	wrapper.mark_unhealthy();

	EXPECT_FALSE(wrapper.is_healthy());
}

TEST_F(ConnectionWrapperTest, AccessUnderlyingConnection)
{
	auto db = create_mock_database();
	connection_wrapper wrapper(std::move(db));

	EXPECT_NE(wrapper.get(), nullptr);
}

TEST_F(ConnectionWrapperTest, UpdateLastUsed)
{
	auto db = create_mock_database();
	connection_wrapper wrapper(std::move(db));

	auto before = wrapper.last_used();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	wrapper.update_last_used();
	auto after = wrapper.last_used();

	EXPECT_GT(after, before);
}

TEST_F(ConnectionWrapperTest, IdleTimeoutNotExceeded)
{
	auto db = create_mock_database();
	connection_wrapper wrapper(std::move(db));

	wrapper.update_last_used();

	EXPECT_FALSE(wrapper.is_idle_timeout_exceeded(std::chrono::milliseconds(1000)));
}

TEST_F(ConnectionWrapperTest, IdleTimeoutExceeded)
{
	auto db = create_mock_database();
	connection_wrapper wrapper(std::move(db));

	wrapper.update_last_used();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	EXPECT_TRUE(wrapper.is_idle_timeout_exceeded(std::chrono::milliseconds(10)));
}

// ============================================================================
// Connection Pool Basic Tests
// ============================================================================

class ConnectionPoolBasicTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.min_connections = 2;
		config_.max_connections = 5;
		config_.acquire_timeout = std::chrono::milliseconds(1000);
		config_.idle_timeout = std::chrono::milliseconds(5000);
	}

	void TearDown() override {}

	connection_pool_config config_;
};

TEST_F(ConnectionPoolBasicTest, InitializeCreatesMinConnections)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);

	bool init_result = pool.initialize();

	EXPECT_TRUE(init_result);
	EXPECT_EQ(pool.available_connections(), config_.min_connections);
}

TEST_F(ConnectionPoolBasicTest, InitializeWithFailingFactoryPartialSuccess)
{
	// Factory that fails after 1 success
	int create_count = 0;

	auto factory = [&create_count]() -> std::unique_ptr<core::database_backend>
	{
		if (create_count++ == 0)
		{
			auto db = std::make_unique<mock_database>();
			(void)db->initialize(core::connection_config{});
			return db;
		}
		return nullptr;
	};

	connection_pool pool(database_types::mysql, config_, factory);
	bool init_result = pool.initialize();

	EXPECT_TRUE(init_result);
	EXPECT_EQ(pool.available_connections(), 1u);
}

TEST_F(ConnectionPoolBasicTest, AcquireConnection)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	auto result = pool.acquire_connection();

	EXPECT_TRUE(result.is_ok());
	EXPECT_NE(result.value(), nullptr);
	EXPECT_EQ(pool.active_connections(), 1u);
}

TEST_F(ConnectionPoolBasicTest, ReleaseConnection)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	auto result = pool.acquire_connection();
	ASSERT_TRUE(result.is_ok());

	size_t available_before = pool.available_connections();
	pool.release_connection(result.value());

	EXPECT_EQ(pool.available_connections(), available_before + 1);
	EXPECT_EQ(pool.active_connections(), 0u);
}

TEST_F(ConnectionPoolBasicTest, ReleaseNullConnectionSafe)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	pool.release_connection(nullptr);

	// Should not crash or change state
	EXPECT_EQ(pool.available_connections(), config_.min_connections);
}

// ============================================================================
// Connection Pool Exhaustion Tests
// ============================================================================

class ConnectionPoolExhaustionTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.min_connections = 1;
		config_.max_connections = 2;
		config_.acquire_timeout = std::chrono::milliseconds(100);
	}

	void TearDown() override {}

	connection_pool_config config_;
};

TEST_F(ConnectionPoolExhaustionTest, AcquireCreatesNewConnectionUpToMax)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	auto conn1 = pool.acquire_connection();
	auto conn2 = pool.acquire_connection();

	EXPECT_TRUE(conn1.is_ok());
	EXPECT_TRUE(conn2.is_ok());
	EXPECT_EQ(pool.active_connections(), 2u);
}

TEST_F(ConnectionPoolExhaustionTest, AcquireTimeoutWhenPoolExhausted)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	// Exhaust pool
	auto conn1 = pool.acquire_connection();
	auto conn2 = pool.acquire_connection();
	ASSERT_TRUE(conn1.is_ok());
	ASSERT_TRUE(conn2.is_ok());

	// This should timeout
	auto conn3 = pool.acquire_connection();

	EXPECT_TRUE(conn3.is_err());
	EXPECT_EQ(conn3.error().code, -501); // Timeout error
}

TEST_F(ConnectionPoolExhaustionTest, AcquireSucceedsAfterRelease)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	// Exhaust pool
	auto conn1 = pool.acquire_connection();
	auto conn2 = pool.acquire_connection();
	ASSERT_TRUE(conn1.is_ok());
	ASSERT_TRUE(conn2.is_ok());

	// Release one
	pool.release_connection(conn1.value());

	// Now acquisition should succeed
	auto conn3 = pool.acquire_connection();
	EXPECT_TRUE(conn3.is_ok());
}

// ============================================================================
// Connection Pool Shutdown Tests
// ============================================================================

class ConnectionPoolShutdownTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.min_connections = 2;
		config_.max_connections = 5;
		config_.acquire_timeout = std::chrono::milliseconds(1000);
	}

	void TearDown() override {}

	connection_pool_config config_;
};

TEST_F(ConnectionPoolShutdownTest, ShutdownStopsPool)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	pool.shutdown();

	EXPECT_TRUE(pool.is_shutting_down());
}

TEST_F(ConnectionPoolShutdownTest, AcquireFailsAfterShutdown)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	pool.shutdown();

	auto result = pool.acquire_connection();

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().code, -500);
}

TEST_F(ConnectionPoolShutdownTest, ShutdownClearsAvailableConnections)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	EXPECT_GT(pool.available_connections(), 0u);

	pool.shutdown();

	EXPECT_EQ(pool.available_connections(), 0u);
}

// ============================================================================
// Connection Pool Statistics Tests
// ============================================================================

class ConnectionPoolStatsTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.min_connections = 2;
		config_.max_connections = 5;
		config_.acquire_timeout = std::chrono::milliseconds(100);
	}

	void TearDown() override {}

	connection_pool_config config_;
};

TEST_F(ConnectionPoolStatsTest, InitialStats)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	auto stats = pool.get_stats();

	EXPECT_EQ(stats.total_connections, config_.min_connections);
	EXPECT_EQ(stats.available_connections, config_.min_connections);
	EXPECT_EQ(stats.active_connections, 0u);
	EXPECT_EQ(stats.successful_acquisitions, 0u);
	EXPECT_EQ(stats.failed_acquisitions, 0u);
}

TEST_F(ConnectionPoolStatsTest, TracksSuccessfulAcquisitions)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	pool.acquire_connection();
	pool.acquire_connection();

	auto stats = pool.get_stats();

	EXPECT_EQ(stats.successful_acquisitions, 2u);
}

TEST_F(ConnectionPoolStatsTest, TracksFailedAcquisitions)
{
	connection_pool_config config;
	config.min_connections = 1;
	config.max_connections = 1;
	config.acquire_timeout = std::chrono::milliseconds(50);

	connection_pool pool(database_types::mysql, config, create_mock_database);
	pool.initialize();

	// Exhaust pool (only 1 connection available)
	auto conn = pool.acquire_connection();
	ASSERT_TRUE(conn.is_ok());

	// This should timeout since the only connection is in use
	auto result = pool.acquire_connection();

	auto stats = pool.get_stats();

	EXPECT_EQ(stats.failed_acquisitions, 1u);
}

// ============================================================================
// Connection Pool Health Check Tests
// ============================================================================

class ConnectionPoolHealthCheckTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.min_connections = 2;
		config_.max_connections = 5;
		config_.idle_timeout = std::chrono::milliseconds(50);
	}

	void TearDown() override {}

	connection_pool_config config_;
};

TEST_F(ConnectionPoolHealthCheckTest, HealthCheckRemovesIdleConnections)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	// Wait for connections to become idle
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// Perform health check
	pool.health_check();

	// Connections that exceeded idle timeout should be removed
	EXPECT_EQ(pool.available_connections(), 0u);
}

TEST_F(ConnectionPoolHealthCheckTest, HealthCheckRemovesUnhealthyConnections)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	// Acquire, mark unhealthy, and release
	auto result = pool.acquire_connection();
	ASSERT_TRUE(result.is_ok());
	result.value()->mark_unhealthy();
	pool.release_connection(result.value());

	// Unhealthy connections should not be added back
	// (release_connection checks is_healthy)
	auto stats = pool.get_stats();
	EXPECT_LT(stats.available_connections, config_.min_connections);
}

// ============================================================================
// Pool Metrics Tests
// ============================================================================

class PoolMetricsTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(PoolMetricsTest, InitialMetricsAreZero)
{
	pool_metrics metrics;

	EXPECT_EQ(metrics.total_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.successful_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.failed_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.timeouts.load(), 0u);
}

TEST_F(PoolMetricsTest, RecordSuccessfulAcquisition)
{
	pool_metrics metrics;

	metrics.record_acquisition(100, true);

	EXPECT_EQ(metrics.total_acquisitions.load(), 1u);
	EXPECT_EQ(metrics.successful_acquisitions.load(), 1u);
	EXPECT_EQ(metrics.failed_acquisitions.load(), 0u);
}

TEST_F(PoolMetricsTest, RecordFailedAcquisition)
{
	pool_metrics metrics;

	metrics.record_acquisition(100, false);

	EXPECT_EQ(metrics.total_acquisitions.load(), 1u);
	EXPECT_EQ(metrics.successful_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.failed_acquisitions.load(), 1u);
}

TEST_F(PoolMetricsTest, TracksMinMaxWaitTime)
{
	pool_metrics metrics;

	metrics.record_acquisition(100, true);
	metrics.record_acquisition(50, true);
	metrics.record_acquisition(200, true);

	EXPECT_EQ(metrics.min_wait_time_us.load(), 50u);
	EXPECT_EQ(metrics.max_wait_time_us.load(), 200u);
}

TEST_F(PoolMetricsTest, CalculatesAverageWaitTime)
{
	pool_metrics metrics;

	metrics.record_acquisition(100, true);
	metrics.record_acquisition(200, true);

	EXPECT_DOUBLE_EQ(metrics.average_wait_time_us(), 150.0);
}

TEST_F(PoolMetricsTest, CalculatesSuccessRate)
{
	pool_metrics metrics;

	metrics.record_acquisition(100, true);
	metrics.record_acquisition(100, true);
	metrics.record_acquisition(100, false);
	metrics.record_acquisition(100, false);

	EXPECT_DOUBLE_EQ(metrics.success_rate(), 50.0);
}

TEST_F(PoolMetricsTest, SuccessRateWithNoAcquisitions)
{
	pool_metrics metrics;

	EXPECT_DOUBLE_EQ(metrics.success_rate(), 100.0);
}

TEST_F(PoolMetricsTest, RecordTimeout)
{
	pool_metrics metrics;

	metrics.record_timeout();
	metrics.record_timeout();

	EXPECT_EQ(metrics.timeouts.load(), 2u);
}

TEST_F(PoolMetricsTest, TracksPeakActive)
{
	pool_metrics metrics;

	metrics.update_active(1);
	metrics.update_active(1);
	metrics.update_active(1);
	metrics.update_active(-1);

	EXPECT_EQ(metrics.current_active.load(), 2u);
	EXPECT_EQ(metrics.peak_active.load(), 3u);
}

TEST_F(PoolMetricsTest, ResetClearsMetrics)
{
	pool_metrics metrics;

	metrics.record_acquisition(100, true);
	metrics.record_acquisition(100, false);
	metrics.record_timeout();

	metrics.reset();

	EXPECT_EQ(metrics.total_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.successful_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.failed_acquisitions.load(), 0u);
	EXPECT_EQ(metrics.timeouts.load(), 0u);
}

// ============================================================================
// Priority Metrics Tests
// ============================================================================

class PriorityMetricsTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(PriorityMetricsTest, TracksByPriority)
{
	priority_metrics<connection_priority> metrics;

	metrics.record_acquisition_with_priority(PRIORITY_CRITICAL, 50, true);
	metrics.record_acquisition_with_priority(PRIORITY_CRITICAL, 60, true);
	metrics.record_acquisition_with_priority(PRIORITY_NORMAL_QUERY, 100, true);

	EXPECT_EQ(metrics.total_acquisitions.load(), 3u);
	EXPECT_DOUBLE_EQ(metrics.average_wait_time_for_priority(PRIORITY_CRITICAL), 55.0);
	EXPECT_DOUBLE_EQ(metrics.average_wait_time_for_priority(PRIORITY_NORMAL_QUERY), 100.0);
}

TEST_F(PriorityMetricsTest, UnknownPriorityReturnsZero)
{
	priority_metrics<connection_priority> metrics;

	EXPECT_DOUBLE_EQ(metrics.average_wait_time_for_priority(PRIORITY_HEALTH_CHECK), 0.0);
}

TEST_F(PriorityMetricsTest, ResetAllClearsPriorityData)
{
	priority_metrics<connection_priority> metrics;

	metrics.record_acquisition_with_priority(PRIORITY_CRITICAL, 100, true);
	metrics.reset_all();

	EXPECT_EQ(metrics.total_acquisitions.load(), 0u);
}

// ============================================================================
// Connection Priority Tests
// ============================================================================

class ConnectionPriorityTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ConnectionPriorityTest, PriorityToString)
{
	EXPECT_STREQ(priority_to_string(PRIORITY_CRITICAL), "CRITICAL");
	EXPECT_STREQ(priority_to_string(PRIORITY_TRANSACTION), "CRITICAL");
	EXPECT_STREQ(priority_to_string(PRIORITY_NORMAL_QUERY), "NORMAL_QUERY");
	EXPECT_STREQ(priority_to_string(PRIORITY_HEALTH_CHECK), "HEALTH_CHECK");
}

TEST_F(ConnectionPriorityTest, PriorityValues)
{
	// CRITICAL should have higher priority than NORMAL
	EXPECT_NE(PRIORITY_CRITICAL, PRIORITY_NORMAL_QUERY);

	// TRANSACTION equals CRITICAL
	EXPECT_EQ(PRIORITY_TRANSACTION, PRIORITY_CRITICAL);
}

// ============================================================================
// Connection Pool Concurrent Access Tests
// ============================================================================

class ConnectionPoolConcurrencyTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.min_connections = 2;
		config_.max_connections = 10;
		config_.acquire_timeout = std::chrono::milliseconds(5000);
	}

	void TearDown() override {}

	connection_pool_config config_;
};

TEST_F(ConnectionPoolConcurrencyTest, ConcurrentAcquireAndRelease)
{
	connection_pool pool(database_types::mysql, config_, create_mock_database);
	pool.initialize();

	constexpr int num_threads = 5;
	constexpr int operations_per_thread = 20;

	std::vector<std::future<int>> futures;

	for (int t = 0; t < num_threads; ++t)
	{
		futures.push_back(std::async(
			std::launch::async,
			[&pool, operations_per_thread]()
			{
				int successful = 0;
				for (int i = 0; i < operations_per_thread; ++i)
				{
					auto result = pool.acquire_connection();
					if (result.is_ok())
					{
						++successful;
						// Simulate some work
						std::this_thread::sleep_for(std::chrono::microseconds(100));
						pool.release_connection(result.value());
					}
				}
				return successful;
			}));
	}

	int total_successful = 0;
	for (auto& future : futures)
	{
		total_successful += future.get();
	}

	// Should have many successful operations
	EXPECT_GT(total_successful, 0);

	// Pool should be in consistent state after concurrent access
	EXPECT_EQ(pool.active_connections(), 0u);
}
