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
 * @file query_cache_test.cpp
 * @brief Unit tests for query cache implementation
 *
 * Tests cover:
 * - Basic cache operations (get, put, clear)
 * - LRU eviction policy
 * - TTL-based expiration
 * - Cache invalidation
 * - Thread safety
 * - Cache key generation
 * - Cache metrics
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include <kcenon/database_server/gateway/query_cache.h>
#include <kcenon/database_server/gateway/query_router.h>

using namespace database_server::gateway;

// ============================================================================
// Cache Configuration Tests
// ============================================================================

class CacheConfigTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(CacheConfigTest, DefaultConfiguration)
{
	cache_config config;

	EXPECT_FALSE(config.enabled);
	EXPECT_EQ(config.max_entries, 10000);
	EXPECT_EQ(config.ttl_seconds, 300);
	EXPECT_EQ(config.max_result_size_bytes, 1024 * 1024);
	EXPECT_TRUE(config.enable_lru);
}

TEST_F(CacheConfigTest, CustomConfiguration)
{
	cache_config config;
	config.enabled = true;
	config.max_entries = 100;
	config.ttl_seconds = 60;
	config.max_result_size_bytes = 4096;
	config.enable_lru = false;

	EXPECT_TRUE(config.enabled);
	EXPECT_EQ(config.max_entries, 100);
	EXPECT_EQ(config.ttl_seconds, 60);
	EXPECT_EQ(config.max_result_size_bytes, 4096);
	EXPECT_FALSE(config.enable_lru);
}

// ============================================================================
// Basic Cache Operations Tests
// ============================================================================

class QueryCacheTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 100;
		config_.ttl_seconds = 60;
		config_.max_result_size_bytes = 1024 * 1024;
	}

	void TearDown() override {}

	cache_config config_;
};

TEST_F(QueryCacheTest, DisabledCacheReturnsNullopt)
{
	config_.enabled = false;
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);

	auto result = cache.get("key1");
	EXPECT_FALSE(result.has_value());
}

TEST_F(QueryCacheTest, EnabledCacheStoresAndRetrieves)
{
	query_cache cache(config_);

	query_response response(1);
	response.affected_rows = 42;

	cache.put("key1", response);
	auto result = cache.get("key1");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->affected_rows, 42);
}

TEST_F(QueryCacheTest, GetNonexistentKeyReturnsNullopt)
{
	query_cache cache(config_);

	auto result = cache.get("nonexistent");
	EXPECT_FALSE(result.has_value());
}

TEST_F(QueryCacheTest, PutReplacesExistingEntry)
{
	query_cache cache(config_);

	query_response response1(1);
	response1.affected_rows = 10;

	query_response response2(2);
	response2.affected_rows = 20;

	cache.put("key1", response1);
	cache.put("key1", response2);

	auto result = cache.get("key1");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->affected_rows, 20);
	EXPECT_EQ(cache.size(), 1);
}

TEST_F(QueryCacheTest, ClearRemovesAllEntries)
{
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);
	cache.put("key2", response);
	cache.put("key3", response);

	EXPECT_EQ(cache.size(), 3);

	cache.clear();

	EXPECT_EQ(cache.size(), 0);
	EXPECT_FALSE(cache.get("key1").has_value());
	EXPECT_FALSE(cache.get("key2").has_value());
	EXPECT_FALSE(cache.get("key3").has_value());
}

TEST_F(QueryCacheTest, ErrorResponseNotCached)
{
	query_cache cache(config_);

	query_response error_response(1, status_code::error, "Error message");
	cache.put("key1", error_response);

	EXPECT_EQ(cache.size(), 0);
	EXPECT_FALSE(cache.get("key1").has_value());
}

// ============================================================================
// LRU Eviction Tests
// ============================================================================

class LRUEvictionTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 3;
		config_.ttl_seconds = 60;
		config_.enable_lru = true;
	}

	cache_config config_;
};

