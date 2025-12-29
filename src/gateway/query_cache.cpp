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

#include <kcenon/database_server/gateway/query_cache.h>

#include <algorithm>
#include <functional>
#include <sstream>

namespace database_server::gateway
{

query_cache::query_cache(const cache_config& config)
	: config_(config)
{
}

std::optional<query_response> query_cache::get(const std::string& cache_key)
{
	if (!config_.enabled)
	{
		return std::nullopt;
	}

	std::unique_lock lock(mutex_);

	auto it = cache_map_.find(cache_key);
	if (it == cache_map_.end())
	{
		++metrics_.misses;
		return std::nullopt;
	}

	auto& entry = *(it->second);

	if (is_expired(entry))
	{
		++metrics_.expirations;
		++metrics_.misses;
		remove_entry(it->second);
		return std::nullopt;
	}

	++metrics_.hits;

	if (config_.enable_lru)
	{
		lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
	}

	return entry.response;
}

void query_cache::put(const std::string& cache_key,
					  const query_response& response,
					  const std::unordered_set<std::string>& table_names)
{
	if (!config_.enabled)
	{
		return;
	}

	if (response.status != status_code::ok)
	{
		return;
	}

	auto estimated_size = estimate_size(response);
	if (estimated_size > config_.max_result_size_bytes)
	{
		++metrics_.skipped_too_large;
		return;
	}

	std::unique_lock lock(mutex_);

	auto existing = cache_map_.find(cache_key);
	if (existing != cache_map_.end())
	{
		remove_entry(existing->second);
	}

	while (cache_map_.size() >= config_.max_entries && !lru_list_.empty())
	{
		evict_lru();
	}

	cache_entry entry;
	entry.key = cache_key;
	entry.response = response;
	entry.tables = table_names;
	entry.estimated_size = estimated_size;

	if (config_.ttl_seconds > 0)
	{
		entry.expires_at = std::chrono::steady_clock::now()
						   + std::chrono::seconds(config_.ttl_seconds);
	}
	else
	{
		entry.expires_at = std::chrono::steady_clock::time_point::max();
	}

	lru_list_.push_front(std::move(entry));
	cache_map_[cache_key] = lru_list_.begin();

	for (const auto& table : table_names)
	{
		table_map_[table].insert(cache_key);
	}

	++metrics_.puts;
}

std::string query_cache::make_key(const query_request& request)
{
	std::hash<std::string> hasher;
	size_t hash_value = hasher(request.sql);

	for (const auto& param : request.params)
	{
		hash_value ^= hasher(param.name) + 0x9e3779b9 + (hash_value << 6) + (hash_value >> 2);

		std::visit(
			[&hash_value, &hasher](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, std::monostate>)
				{
					hash_value ^= 0 + 0x9e3779b9 + (hash_value << 6) + (hash_value >> 2);
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					hash_value ^= std::hash<bool>{}(arg) + 0x9e3779b9 + (hash_value << 6)
								  + (hash_value >> 2);
				}
				else if constexpr (std::is_same_v<T, int64_t>)
				{
					hash_value ^= std::hash<int64_t>{}(arg) + 0x9e3779b9 + (hash_value << 6)
								  + (hash_value >> 2);
				}
				else if constexpr (std::is_same_v<T, double>)
				{
					hash_value ^= std::hash<double>{}(arg) + 0x9e3779b9 + (hash_value << 6)
								  + (hash_value >> 2);
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					hash_value ^= hasher(arg) + 0x9e3779b9 + (hash_value << 6) + (hash_value >> 2);
				}
				else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
				{
					for (auto byte : arg)
					{
						hash_value ^= std::hash<uint8_t>{}(byte) + 0x9e3779b9 + (hash_value << 6)
									  + (hash_value >> 2);
					}
				}
			},
			param.value);
	}

	hash_value ^= std::hash<uint32_t>{}(request.options.max_rows) + 0x9e3779b9 + (hash_value << 6)
				  + (hash_value >> 2);

	std::ostringstream oss;
	oss << std::hex << hash_value;
	return oss.str();
}

void query_cache::invalidate(const std::string& table_name)
{
	std::unique_lock lock(mutex_);

	auto it = table_map_.find(table_name);
	if (it == table_map_.end())
	{
		return;
	}

	auto keys_to_remove = it->second;
	for (const auto& key : keys_to_remove)
	{
		auto cache_it = cache_map_.find(key);
		if (cache_it != cache_map_.end())
		{
			remove_entry(cache_it->second);
			++metrics_.invalidations;
		}
	}

	table_map_.erase(it);
}

void query_cache::invalidate_key(const std::string& cache_key)
{
	std::unique_lock lock(mutex_);

	auto it = cache_map_.find(cache_key);
	if (it != cache_map_.end())
	{
		remove_entry(it->second);
		++metrics_.invalidations;
	}
}

void query_cache::clear()
{
	std::unique_lock lock(mutex_);

	lru_list_.clear();
	cache_map_.clear();
	table_map_.clear();
}

const cache_metrics& query_cache::metrics() const noexcept
{
	return metrics_;
}

void query_cache::reset_metrics()
{
	metrics_.reset();
}

size_t query_cache::size() const noexcept
{
	std::shared_lock lock(mutex_);
	return cache_map_.size();
}

bool query_cache::is_enabled() const noexcept
{
	return config_.enabled;
}

const cache_config& query_cache::config() const noexcept
{
	return config_;
}

bool query_cache::is_expired(const cache_entry& entry) const noexcept
{
	if (config_.ttl_seconds == 0)
	{
		return false;
	}
	return std::chrono::steady_clock::now() > entry.expires_at;
}

void query_cache::evict_lru()
{
	if (lru_list_.empty())
	{
		return;
	}

	auto& entry = lru_list_.back();
	remove_entry(std::prev(lru_list_.end()));
	++metrics_.evictions;
}

size_t query_cache::estimate_size(const query_response& response)
{
	size_t size = sizeof(query_response);

	size += response.error_message.size();

	for (const auto& col : response.columns)
	{
		size += sizeof(column_metadata);
		size += col.name.size();
		size += col.type_name.size();
	}

	for (const auto& row : response.rows)
	{
		size += sizeof(result_row);
		for (const auto& cell : row.cells)
		{
			size += sizeof(result_row::cell_value);
			std::visit(
				[&size](auto&& arg) {
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, std::string>)
					{
						size += arg.size();
					}
					else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
					{
						size += arg.size();
					}
				},
				cell);
		}
	}

	return size;
}

void query_cache::remove_entry(cache_list::iterator it)
{
	const auto& entry = *it;

	for (const auto& table : entry.tables)
	{
		auto table_it = table_map_.find(table);
		if (table_it != table_map_.end())
		{
			table_it->second.erase(entry.key);
			if (table_it->second.empty())
			{
				table_map_.erase(table_it);
			}
		}
	}

	cache_map_.erase(entry.key);
	lru_list_.erase(it);
}

} // namespace database_server::gateway
