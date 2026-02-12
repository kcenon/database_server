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
 * @file response_serializer.cpp
 * @brief Implementation of query_response, column_metadata, result_row serialization
 */

#include "serialization_helpers.h"

namespace database_server::gateway
{

query_response::query_response(uint64_t request_id)
	: status(status_code::ok)
{
	header.message_id = request_id;
	header.timestamp = detail::current_timestamp_ms();
}

query_response::query_response(uint64_t request_id, status_code error_status, std::string error_msg)
	: status(error_status)
	, error_message(std::move(error_msg))
{
	header.message_id = request_id;
	header.timestamp = detail::current_timestamp_ms();
}

std::shared_ptr<container_module::value_container> query_response::serialize() const
{
#if KCENON_WITH_CONTAINER_SYSTEM
	auto container = std::make_shared<container_module::value_container>();
	container->set_message_type("query_response");

	// Header
	container->set("version", static_cast<int>(header.version));
	container->set("message_id", static_cast<long long>(header.message_id));
	container->set("timestamp", static_cast<long long>(header.timestamp));
	container->set("correlation_id", header.correlation_id);

	// Status
	container->set("status", static_cast<int>(status));
	container->set("error_message", error_message);
	container->set("affected_rows", static_cast<long long>(affected_rows));
	container->set("execution_time_us", static_cast<long long>(execution_time_us));

	// Column metadata
	container->set("columns_count", static_cast<int>(columns.size()));
	for (size_t i = 0; i < columns.size(); ++i)
	{
		const auto& col = columns[i];
		std::string prefix = "col_" + std::to_string(i) + "_";

		container->set(prefix + "name", col.name);
		container->set(prefix + "type_name", col.type_name);
		container->set(prefix + "type_id", static_cast<int>(col.type_id));
		container->set(prefix + "nullable", col.nullable);
		container->set(prefix + "precision", static_cast<int>(col.precision));
		container->set(prefix + "scale", static_cast<int>(col.scale));
	}

	// Rows
	container->set("rows_count", static_cast<int>(rows.size()));
	for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx)
	{
		const auto& row = rows[row_idx];
		std::string row_prefix = "row_" + std::to_string(row_idx) + "_";

		for (size_t cell_idx = 0; cell_idx < row.cells.size(); ++cell_idx)
		{
			std::string cell_prefix = row_prefix + "cell_" + std::to_string(cell_idx) + "_";
			detail::serialize_variant_value(container, cell_prefix, row.cells[cell_idx]);
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
	if (auto val = container->get("version"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			response.header.version = static_cast<uint32_t>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get("message_id"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.header.message_id = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get("timestamp"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.header.timestamp = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get("correlation_id"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			response.header.correlation_id = std::get<std::string>(val->data);
		}
	}

	// Status
	if (auto val = container->get("status"))
	{
		if (std::holds_alternative<int>(val->data))
		{
			response.status = static_cast<status_code>(std::get<int>(val->data));
		}
	}
	if (auto val = container->get("error_message"))
	{
		if (std::holds_alternative<std::string>(val->data))
		{
			response.error_message = std::get<std::string>(val->data);
		}
	}
	if (auto val = container->get("affected_rows"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.affected_rows = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}
	if (auto val = container->get("execution_time_us"))
	{
		if (std::holds_alternative<long long>(val->data))
		{
			response.execution_time_us = static_cast<uint64_t>(std::get<long long>(val->data));
		}
	}

	// Column metadata
	int columns_count = 0;
	if (auto val = container->get("columns_count"))
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

		if (auto val = container->get(prefix + "name"))
		{
			if (std::holds_alternative<std::string>(val->data))
			{
				col.name = std::get<std::string>(val->data);
			}
		}
		if (auto val = container->get(prefix + "type_name"))
		{
			if (std::holds_alternative<std::string>(val->data))
			{
				col.type_name = std::get<std::string>(val->data);
			}
		}
		if (auto val = container->get(prefix + "type_id"))
		{
			if (std::holds_alternative<int>(val->data))
			{
				col.type_id = static_cast<uint32_t>(std::get<int>(val->data));
			}
		}
		if (auto val = container->get(prefix + "nullable"))
		{
			if (std::holds_alternative<bool>(val->data))
			{
				col.nullable = std::get<bool>(val->data);
			}
		}
		if (auto val = container->get(prefix + "precision"))
		{
			if (std::holds_alternative<int>(val->data))
			{
				col.precision = static_cast<uint32_t>(std::get<int>(val->data));
			}
		}
		if (auto val = container->get(prefix + "scale"))
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
	if (auto val = container->get("rows_count"))
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
			row.cells.push_back(
				detail::deserialize_variant_value<result_row::cell_value>(container, cell_prefix));
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