TEST_F(LRUEvictionTest, EvictsLeastRecentlyUsedWhenFull)
{
	query_cache cache(config_);

	query_response response(1);

	cache.put("key1", response);
	cache.put("key2", response);
	cache.put("key3", response);

	EXPECT_EQ(cache.size(), 3);

	// Adding a new entry should evict key1 (oldest)
	cache.put("key4", response);

	EXPECT_EQ(cache.size(), 3);
	EXPECT_FALSE(cache.get("key1").has_value());
	EXPECT_TRUE(cache.get("key2").has_value());
	EXPECT_TRUE(cache.get("key3").has_value());
	EXPECT_TRUE(cache.get("key4").has_value());
}

TEST_F(LRUEvictionTest, AccessRefreshesEntry)
{
	query_cache cache(config_);

	query_response response(1);

	cache.put("key1", response);
	cache.put("key2", response);
	cache.put("key3", response);

	// Access key1 to refresh it
	cache.get("key1");

	// Now key2 should be the oldest
	cache.put("key4", response);

	EXPECT_TRUE(cache.get("key1").has_value());
	EXPECT_FALSE(cache.get("key2").has_value());
	EXPECT_TRUE(cache.get("key3").has_value());
	EXPECT_TRUE(cache.get("key4").has_value());
}

// ============================================================================
// TTL Expiration Tests
// ============================================================================

class TTLExpirationTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 100;
		config_.ttl_seconds = 1;  // 1 second TTL for fast testing
		config_.enable_lru = true;
	}

	cache_config config_;
};

TEST_F(TTLExpirationTest, ExpiredEntryReturnsNullopt)
{
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);

	EXPECT_TRUE(cache.get("key1").has_value());

	// Wait for expiration
	std::this_thread::sleep_for(std::chrono::milliseconds(1200));

	EXPECT_FALSE(cache.get("key1").has_value());
}

TEST_F(TTLExpirationTest, NoExpirationWhenTTLZero)
{
	config_.ttl_seconds = 0;
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);

	// Wait a bit
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	EXPECT_TRUE(cache.get("key1").has_value());
}

// ============================================================================
// Cache Invalidation Tests
// ============================================================================

class CacheInvalidationTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 100;
		config_.ttl_seconds = 60;
	}

	cache_config config_;
};

TEST_F(CacheInvalidationTest, InvalidateByTableName)
{
	query_cache cache(config_);

	query_response response(1);

	cache.put("key1", response, {"users"});
	cache.put("key2", response, {"users", "orders"});
	cache.put("key3", response, {"products"});

	EXPECT_EQ(cache.size(), 3);

	cache.invalidate("users");

	EXPECT_EQ(cache.size(), 1);
	EXPECT_FALSE(cache.get("key1").has_value());
	EXPECT_FALSE(cache.get("key2").has_value());
	EXPECT_TRUE(cache.get("key3").has_value());
}

TEST_F(CacheInvalidationTest, InvalidateByKey)
{
	query_cache cache(config_);

	query_response response(1);

	cache.put("key1", response);
	cache.put("key2", response);

	cache.invalidate_key("key1");

	EXPECT_EQ(cache.size(), 1);
	EXPECT_FALSE(cache.get("key1").has_value());
	EXPECT_TRUE(cache.get("key2").has_value());
}

TEST_F(CacheInvalidationTest, InvalidateNonexistentTableNoOp)
{
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response, {"users"});

	cache.invalidate("nonexistent_table");

	EXPECT_EQ(cache.size(), 1);
	EXPECT_TRUE(cache.get("key1").has_value());
}

// ============================================================================
// Cache Key Generation Tests
// ============================================================================

class CacheKeyTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(CacheKeyTest, SameQueryGeneratesSameKey)
{
	query_request request1("SELECT * FROM users", query_type::select);
	query_request request2("SELECT * FROM users", query_type::select);

