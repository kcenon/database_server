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

#include <kcenon/database_server/gateway/query_protocol.h>

#include <kcenon/common/config/feature_flags.h>

#if KCENON_WITH_CONTAINER_SYSTEM
#include <container/core/container.h>
#endif

#include <chrono>

namespace database_server::gateway
{

// ============================================================================
// message_header
// ============================================================================

message_header::message_header(uint64_t id)
	: message_id(id)
	, timestamp(static_cast<uint64_t>(
		  std::chrono::duration_cast<std::chrono::milliseconds>(
			  std::chrono::system_clock::now().time_since_epoch())
			  .count()))
{
}

// ============================================================================
// auth_token
// ============================================================================

bool auth_token::is_expired() const noexcept
{
	if (expires_at == 0)
	{
		return false;
	}

	auto now = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());

	return now > expires_at;
}

bool auth_token::is_valid() const noexcept
{
	return !token.empty() && !is_expired();
}

// ============================================================================
// query_param
// ============================================================================

query_param::query_param(std::string n, param_value v)
	: name(std::move(n))
	, value(std::move(v))
{
}

// ============================================================================
// query_request
// ============================================================================

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

	// Parameters count
	container->set_value("params_count", static_cast<int>(params.size()));

	// Serialize each parameter
	for (size_t i = 0; i < params.size(); ++i)
	{
		const auto& param = params[i];
		std::string prefix = "param_" + std::to_string(i) + "_";

		container->set_value(prefix + "name", param.name);
		container->set_value(prefix + "type", static_cast<int>(param.value.index()));

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
					container->set_value(prefix + "value_bool", arg);
				}
				else if constexpr (std::is_same_v<T, int64_t>)
				{
					container->set_value(prefix + "value_int", static_cast<long long>(arg));
				}
				else if constexpr (std::is_same_v<T, double>)
				{
					container->set_value(prefix + "value_double", arg);
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					container->set_value(prefix + "value_string", arg);
				}
				else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
				{
					container->set_value(prefix + "value_bytes", arg);
				}
			},
			param.value);
	}

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
	int params_count = 0;
	if (auto val = container->get_value("params_count"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			params_count = std::get<int>(val->data);
		}
	}

	for (int i = 0; i < params_count; ++i)
	{
		std::string prefix = "param_" + std::to_string(i) + "_";
		query_param param;

		if (auto val = container->get_value(prefix + "name"))
		{
			if (std::holds_alternative<std::string>(val->data))
			{
				param.name = std::get<std::string>(val->data);
			}
		}

		int param_type = 0;
		if (auto val = container->get_value(prefix + "type"))
		{
			if (std::holds_alternative<int>(val->data))
			{
				param_type = std::get<int>(val->data);
			}
		}

		switch (param_type)
		{
		case 0: // monostate (NULL)
			param.value = std::monostate{};
			break;
		case 1: // bool
			if (auto val = container->get_value(prefix + "value_bool"))
			{
				if (std::holds_alternative<bool>(val->data))
				{
					param.value = std::get<bool>(val->data);
				}
			}
			break;
		case 2: // int64_t
			if (auto val = container->get_value(prefix + "value_int"))
			{
				if (std::holds_alternative<long long>(val->data))
				{
					param.value = static_cast<int64_t>(std::get<long long>(val->data));
				}
			}
			break;
		case 3: // double
			if (auto val = container->get_value(prefix + "value_double"))
			{
				if (std::holds_alternative<double>(val->data))
				{
					param.value = std::get<double>(val->data);
				}
			}
			break;
		case 4: // string
			if (auto val = container->get_value(prefix + "value_string"))
			{
				if (std::holds_alternative<std::string>(val->data))
				{
					param.value = std::get<std::string>(val->data);
				}
			}
			break;
		case 5: // bytes
			if (auto val = container->get_value(prefix + "value_bytes"))
			{
				if (std::holds_alternative<std::vector<uint8_t>>(val->data))
				{
					param.value = std::get<std::vector<uint8_t>>(val->data);
				}
			}
			break;
		}

		request.params.push_back(std::move(param));
	}

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

// ============================================================================
// query_response
// ============================================================================

