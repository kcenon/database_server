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

#include <kcenon/database_server/gateway/gateway_server.h>
#include <kcenon/database_server/gateway/auth_middleware.h>
#include <kcenon/database_server/gateway/session_id_generator.h>

#include <kcenon/network/facade/tcp_facade.h>
#include <kcenon/network/interfaces/i_protocol_server.h>
#include <kcenon/network/session/session.h>

#include <kcenon/common/config/feature_flags.h>

#if KCENON_WITH_CONTAINER_SYSTEM
#include <container/core/container.h>
#endif

#include <chrono>

namespace database_server::gateway
{

namespace
{

uint64_t current_timestamp_ms()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());
}

} // namespace

// ============================================================================
// gateway_server
// ============================================================================

gateway_server::gateway_server(const gateway_config& config)
	: config_(config)
	, server_(kcenon::network::facade::tcp_facade().create_server(
		  {.port = config.port, .server_id = config.server_id}))
	, auth_middleware_(std::make_unique<auth_middleware>(config.auth, config.rate_limit))
{
	// Set up network callbacks using i_protocol_server interface
	server_->set_connection_callback(
		[this](std::shared_ptr<kcenon::network::interfaces::i_session> session)
		{
			on_connection(std::move(session));
		});

	server_->set_disconnection_callback(
		[this](std::string_view session_id)
		{
			on_disconnection(session_id);
		});

	server_->set_receive_callback(
		[this](std::string_view session_id, const std::vector<uint8_t>& data)
		{
			on_message(session_id, data);
		});

	server_->set_error_callback(
		[this](std::string_view session_id, std::error_code ec)
		{
			on_error(session_id, ec);
		});
}

gateway_server::~gateway_server()
{
	if (running_)
	{
		(void)stop();
	}
}

kcenon::common::VoidResult gateway_server::start()
{
	if (running_.exchange(true))
	{
		return kcenon::common::error_info{
			-1, "Server already running", "gateway_server"
		};
	}

	auto result = server_->start(config_.port);
	if (result.is_err())
	{
		running_ = false;
		return kcenon::common::error_info{
			-2, "Failed to start network server", "gateway_server"
		};
	}

	return kcenon::common::ok();
}

kcenon::common::VoidResult gateway_server::stop()
{
	if (!running_.exchange(false))
	{
		return kcenon::common::error_info{
			-1, "Server not running", "gateway_server"
		};
	}

	// Clear all sessions
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		sessions_.clear();
		network_id_map_.clear();
	}

	auto result = server_->stop();
	if (result.is_err())
	{
		return kcenon::common::error_info{
			-2, "Failed to stop network server", "gateway_server"
		};
	}

	stop_cv_.notify_all();
	return kcenon::common::ok();
}

void gateway_server::wait()
{
	std::unique_lock<std::mutex> lock(stop_mutex_);
	stop_cv_.wait(lock, [this] { return !running_.load(); });
}

bool gateway_server::is_running() const noexcept
{
	return running_;
}

void gateway_server::set_request_handler(request_handler_t handler)
{
	request_handler_ = std::move(handler);
}

void gateway_server::set_connection_callback(std::function<void(const client_session&)> callback)
{
	connection_callback_ = std::move(callback);
}

void gateway_server::set_disconnection_callback(std::function<void(const std::string&)> callback)
{
	disconnection_callback_ = std::move(callback);
}

size_t gateway_server::connection_count() const
{
	std::lock_guard<std::mutex> lock(sessions_mutex_);
	return sessions_.size();
}

std::optional<client_session> gateway_server::get_session(const std::string& session_id) const
{
	std::lock_guard<std::mutex> lock(sessions_mutex_);
	auto it = sessions_.find(session_id);
	if (it != sessions_.end())
	{
		return it->second;
	}
	return std::nullopt;
}

bool gateway_server::disconnect_client(const std::string& session_id)
{
	std::lock_guard<std::mutex> lock(sessions_mutex_);
	auto it = sessions_.find(session_id);
	if (it == sessions_.end())
	{
		return false;
	}

	if (auto& network_session = it->second.network_session)
	{
		// Remove reverse mapping before erasing session
		network_id_map_.erase(std::string(network_session->id()));
		network_session->close();
	}

	sessions_.erase(it);
	return true;
}

const gateway_config& gateway_server::config() const noexcept
{
	return config_;
}

auth_middleware& gateway_server::get_auth_middleware() noexcept
{
	return *auth_middleware_;
}

const auth_middleware& gateway_server::get_auth_middleware() const noexcept
{
	return *auth_middleware_;
}

void gateway_server::set_token_validator(std::shared_ptr<auth_validator> validator)
{
	// Recreate auth middleware with new validator
	auth_middleware_ = std::make_unique<auth_middleware>(
		config_.auth, config_.rate_limit, std::move(validator));
}

void gateway_server::set_audit_callback(audit_callback_t callback)
{
	auth_middleware_->set_audit_callback(std::move(callback));
}

