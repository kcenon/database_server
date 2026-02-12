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
 * @file serialization_helpers.h
 * @brief Common serialization utilities for query protocol
 *
 * This header provides shared helper functions and type tags used across
 * all protocol serialization modules.
 */

#pragma once

#include <kcenon/common/config/feature_flags.h>
#include <kcenon/common/patterns/result.h>
#include <kcenon/database_server/gateway/query_protocol.h>

#if KCENON_WITH_CONTAINER_SYSTEM
#include <container.h>
#endif

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace database_server::gateway::detail
{

/**
 * @brief Get current timestamp in milliseconds since Unix epoch
 */
inline uint64_t current_timestamp_ms()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

/**
 * @brief Type tags for variant serialization
 */
enum class value_type_tag : int
{
	null_value = 0,
	bool_value = 1,
	int64_value = 2,
	double_value = 3,
	string_value = 4,
	bytes_value = 5
};

#if KCENON_WITH_CONTAINER_SYSTEM

/**
 * @brief Helper to serialize a variant value to container
 */
template<typename VariantT>
void serialize_variant_value(std::shared_ptr<container_module::value_container>& container,
							 const std::string& prefix,
							 const VariantT& value)
{
	container->set(prefix + "type", static_cast<int>(value.index()));

	std::visit(
		[&container, &prefix](auto&& arg)
		{
			using T = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<T, std::monostate>)
			{
				// NULL value - nothing to store
			}
			else if constexpr (std::is_same_v<T, bool>)
			{
				container->set(prefix + "value_bool", arg);
			}
			else if constexpr (std::is_same_v<T, int64_t>)
			{
				container->set(prefix + "value_int", static_cast<long long>(arg));
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				container->set(prefix + "value_double", arg);
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				container->set(prefix + "value_string", arg);
			}
			else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
			{
				container->set(prefix + "value_bytes", arg);
			}
		},
		value);
}

/**
 * @brief Helper to deserialize a variant value from container
 */
template<typename VariantT>
VariantT deserialize_variant_value(
	const std::shared_ptr<container_module::value_container>& container,
	const std::string& prefix)
{
	int type_tag = 0;
	if (auto val = container->get(prefix + "type"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			type_tag = std::get<int>(val->data);
		}
	}

	VariantT result;
	switch (type_tag)
	{
	case static_cast<int>(value_type_tag::null_value):
		result = std::monostate{};
		break;
	case static_cast<int>(value_type_tag::bool_value):
		if (auto val = container->get(prefix + "value_bool"))
		{
			if (std::holds_alternative<bool>(val->data))
			{
				result = std::get<bool>(val->data);
			}
		}
		break;
	case static_cast<int>(value_type_tag::int64_value):
		if (auto val = container->get(prefix + "value_int"))
		{
			if (std::holds_alternative<long long>(val->data))
			{
				result = static_cast<int64_t>(std::get<long long>(val->data));
			}
		}
		break;
	case static_cast<int>(value_type_tag::double_value):
		if (auto val = container->get(prefix + "value_double"))
		{
			if (std::holds_alternative<double>(val->data))
			{
				result = std::get<double>(val->data);
			}
		}
		break;
	case static_cast<int>(value_type_tag::string_value):
		if (auto val = container->get(prefix + "value_string"))
		{
			if (std::holds_alternative<std::string>(val->data))
			{
				result = std::get<std::string>(val->data);
			}
		}
		break;
	case static_cast<int>(value_type_tag::bytes_value):
		if (auto val = container->get(prefix + "value_bytes"))
		{
			if (std::holds_alternative<std::vector<uint8_t>>(val->data))
			{
				result = std::get<std::vector<uint8_t>>(val->data);
			}
		}
		break;
	}

	return result;
}

#endif // KCENON_WITH_CONTAINER_SYSTEM

} // namespace database_server::gateway::detail
