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
 * @file resilience_test.cpp
 * @brief Unit tests for resilience components (Phase 2)
 *
 * Tests cover:
 * - Health status structure
 * - Health check configuration
 * - Reconnection configuration
 * - Connection state enum
 * - Connection health monitor basic functionality
 * - Resilient database connection basic functionality
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <kcenon/database_server/resilience/connection_health_monitor.h>
#include <kcenon/database_server/resilience/resilient_database_connection.h>

using namespace database_server::resilience;
using namespace std::chrono_literals;

// ============================================================================
// Health Status Tests
// ============================================================================

class HealthStatusTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(HealthStatusTest, DefaultConstruction)
{
	health_status status;

	EXPECT_FALSE(status.is_healthy);
	EXPECT_EQ(status.health_score, 0u);
	EXPECT_EQ(status.latency.count(), 0);
	EXPECT_EQ(status.successful_queries, 0u);
	EXPECT_EQ(status.failed_queries, 0u);
	EXPECT_TRUE(status.status_message.empty());
}

TEST_F(HealthStatusTest, SuccessRateWithNoQueries)
{
	health_status status;

	// With no queries, success rate should be 100%
	EXPECT_DOUBLE_EQ(status.success_rate(), 100.0);
}

TEST_F(HealthStatusTest, SuccessRateWithQueries)
{
	health_status status;
	status.successful_queries = 8;
	status.failed_queries = 2;

	// 8 out of 10 = 80%
	EXPECT_DOUBLE_EQ(status.success_rate(), 80.0);
}

TEST_F(HealthStatusTest, SuccessRateAllFailed)
{
	health_status status;
	status.successful_queries = 0;
	status.failed_queries = 10;

	EXPECT_DOUBLE_EQ(status.success_rate(), 0.0);
}

TEST_F(HealthStatusTest, SuccessRateAllSuccessful)
{
	health_status status;
	status.successful_queries = 100;
	status.failed_queries = 0;

	EXPECT_DOUBLE_EQ(status.success_rate(), 100.0);
}

// ============================================================================
// Health Check Config Tests
// ============================================================================

class HealthCheckConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(HealthCheckConfigTest, DefaultValues)
{
	health_check_config config;

	EXPECT_EQ(config.heartbeat_interval, 5000ms);
	EXPECT_EQ(config.timeout, 2000ms);
	EXPECT_EQ(config.failure_threshold, 3u);
	EXPECT_EQ(config.min_health_score, 50u);
	EXPECT_TRUE(config.enable_heartbeat);
}

TEST_F(HealthCheckConfigTest, CustomValues)
{
	health_check_config config;
	config.heartbeat_interval = 10000ms;
	config.timeout = 5000ms;
	config.failure_threshold = 5;
	config.min_health_score = 70;
	config.enable_heartbeat = false;

	EXPECT_EQ(config.heartbeat_interval, 10000ms);
	EXPECT_EQ(config.timeout, 5000ms);
	EXPECT_EQ(config.failure_threshold, 5u);
	EXPECT_EQ(config.min_health_score, 70u);
	EXPECT_FALSE(config.enable_heartbeat);
}

// ============================================================================
// Reconnection Config Tests
// ============================================================================

class ReconnectionConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ReconnectionConfigTest, DefaultValues)
{
	reconnection_config config;

	EXPECT_EQ(config.initial_delay, 100ms);
	EXPECT_EQ(config.max_delay, 30000ms);
	EXPECT_DOUBLE_EQ(config.backoff_multiplier, 2.0);
	EXPECT_EQ(config.max_retries, 10u);
	EXPECT_TRUE(config.enable_auto_reconnect);
}

TEST_F(ReconnectionConfigTest, CustomValues)
{
	reconnection_config config;
	config.initial_delay = 500ms;
	config.max_delay = 60000ms;
	config.backoff_multiplier = 1.5;
	config.max_retries = 5;
	config.enable_auto_reconnect = false;

	EXPECT_EQ(config.initial_delay, 500ms);
	EXPECT_EQ(config.max_delay, 60000ms);
	EXPECT_DOUBLE_EQ(config.backoff_multiplier, 1.5);
	EXPECT_EQ(config.max_retries, 5u);
	EXPECT_FALSE(config.enable_auto_reconnect);
}

// ============================================================================
// Connection State Tests
// ============================================================================

class ConnectionStateTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ConnectionStateTest, ToString)
{
	EXPECT_STREQ(to_string(connection_state::disconnected), "disconnected");
	EXPECT_STREQ(to_string(connection_state::connecting), "connecting");
	EXPECT_STREQ(to_string(connection_state::connected), "connected");
	EXPECT_STREQ(to_string(connection_state::reconnecting), "reconnecting");
	EXPECT_STREQ(to_string(connection_state::failed), "failed");
}

