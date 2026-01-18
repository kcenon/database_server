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
 * @file console_logger.h
 * @brief Console logger implementation for database_server
 *
 * Provides a simple ILogger implementation that outputs to stdout/stderr.
 * This serves as the foundation for unified logging infrastructure.
 *
 * Features:
 * - Implements kcenon::common::interfaces::ILogger
 * - Thread-safe console output
 * - Configurable log levels
 * - Timestamps and log level prefixes
 *
 * Part of #57: Integrate logger_system for unified logging infrastructure
 * Phase 1: Logger Integration Infrastructure
 */

#pragma once

#include <kcenon/common/interfaces/logger_interface.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace database_server::logging
{

/**
 * @class console_logger
 * @brief Simple console logger implementing ILogger interface
 *
 * This logger outputs messages to stdout (info and below) or stderr
 * (warning and above). It provides thread-safe logging with configurable
 * log levels and formatted output.
 *
 * Thread Safety:
 * - All logging methods are thread-safe
 * - Level changes are atomic
 *
 * Usage:
 * @code
 *   auto logger = database_server::logging::create_console_logger();
 *   logger->log(kcenon::common::interfaces::log_level::info, "Server started");
 * @endcode
 */
class console_logger : public kcenon::common::interfaces::ILogger
{
public:
	/**
	 * @brief Construct a console logger with specified minimum level
	 * @param min_level Minimum log level to output (default: info)
	 */
	explicit console_logger(
		kcenon::common::interfaces::log_level min_level
		= kcenon::common::interfaces::log_level::info);

	~console_logger() override = default;

	// Non-copyable but movable
	console_logger(const console_logger&) = delete;
	console_logger& operator=(const console_logger&) = delete;
	console_logger(console_logger&&) = default;
	console_logger& operator=(console_logger&&) = default;

	/**
	 * @brief Log a message with specified level
	 * @param level Log level
	 * @param message Log message
	 * @return VoidResult indicating success or error
	 */
	kcenon::common::VoidResult log(kcenon::common::interfaces::log_level level,
								   const std::string& message) override;

	/**
	 * @brief Log a message with source location (C++20)
	 * @param level Log level
	 * @param message Log message
	 * @param loc Source location
	 * @return VoidResult indicating success or error
	 */
	kcenon::common::VoidResult log(
		kcenon::common::interfaces::log_level level,
		std::string_view message,
		const kcenon::common::source_location& loc
		= kcenon::common::source_location::current()) override;

	/**
	 * @brief Log a structured entry
	 * @param entry Log entry with all information
	 * @return VoidResult indicating success or error
	 */
	kcenon::common::VoidResult log(
		const kcenon::common::interfaces::log_entry& entry) override;

	/**
	 * @brief Check if logging is enabled for the specified level
	 * @param level Log level to check
	 * @return true if logging is enabled for this level
	 */
	bool is_enabled(kcenon::common::interfaces::log_level level) const override;

	/**
	 * @brief Set the minimum log level
	 * @param level Minimum level for messages to be logged
	 * @return VoidResult indicating success
	 */
	kcenon::common::VoidResult set_level(
		kcenon::common::interfaces::log_level level) override;

	/**
	 * @brief Get the current minimum log level
	 * @return Current minimum log level
	 */
	kcenon::common::interfaces::log_level get_level() const override;

	/**
	 * @brief Flush any buffered log messages
	 * @return VoidResult indicating success
	 */
	kcenon::common::VoidResult flush() override;

private:
	/**
	 * @brief Format and write a log message
	 * @param level Log level
	 * @param message Message to write
	 * @param file Source file (optional)
	 * @param line Source line (optional)
	 * @param function Source function (optional)
	 */
	void write_message(kcenon::common::interfaces::log_level level,
					   const std::string& message,
					   const std::string& file = "",
					   int line = 0,
					   const std::string& function = "");

	/**
	 * @brief Get timestamp string for current time
	 * @return Formatted timestamp
	 */
	std::string get_timestamp() const;

	std::atomic<kcenon::common::interfaces::log_level> min_level_;
	mutable std::mutex output_mutex_;
};

/**
 * @brief Factory function to create a console logger
 * @param min_level Minimum log level (default: info)
 * @return Shared pointer to ILogger
 */
std::shared_ptr<kcenon::common::interfaces::ILogger> create_console_logger(
	kcenon::common::interfaces::log_level min_level
	= kcenon::common::interfaces::log_level::info);

} // namespace database_server::logging
