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
 * @file gateway_server.h
 * @brief Database gateway TCP server
 *
 * Implements the network layer for the database gateway using network_system.
 * Handles client connections, message routing, and lifecycle management.
 *
 * Architecture:
 * - Uses messaging_server from network_system for TCP handling
 * - Integrates with query_protocol for message serialization
 * - Provides callbacks for request handling
 */

#pragma once

#include "auth_middleware.h"
#include "query_protocol.h"
#include "query_types.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Common system integration
#include <kcenon/common/patterns/result.h>

// Forward declarations
namespace network_system::core
{
class messaging_server;
}

namespace network_system::session
{
class messaging_session;
}

namespace database_server::gateway
{

/**
 * @struct gateway_config
 * @brief Configuration for the gateway server
 */
struct gateway_config
{
	std::string server_id = "db_gateway";  ///< Server identifier
	unsigned short port = 5432;            ///< TCP port to listen on
	uint32_t max_connections = 1000;       ///< Maximum concurrent connections
	uint32_t idle_timeout_ms = 300000;     ///< Idle connection timeout (5 min)
	bool require_auth = true;              ///< Require authentication

	auth_config auth;                      ///< Authentication configuration
	rate_limit_config rate_limit;          ///< Rate limiting configuration
};

/**
 * @struct client_session
 * @brief Represents a connected client session
 */
struct client_session
{
	std::string session_id;     ///< Unique session identifier
	std::string client_id;      ///< Authenticated client ID
	bool authenticated = false; ///< Whether client is authenticated
	uint64_t connected_at = 0;  ///< Connection timestamp
	uint64_t last_activity = 0; ///< Last activity timestamp
	uint64_t requests_count = 0; ///< Number of requests processed

	std::weak_ptr<network_system::session::messaging_session> network_session;
};

/**
 * @brief Request handler callback type
 *
 * Called when a query request is received from a client.
 * The handler should process the request and return a response.
 */
using request_handler_t = std::function<query_response(
	const client_session& session,
	const query_request& request)>;

/**
 * @class gateway_server
 * @brief TCP server for database gateway
 *
 * Provides the network interface for the database gateway:
 * - Accepts TCP connections from clients
 * - Handles message serialization/deserialization
 * - Routes requests to appropriate handlers
 * - Manages client sessions
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Callbacks may be invoked from I/O thread
 *
 * Usage Example:
 * @code
 * gateway_config config;
 * config.port = 5432;
 *
 * gateway_server server(config);
 *
 * server.set_request_handler([](const auto& session, const auto& request) {
 *     // Process request and return response
 *     return query_response(request.header.message_id);
 * });
 *
 * if (auto result = server.start(); !result) {
 *     std::cerr << "Failed to start: " << result.error().message << "\n";
 *     return 1;
 * }
 *
 * server.wait();
 * @endcode
 */
class gateway_server
{
public:
	/**
	 * @brief Constructs a gateway server with configuration
	 * @param config Server configuration
	 */
	explicit gateway_server(const gateway_config& config);

	/**
	 * @brief Destructor - ensures graceful shutdown
	 */
	~gateway_server();

	// Non-copyable, non-movable
	gateway_server(const gateway_server&) = delete;
	gateway_server& operator=(const gateway_server&) = delete;
	gateway_server(gateway_server&&) = delete;
	gateway_server& operator=(gateway_server&&) = delete;

	/**
	 * @brief Start the server and begin accepting connections
	 * @return Result indicating success or error
	 */
	[[nodiscard]] kcenon::common::VoidResult start();

	/**
	 * @brief Stop the server and close all connections
	 * @return Result indicating success or error
	 */
	[[nodiscard]] kcenon::common::VoidResult stop();

	/**
	 * @brief Wait for the server to stop
	 *
	 * Blocks until stop() is called or the server shuts down.
	 */
	void wait();

	/**
	 * @brief Check if server is running
	 * @return true if server is running
	 */
	[[nodiscard]] bool is_running() const noexcept;

	/**
	 * @brief Set the request handler
	 * @param handler Function to handle incoming requests
	 */
	void set_request_handler(request_handler_t handler);

	/**
	 * @brief Set connection callback
	 * @param callback Called when a client connects
	 */
	void set_connection_callback(std::function<void(const client_session&)> callback);

	/**
	 * @brief Set disconnection callback
	 * @param callback Called when a client disconnects
	 */
	void set_disconnection_callback(std::function<void(const std::string&)> callback);

	/**
	 * @brief Get number of connected clients
	 * @return Number of active connections
	 */
	[[nodiscard]] size_t connection_count() const;

	/**
	 * @brief Get session by ID
	 * @param session_id Session identifier
	 * @return Session if found, nullopt otherwise
	 */
	[[nodiscard]] std::optional<client_session> get_session(const std::string& session_id) const;

	/**
	 * @brief Disconnect a specific client
	 * @param session_id Session to disconnect
	 * @return true if session was found and disconnected
	 */
	bool disconnect_client(const std::string& session_id);

	/**
	 * @brief Get server configuration
	 * @return Current configuration
	 */
	[[nodiscard]] const gateway_config& config() const noexcept;

	/**
	 * @brief Get authentication middleware
	 * @return Reference to auth middleware
	 */
	[[nodiscard]] auth_middleware& get_auth_middleware() noexcept;

	/**
	 * @brief Get authentication middleware (const)
	 * @return Const reference to auth middleware
	 */
	[[nodiscard]] const auth_middleware& get_auth_middleware() const noexcept;

	/**
	 * @brief Set custom token validator
	 * @param validator Custom validator implementation
	 */
	void set_token_validator(std::shared_ptr<auth_validator> validator);

	/**
	 * @brief Set audit callback for auth events
	 * @param callback Function to call for each auth event
	 */
	void set_audit_callback(audit_callback_t callback);

private:
	/**
	 * @brief Handle new client connection
	 */
	void on_connection(std::shared_ptr<network_system::session::messaging_session> session);

	/**
	 * @brief Handle client disconnection
	 */
	void on_disconnection(const std::string& session_id);

	/**
	 * @brief Handle incoming message
	 */
	void on_message(std::shared_ptr<network_system::session::messaging_session> session,
					const std::vector<uint8_t>& data);

	/**
	 * @brief Handle connection error
	 */
	void on_error(std::shared_ptr<network_system::session::messaging_session> session,
				  std::error_code ec);

	/**
	 * @brief Process a query request
	 */
	void process_request(const std::string& session_id,
						 std::shared_ptr<network_system::session::messaging_session> network_session,
						 const query_request& request);

	/**
	 * @brief Send response to client
	 */
	void send_response(std::shared_ptr<network_system::session::messaging_session> session,
					   const query_response& response);

private:
	gateway_config config_;
	std::shared_ptr<network_system::core::messaging_server> server_;
	std::unique_ptr<auth_middleware> auth_middleware_;

	mutable std::mutex sessions_mutex_;
	std::unordered_map<std::string, client_session> sessions_;

	request_handler_t request_handler_;
	std::function<void(const client_session&)> connection_callback_;
	std::function<void(const std::string&)> disconnection_callback_;

	std::atomic<bool> running_{false};
};

} // namespace database_server::gateway
