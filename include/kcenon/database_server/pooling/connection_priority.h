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
 *
 * Note: Uses kcenon::thread::job_types as the underlying type to ensure
 * compatibility with the thread_system template instantiations.
 */

#pragma once

#include <kcenon/thread/impl/typed_pool/job_types.h>

namespace database_server::pooling
{

/**
 * @brief Type alias for connection priority using thread_system's job_types.
 *
 * This allows the connection pool to use the pre-instantiated template classes
 * from thread_system while maintaining semantic clarity in the API.
 *
 * ### Priority Mapping
 * - CRITICAL/TRANSACTION -> RealTime (highest priority)
 * - NORMAL_QUERY -> Batch (default priority)
 * - HEALTH_CHECK -> Background (lowest priority)
 */
using connection_priority = kcenon::thread::job_types;

/**
 * @brief Priority level for time-critical operations.
 *
 * Use this for operations that cannot be delayed, such as:
 * - Payment processing
 * - Real-time data updates
 * - Emergency queries
 */
inline constexpr connection_priority PRIORITY_CRITICAL = kcenon::thread::job_types::RealTime;

/**
 * @brief Priority level for active transactions.
 *
 * Use this for operations within an active transaction that need
 * prompt response to avoid holding locks too long.
 */
inline constexpr connection_priority PRIORITY_TRANSACTION = kcenon::thread::job_types::RealTime;

/**
 * @brief Default priority level for standard queries.
 *
 * This is the default priority for most database operations.
 */
inline constexpr connection_priority PRIORITY_NORMAL_QUERY = kcenon::thread::job_types::Batch;

/**
 * @brief Lowest priority for background maintenance.
 *
 * Use this for health checks, statistics gathering, and other
 * non-time-sensitive operations.
 */
inline constexpr connection_priority PRIORITY_HEALTH_CHECK = kcenon::thread::job_types::Background;

/**
 * @brief Converts connection_priority to string representation.
 * @param priority The priority level to convert.
 * @return String representation of the priority level.
 *
 * Uses the underlying job_types string conversion.
 */
inline const char* priority_to_string(connection_priority priority) noexcept
{
	switch (priority)
	{
	case kcenon::thread::job_types::RealTime:
		return "CRITICAL";
	case kcenon::thread::job_types::Batch:
		return "NORMAL_QUERY";
	case kcenon::thread::job_types::Background:
		return "HEALTH_CHECK";
	default:
		return "UNKNOWN";
	}
}

} // namespace database_server::pooling
