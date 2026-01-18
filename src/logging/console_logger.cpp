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

#include <kcenon/database_server/logging/console_logger.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace database_server::logging
{

console_logger::console_logger(kcenon::common::interfaces::log_level min_level)
	: min_level_(min_level)
{
}

kcenon::common::VoidResult console_logger::log(
	kcenon::common::interfaces::log_level level,
	const std::string& message)
{
	if (!is_enabled(level))
	{
		return kcenon::common::VoidResult(std::monostate{});
	}

	write_message(level, message);
	return kcenon::common::VoidResult(std::monostate{});
}

kcenon::common::VoidResult console_logger::log(
	kcenon::common::interfaces::log_level level,
	std::string_view message,
	const kcenon::common::source_location& loc)
{
	if (!is_enabled(level))
	{
		return kcenon::common::VoidResult(std::monostate{});
	}

	write_message(level, std::string(message), loc.file_name(), loc.line(),
				  loc.function_name());
	return kcenon::common::VoidResult(std::monostate{});
}

kcenon::common::VoidResult console_logger::log(
	const kcenon::common::interfaces::log_entry& entry)
{
	if (!is_enabled(entry.level))
	{
		return kcenon::common::VoidResult(std::monostate{});
	}

	write_message(entry.level, entry.message, entry.file, entry.line,
				  entry.function);
	return kcenon::common::VoidResult(std::monostate{});
}

bool console_logger::is_enabled(
	kcenon::common::interfaces::log_level level) const
{
	return static_cast<int>(level) >= static_cast<int>(min_level_.load());
}

kcenon::common::VoidResult console_logger::set_level(
	kcenon::common::interfaces::log_level level)
{
	min_level_.store(level);
	return kcenon::common::VoidResult(std::monostate{});
}

kcenon::common::interfaces::log_level console_logger::get_level() const
{
	return min_level_.load();
}

kcenon::common::VoidResult console_logger::flush()
{
	std::lock_guard<std::mutex> lock(output_mutex_);
	std::cout.flush();
	std::cerr.flush();
	return kcenon::common::VoidResult(std::monostate{});
}

void console_logger::write_message(
	kcenon::common::interfaces::log_level level,
	const std::string& message,
	const std::string& file,
	int line,
	const std::string& function)
{
	std::lock_guard<std::mutex> lock(output_mutex_);

	std::ostringstream oss;
	oss << "[" << get_timestamp() << "] ";
	oss << "[" << kcenon::common::interfaces::to_string(level) << "] ";

	if (!file.empty())
	{
		// Extract filename from full path
		std::string filename = file;
		auto pos = filename.find_last_of("/\\");
		if (pos != std::string::npos)
		{
			filename = filename.substr(pos + 1);
		}
		oss << "[" << filename << ":" << line << "] ";
	}

	oss << message << "\n";

	// Output to stderr for warning and above, stdout otherwise
	if (level >= kcenon::common::interfaces::log_level::warning)
	{
		std::cerr << oss.str();
	}
	else
	{
		std::cout << oss.str();
	}
}

std::string console_logger::get_timestamp() const
{
	auto now = std::chrono::system_clock::now();
	auto time_t_now = std::chrono::system_clock::to_time_t(now);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				  now.time_since_epoch())
			  % 1000;

	std::ostringstream oss;
	oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
	oss << "." << std::setfill('0') << std::setw(3) << ms.count();
	return oss.str();
}

std::shared_ptr<kcenon::common::interfaces::ILogger> create_console_logger(
	kcenon::common::interfaces::log_level min_level)
{
	return std::make_shared<console_logger>(min_level);
}

} // namespace database_server::logging
