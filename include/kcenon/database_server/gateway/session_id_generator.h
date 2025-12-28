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
 * @file session_id_generator.h
 * @brief Cryptographically secure session ID generation
 *
 * Provides functions for generating unpredictable session identifiers
 * using hardware random device seeding.
 *
 * Security Properties:
 * - 128 bits of entropy per session ID
 * - Uses std::random_device for hardware entropy
 * - Thread-safe with thread-local RNG state
 * - Collision probability < 2^-64
 */

#pragma once

#include <string>

namespace database_server::gateway
{

/**
 * @brief Generate a cryptographically secure session ID
 *
 * Creates a 32-character hexadecimal string representing 128 bits
 * of pseudo-random data seeded from hardware entropy.
 *
 * Thread Safety:
 * - This function is thread-safe
 * - Uses thread-local RNG for optimal performance
 *
 * Performance:
 * - Expected generation time: < 1μs
 * - No locking required between threads
 *
 * @return A 32-character lowercase hexadecimal string
 *
 * Example:
 * @code
 * auto session_id = generate_session_id();
 * // session_id == "a1b2c3d4e5f6789012345678abcdef01"
 * @endcode
 */
[[nodiscard]] std::string generate_session_id();

} // namespace database_server::gateway
