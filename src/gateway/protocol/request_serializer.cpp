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
 * @file request_serializer.cpp
 * @brief Implementation of query_request serialization
 */

#include "serialization_helpers.h"

namespace database_server::gateway
{

namespace detail
{

#if KCENON_WITH_CONTAINER_SYSTEM

void serialize_params(std::shared_ptr<container_module::value_container>& container,
					  const std::vector<query_param>& params);

std::vector<query_param> deserialize_params(
	const std::shared_ptr<container_module::value_container>& container);

#endif // KCENON_WITH_CONTAINER_SYSTEM

} // namespace detail

query_request::query_request(std::string query_sql, query_type qtype)
	: sql(std::move(query_sql))
	, type(qtype)
{
}

std::shared_ptr<container_module::value_container> query_request::serialize() const
{
#if KCENON_WITH_CONTAINER_SYSTEM
	auto container = std::make_shared<container_module::value_container>();
	container->set_message_type("query_request");

	// Header
	container->set_value("version", static_cast<int>(header.version));
	container->set_value("message_id", static_cast<long long>(header.message_id));
	container->set_value("timestamp", static_cast<long long>(header.timestamp));
	container->set_value("correlation_id", header.correlation_id);

	// Auth token
	container->set_value("auth_token", token.token);
	container->set_value("client_id", token.client_id);
	container->set_value("token_expires", static_cast<long long>(token.expires_at));

	// Query
	container->set_value("query_type", static_cast<int>(type));
	container->set_value("sql", sql);

	// Options
	container->set_value("timeout_ms", static_cast<int>(options.timeout_ms));
	container->set_value("read_only", options.read_only);
	container->set_value("isolation_level", options.isolation_level);
	container->set_value("max_rows", static_cast<int>(options.max_rows));
	container->set_value("include_metadata", options.include_metadata);

	// Parameters
	detail::serialize_params(container, params);

	return container;
#else
	return nullptr;
#endif
}

kcenon::common::Result<query_request>
query_request::deserialize(std::shared_ptr<container_module::value_container> container)
{
#if KCENON_WITH_CONTAINER_SYSTEM
	if (!container)
	{
		return kcenon::common::error_info{ -1, "Null container", "query_protocol" };
	}

	query_request request;

	// Header
	if (auto val = container->get_value("version"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			request.header.version = static_cast<uint32_t>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get_value("message_id"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			request.header.message_id = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get_value("timestamp"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			request.header.timestamp = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get_value("correlation_id"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			request.header.correlation_id = std::get<std::string>(val->data);
		}
	}

	// Auth token
	if (auto val = container->get_value("auth_token"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			request.token.token = std::get<std::string>(val->data);
		}
	}
	if (auto val = container->get_value("client_id"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			request.token.client_id = std::get<std::string>(val->data);
		}
	}
	if (auto val = container->get_value("token_expires"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			request.token.expires_at = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}

	// Query
	if (auto val = container->get_value("query_type"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			request.type = static_cast<query_type>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get_value("sql"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			request.sql = std::get<std::string>(val->data);
		}
	}

	// Options
	if (auto val = container->get_value("timeout_ms"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			request.options.timeout_ms = static_cast<uint32_t>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get_value("read_only"))
	{
		if (std::holds_alternative<bool>(val->data))
		{
			request.options.read_only = std::get<bool>(val->data);
		}
	}
	if (auto val = container->get_value("isolation_level"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			request.options.isolation_level = std::get<std::string>(val->data);
		}
	}
	if (auto val = container->get_value("max_rows"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			request.options.max_rows = static_cast<uint32_t>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get_value("include_metadata"))
	{
		if (std::holds_alternative<bool>(val->data))
		{
			request.options.include_metadata = std::get<bool>(val->data);
		}
	}

	// Parameters
	request.params = detail::deserialize_params(container);

	return request;
#else
	return kcenon::common::error_info{
		-2, "container_system not available", "query_protocol"
	};
#endif
}

kcenon::common::Result<query_request>
query_request::deserialize(const std::vector<uint8_t>& data)
{
#if KCENON_WITH_CONTAINER_SYSTEM
	auto container = std::make_shared<container_module::value_container>(data, false);
	return deserialize(container);
#else
	return kcenon::common::error_info{
		-2, "container_system not available", "query_protocol"
	};
#endif
}

bool query_request::is_valid() const noexcept
{
	if (type == query_type::unknown)
	{
		return false;
	}

	if (type != query_type::ping && sql.empty())
	{
		return false;
	}

	return true;
}

} // namespace database_server::gateway