// ============================================================================
// Connection Health Monitor Tests
// ============================================================================

class ConnectionHealthMonitorTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ConnectionHealthMonitorTest, NullBackendCheckNow)
{
	// Create monitor with null backend
	connection_health_monitor monitor(nullptr);

	auto result = monitor.check_now();

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend not initialized");
}

TEST_F(ConnectionHealthMonitorTest, NullBackendHealthStatus)
{
	connection_health_monitor monitor(nullptr);

	auto status = monitor.get_health_status();

	// Initial status should be unhealthy for null backend
	EXPECT_FALSE(status.is_healthy);
	EXPECT_EQ(status.health_score, 0u);
}

TEST_F(ConnectionHealthMonitorTest, StartStopMonitoring)
{
	connection_health_monitor monitor(nullptr);

	// Should not crash when starting/stopping with null backend
	monitor.start_monitoring();
	monitor.stop_monitoring();

	// Multiple stop calls should be safe
	monitor.stop_monitoring();
	monitor.stop_monitoring();
}

TEST_F(ConnectionHealthMonitorTest, ResetStatistics)
{
	connection_health_monitor monitor(nullptr);

	// Record some activity
	monitor.record_success(10ms);
	monitor.record_failure("Test failure");

	// Reset should clear everything
	monitor.reset_statistics();

	auto status = monitor.get_health_status();
	EXPECT_EQ(status.successful_queries, 0u);
	EXPECT_EQ(status.failed_queries, 0u);
}

// ============================================================================
// Resilient Database Connection Tests
// ============================================================================

class ResilientDatabaseConnectionTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ResilientDatabaseConnectionTest, NullBackendType)
{
	resilient_database_connection conn(nullptr);

	EXPECT_EQ(conn.type(), database::database_types::none);
}

TEST_F(ResilientDatabaseConnectionTest, NullBackendNotInitialized)
{
	resilient_database_connection conn(nullptr);

	EXPECT_FALSE(conn.is_initialized());
}

TEST_F(ResilientDatabaseConnectionTest, NullBackendState)
{
	resilient_database_connection conn(nullptr);

	EXPECT_EQ(conn.get_state(), connection_state::disconnected);
	EXPECT_EQ(conn.get_retry_count(), 0u);
}

TEST_F(ResilientDatabaseConnectionTest, InitializeWithNullBackend)
{
	resilient_database_connection conn(nullptr);
	database::core::connection_config config;

	auto result = conn.initialize(config);

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, ShutdownWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.shutdown();

	EXPECT_TRUE(result.is_ok());
}

TEST_F(ResilientDatabaseConnectionTest, InsertQueryWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.insert_query("INSERT INTO test VALUES (1)");

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, UpdateQueryWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.update_query("UPDATE test SET value = 2");

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, DeleteQueryWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.delete_query("DELETE FROM test WHERE id = 1");

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, SelectQueryWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.select_query("SELECT * FROM test");

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, ExecuteQueryWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.execute_query("CREATE TABLE test (id INT)");

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, TransactionWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto begin_result = conn.begin_transaction();
	EXPECT_TRUE(begin_result.is_err());

	auto commit_result = conn.commit_transaction();
	EXPECT_TRUE(commit_result.is_err());
	EXPECT_EQ(commit_result.error().message, "Backend is null");

	auto rollback_result = conn.rollback_transaction();
	EXPECT_TRUE(rollback_result.is_err());
	EXPECT_EQ(rollback_result.error().message, "Backend is null");
}

TEST_F(ResilientDatabaseConnectionTest, CheckHealthWithNullBackend)
{
	resilient_database_connection conn(nullptr);

	auto result = conn.check_health();

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Health monitor not initialized");
}

TEST_F(ResilientDatabaseConnectionTest, ConnectionInfo)
{
	resilient_database_connection conn(nullptr);

	auto info = conn.connection_info();

	EXPECT_EQ(info["resilience_enabled"], "true");
	EXPECT_EQ(info["connection_state"], "disconnected");
	EXPECT_EQ(info["retry_count"], "0");
	EXPECT_EQ(info["auto_recovery_enabled"], "false");
}

TEST_F(ResilientDatabaseConnectionTest, AutoRecoveryDisabledReconnect)
{
	reconnection_config config;
	config.enable_auto_reconnect = false;

	resilient_database_connection conn(nullptr, config);

	auto result = conn.ensure_connected();

	EXPECT_TRUE(result.is_err());
	EXPECT_EQ(result.error().message, "Auto reconnect disabled");
}

TEST_F(ResilientDatabaseConnectionTest, StartStopAutoRecovery)
{
	resilient_database_connection conn(nullptr);

	// Should not crash with null backend
	conn.start_auto_recovery();
	conn.stop_auto_recovery();
}
