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
 * @file query_handler_base.h
 * @brief CRTP-based query handler infrastructure for database gateway
 *
 * Provides a Curiously Recurring Template Pattern (CRTP) based handler
 * hierarchy for query processing. This eliminates virtual function overhead
 * while maintaining polymorphic behavior through type erasure.
 *
 * Features:
 * - Zero virtual dispatch overhead for handler operations
 * - Compile-time polymorphism via CRTP
 * - Type erasure for runtime handler selection
 * - Query type validation and routing
 *
 * ## Thread Safety
 * - `query_handler_base` and CRTP handlers are stateless; `handle()` and
 *   `can_handle()` are safe to call concurrently on the same instance.
 * - `i_query_handler` and `query_handler_wrapper` share the same guarantees
 *   as the wrapped handler (stateless handlers are thread-safe).
 * - `handler_context` is a shared resource holder; the referenced pool and
 *   cache must themselves be thread-safe (which they are).
 *
 * @see https://github.com/kcenon/database_server/issues/48
 */

#pragma once

#include "query_protocol.h"
#include "query_types.h"

#include <cstdint>
#include <memory>
#include <type_traits>

// Common system integration
#include <kcenon/common/patterns/result.h>

// Forward declarations
namespace database_server::pooling
{
class connection_pool;
}

namespace database_server::gateway
{

// Forward declarations
class query_cache;

/**
 * @brief Context passed to query handlers for execution
 *
 * Contains all resources needed to execute a query, including
 * connection pool and cache references.
 */
struct handler_context
{
	std::shared_ptr<pooling::connection_pool> pool;
	std::shared_ptr<query_cache> cache;
	uint32_t default_timeout_ms = 30000;
};

/**
 * @class query_handler_base
 * @brief CRTP base class for query handlers
 *
 * Provides a compile-time polymorphic interface for query handlers.
 * Derived classes must implement:
 * - handle_impl(const query_request&, const handler_context&) -> query_response
 * - can_handle_impl(query_type) const -> bool
 *
 * @tparam Derived The derived handler class
 *
 * Usage Example:
 * @code
 * class select_handler : public query_handler_base<select_handler> {
 *     friend class query_handler_base<select_handler>;
 *
 *     query_response handle_impl(const query_request& request,
 *                                const handler_context& context) {
 *         // Implementation
 *     }
 *
 *     bool can_handle_impl(query_type type) const noexcept {
 *         return type == query_type::select;
 *     }
 * };
 * @endcode
 */
template <typename Derived>
class query_handler_base
{
public:
	/**
	 * @brief Handle a query request
	 * @param request The query request to handle
	 * @param context Handler context with resources
	 * @return Query response
	 */
	[[nodiscard]] query_response handle(const query_request& request,
										const handler_context& context)
	{
		return derived().handle_impl(request, context);
	}

	/**
	 * @brief Check if this handler can handle the given query type
	 * @param type Query type to check
	 * @return true if this handler can handle the query type
	 */
	[[nodiscard]] bool can_handle(query_type type) const noexcept
	{
		return derived().can_handle_impl(type);
	}

protected:
	query_handler_base() = default;
	~query_handler_base() = default;

	// Allow copy and move for value semantics
	query_handler_base(const query_handler_base&) = default;
	query_handler_base& operator=(const query_handler_base&) = default;
	query_handler_base(query_handler_base&&) = default;
	query_handler_base& operator=(query_handler_base&&) = default;

private:
	[[nodiscard]] Derived& derived() noexcept
	{
		return static_cast<Derived&>(*this);
	}

	[[nodiscard]] const Derived& derived() const noexcept
	{
		return static_cast<const Derived&>(*this);
	}
};

/**
 * @class i_query_handler
 * @brief Type-erased interface for query handlers
 *
 * Provides a virtual interface for runtime handler selection while
 * preserving the performance benefits of CRTP within each handler.
 * Use query_handler_wrapper to wrap CRTP handlers.
 */
class i_query_handler
{
public:
	virtual ~i_query_handler() = default;

	/**
	 * @brief Handle a query request
	 * @param request The query request to handle
	 * @param context Handler context with resources
	 * @return Query response
	 */
	[[nodiscard]] virtual query_response handle(const query_request& request,
												const handler_context& context) = 0;

	/**
	 * @brief Check if this handler can handle the given query type
	 * @param type Query type to check
	 * @return true if this handler can handle the query type
	 */
	[[nodiscard]] virtual bool can_handle(query_type type) const noexcept = 0;
};

/**
 * @class query_handler_wrapper
 * @brief Type erasure wrapper for CRTP handlers
 *
 * Wraps a CRTP-based handler to provide a virtual interface for
 * runtime polymorphism. The actual handler operations use CRTP
 * for zero-overhead dispatch.
 *
 * @tparam Handler The CRTP handler type to wrap
 *
 * Usage Example:
 * @code
 * auto handler = std::make_unique<query_handler_wrapper<select_handler>>();
 * registry.register_handler(std::move(handler));
 * @endcode
 */
template <typename Handler>
class query_handler_wrapper : public i_query_handler
{
public:
	/**
	 * @brief Default construct with default handler
	 */
	query_handler_wrapper() = default;

	/**
	 * @brief Construct with existing handler
	 * @param handler Handler instance to wrap
	 */
	explicit query_handler_wrapper(Handler handler)
		: handler_(std::move(handler))
	{
	}

	/**
	 * @brief Handle a query request
	 * @param request The query request to handle
	 * @param context Handler context with resources
	 * @return Query response
	 */
	[[nodiscard]] query_response handle(const query_request& request,
										const handler_context& context) override
	{
		return handler_.handle(request, context);
	}

	/**
	 * @brief Check if this handler can handle the given query type
	 * @param type Query type to check
	 * @return true if this handler can handle the query type
	 */
	[[nodiscard]] bool can_handle(query_type type) const noexcept override
	{
		return handler_.can_handle(type);
	}

	/**
	 * @brief Access the underlying handler
	 * @return Reference to the wrapped handler
	 */
	[[nodiscard]] Handler& get() noexcept { return handler_; }

	/**
	 * @brief Access the underlying handler (const)
	 * @return Const reference to the wrapped handler
	 */
	[[nodiscard]] const Handler& get() const noexcept { return handler_; }

private:
	Handler handler_;
};

/**
 * @brief Concept for valid query handlers
 *
 * Ensures a type satisfies the query handler requirements:
 * - Has handle(query_request, handler_context) -> query_response
 * - Has can_handle(query_type) -> bool
 */
template <typename T>
concept QueryHandler = requires(T handler, const query_request& req,
								const handler_context& ctx, query_type type) {
	{ handler.handle(req, ctx) } -> std::same_as<query_response>;
	{ handler.can_handle(type) } -> std::same_as<bool>;
};

/**
 * @brief Factory function to create wrapped handler
 * @tparam Handler The handler type to create and wrap
 * @tparam Args Constructor argument types
 * @param args Constructor arguments for the handler
 * @return Unique pointer to wrapped handler
 */
template <typename Handler, typename... Args>
[[nodiscard]] std::unique_ptr<i_query_handler> make_handler(Args&&... args)
{
	return std::make_unique<query_handler_wrapper<Handler>>(
		Handler(std::forward<Args>(args)...));
}

} // namespace database_server::gateway