query_response::query_response(uint64_t request_id)
	: status(status_code::ok)
{
	header.message_id = request_id;
	header.timestamp = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

query_response::query_response(uint64_t request_id, status_code error_status, std::string error_msg)
	: status(error_status)
	, error_message(std::move(error_msg))
{
	header.message_id = request_id;
	header.timestamp = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

std::shared_ptr<container_module::value_container> query_response::serialize() const
{
#if KCENON_WITH_CONTAINER_SYSTEM
	auto container = std::make_shared<container_module::value_container>();
	container->set_message_type("query_response");

	// Header
	container->set_value("version", static_cast<int>(header.version));
	container->set_value("message_id", static_cast<long long>(header.message_id));
	container->set_value("timestamp", static_cast<long long>(header.timestamp));
	container->set_value("correlation_id", header.correlation_id);

	// Status
	container->set_value("status", static_cast<int>(status));
	container->set_value("error_message", error_message);
	container->set_value("affected_rows", static_cast<long long>(affected_rows));
	container->set_value("execution_time_us", static_cast<long long>(execution_time_us));

	// Column metadata
	container->set_value("columns_count", static_cast<int>(columns.size()));
	for (size_t i = 0; i < columns.size(); ++i)
	{
		const auto& col = columns[i];
		std::string prefix = "col_" + std::to_string(i) + "_";

		container->set_value(prefix + "name", col.name);
		container->set_value(prefix + "type_name", col.type_name);
		container->set_value(prefix + "type_id", static_cast<int>(col.type_id));
		container->set_value(prefix + "nullable", col.nullable);
		container->set_value(prefix + "precision", static_cast<int>(col.precision));
		container->set_value(prefix + "scale", static_cast<int>(col.scale));
	}

	// Rows
	container->set_value("rows_count", static_cast<int>(rows.size()));
	for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx)
	{
		const auto& row = rows[row_idx];
		std::string row_prefix = "row_" + std::to_string(row_idx) + "_";

		for (size_t cell_idx = 0; cell_idx < row.cells.size(); ++cell_idx)
		{
			std::string cell_prefix = row_prefix + "cell_" + std::to_string(cell_idx) + "_";

			container->set_value(cell_prefix + "type",
								 static_cast<int>(row.cells[cell_idx].index()));

			std::visit(
				[&container, &cell_prefix](auto&& arg)
				{
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, std::monostate>)
					{
						// NULL value
					}
					else if constexpr (std::is_same_v<T, bool>)
					{
						container->set_value(cell_prefix + "value_bool", arg);
					}
					else if constexpr (std::is_same_v<T, int64_t>)
					{
						container->set_value(cell_prefix + "value_int",
											 static_cast<long long>(arg));
					}
					else if constexpr (std::is_same_v<T, double>)
					{
						container->set_value(cell_prefix + "value_double", arg);
					}
					else if constexpr (std::is_same_v<T, std::string>)
					{
						container->set_value(cell_prefix + "value_string", arg);
					}
					else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
					{
						container->set_value(cell_prefix + "value_bytes", arg);
					}
				},
				row.cells[cell_idx]);
		}
	}

	return container;
#else
	return nullptr;
#endif
}

