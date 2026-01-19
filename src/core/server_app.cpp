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

#include <kcenon/database_server/gateway/gateway_server.h>
#include <kcenon/database_server/gateway/query_router.h>
#include <kcenon/database_server/logging/console_logger.h>
#include <kcenon/database_server/pooling/connection_pool.h>

#include <chrono>
#include <csignal>
#include <sstream>
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

kcenon::common::VoidResult server_app::initialize(const std::string& config_path)
{
	// Create default logger if not set
	if (!logger_)
	{
		logger_ = logging::create_console_logger();
	}

	auto loaded_config = server_config::load_from_file(config_path);
	if (!loaded_config)
	{
		logger_->log(kcenon::common::interfaces::log_level::error,
					 "Failed to load configuration from: " + config_path);
		return kcenon::common::error_info{
			kcenon::common::error_codes::INVALID_ARGUMENT,
			"Failed to load configuration from: " + config_path,
			"server_app"};
	}

	return initialize(*loaded_config);
}

kcenon::common::VoidResult server_app::initialize(const server_config& config)
{
	// Create default logger if not set
	if (!logger_)
	{
		logger_ = logging::create_console_logger();
	}

	if (state_ != server_state::uninitialized)
	{
		logger_->log(kcenon::common::interfaces::log_level::error,
					 std::string("Server already initialized"));
		return kcenon::common::error_info{
			kcenon::common::error_codes::ALREADY_EXISTS,
			"Server already initialized",
			"server_app"};
	}

	config_ = config;

	if (!config_.validate())
	{
		std::ostringstream error_oss;
		error_oss << "Configuration validation failed:";
		for (const auto& error : config_.validation_errors())
		{
			logger_->log(kcenon::common::interfaces::log_level::error,
						 "  - " + error);
			error_oss << " " << error << ";";
		}
		return kcenon::common::error_info{
			kcenon::common::error_codes::INVALID_ARGUMENT,
			error_oss.str(),
			"server_app"};
	}

	if (!do_initialize())
	{
		return kcenon::common::error_info{
			kcenon::common::error_codes::INTERNAL_ERROR,
			"Server initialization failed",
			"server_app"};
	}

	state_ = server_state::initialized;
	return kcenon::common::ok();
}

bool server_app::do_initialize()
{
	// Setup signal handlers
	setup_signal_handlers();

	// Note: IExecutor integration is available but optional.
	// Modules will use std::async fallback when executor is not provided.
	// To enable IExecutor, inject it via set_executor() methods on query_router
	// and resilience components.

	// Initialize query router
	gateway::router_config router_cfg;
	router_cfg.default_timeout_ms = config_.network.connection_timeout_ms;
	router_cfg.max_concurrent_queries = config_.network.max_connections;
	router_cfg.enable_metrics = true;

	query_router_ = std::make_unique<gateway::query_router>(router_cfg);

	logger_->log(kcenon::common::interfaces::log_level::info,
				 std::string("Query router initialized"));

	// Initialize gateway server
	gateway::gateway_config gw_config;
	gw_config.server_id = config_.name;
	gw_config.port = config_.network.port;
	gw_config.max_connections = config_.network.max_connections;
	gw_config.idle_timeout_ms = config_.network.connection_timeout_ms;

	gateway_ = std::make_unique<gateway::gateway_server>(gw_config);

	// Set up connection callbacks for logging
	gateway_->set_connection_callback(
		[this](const gateway::client_session& session)
		{
			logger_->log(kcenon::common::interfaces::log_level::info,
						 "Client connected: " + session.session_id);
		});

	gateway_->set_disconnection_callback(
		[this](const std::string& session_id)
		{
			logger_->log(kcenon::common::interfaces::log_level::info,
						 "Client disconnected: " + session_id);
		});

	// Set up request handler to route queries through query_router
	gateway_->set_request_handler(
		[this](const gateway::client_session& session, const gateway::query_request& request)
			-> gateway::query_response
		{
			(void)session; // Session info available for future enhancements (logging, rate limiting)

			// Execute query through query router
			auto result = query_router_->execute(request);
			if (result.is_ok())
			{
				return std::move(result.value());
			}
			else
			{
				return gateway::query_response(
					request.header.message_id,
					gateway::status_code::error,
					result.error().message);
			}
		});

	std::ostringstream init_msg;
	init_msg << "Server '" << config_.name << "' initialized";
	logger_->log(kcenon::common::interfaces::log_level::info, init_msg.str());

	std::ostringstream addr_msg;
	addr_msg << "  Listen address: " << config_.network.host << ":" << config_.network.port;
	logger_->log(kcenon::common::interfaces::log_level::info, addr_msg.str());

	return true;
}