	auto key1 = query_cache::make_key(request1);
	auto key2 = query_cache::make_key(request2);

	EXPECT_EQ(key1, key2);
}

TEST_F(CacheKeyTest, DifferentQueryGeneratesDifferentKey)
{
	query_request request1("SELECT * FROM users", query_type::select);
	query_request request2("SELECT * FROM orders", query_type::select);

	auto key1 = query_cache::make_key(request1);
	auto key2 = query_cache::make_key(request2);

	EXPECT_NE(key1, key2);
}

TEST_F(CacheKeyTest, DifferentParamsGenerateDifferentKey)
{
	query_request request1("SELECT * FROM users WHERE id = ?", query_type::select);
	request1.params.emplace_back("id", static_cast<int64_t>(1));

	query_request request2("SELECT * FROM users WHERE id = ?", query_type::select);
	request2.params.emplace_back("id", static_cast<int64_t>(2));

	auto key1 = query_cache::make_key(request1);
	auto key2 = query_cache::make_key(request2);

	EXPECT_NE(key1, key2);
}

TEST_F(CacheKeyTest, KeyIsHexString)
{
	query_request request("SELECT 1", query_type::select);
	auto key = query_cache::make_key(request);

	// Key should only contain hex characters
	for (char c : key)
	{
		EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
	}
}

// ============================================================================
// Cache Metrics Tests
// ============================================================================

class CacheMetricsTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 100;
		config_.ttl_seconds = 60;
	}

	cache_config config_;
};

TEST_F(CacheMetricsTest, HitMissTracking)
{
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);

	// Hit
	cache.get("key1");
	EXPECT_EQ(cache.metrics().hits.load(), 1);
	EXPECT_EQ(cache.metrics().misses.load(), 0);

	// Miss
	cache.get("nonexistent");
	EXPECT_EQ(cache.metrics().hits.load(), 1);
	EXPECT_EQ(cache.metrics().misses.load(), 1);
}

TEST_F(CacheMetricsTest, HitRateCalculation)
{
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);

	// 3 hits
	cache.get("key1");
	cache.get("key1");
	cache.get("key1");

	// 1 miss
	cache.get("nonexistent");

	// Hit rate should be 75%
	EXPECT_NEAR(cache.metrics().hit_rate(), 75.0, 0.01);
}

TEST_F(CacheMetricsTest, EvictionTracking)
{
	config_.max_entries = 2;
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);
	cache.put("key2", response);
	cache.put("key3", response);  // Should evict key1

	EXPECT_EQ(cache.metrics().evictions.load(), 1);
}

TEST_F(CacheMetricsTest, ResetMetrics)
{
	query_cache cache(config_);

	query_response response(1);
	cache.put("key1", response);
	cache.get("key1");
	cache.get("nonexistent");

	cache.reset_metrics();

	EXPECT_EQ(cache.metrics().hits.load(), 0);
	EXPECT_EQ(cache.metrics().misses.load(), 0);
	EXPECT_EQ(cache.metrics().puts.load(), 0);
}

// ============================================================================
// Table Name Extraction Tests
// ============================================================================

class TableExtractionTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(TableExtractionTest, ExtractFromSelect)
{
	auto tables = query_router::extract_table_names(
		"SELECT * FROM users WHERE id = 1", query_type::select);

	EXPECT_EQ(tables.size(), 1);
	EXPECT_TRUE(tables.count("users") > 0);
}

TEST_F(TableExtractionTest, ExtractFromJoin)
{
	auto tables = query_router::extract_table_names(
		"SELECT * FROM users JOIN orders ON users.id = orders.user_id",
		query_type::select);

	EXPECT_EQ(tables.size(), 2);
	EXPECT_TRUE(tables.count("users") > 0);
	EXPECT_TRUE(tables.count("orders") > 0);
}