kcenon::common::Result<query_response>
query_response::deserialize(std::shared_ptr<container_module::value_container> container)
{
#if KCENON_WITH_CONTAINER_SYSTEM
	if (!container)
	{
		return kcenon::common::error_info{ -1, "Null container", "query_protocol" };
	}

	query_response response;

	// Header
	if (auto val = container->get_value("version"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			response.header.version = static_cast<uint32_t>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get_value("message_id"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.header.message_id = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get_value("timestamp"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.header.timestamp = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get_value("correlation_id"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			response.header.correlation_id = std::get<std::string>(val->data);
		}
	}

	// Status
	if (auto val = container->get_value("status"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			response.status = static_cast<status_code>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get_value("error_message"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			response.error_message = std::get<std::string>(val->data);
		}
	}
	if (auto val = container->get_value("affected_rows"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.affected_rows = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get_value("execution_time_us"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.execution_time_us = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}

	// Column metadata
	int columns_count = 0;
	if (auto val = container->get_value("columns_count"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			columns_count = std::get<int>(val->data);
		}
	}

	for (int i = 0; i < columns_count; ++i)
	{
		std::string prefix = "col_" + std::to_string(i) + "_";
		column_metadata col;

		if (auto val = container->get_value(prefix + "name"))
		{
			if (std::holds_alternative<std::string>(val->data))
			{
				col.name = std::get<std::string>(val->data);
			}
		}
		if (auto val = container->get_value(prefix + "type_name"))
		{
			if (std::holds_alternative<std::string>(val->data))
			{
				col.type_name = std::get<std::string>(val->data);
			}
		}
		if (auto val = container->get_value(prefix + "type_id"))
		{
			if (std::holds_alternative<int>(val->data))
			{
				col.type_id = static_cast<uint32_t>(std::get<int>(val->data));
			}
		}
		if (auto val = container->get_value(prefix + "nullable"))
		{
			if (std::holds_alternative<bool>(val->data))
			{
				col.nullable = std::get<bool>(val->data);
			}
		}
		if (auto val = container->get_value(prefix + "precision"))
		{
			if (std::holds_alternative<int>(val->data))
			{
				col.precision = static_cast<uint32_t>(std::get<int>(val->data));
			}
		}
		if (auto val = container->get_value(prefix + "scale"))
		{
			if (std::holds_alternative<int>(val->data))
			{
				col.scale = static_cast<uint32_t>(std::get<int>(val->data));
			}
		}

		response.columns.push_back(std::move(col));
	}

	// Rows
	int rows_count = 0;
	if (auto val = container->get_value("rows_count"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			rows_count = std::get<int>(val->data);
		}
	}

	for (int row_idx = 0; row_idx < rows_count; ++row_idx)
	{
		result_row row;
		std::string row_prefix = "row_" + std::to_string(row_idx) + "_";

		for (int cell_idx = 0; cell_idx < columns_count; ++cell_idx)
		{
			std::string cell_prefix = row_prefix + "cell_" + std::to_string(cell_idx) + "_";

			int cell_type = 0;
			if (auto val = container->get_value(cell_prefix + "type"))
			{
				if (std::holds_alternative<int>(val->data))
				{
					cell_type = std::get<int>(val->data);
				}
			}

			result_row::cell_value cell;
			switch (cell_type)
			{
			case 0: // monostate
				cell = std::monostate{};
				break;
			case 1: // bool
				if (auto val = container->get_value(cell_prefix + "value_bool"))
				{
					if (std::holds_alternative<bool>(val->data))
					{
						cell = std::get<bool>(val->data);
					}
				}
				break;
			case 2: // int64_t
				if (auto val = container->get_value(cell_prefix + "value_int"))
				{
					if (std::holds_alternative<long long>(val->data))
					{
						cell = static_cast<int64_t>(std::get<long long>(val->data));
					}
				}
				break;
			case 3: // double
				if (auto val = container->get_value(cell_prefix + "value_double"))
				{
					if (std::holds_alternative<double>(val->data))
					{
						cell = std::get<double>(val->data);
					}
				}
				break;
			case 4: // string
				if (auto val = container->get_value(cell_prefix + "value_string"))
				{
					if (std::holds_alternative<std::string>(val->data))
					{
						cell = std::get<std::string>(val->data);
					}
				}
				break;
			case 5: // bytes
				if (auto val = container->get_value(cell_prefix + "value_bytes"))
				{
					if (std::holds_alternative<std::vector<uint8_t>>(val->data))
					{
						cell = std::get<std::vector<uint8_t>>(val->data);
					}
				}
				break;
			}

			row.cells.push_back(std::move(cell));
		}

		response.rows.push_back(std::move(row));
	}

	return response;
#else
	return kcenon::common::error_info{
		-2, "container_system not available", "query_protocol"
	};
#endif
}

kcenon::common::Result<query_response>
query_response::deserialize(const std::vector<uint8_t>& data)
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

bool query_response::is_success() const noexcept
{
	return status == status_code::ok;
}

} // namespace database_server::gateway
