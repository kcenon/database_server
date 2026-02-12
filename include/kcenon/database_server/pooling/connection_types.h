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
 * @file connection_types.h
 * @brief Connection pool types for database_server middleware
 *
 * These types were previously in database_system but have been moved here
 * as part of the Client Library Diet initiative. Connection pooling is now
 * handled server-side via database_server middleware.
 *
 * @note These types are server-side implementations. Client applications
 *       should use ProxyMode to connect through database_server.
 */

#pragma once

#include <database/core/database_backend.h>
#include <database/database_types.h>

#include <kcenon/common/patterns/result.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace database
{

/**
 * @struct connection_pool_config
 * @brief Configuration parameters for connection pools.
 */
struct connection_pool_config
{
	size_t min_connections = 2;        ///< Minimum number of connections to maintain
	size_t max_connections = 20;       ///< Maximum number of connections allowed
	std::chrono::milliseconds acquire_timeout{5000}; ///< Timeout for acquiring connections
	std::chrono::milliseconds idle_timeout{30000};   ///< Timeout for idle connections
	std::chrono::milliseconds health_check_interval{60000}; ///< Health check interval
	bool enable_health_checks = true;  ///< Enable periodic health checks
	std::string connection_string;     ///< Database connection string
};

/**
 * @struct connection_stats
 * @brief Statistics for connection pool monitoring.
 */
struct connection_stats
{
	size_t total_connections = 0;      ///< Total connections created
	size_t active_connections = 0;     ///< Currently active connections
	size_t available_connections = 0;  ///< Available connections in pool
	size_t failed_acquisitions = 0;    ///< Number of failed connection acquisitions
	size_t successful_acquisitions = 0; ///< Number of successful acquisitions
	std::chrono::steady_clock::time_point last_health_check; ///< Last health check time
};

/**
 * @class connection_wrapper
 * @brief Wrapper for database connections with metadata.
 */
class connection_wrapper
{
public:
	explicit connection_wrapper(std::unique_ptr<core::database_backend> conn)
		: connection_(std::move(conn))
		, is_healthy_(true)
		, last_used_(std::chrono::steady_clock::now())
	{
	}

	~connection_wrapper() = default;

	core::database_backend* get() const { return connection_.get(); }
	core::database_backend* operator->() const { return connection_.get(); }
	core::database_backend& operator*() const { return *connection_; }

	bool is_healthy() const { return is_healthy_.load(); }
	void mark_unhealthy() { is_healthy_.store(false); }

	void update_last_used()
	{
		std::lock_guard<std::mutex> lock(metadata_mutex_);
		last_used_ = std::chrono::steady_clock::now();
	}

	std::chrono::steady_clock::time_point last_used() const
	{
		std::lock_guard<std::mutex> lock(metadata_mutex_);
		return last_used_;
	}

	bool is_idle_timeout_exceeded(std::chrono::milliseconds timeout) const
	{
		std::lock_guard<std::mutex> lock(metadata_mutex_);
		auto now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_used_) > timeout;
	}

private:
	std::unique_ptr<core::database_backend> connection_;
	std::atomic<bool> is_healthy_;
	std::chrono::steady_clock::time_point last_used_;
	mutable std::mutex metadata_mutex_;
};

/**
 * @class connection_pool_base
 * @brief Abstract base class for database connection pools.
 */
class connection_pool_base
{
public:
	virtual ~connection_pool_base() = default;

	/**
	 * @brief Acquires a connection from the pool.
	 * @return Result containing a shared pointer to a connection wrapper on success,
	 *         or an error_info with details if acquisition failed
	 */
	virtual kcenon::common::Result<std::shared_ptr<connection_wrapper>> acquire_connection() = 0;

	/**
	 * @brief Returns a connection to the pool.
	 * @param connection The connection to return
	 */
	virtual void release_connection(std::shared_ptr<connection_wrapper> connection) = 0;

	/**
	 * @brief Gets current pool statistics.
	 * @return Current connection statistics
	 */
	virtual connection_stats get_stats() const = 0;

	/**
	 * @brief Gets the number of active connections.
	 * @return Number of connections currently in use
	 */
	virtual size_t active_connections() const = 0;

	/**
	 * @brief Gets the number of available connections.
	 * @return Number of idle connections in the pool
	 */
	virtual size_t available_connections() const = 0;

	/**
	 * @brief Performs health check on connections.
	 */
	virtual void health_check() = 0;

	/**
	 * @brief Shuts down the pool.
	 */
	virtual void shutdown() = 0;

	/**
	 * @brief Checks if pool is shutting down.
	 * @return true if shutdown is in progress
	 */
	virtual bool is_shutting_down() const = 0;
};

