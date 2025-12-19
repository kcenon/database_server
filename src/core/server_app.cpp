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

#include <kcenon/database_server/server_app.h>

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace database_server
{

// Static member initialization
server_app* server_app::instance_ = nullptr;

server_app::server_app() : state_(server_state::uninitialized)
{
}

server_app::~server_app()
{
	if (state_ == server_state::running)
	{
		stop();
	}
	do_cleanup();
}

bool server_app::initialize(const std::string& config_path)
{
	auto loaded_config = server_config::load_from_file(config_path);
	if (!loaded_config)
	{
		std::cerr << "Failed to load configuration from: " << config_path << std::endl;
		return false;
	}

	return initialize(*loaded_config);
}

bool server_app::initialize(const server_config& config)
{
	if (state_ != server_state::uninitialized)
	{
		std::cerr << "Server already initialized" << std::endl;
		return false;
	}

	config_ = config;

	if (!config_.validate())
	{
		std::cerr << "Configuration validation failed:" << std::endl;
		for (const auto& error : config_.validation_errors())
		{
			std::cerr << "  - " << error << std::endl;
		}
		return false;
	}

	if (!do_initialize())
	{
		return false;
	}

	state_ = server_state::initialized;
	return true;
}

bool server_app::do_initialize()
{
	// Setup signal handlers
	setup_signal_handlers();

	// TODO (Phase 3): Initialize network listener
	// TODO (Phase 2): Initialize connection pool

	std::cout << "Server '" << config_.name << "' initialized" << std::endl;
	std::cout << "  Listen address: " << config_.network.host << ":" << config_.network.port
			  << std::endl;

	return true;
}

int server_app::run()
{
	if (state_ != server_state::initialized)
	{
		std::cerr << "Server not initialized" << std::endl;
		return 1;
	}

	state_ = server_state::starting;

	// TODO (Phase 3): Start network listener
	// TODO (Phase 2): Start connection pool health monitoring

	state_ = server_state::running;
	std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

	// Main event loop - wait for shutdown signal
	while (state_ == server_state::running)
	{
		// Sleep to prevent busy waiting
		// In Phase 3, this will be replaced with actual event processing
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	state_ = server_state::stopped;
	std::cout << "Server stopped" << std::endl;

	return 0;
}

void server_app::stop()
{
	if (state_ != server_state::running)
	{
		return;
	}

	state_ = server_state::stopping;

	// TODO (Phase 3): Stop accepting new connections
	// TODO (Phase 3): Wait for active requests to complete
	// TODO (Phase 2): Close connection pool

	std::cout << "Shutdown requested" << std::endl;
}

void server_app::do_cleanup()
{
	// TODO (Phase 2): Cleanup connection pool
	// TODO (Phase 3): Cleanup network resources

	if (instance_ == this)
	{
		instance_ = nullptr;
	}
}

server_state server_app::state() const
{
	return state_.load();
}

bool server_app::is_running() const
{
	return state_ == server_state::running;
}

const server_config& server_app::config() const
{
	return config_;
}

void server_app::setup_signal_handlers()
{
	instance_ = this;

#ifdef _WIN32
	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);
#else
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);
#endif
}

void server_app::signal_handler(int signal)
{
	if (instance_ != nullptr)
	{
		std::cout << "\nReceived signal " << signal << std::endl;
		instance_->stop();
	}
}

} // namespace database_server
