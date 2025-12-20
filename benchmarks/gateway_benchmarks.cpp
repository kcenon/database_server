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
 * @file gateway_benchmarks.cpp
 * @brief Performance benchmarks for database gateway components (Phase 3.5)
 *
 * Benchmarks cover:
 * - Query protocol serialization/deserialization throughput
 * - Query router routing overhead (target: < 1ms)
 * - Auth middleware authentication throughput
 * - Rate limiter performance
 * - Overall pipeline throughput (target: 10k+ queries/sec)
 */

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <kcenon/database_server/gateway/auth_middleware.h>
#include <kcenon/database_server/gateway/query_protocol.h>
#include <kcenon/database_server/gateway/query_router.h>
#include <kcenon/database_server/gateway/query_types.h>

using namespace database_server::gateway;

// ============================================================================
// Benchmark Fixtures
// ============================================================================

class GatewayBenchmarkFixture : public benchmark::Fixture
{
public:
	void SetUp(const benchmark::State& /*state*/) override
	{
		auth_config_.enabled = true;
		auth_config_.validate_on_each_request = true;

		rate_config_.enabled = true;
		rate_config_.requests_per_second = 100000;
		rate_config_.burst_size = 200000;
		rate_config_.window_size_ms = 1000;
		rate_config_.block_duration_ms = 60000;

		router_config_.default_timeout_ms = 5000;
		router_config_.max_concurrent_queries = 10000;
		router_config_.enable_metrics = true;
	}

	void TearDown(const benchmark::State& /*state*/) override {}

protected:
	auth_token create_valid_token()
	{
		auth_token token;
		token.token = "benchmark-token-" + std::to_string(token_counter_++);
		token.client_id = "benchmark-client";

		auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
		token.expires_at = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				future.time_since_epoch())
				.count());

		return token;
	}

	query_request create_simple_request()
	{
		query_request request("SELECT * FROM users WHERE id = ?", query_type::select);
		request.header.message_id = message_counter_++;
		request.params.emplace_back("id", static_cast<int64_t>(42));
		return request;
	}

	query_request create_complex_request()
	{
		query_request request(
			"INSERT INTO orders (user_id, product_id, quantity, price, notes) "
			"VALUES (?, ?, ?, ?, ?)",
			query_type::insert);
		request.header.message_id = message_counter_++;
		request.params.emplace_back("user_id", static_cast<int64_t>(12345));
		request.params.emplace_back("product_id", static_cast<int64_t>(67890));
		request.params.emplace_back("quantity", static_cast<int64_t>(10));
		request.params.emplace_back("price", 99.99);
		request.params.emplace_back("notes",
									std::string("Express shipping requested"));
		return request;
	}

	query_response create_result_response(size_t row_count)
	{
		query_response response(message_counter_++);
		response.status = status_code::ok;

		column_metadata col1;
		col1.name = "id";
		col1.type_name = "INTEGER";
		response.columns.push_back(col1);

		column_metadata col2;
		col2.name = "name";
		col2.type_name = "VARCHAR";
		response.columns.push_back(col2);

		for (size_t i = 0; i < row_count; ++i)
		{
			result_row row;
			row.cells.emplace_back(static_cast<int64_t>(i));
			row.cells.emplace_back(std::string("User " + std::to_string(i)));
			response.rows.push_back(row);
		}

		return response;
	}

	auth_config auth_config_;
	rate_limit_config rate_config_;
	router_config router_config_;

private:
	std::atomic<uint64_t> token_counter_{0};
	std::atomic<uint64_t> message_counter_{0};
};

// ============================================================================
// Query Router Benchmarks - Routing Overhead
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RouterOverhead)(benchmark::State& state)
{
	query_router router(router_config_);

	for (auto _ : state)
	{
		auto request = create_simple_request();
		auto start = std::chrono::high_resolution_clock::now();

		auto response = router.execute(request);

		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

		benchmark::DoNotOptimize(response);
		state.SetIterationTime(duration.count() / 1e9);
	}

	state.SetItemsProcessed(state.iterations());

	auto avg_time_us
		= router.metrics().average_execution_time_us();
	state.counters["avg_time_us"] = avg_time_us;
	state.counters["routing_overhead_us"]
		= benchmark::Counter(avg_time_us, benchmark::Counter::kAvgThreads);
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RouterOverhead)
	->UseManualTime()
	->Unit(benchmark::kMicrosecond)
	->Iterations(10000);