void gateway_server::on_connection(
	std::shared_ptr<kcenon::network::interfaces::i_session> session)
{
	if (!session)
	{
		return;
	}

	auto session_id = generate_session_id();
	auto network_id = std::string(session->id());
	auto now = current_timestamp_ms();

	client_session client;
	client.session_id = session_id;
	client.connected_at = now;
	client.last_activity = now;
	client.authenticated = !config_.require_auth;
	client.network_session = session;

	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);

		// Check max connections
		if (sessions_.size() >= config_.max_connections)
		{
			// Reject connection
			session->close();
			return;
		}

		sessions_[session_id] = client;
		network_id_map_[network_id] = session_id;
	}

	// Notify auth middleware of session creation
	auth_middleware_->on_session_created(session_id, "");

	if (connection_callback_)
	{
		connection_callback_(client);
	}
}

void gateway_server::on_disconnection(std::string_view network_session_id)
{
	std::string session_id;
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		auto map_it = network_id_map_.find(std::string(network_session_id));
		if (map_it == network_id_map_.end())
		{
			return;
		}
		session_id = map_it->second;
		network_id_map_.erase(map_it);
		sessions_.erase(session_id);
	}

	// Notify auth middleware of session destruction
	auth_middleware_->on_session_destroyed(session_id);

	if (disconnection_callback_)
	{
		disconnection_callback_(session_id);
	}
}

void gateway_server::on_message(
	std::string_view network_session_id,
	const std::vector<uint8_t>& data)
{
	if (data.empty())
	{
		return;
	}

	// Look up session by network session ID (O(1) hash lookup)
	std::string session_id;
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		auto map_it = network_id_map_.find(std::string(network_session_id));
		if (map_it == network_id_map_.end())
		{
			return;
		}
		session_id = map_it->second;
	}

	// Deserialize request
	auto request_result = query_request::deserialize(data);
	if (request_result.is_err())
	{
		// Send error response
		query_response error_response(0, status_code::invalid_query,
									  "Failed to parse request: " +
										  request_result.error().message);
		send_response(session_id, error_response);
		return;
	}

	// Update last activity
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		if (auto it = sessions_.find(session_id); it != sessions_.end())
		{
			it->second.last_activity = current_timestamp_ms();
			it->second.requests_count++;
		}
	}

	process_request(session_id, request_result.value());
}

void gateway_server::on_error(
	std::string_view network_session_id, std::error_code ec)
{
	(void)network_session_id;
	(void)ec;
	// Log error if logging is available
}

void gateway_server::process_request(
	const std::string& session_id,
	const query_request& request)
{
	// Handle ping request directly
	if (request.type == query_type::ping)
	{
		query_response response(request.header.message_id);
		response.header.correlation_id = request.header.correlation_id;
		send_response(session_id, response);
		return;
	}

	// Get client session
	std::optional<client_session> client;
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		if (auto it = sessions_.find(session_id); it != sessions_.end())
		{
			client = it->second;
		}
	}

	if (!client)
	{
		query_response error_response(request.header.message_id,
									  status_code::error,
									  "Session not found");
		send_response(session_id, error_response);
		return;
	}

	// Check authentication and rate limiting using middleware
	if (config_.require_auth && !client->authenticated)
	{
		auto auth_result = auth_middleware_->check(session_id, request.token);
		if (!auth_result.success)
		{
			query_response error_response(request.header.message_id,
										  auth_result.code,
										  auth_result.message);
			send_response(session_id, error_response);
			return;
		}

		// Mark as authenticated
		{
			std::lock_guard<std::mutex> lock(sessions_mutex_);
			if (auto it = sessions_.find(session_id); it != sessions_.end())
			{
				it->second.authenticated = true;
				it->second.client_id = auth_result.client_id;
			}
		}
	}
	else if (config_.require_auth)
	{
		// Already authenticated, but still check rate limit
		if (!auth_middleware_->check_rate_limit(client->client_id))
		{
			query_response error_response(request.header.message_id,
										  status_code::rate_limited,
										  "Rate limit exceeded");
			send_response(session_id, error_response);
			return;
		}
	}

	// Validate request
	if (!request.is_valid())
	{
		query_response error_response(request.header.message_id,
									  status_code::invalid_query,
									  "Invalid query request");
		send_response(session_id, error_response);
		return;
	}

	// Invoke request handler
	if (request_handler_)
	{
		auto response = request_handler_(*client, request);
		response.header.correlation_id = request.header.correlation_id;
		send_response(session_id, response);
	}
	else
	{
		query_response error_response(request.header.message_id,
									  status_code::error,
									  "No request handler configured");
		send_response(session_id, error_response);
	}
}

void gateway_server::send_response(
	const std::string& session_id,
	const query_response& response)
{
	std::shared_ptr<kcenon::network::interfaces::i_session> session;
	{
		std::lock_guard<std::mutex> lock(sessions_mutex_);
		auto it = sessions_.find(session_id);
		if (it == sessions_.end())
		{
			return;
		}
		session = it->second.network_session;
	}

	if (!session || !session->is_connected())
	{
		return;
	}

#if KCENON_WITH_CONTAINER_SYSTEM
	auto container = response.serialize();
	if (container)
	{
		auto data = container->serialize_array();
		(void)session->send(std::move(data));
	}
#else
	(void)response;
#endif
}

} // namespace database_server::gateway