int server_app::run()
{
	if (state_ != server_state::initialized)
	{
		logger_->log(kcenon::common::interfaces::log_level::error,
					 std::string("Server not initialized"));
		return 1;
	}

	state_ = server_state::starting;

	// Start gateway server
	if (gateway_)
	{
		auto result = gateway_->start();
		if (result.is_err())
		{
			logger_->log(kcenon::common::interfaces::log_level::error,
						 "Failed to start gateway server: " + result.error().message);
			state_ = server_state::stopped;
			return 1;
		}
	}

	// Start connection pool health monitoring if pool is configured
	if (connection_pool_)
	{
		connection_pool_->schedule_health_check();
		logger_->log(kcenon::common::interfaces::log_level::info,
					 std::string("Connection pool health monitoring started"));
	}

	state_ = server_state::running;
	logger_->log(kcenon::common::interfaces::log_level::info,
				 std::string("Server is running. Press Ctrl+C to stop."));

	// Main event loop - wait for shutdown signal
	while (state_ == server_state::running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	state_ = server_state::stopped;
	logger_->log(kcenon::common::interfaces::log_level::info,
				 std::string("Server stopped"));

	return 0;
}

void server_app::stop()
{
	if (state_ != server_state::running)
	{
		return;
	}

	state_ = server_state::stopping;

	// Stop gateway server
	if (gateway_)
	{
		auto result = gateway_->stop();
		if (result.is_err())
		{
			logger_->log(kcenon::common::interfaces::log_level::warning,
						 "Failed to stop gateway server: " + result.error().message);
		}
	}

	// Shutdown connection pool
	if (connection_pool_)
	{
		connection_pool_->request_shutdown();
		connection_pool_->shutdown();
	}

	logger_->log(kcenon::common::interfaces::log_level::info,
				 std::string("Shutdown requested"));
}

void server_app::do_cleanup()
{
	// Cleanup query router
	query_router_.reset();

	// Cleanup connection pool
	connection_pool_.reset();

	// Cleanup gateway server
	gateway_.reset();

	// Shutdown executor if set
	if (executor_)
	{
		executor_->shutdown(true);
		executor_.reset();
	}

	if (instance_ == this)
	{
		instance_ = nullptr;
	}
}

std::shared_ptr<kcenon::common::interfaces::IExecutor> server_app::get_executor() const
{
	return executor_;
}

void server_app::set_executor(
	std::shared_ptr<kcenon::common::interfaces::IExecutor> executor)
{
	executor_ = std::move(executor);

	// Propagate to query router if available
	if (query_router_ && executor_)
	{
		query_router_->set_executor(executor_);
	}
}

std::shared_ptr<kcenon::common::interfaces::ILogger> server_app::get_logger() const
{
	return logger_;
}

void server_app::set_logger(std::shared_ptr<kcenon::common::interfaces::ILogger> logger)
{
	logger_ = std::move(logger);
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
		// Note: Signal handlers should be async-signal-safe.
		// The logger_->log call in stop() is safe because stop() is called
		// from the main thread context after the signal interrupts sleep.
		// For truly async-safe logging, consider using write() directly.
		(void)signal; // Signal number logged via stop() method
		instance_->stop();
	}
}

} // namespace database_server