TEST_F(TableExtractionTest, ExtractFromInsert)
{
	auto tables = query_router::extract_table_names(
		"INSERT INTO users (name, email) VALUES ('John', 'john@example.com')",
		query_type::insert);

	EXPECT_EQ(tables.size(), 1);
	EXPECT_TRUE(tables.count("users") > 0);
}

TEST_F(TableExtractionTest, ExtractFromUpdate)
{
	auto tables = query_router::extract_table_names(
		"UPDATE users SET name = 'Jane' WHERE id = 1", query_type::update);

	EXPECT_EQ(tables.size(), 1);
	EXPECT_TRUE(tables.count("users") > 0);
}

TEST_F(TableExtractionTest, ExtractFromDelete)
{
	auto tables = query_router::extract_table_names(
		"DELETE FROM users WHERE id = 1", query_type::del);

	EXPECT_EQ(tables.size(), 1);
	EXPECT_TRUE(tables.count("users") > 0);
}

TEST_F(TableExtractionTest, CaseInsensitive)
{
	auto tables = query_router::extract_table_names(
		"select * from Users where id = 1", query_type::select);

	EXPECT_EQ(tables.size(), 1);
	EXPECT_TRUE(tables.count("Users") > 0);
}

// ============================================================================
// Size Limit Tests
// ============================================================================

class SizeLimitTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 100;
		config_.ttl_seconds = 60;
		config_.max_result_size_bytes = 100;  // Very small limit
	}

	cache_config config_;
};

TEST_F(SizeLimitTest, LargeResultNotCached)
{
	query_cache cache(config_);

	query_response response(1);
	// Add many rows to exceed size limit
	for (int i = 0; i < 100; ++i)
	{
		result_row row;
		row.cells.emplace_back(std::string(10, 'x'));
		response.rows.push_back(row);
	}

	cache.put("key1", response);

	EXPECT_EQ(cache.size(), 0);
	EXPECT_EQ(cache.metrics().skipped_too_large.load(), 1);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

class ThreadSafetyTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		config_.enabled = true;
		config_.max_entries = 1000;
		config_.ttl_seconds = 60;
	}

	cache_config config_;
};

TEST_F(ThreadSafetyTest, ConcurrentPutAndGet)
{
	query_cache cache(config_);

	constexpr int num_threads = 10;
	constexpr int ops_per_thread = 100;

	std::vector<std::thread> threads;

	for (int t = 0; t < num_threads; ++t)
	{
		threads.emplace_back(
			[&cache, t]()
			{
				for (int i = 0; i < ops_per_thread; ++i)
				{
					std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
					query_response response(static_cast<uint64_t>(t * ops_per_thread + i));

					cache.put(key, response);
					cache.get(key);
				}
			});
	}

	for (auto& t : threads)
	{
		t.join();
	}

	// Should not crash or deadlock
	EXPECT_LE(cache.size(), config_.max_entries);
}

TEST_F(ThreadSafetyTest, ConcurrentInvalidation)
{
	query_cache cache(config_);

	// Pre-populate cache
	query_response response(1);
	for (int i = 0; i < 100; ++i)
	{
		cache.put("key_" + std::to_string(i), response, {"table_" + std::to_string(i % 10)});
	}

	std::vector<std::thread> threads;

	// Readers
	for (int t = 0; t < 5; ++t)
	{
		threads.emplace_back(
			[&cache]()
			{
				for (int i = 0; i < 100; ++i)
				{
					cache.get("key_" + std::to_string(i % 100));
				}
			});
	}

	// Invalidators
	for (int t = 0; t < 5; ++t)
	{
		threads.emplace_back(
			[&cache, t]()
			{
				for (int i = 0; i < 10; ++i)
				{
					cache.invalidate("table_" + std::to_string((t + i) % 10));
				}
			});
	}

	for (auto& t : threads)
	{
		t.join();
	}

	// Should not crash or deadlock
	SUCCEED();
}
