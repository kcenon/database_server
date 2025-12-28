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
 * @file session_id_test.cpp
 * @brief Unit tests for cryptographically secure session ID generation
 *
 * Tests cover:
 * - Session ID format validation (32-char hex string)
 * - Uniqueness verification across large sample
 * - Entropy estimation
 * - Thread-safety with concurrent generation
 * - Performance validation (< 1μs per generation)
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <vector>

#include <kcenon/database_server/gateway/session_id_generator.h>

using namespace database_server::gateway;

// ============================================================================
// Session ID Format Tests
// ============================================================================

class SessionIdFormatTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(SessionIdFormatTest, GeneratesCorrectLength)
{
	auto id = generate_session_id();
	EXPECT_EQ(id.length(), 32) << "Session ID should be 32 hex characters (128 bits)";
}

TEST_F(SessionIdFormatTest, ContainsOnlyHexCharacters)
{
	auto id = generate_session_id();

	for (char c : id)
	{
		bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		EXPECT_TRUE(is_hex) << "Character '" << c << "' is not a valid hex character";
	}
}

TEST_F(SessionIdFormatTest, GeneratesLowercaseHex)
{
	for (int i = 0; i < 100; ++i)
	{
		auto id = generate_session_id();
		for (char c : id)
		{
			if (c >= 'a' && c <= 'f')
			{
				// Valid lowercase
				continue;
			}
			EXPECT_FALSE(c >= 'A' && c <= 'F')
				<< "Session ID should use lowercase hex, found: " << c;
		}
	}
}

// ============================================================================
// Session ID Uniqueness Tests
// ============================================================================

class SessionIdUniquenessTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(SessionIdUniquenessTest, GeneratesUniqueIds)
{
	constexpr size_t sample_size = 10000;
	std::unordered_set<std::string> ids;

	for (size_t i = 0; i < sample_size; ++i)
	{
		auto id = generate_session_id();
		auto [_, inserted] = ids.insert(id);
		EXPECT_TRUE(inserted) << "Duplicate session ID detected: " << id;
	}

	EXPECT_EQ(ids.size(), sample_size);
}

TEST_F(SessionIdUniquenessTest, LargeScaleUniqueness)
{
	constexpr size_t sample_size = 100000;
	std::unordered_set<std::string> ids;
	ids.reserve(sample_size);

	for (size_t i = 0; i < sample_size; ++i)
	{
		ids.insert(generate_session_id());
	}

	EXPECT_EQ(ids.size(), sample_size)
		<< "Expected " << sample_size << " unique IDs but got " << ids.size();
}

// ============================================================================
// Entropy Tests
// ============================================================================

class SessionIdEntropyTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}

	// Calculate Shannon entropy of a string
	double calculate_entropy(const std::string& data)
	{
		std::array<int, 256> freq{};
		for (unsigned char c : data)
		{
			freq[c]++;
		}

		double entropy = 0.0;
		const double len = static_cast<double>(data.length());

		for (int count : freq)
		{
			if (count > 0)
			{
				double p = static_cast<double>(count) / len;
				entropy -= p * std::log2(p);
			}
		}

		return entropy;
	}
};

TEST_F(SessionIdEntropyTest, HasSufficientEntropy)
{
	// Concatenate multiple session IDs to get a larger sample
	std::string combined;
	constexpr size_t sample_count = 1000;

	for (size_t i = 0; i < sample_count; ++i)
	{
		combined += generate_session_id();
	}

	double entropy = calculate_entropy(combined);

	// For uniformly distributed hex characters (16 values),
	// maximum entropy is log2(16) = 4.0 bits per character
	// We expect at least 3.5 bits per character for good randomness
	EXPECT_GE(entropy, 3.5)
		<< "Entropy " << entropy << " bits per char is below threshold";
}

TEST_F(SessionIdEntropyTest, NoPredictablePatterns)
{
	std::vector<std::string> ids;
	constexpr size_t sample_size = 100;

	for (size_t i = 0; i < sample_size; ++i)
	{
		ids.push_back(generate_session_id());
	}

	// Check that consecutive IDs don't share common prefixes
	for (size_t i = 1; i < ids.size(); ++i)
	{
		size_t common_prefix_len = 0;
		for (size_t j = 0; j < ids[i].length() && ids[i][j] == ids[i - 1][j]; ++j)
		{
			common_prefix_len++;
		}
		// Statistically, common prefix should rarely exceed 2 chars (1/256 probability)
		EXPECT_LT(common_prefix_len, 8)
			<< "Suspiciously long common prefix between consecutive IDs";
	}
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

class SessionIdThreadSafetyTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(SessionIdThreadSafetyTest, ConcurrentGeneration)
{
	constexpr size_t thread_count = 8;
	constexpr size_t ids_per_thread = 1000;

	std::vector<std::vector<std::string>> thread_results(thread_count);
	std::vector<std::thread> threads;

	for (size_t t = 0; t < thread_count; ++t)
	{
		threads.emplace_back(
			[&thread_results, t, ids_per_thread]()
			{
				thread_results[t].reserve(ids_per_thread);
				for (size_t i = 0; i < ids_per_thread; ++i)
				{
					thread_results[t].push_back(generate_session_id());
				}
			});
	}

	for (auto& thread : threads)
	{
		thread.join();
	}

	// Collect all IDs and verify uniqueness
	std::unordered_set<std::string> all_ids;
	for (const auto& results : thread_results)
	{
		for (const auto& id : results)
		{
			auto [_, inserted] = all_ids.insert(id);
			EXPECT_TRUE(inserted) << "Duplicate ID in concurrent generation: " << id;
		}
	}

	EXPECT_EQ(all_ids.size(), thread_count * ids_per_thread);
}

// ============================================================================
// Performance Tests
// ============================================================================

class SessionIdPerformanceTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(SessionIdPerformanceTest, GenerationSpeed)
{
	constexpr size_t iterations = 10000;

	// Warm up
	for (size_t i = 0; i < 100; ++i)
	{
		(void)generate_session_id();
	}

	auto start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < iterations; ++i)
	{
		(void)generate_session_id();
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

	double avg_ns = static_cast<double>(duration.count()) / iterations;

	// Expect less than 1 microsecond (1000 ns) per generation
	EXPECT_LT(avg_ns, 1000.0)
		<< "Average generation time " << avg_ns << "ns exceeds 1μs threshold";

	// Print for informational purposes
	std::cout << "Average session ID generation time: " << avg_ns << " ns" << std::endl;
}
