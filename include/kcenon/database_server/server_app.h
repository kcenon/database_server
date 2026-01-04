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
 * @file server_app.h
 * @brief Database server application interface
 *
 * Defines the main server application class that manages the lifecycle
 * of the database gateway middleware. This includes initialization,
 * running the server, and graceful shutdown.
 *
 * The server_app class provides:
 * - Configuration loading and validation
 * - Server lifecycle management (start/stop)
 * - Signal handling for graceful shutdown
 * - Integration with logging and monitoring systems
 *
 * Design Goals:
 * - Clean separation between configuration and runtime
 * - Support for graceful shutdown on SIGTERM/SIGINT
 * - Extensible architecture for future features (pooling, caching)
 */

#pragma once

#include "core/server_config.h"

#include <atomic>
#include <memory>
#include <string>

// Common system interfaces
#include <kcenon/common/interfaces/executor_interface.h>

// Forward declarations
namespace database_server::gateway
{
class gateway_server;
class query_router;
struct gateway_config;
} // namespace database_server::gateway

namespace database_server::pooling
{
class connection_pool;
} // namespace database_server::pooling

namespace database_server
{

/**
 * @enum server_state
 * @brief Represents the current state of the server
 */
enum class server_state
{
	uninitialized, ///< Server has not been initialized
	initialized,   ///< Server is initialized but not running
	starting,      ///< Server is in the process of starting
	running,       ///< Server is running and accepting connections
	stopping,      ///< Server is in the process of stopping
	stopped        ///< Server has stopped
};

/**
 * @class server_app
 * @brief Main database server application
 *
 * This class manages the complete lifecycle of the database gateway server.
 * It handles configuration loading, server initialization, running the main
 * event loop, and graceful shutdown.
 *
 * Thread Safety:
 * - The start() and stop() methods are thread-safe
 * - State transitions are atomic
 *
 * Usage Example:
 * @code
 *   database_server::server_app app;
 *   if (!app.initialize("config.yaml")) {
 *       return 1;
 *   }
 *   return app.run();
 * @endcode
 */
class server_app
{
public:
	/**
	 * @brief Construct a new server application
	 */
	server_app();

	/**
	 * @brief Destructor - ensures graceful shutdown
	 */
	~server_app();

	// Non-copyable, non-movable
	server_app(const server_app&) = delete;
	server_app& operator=(const server_app&) = delete;
	server_app(server_app&&) = delete;
	server_app& operator=(server_app&&) = delete;

	/**
	 * @brief Initialize the server with configuration
	 * @param config_path Path to the configuration file (YAML)
	 * @return true if initialization succeeded, false otherwise
	 *
	 * This method:
	 * - Loads and validates the configuration file
	 * - Initializes logging system
	 * - Sets up signal handlers
	 * - Prepares network listeners (but does not start them)
	 */
	bool initialize(const std::string& config_path);

	/**
	 * @brief Initialize the server with a configuration object
	 * @param config Server configuration
	 * @return true if initialization succeeded, false otherwise
	 */
	bool initialize(const server_config& config);

	/**
	 * @brief Run the server main loop
	 * @return Exit code (0 for success, non-zero for error)
	 *
	 * This method blocks until the server is stopped via:
	 * - A call to stop()
	 * - A SIGTERM or SIGINT signal
	 */
	int run();

	/**
	 * @brief Request graceful server shutdown
	 *
	 * This method is thread-safe and can be called from any thread
	 * or from a signal handler.
	 */
	void stop();

	/**
	 * @brief Get current server state
	 * @return Current server_state
	 */
	server_state state() const;

	/**
	 * @brief Check if server is currently running
	 * @return true if server is in running state
	 */
	bool is_running() const;

	/**
	 * @brief Get the server configuration
	 * @return Reference to the current configuration
	 */
	const server_config& config() const;

	/**
	 * @brief Get the executor used for background tasks
	 * @return Shared pointer to executor, or nullptr if not set
	 */
	std::shared_ptr<kcenon::common::interfaces::IExecutor> get_executor() const;

	/**
	 * @brief Set the executor for background tasks
	 * @param executor Shared pointer to executor
	 *
	 * When set, the executor is propagated to query_router and other
	 * components that support IExecutor for background task execution.
	 */
	void set_executor(std::shared_ptr<kcenon::common::interfaces::IExecutor> executor);

private:
	/**
	 * @brief Setup signal handlers for graceful shutdown
	 */
	void setup_signal_handlers();

	/**
	 * @brief Internal initialization logic
	 * @return true on success
	 */
	bool do_initialize();

	/**
	 * @brief Internal cleanup logic
	 */
	void do_cleanup();

	std::atomic<server_state> state_;
	server_config config_;

	// Network gateway
	std::unique_ptr<gateway::gateway_server> gateway_;

	// Connection pool and query router (Phase 3.3)
	std::shared_ptr<pooling::connection_pool> connection_pool_;
	std::unique_ptr<gateway::query_router> query_router_;

	// Executor for background tasks
	std::shared_ptr<kcenon::common::interfaces::IExecutor> executor_;

	// Signal handling
	static server_app* instance_;
	static void signal_handler(int signal);
};

} // namespace database_server
