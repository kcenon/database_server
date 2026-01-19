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

#include <kcenon/database_server/core/server_config.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace database_server
{

std::optional<server_config> server_config::load_from_file(const std::string& path)
{
	if (!std::filesystem::exists(path))
	{
		// Error will be logged by caller with appropriate context
		return std::nullopt;
	}

	std::ifstream file(path);
	if (!file.is_open())
	{
		// Error will be logged by caller with appropriate context
		return std::nullopt;
	}

	// For Phase 1, we use a simple key=value format
	// TODO: Add YAML parsing in future phases (using yaml-cpp or similar)
	server_config config = default_config();

	std::string line;
	while (std::getline(file, line))
	{
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#')
		{
			continue;
		}

		auto delimiter_pos = line.find('=');
		if (delimiter_pos == std::string::npos)
		{
			continue;
		}

		std::string key = line.substr(0, delimiter_pos);
		std::string value = line.substr(delimiter_pos + 1);

		// Trim whitespace
		auto trim = [](std::string& s)
		{
			s.erase(0, s.find_first_not_of(" \t\r\n"));
			s.erase(s.find_last_not_of(" \t\r\n") + 1);
		};
		trim(key);
		trim(value);

		// Parse configuration values
		if (key == "name")
		{
			config.name = value;
		}
		else if (key == "network.host")
		{
			config.network.host = value;
		}
		else if (key == "network.port")
		{
			config.network.port = static_cast<uint16_t>(std::stoi(value));
		}
		else if (key == "network.enable_tls")
		{
			config.network.enable_tls = (value == "true" || value == "1");
		}
		else if (key == "network.cert_file")
		{
			config.network.cert_file = value;
		}
		else if (key == "network.key_file")
		{
			config.network.key_file = value;
		}
		else if (key == "network.max_connections")
		{
			config.network.max_connections = static_cast<uint32_t>(std::stoul(value));
		}
		else if (key == "network.connection_timeout_ms")
		{
			config.network.connection_timeout_ms = static_cast<uint32_t>(std::stoul(value));
		}
		else if (key == "logging.level")
		{
			config.logging.level = value;
		}
		else if (key == "logging.log_file")
		{
			config.logging.log_file = value;
		}
		else if (key == "logging.enable_console")
		{
			config.logging.enable_console = (value == "true" || value == "1");
		}
		else if (key == "pool.min_connections")
		{
			config.pool.min_connections = static_cast<uint32_t>(std::stoul(value));
		}
		else if (key == "pool.max_connections")
		{
			config.pool.max_connections = static_cast<uint32_t>(std::stoul(value));
		}
		else if (key == "pool.idle_timeout_ms")
		{
			config.pool.idle_timeout_ms = static_cast<uint32_t>(std::stoul(value));
		}
		else if (key == "pool.health_check_interval_ms")
		{
			config.pool.health_check_interval_ms = static_cast<uint32_t>(std::stoul(value));
		}
	}

	return config;
}

server_config server_config::default_config()
{
	server_config config;
	// All defaults are set in the struct definition
	return config;
}

bool server_config::validate() const
{
	return validation_errors().empty();
}

std::vector<std::string> server_config::validation_errors() const
{
	std::vector<std::string> errors;

	// Validate name
	if (name.empty())
	{
		errors.push_back("Server name cannot be empty");
	}

	// Validate network configuration
	if (network.host.empty())
	{
		errors.push_back("Network host cannot be empty");
	}

	if (network.port == 0)
	{
		errors.push_back("Network port must be greater than 0");
	}

	if (network.enable_tls)
	{
		if (network.cert_file.empty())
		{
			errors.push_back("TLS is enabled but no certificate file specified");
		}
		else if (!std::filesystem::exists(network.cert_file))
		{
			errors.push_back("TLS certificate file not found: " + network.cert_file);
		}

		if (network.key_file.empty())
		{
			errors.push_back("TLS is enabled but no key file specified");
		}
		else if (!std::filesystem::exists(network.key_file))
		{
			errors.push_back("TLS key file not found: " + network.key_file);
		}
	}

	if (network.max_connections == 0)
	{
		errors.push_back("Maximum connections must be greater than 0");
	}

	// Validate pool configuration
	if (pool.min_connections > pool.max_connections)
	{
		errors.push_back("Pool minimum connections cannot exceed maximum connections");
	}

	// Validate logging configuration
	if (logging.level != "debug" && logging.level != "info" && logging.level != "warn"
		&& logging.level != "error")
	{
		errors.push_back("Invalid log level: " + logging.level
						 + " (valid: debug, info, warn, error)");
	}

	return errors;
}

} // namespace database_server
