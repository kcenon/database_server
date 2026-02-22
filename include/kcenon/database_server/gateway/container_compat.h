// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file container_compat.h
 * @brief Compatibility header for container_system includes
 *
 * Centralizes all container_system include paths to prepare for the
 * upcoming namespace migration from container_module to kcenon::container.
 *
 * Migration timeline (container_system #364):
 * - v1.x (current): Legacy paths (<container.h>, container_module::)
 * - v2.0: Deprecated aliases added
 * - v3.0: Legacy paths removed — only this file needs updating
 *
 * When v3.0 lands, update the include path and forward declaration below.
 *
 * ## Thread Safety
 * This is a header-only include shim with no runtime state.
 * Thread safety depends on the included container_system headers.
 */

#pragma once

#if KCENON_WITH_CONTAINER_SYSTEM

#if __has_include(<kcenon/container/container.h>)
#include <kcenon/container/container.h>
#else
#include <container.h>
#endif

#endif // KCENON_WITH_CONTAINER_SYSTEM
