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
 * @file param_serializer.cpp
 * @brief Implementation of query_param serialization
 */

#include "serialization_helpers.h"

namespace database_server::gateway
{

query_param::query_param(std::string n, param_value v)
	: name(std::move(n))
	, value(std::move(v))
{
}

namespace detail
{

#if KCENON_WITH_CONTAINER_SYSTEM

void serialize_params(std::shared_ptr<container_module::value_container>& container,
					  const std::vector<query_param>& params)
{
	container->set_value("params_count", static_cast<int>(params.size()));

	for (size_t i = 0; i < params.size(); ++i)
	{
		const auto& param = params[i];
		std::string prefix = "param_" + std::to_string(i) + "_";

		container->set_value(prefix + "name", param.name);
		serialize_variant_value(container, prefix, param.value);
	}
}

std::vector<query_param> deserialize_params(
	const std::shared_ptr<container_module::value_container>& container)
{
	std::vector<query_param> params;

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

		param.value = deserialize_variant_value<query_param::param_value>(container, prefix);
		params.push_back(std::move(param));
	}

	return params;
}

#endif // KCENON_WITH_CONTAINER_SYSTEM

} // namespace detail

} // namespace database_server::gateway