// ============================================================================
// Auth Middleware Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, AuthValidation)(benchmark::State& state)
{
	auth_middleware middleware(auth_config_, rate_config_);
	auto token = create_valid_token();

	for (auto _ : state)
	{
		auto result = middleware.authenticate("session-001", token);
		benchmark::DoNotOptimize(result);
	}

	state.SetItemsProcessed(state.iterations());
	state.counters["auth_attempts"] = middleware.metrics().total_auth_attempts.load();
	state.counters["success_rate"] = middleware.metrics().success_rate();
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, AuthValidation)
	->Unit(benchmark::kNanosecond)
	->Iterations(100000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, AuthWithRateLimiting)
(benchmark::State& state)
{
	auth_middleware middleware(auth_config_, rate_config_);
	auto token = create_valid_token();

	for (auto _ : state)
	{
		auto result = middleware.check("session-001", token);
		benchmark::DoNotOptimize(result);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, AuthWithRateLimiting)
	->Unit(benchmark::kNanosecond)
	->Iterations(100000);

// ============================================================================
// Rate Limiter Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RateLimiterCheck)(benchmark::State& state)
{
	rate_limiter limiter(rate_config_);

	for (auto _ : state)
	{
		bool allowed = limiter.allow_request("client-001");
		benchmark::DoNotOptimize(allowed);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RateLimiterCheck)
	->Unit(benchmark::kNanosecond)
	->Iterations(100000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RateLimiterMultiClient)
(benchmark::State& state)
{
	rate_limiter limiter(rate_config_);
	const int num_clients = state.range(0);
	int client_index = 0;

	for (auto _ : state)
	{
		std::string client_id = "client-" + std::to_string(client_index % num_clients);
		bool allowed = limiter.allow_request(client_id);
		benchmark::DoNotOptimize(allowed);
		++client_index;
	}

	state.SetItemsProcessed(state.iterations());
	state.counters["clients"] = num_clients;
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RateLimiterMultiClient)
	->Unit(benchmark::kNanosecond)
	->Arg(10)
	->Arg(100)
	->Arg(1000);

// ============================================================================
// Query Protocol Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, QueryRequestCreation)
(benchmark::State& state)
{
	for (auto _ : state)
	{
		auto request = create_simple_request();
		benchmark::DoNotOptimize(request);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, QueryRequestCreation)
	->Unit(benchmark::kNanosecond)
	->Iterations(100000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, QueryResponseCreation)
(benchmark::State& state)
{
	const size_t row_count = state.range(0);

	for (auto _ : state)
	{
		auto response = create_result_response(row_count);
		benchmark::DoNotOptimize(response);
	}

	state.SetItemsProcessed(state.iterations());
	state.SetBytesProcessed(state.iterations() * row_count * 64);
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, QueryResponseCreation)
	->Unit(benchmark::kMicrosecond)
	->Arg(10)
	->Arg(100)
	->Arg(1000);

#ifdef BUILD_WITH_CONTAINER_SYSTEM

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RequestSerialize)(benchmark::State& state)
{
	auto request = create_complex_request();

	for (auto _ : state)
	{
		auto container = request.serialize();
		benchmark::DoNotOptimize(container);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RequestSerialize)
	->Unit(benchmark::kMicrosecond)
	->Iterations(10000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RequestDeserialize)
(benchmark::State& state)
{
	auto request = create_complex_request();
	auto container = request.serialize();

	for (auto _ : state)
	{
		auto result = query_request::deserialize(container);
		benchmark::DoNotOptimize(result);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RequestDeserialize)
	->Unit(benchmark::kMicrosecond)
	->Iterations(10000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RequestRoundTrip)(benchmark::State& state)
{
	for (auto _ : state)
	{
		auto original = create_complex_request();
		auto container = original.serialize();
		auto result = query_request::deserialize(container);
		benchmark::DoNotOptimize(result);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RequestRoundTrip)
	->Unit(benchmark::kMicrosecond)
	->Iterations(10000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, ResponseSerialize)(benchmark::State& state)
{
	const size_t row_count = state.range(0);
	auto response = create_result_response(row_count);

	for (auto _ : state)
	{
		auto container = response.serialize();
		benchmark::DoNotOptimize(container);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, ResponseSerialize)
	->Unit(benchmark::kMicrosecond)
	->Arg(10)
	->Arg(100)
	->Arg(1000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, ResponseDeserialize)
(benchmark::State& state)
{
	const size_t row_count = state.range(0);
	auto response = create_result_response(row_count);
	auto container = response.serialize();

	for (auto _ : state)
	{
		auto result = query_response::deserialize(container);
		benchmark::DoNotOptimize(result);
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, ResponseDeserialize)
	->Unit(benchmark::kMicrosecond)
	->Arg(10)
	->Arg(100)
	->Arg(1000);

#endif // BUILD_WITH_CONTAINER_SYSTEM

// ============================================================================
// Full Pipeline Throughput Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, FullPipelineThroughput)
(benchmark::State& state)
{
	auth_middleware middleware(auth_config_, rate_config_);
	query_router router(router_config_);
	auto token = create_valid_token();

	for (auto _ : state)
	{
		auto auth_result = middleware.check("session-001", token);
		if (auth_result.success)
		{
			auto request = create_simple_request();
			auto response = router.execute(request);
			benchmark::DoNotOptimize(response);
		}
	}

	state.SetItemsProcessed(state.iterations());

	double queries_per_second
		= state.iterations() / (state.iterations() * state.min_time() / 1e9);
	state.counters["queries_per_sec"]
		= benchmark::Counter(queries_per_second, benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, FullPipelineThroughput)
	->Unit(benchmark::kMicrosecond)
	->Iterations(100000)
	->MeasureProcessCPUTime();

// ============================================================================
// Concurrent Throughput Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, ConcurrentRouterThroughput)
(benchmark::State& state)
{
	query_router router(router_config_);
	const int num_threads = state.range(0);

	for (auto _ : state)
	{
		std::vector<std::thread> threads;
		threads.reserve(num_threads);
		std::atomic<uint64_t> total_queries{0};

		const int queries_per_thread = 1000;

		for (int t = 0; t < num_threads; ++t)
		{
			threads.emplace_back([&, t]() {
				for (int i = 0; i < queries_per_thread; ++i)
				{
					query_request request("SELECT " + std::to_string(i),
										  query_type::select);
					request.header.message_id = t * queries_per_thread + i;
					auto response = router.execute(request);
					benchmark::DoNotOptimize(response);
					total_queries.fetch_add(1);
				}
			});
		}

		for (auto& thread : threads)
		{
			thread.join();
		}

		benchmark::DoNotOptimize(total_queries.load());
	}

	state.SetItemsProcessed(state.iterations() * num_threads * 1000);
	state.counters["threads"] = num_threads;
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, ConcurrentRouterThroughput)
	->Unit(benchmark::kMillisecond)
	->Arg(1)
	->Arg(2)
	->Arg(4)
	->Arg(8)
	->UseRealTime();

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, ConcurrentAuthThroughput)
(benchmark::State& state)
{
	auth_middleware middleware(auth_config_, rate_config_);
	const int num_threads = state.range(0);

	for (auto _ : state)
	{
		std::vector<std::thread> threads;
		threads.reserve(num_threads);
		std::atomic<uint64_t> total_auths{0};

		const int auths_per_thread = 1000;

		for (int t = 0; t < num_threads; ++t)
		{
			threads.emplace_back([&, t]() {
				auto token = create_valid_token();
				std::string session_id = "session-" + std::to_string(t);

				for (int i = 0; i < auths_per_thread; ++i)
				{
					auto result = middleware.authenticate(session_id, token);
					benchmark::DoNotOptimize(result);
					total_auths.fetch_add(1);
				}
			});
		}

		for (auto& thread : threads)
		{
			thread.join();
		}

		benchmark::DoNotOptimize(total_auths.load());
	}

	state.SetItemsProcessed(state.iterations() * num_threads * 1000);
	state.counters["threads"] = num_threads;
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, ConcurrentAuthThroughput)
	->Unit(benchmark::kMillisecond)
	->Arg(1)
	->Arg(2)
	->Arg(4)
	->Arg(8)
	->UseRealTime();

// ============================================================================
// Latency Measurement Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, RouterLatencyDistribution)
(benchmark::State& state)
{
	query_router router(router_config_);
	std::vector<double> latencies;
	latencies.reserve(10000);

	for (auto _ : state)
	{
		auto request = create_simple_request();
		auto start = std::chrono::high_resolution_clock::now();

		auto response = router.execute(request);

		auto end = std::chrono::high_resolution_clock::now();
		auto duration_us
			= std::chrono::duration_cast<std::chrono::microseconds>(end - start)
				  .count();

		latencies.push_back(duration_us);
		benchmark::DoNotOptimize(response);
	}

	std::sort(latencies.begin(), latencies.end());

	if (!latencies.empty())
	{
		size_t n = latencies.size();
		state.counters["p50_us"] = latencies[n * 50 / 100];
		state.counters["p90_us"] = latencies[n * 90 / 100];
		state.counters["p99_us"] = latencies[n * 99 / 100];
		state.counters["max_us"] = latencies.back();

		double sum = 0;
		for (auto l : latencies)
		{
			sum += l;
		}
		state.counters["avg_us"] = sum / n;

		bool under_1ms = latencies[n * 99 / 100] < 1000;
		state.counters["p99_under_1ms"] = under_1ms ? 1 : 0;
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, RouterLatencyDistribution)
	->Unit(benchmark::kMicrosecond)
	->Iterations(10000);

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, AuthLatencyDistribution)
(benchmark::State& state)
{
	auth_middleware middleware(auth_config_, rate_config_);
	auto token = create_valid_token();
	std::vector<double> latencies;
	latencies.reserve(10000);

	for (auto _ : state)
	{
		auto start = std::chrono::high_resolution_clock::now();

		auto result = middleware.check("session-001", token);

		auto end = std::chrono::high_resolution_clock::now();
		auto duration_us
			= std::chrono::duration_cast<std::chrono::microseconds>(end - start)
				  .count();

		latencies.push_back(duration_us);
		benchmark::DoNotOptimize(result);
	}

	std::sort(latencies.begin(), latencies.end());

	if (!latencies.empty())
	{
		size_t n = latencies.size();
		state.counters["p50_us"] = latencies[n * 50 / 100];
		state.counters["p90_us"] = latencies[n * 90 / 100];
		state.counters["p99_us"] = latencies[n * 99 / 100];
		state.counters["max_us"] = latencies.back();
	}

	state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, AuthLatencyDistribution)
	->Unit(benchmark::kMicrosecond)
	->Iterations(10000);

// ============================================================================
// Metrics Overhead Benchmarks
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, MetricsOverhead)(benchmark::State& state)
{
	router_config_.enable_metrics = state.range(0) == 1;
	query_router router(router_config_);

	for (auto _ : state)
	{
		auto request = create_simple_request();
		auto response = router.execute(request);
		benchmark::DoNotOptimize(response);
	}

	state.SetItemsProcessed(state.iterations());
	state.counters["metrics_enabled"] = router_config_.enable_metrics ? 1 : 0;
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, MetricsOverhead)
	->Unit(benchmark::kNanosecond)
	->Arg(0)
	->Arg(1);

// ============================================================================
// Throughput Target Verification
// ============================================================================

BENCHMARK_DEFINE_F(GatewayBenchmarkFixture, ThroughputTarget10k)
(benchmark::State& state)
{
	query_router router(router_config_);
	const int target_qps = 10000;
	const int test_duration_ms = 1000;
	const int expected_queries = target_qps * test_duration_ms / 1000;

	for (auto _ : state)
	{
		auto start = std::chrono::high_resolution_clock::now();
		int executed = 0;

		while (executed < expected_queries)
		{
			query_request request("SELECT 1", query_type::select);
			request.header.message_id = executed;
			auto response = router.execute(request);
			benchmark::DoNotOptimize(response);
			++executed;
		}

		auto end = std::chrono::high_resolution_clock::now();
		auto duration_ms
			= std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
				  .count();

		double actual_qps
			= static_cast<double>(executed) / (duration_ms / 1000.0);

		state.counters["queries_executed"] = executed;
		state.counters["duration_ms"] = duration_ms;
		state.counters["actual_qps"] = actual_qps;
		state.counters["target_met"] = (actual_qps >= target_qps) ? 1 : 0;
	}

	state.SetItemsProcessed(expected_queries);
}

BENCHMARK_REGISTER_F(GatewayBenchmarkFixture, ThroughputTarget10k)
	->Unit(benchmark::kMillisecond)
	->Iterations(1)
	->UseRealTime();

BENCHMARK_MAIN();