/**
 * @class connection_pool
 * @brief Basic implementation of a database connection pool.
 *
 * This is a server-side connection pool implementation for database_server middleware.
 */
class connection_pool : public connection_pool_base
{
public:
	using connection_factory = std::function<std::unique_ptr<core::database_backend>()>;

	connection_pool(
		database_types db_type,
		const connection_pool_config& config,
		connection_factory factory)
		: db_type_(db_type)
		, config_(config)
		, factory_(std::move(factory))
		, shutting_down_(false)
	{
	}

	~connection_pool() override
	{
		shutdown();
	}

	bool initialize()
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);

		for (size_t i = 0; i < config_.min_connections; ++i)
		{
			auto conn = create_connection();
			if (conn)
			{
				available_connections_.push(std::move(conn));
				stats_.total_connections++;
			}
		}

		stats_.available_connections = available_connections_.size();
		return stats_.available_connections > 0;
	}

	kcenon::common::Result<std::shared_ptr<connection_wrapper>> acquire_connection() override
	{
		if (shutting_down_.load())
		{
			return kcenon::common::error_info{-500, "Pool is shutting down", "connection_pool"};
		}

		std::unique_lock<std::mutex> lock(pool_mutex_);

		// Wait for available connection or timeout
		auto deadline = std::chrono::steady_clock::now() + config_.acquire_timeout;

		while (available_connections_.empty())
		{
			// Try to create new connection if under max
			if (stats_.total_connections < config_.max_connections)
			{
				lock.unlock();
				auto conn = create_connection();
				lock.lock();

				if (conn)
				{
					stats_.total_connections++;
					stats_.active_connections++;
					stats_.successful_acquisitions++;
					return conn;
				}
			}

			// Wait for connection to be released
			if (pool_condition_.wait_until(lock, deadline) == std::cv_status::timeout)
			{
				stats_.failed_acquisitions++;
				return kcenon::common::error_info{-501, "Connection acquisition timeout", "connection_pool"};
			}

			if (shutting_down_.load())
			{
				return kcenon::common::error_info{-500, "Pool is shutting down", "connection_pool"};
			}
		}

		auto conn = std::move(available_connections_.front());
		available_connections_.pop();
		stats_.available_connections = available_connections_.size();
		stats_.active_connections++;
		stats_.successful_acquisitions++;

		conn->update_last_used();
		return conn;
	}

	void release_connection(std::shared_ptr<connection_wrapper> connection) override
	{
		if (!connection)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(pool_mutex_);

		if (shutting_down_.load())
		{
			stats_.active_connections--;
			return;
		}

		if (connection->is_healthy())
		{
			available_connections_.push(std::move(connection));
			stats_.available_connections = available_connections_.size();
		}

		stats_.active_connections--;
		pool_condition_.notify_one();
	}

	connection_stats get_stats() const override
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		return stats_;
	}

	size_t active_connections() const override
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		return stats_.active_connections;
	}

	size_t available_connections() const override
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		return stats_.available_connections;
	}

	void health_check() override
	{
		std::lock_guard<std::mutex> lock(pool_mutex_);
		stats_.last_health_check = std::chrono::steady_clock::now();

		// Check and remove unhealthy connections
		std::queue<std::shared_ptr<connection_wrapper>> healthy_connections;
		while (!available_connections_.empty())
		{
			auto conn = std::move(available_connections_.front());
			available_connections_.pop();

			if (conn->is_healthy() && !conn->is_idle_timeout_exceeded(config_.idle_timeout))
			{
				healthy_connections.push(std::move(conn));
			}
			else
			{
				stats_.total_connections--;
			}
		}
		available_connections_ = std::move(healthy_connections);
		stats_.available_connections = available_connections_.size();
	}

	void shutdown() override
	{
		shutting_down_.store(true);
		pool_condition_.notify_all();

		std::lock_guard<std::mutex> lock(pool_mutex_);
		while (!available_connections_.empty())
		{
			available_connections_.pop();
		}
		stats_.available_connections = 0;
	}

	bool is_shutting_down() const override
	{
		return shutting_down_.load();
	}

private:
	std::shared_ptr<connection_wrapper> create_connection()
	{
		try
		{
			auto db = factory_();
			if (db)
			{
				return std::make_shared<connection_wrapper>(std::move(db));
			}
		}
		catch (...)
		{
			// Failed to create connection
		}
		return nullptr;
	}

	database_types db_type_;
	connection_pool_config config_;
	connection_factory factory_;

	mutable std::mutex pool_mutex_;
	std::condition_variable pool_condition_;
	std::queue<std::shared_ptr<connection_wrapper>> available_connections_;
	connection_stats stats_;
	std::atomic<bool> shutting_down_;
};

} // namespace database
