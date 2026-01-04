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
 * @file main.cpp
 * @brief Database server entry point
 *
 * This is the main entry point for the database gateway server.
 * It handles command-line argument parsing and launches the server.
 */

#include <kcenon/database_server/server_app.h>

#include <cstring>
#include <iostream>
#include <string>

namespace
{

constexpr const char* VERSION = "0.1.0";
constexpr const char* DEFAULT_CONFIG = "config.conf";

void print_usage(const char* program_name)
{
	std::cout << "Database Server v" << VERSION << "\n\n";
	std::cout << "Usage: " << program_name << " [options]\n\n";
	std::cout << "Options:\n";
	std::cout << "  -c, --config <file>  Path to configuration file (default: " << DEFAULT_CONFIG
			  << ")\n";
	std::cout << "  -h, --help           Show this help message\n";
	std::cout << "  -v, --version        Show version information\n";
	std::cout << "\n";
	std::cout << "Configuration file format (key=value):\n";
	std::cout << "  name=my_server\n";
	std::cout << "  network.host=0.0.0.0\n";
	std::cout << "  network.port=5432\n";
	std::cout << "  logging.level=info\n";
}

void print_version()
{
	std::cout << "Database Server v" << VERSION << "\n";
	std::cout << "Part of the kcenon unified system\n";
}

} // namespace

int main(int argc, char* argv[])
{
	std::string config_path = DEFAULT_CONFIG;

	// Parse command-line arguments
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
		{
			print_usage(argv[0]);
			return 0;
		}

		if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--version") == 0)
		{
			print_version();
			return 0;
		}

		if ((std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--config") == 0)
			&& i + 1 < argc)
		{
			config_path = argv[++i];
			continue;
		}

		std::cerr << "Unknown option: " << argv[i] << "\n";
		std::cerr << "Use --help for usage information.\n";
		return 1;
	}

	// Create and run the server
	database_server::server_app app;

	// Try to load config file, fall back to defaults if not found
	database_server::server_config config;
	auto loaded = database_server::server_config::load_from_file(config_path);
	if (loaded)
	{
		config = *loaded;
	}
	else
	{
		std::cout << "Using default configuration\n";
		config = database_server::server_config::default_config();
	}

	auto init_result = app.initialize(config);
	if (init_result.is_err())
	{
		std::cerr << "Failed to initialize server: " << init_result.error().message << "\n";
		return 1;
	}

	return app.run();
}
