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
 * @file connection_priority.h
 * @brief Priority levels for connection acquisition requests
 *
 * Defines priority levels that determine the order in which connection
 * requests are serviced by the connection pool.
 */

#pragma once

namespace database_server::pooling
{

/**
 * @enum connection_priority
 * @brief Priority levels for connection acquisition requests
 *
 * These priority levels determine the order in which connection requests
 * are serviced by the connection pool. Higher priority requests are
 * processed first when multiple requests are pending.
 *
 * ### Priority Order (Highest to Lowest)
 * 1. CRITICAL - Time-sensitive operations that cannot be delayed
 * 2. TRANSACTION - Active transactions requiring immediate response
 * 3. NORMAL_QUERY - Standard database queries (default priority)
 * 4. HEALTH_CHECK - Background health monitoring (lowest priority)
 *
 * ### Usage Example
 * @code
 * // Critical operation (e.g., payment processing)
 * auto result = pool.acquire_connection(connection_priority::CRITICAL);
 *
 * // Normal query (default)
 * auto result = pool.acquire_connection(connection_priority::NORMAL_QUERY);
 *
 * // Background health check
 * auto result = pool.acquire_connection(connection_priority::HEALTH_CHECK);
 * @endcode
 */
enum class connection_priority : int
{
	HEALTH_CHECK = 0, ///< Lowest priority - background maintenance
	NORMAL_QUERY = 1, ///< Default priority - standard queries
	TRANSACTION = 2,  ///< High priority - active transactions
	CRITICAL = 3      ///< Highest priority - time-critical operations
};

/**
 * @brief Converts connection_priority enum to string representation.
 * @param priority The priority level to convert.
 * @return String representation of the priority level.
 */
constexpr const char* to_string(connection_priority priority) noexcept
{
	switch (priority)
	{
	case connection_priority::HEALTH_CHECK:
		return "HEALTH_CHECK";
	case connection_priority::NORMAL_QUERY:
		return "NORMAL_QUERY";
	case connection_priority::TRANSACTION:
		return "TRANSACTION";
	case connection_priority::CRITICAL:
		return "CRITICAL";
	default:
		return "UNKNOWN";
	}
}

} // namespace database_server::pooling
