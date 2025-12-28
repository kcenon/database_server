cmake_minimum_required(VERSION 3.16)

# Set policy for FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

##################################################
# Database Server CMakeLists.txt
#
# Gateway Middleware for Database Access
#
# Dependency Chain:
#   database_server
#     ├── common_system (Tier 0) [REQUIRED] - ILogger, Result<T>
#     ├── thread_system (Tier 1) [REQUIRED] - Job scheduling
#     ├── database_system (Tier 2) [REQUIRED] - Database interfaces
#     ├── network_system (Tier 4) [REQUIRED] - TCP/QUIC server
#     ├── monitoring_system (Tier 3) [OPTIONAL] - Metrics
#     └── container_system (Tier 1) [REQUIRED] - Protocol serialization
##################################################

project(database_server
    VERSION 0.1.0.0
    DESCRIPTION "Database Gateway Middleware Server"
    LANGUAGES CXX
)

##################################################
# C++ Standard
##################################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

##################################################
# Options
##################################################

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_SAMPLES "Build samples" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Required dependencies
option(BUILD_WITH_COMMON_SYSTEM "Build with common_system integration (REQUIRED)" ON)
option(BUILD_WITH_THREAD_SYSTEM "Build with thread_system integration (REQUIRED)" ON)
option(BUILD_WITH_DATABASE_SYSTEM "Build with database_system integration (REQUIRED)" ON)
option(BUILD_WITH_NETWORK_SYSTEM "Build with network_system integration (REQUIRED)" ON)

# Optional dependencies
option(BUILD_WITH_MONITORING_SYSTEM "Build with monitoring_system integration (OPTIONAL)" OFF)

# Required for protocol serialization
option(BUILD_WITH_CONTAINER_SYSTEM "Build with container_system integration (REQUIRED)" ON)

##################################################
# Global Configuration
##################################################

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")

# Platform-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10
elseif(APPLE)
    add_definitions(-DAPPLE_PLATFORM)
endif()

# Coverage settings
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -latomic")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -latomic")
        endif()
    endif()
endif()

##################################################
# Include CMake Modules
##################################################

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindSystemDependency)

##################################################
# Find Required Packages
##################################################

find_package(Threads REQUIRED)

# Add standard library paths for installed dependencies (CI environment)
# This ensures find_library can locate libraries installed to /usr/local
if(UNIX)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local")
    link_directories("/usr/local/lib")

    # Also add standard include paths for installed headers
    include_directories("/usr/local/include")
endif()

# Windows: Use CMAKE_PREFIX_PATH for library search (set by CI via -DCMAKE_PREFIX_PATH)
if(WIN32 AND CMAKE_PREFIX_PATH)
    foreach(_prefix ${CMAKE_PREFIX_PATH})
        if(EXISTS "${_prefix}/lib")
            link_directories("${_prefix}/lib")
            message(STATUS "Added Windows library path: ${_prefix}/lib")
        endif()
        if(EXISTS "${_prefix}/include")
            include_directories("${_prefix}/include")
            message(STATUS "Added Windows include path: ${_prefix}/include")
        endif()
    endforeach()
endif()

##################################################
# Dependency Detection
##################################################

# common_system (REQUIRED)
message(STATUS "Searching for common_system...")
find_system_dependency(common_system)

if(NOT common_system_FOUND AND TARGET common_system)
    set(common_system_FOUND TRUE)
endif()

if(common_system_FOUND)
    message(STATUS "common_system integration enabled")
    find_system_dependency_include(common_system)
else()
    message(FATAL_ERROR "common_system not found! database_server requires common_system.")
endif()

# thread_system (REQUIRED)
message(STATUS "Searching for thread_system...")
find_system_dependency(thread_system)

if(NOT thread_system_FOUND AND TARGET ThreadSystem)
    set(thread_system_FOUND TRUE)
endif()

if(thread_system_FOUND)
    message(STATUS "thread_system integration enabled")
    find_system_dependency_include(thread_system)
else()
    message(FATAL_ERROR "thread_system not found! database_server requires thread_system.")
endif()

# database_system (REQUIRED)
message(STATUS "Searching for database_system...")
find_system_dependency(database_system)

if(NOT database_system_FOUND AND TARGET DatabaseSystem)
    set(database_system_FOUND TRUE)
endif()

if(database_system_FOUND)
    message(STATUS "database_system integration enabled")
    find_system_dependency_include(database_system)
else()
    message(FATAL_ERROR "database_system not found! database_server requires database_system.")
endif()

# network_system (REQUIRED)
message(STATUS "Searching for network_system...")
find_system_dependency(network_system)

if(NOT network_system_FOUND AND TARGET NetworkSystem)
    set(network_system_FOUND TRUE)
endif()

if(network_system_FOUND)
    message(STATUS "network_system integration enabled")
    find_system_dependency_include(network_system)
else()
    message(FATAL_ERROR "network_system not found! database_server requires network_system.")
endif()

# Find ASIO for network_system dependency
# Check for standalone ASIO (required by network_system)
if(NOT ASIO_INCLUDE_DIR)
    find_path(ASIO_INCLUDE_DIR asio.hpp
        PATHS
            /opt/homebrew/include
            /usr/local/include
            /usr/include
            $ENV{ASIO_ROOT}
    )
endif()

if(ASIO_INCLUDE_DIR)
    message(STATUS "Found ASIO at: ${ASIO_INCLUDE_DIR}")
else()
    message(WARNING "ASIO not found - network features may not compile correctly")
endif()

# monitoring_system (OPTIONAL)
if(BUILD_WITH_MONITORING_SYSTEM)
    message(STATUS "Searching for monitoring_system...")
    find_system_dependency(monitoring_system)

    if(monitoring_system_FOUND)
        message(STATUS "monitoring_system integration enabled")
        find_system_dependency_include(monitoring_system)
    else()
        message(WARNING "monitoring_system not found - integration disabled.")
        set(BUILD_WITH_MONITORING_SYSTEM OFF CACHE BOOL "monitoring_system not found" FORCE)
    endif()
endif()

# container_system (REQUIRED for protocol serialization)
# Without container_system, the server cannot serialize/deserialize query protocol messages
message(STATUS "Searching for container_system...")
find_system_dependency(container_system)

if(NOT container_system_FOUND AND TARGET container_system)
    set(container_system_FOUND TRUE)
endif()

if(container_system_FOUND)
    message(STATUS "container_system integration enabled")
    find_system_dependency_include(container_system)
else()
    message(FATAL_ERROR
        "container_system not found! database_server requires container_system for protocol serialization.\n"
        "Without container_system, the server cannot send or receive query protocol messages.\n"
        "Please ensure container_system is built and available.")
endif()

##################################################
# Create Main Library
##################################################

add_library(DatabaseServerLib
    # Core
    src/core/server_app.cpp
    src/core/server_config.cpp
    # Pooling (Phase 2)
    src/pooling/connection_pool.cpp
    # Resilience (Phase 2)
    src/resilience/connection_health_monitor.cpp
    src/resilience/resilient_database_connection.cpp
    # Gateway (Phase 3)
    src/gateway/query_protocol.cpp
    src/gateway/gateway_server.cpp
    src/gateway/query_router.cpp
    src/gateway/auth_middleware.cpp
)

set_target_properties(DatabaseServerLib PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "database_server"
)

# Include directories
target_include_directories(DatabaseServerLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link required dependencies
target_link_libraries(DatabaseServerLib
    PUBLIC
        Threads::Threads
)

##################################################
# Helper function for robust library linking
# This function handles both CMake target and installed library scenarios
##################################################
function(link_system_dependency TARGET_NAME DEP_NAME)
    set(TARGET_NAMES ${ARGN})
    set(LINKED FALSE)

    # First try CMake targets
    foreach(target_name ${TARGET_NAMES})
        if(TARGET ${target_name})
            target_link_libraries(${TARGET_NAME} PUBLIC ${target_name})
            message(STATUS "Linking ${DEP_NAME} via CMake target: ${target_name}")
            set(LINKED TRUE)
            break()
        endif()
    endforeach()

    # If no target found, search in sibling project build directories first
    if(NOT LINKED)
        # Convert DEP_NAME to lowercase for directory search
        string(TOLOWER ${DEP_NAME} DEP_NAME_LOWER)
        string(REPLACE "_" "_" DEP_DIR_NAME ${DEP_NAME_LOWER})

        # Build list of sibling project paths
        set(SIBLING_PATHS
            "${CMAKE_SOURCE_DIR}/../${DEP_DIR_NAME}/build/lib"
            "${CMAKE_SOURCE_DIR}/../${DEP_DIR_NAME}/build"
            "${CMAKE_SOURCE_DIR}/${DEP_DIR_NAME}/build/lib"
            "${CMAKE_SOURCE_DIR}/${DEP_DIR_NAME}/build"
        )

        # Add CMAKE_PREFIX_PATH paths for Windows CI
        set(PREFIX_LIB_PATHS)
        foreach(_prefix ${CMAKE_PREFIX_PATH})
            list(APPEND PREFIX_LIB_PATHS "${_prefix}/lib")
        endforeach()

        find_library(${DEP_NAME}_LIB
            NAMES ${TARGET_NAMES}
            PATHS
                ${PREFIX_LIB_PATHS}
                ${SIBLING_PATHS}
                /usr/local/lib
                /usr/lib
                /opt/homebrew/lib
            NO_DEFAULT_PATH
            NO_CMAKE_SYSTEM_PATH
        )

        # Exclude macOS system frameworks (avoid matching Network.framework etc.)
        if(${DEP_NAME}_LIB)
            string(FIND "${${DEP_NAME}_LIB}" ".framework" FRAMEWORK_IDX)
            string(FIND "${${DEP_NAME}_LIB}" "/System/" SYSTEM_IDX)
            string(FIND "${${DEP_NAME}_LIB}" "Xcode.app" XCODE_IDX)
            if(FRAMEWORK_IDX GREATER -1 OR SYSTEM_IDX GREATER -1 OR XCODE_IDX GREATER -1)
                message(STATUS "Skipping system library: ${${DEP_NAME}_LIB}")
                unset(${DEP_NAME}_LIB CACHE)
            endif()
        endif()

        if(${DEP_NAME}_LIB)
            # MSVC requires /WHOLEARCHIVE for network_system to include all symbols
            # This is needed because messaging_server symbols are not referenced directly
            # WHOLEARCHIVE also brings in HTTP/2, WebSocket code that depends on OpenSSL
            if(MSVC AND "${DEP_NAME}" STREQUAL "NETWORK_SYSTEM")
                target_link_options(${TARGET_NAME} PUBLIC "/WHOLEARCHIVE:${${DEP_NAME}_LIB}")
                message(STATUS "Linking ${DEP_NAME} with WHOLEARCHIVE from: ${${DEP_NAME}_LIB}")
                # Link OpenSSL for HTTP/2, WebSocket, and QUIC support in network_system
                find_package(OpenSSL QUIET)
                if(OPENSSL_FOUND)
                    target_link_libraries(${TARGET_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
                    message(STATUS "Linking OpenSSL for WHOLEARCHIVE network_system support")
                endif()
            else()
                target_link_libraries(${TARGET_NAME} PUBLIC ${${DEP_NAME}_LIB})
                message(STATUS "Linking ${DEP_NAME} from: ${${DEP_NAME}_LIB}")
            endif()
            set(LINKED TRUE)
        else()
            message(WARNING "${DEP_NAME} library not found - linking may fail. Searched: ${TARGET_NAMES}")
        endif()
    endif()
endfunction()

# Add common_system include (header-only library, no linking needed)
if(common_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${common_system_INCLUDE_DIR})
    # Define WITH_COMMON_SYSTEM for feature_flags.h (maps to KCENON_WITH_COMMON_SYSTEM)
    # This ensures VoidResult type matches between network_system and database_server
    target_compile_definitions(DatabaseServerLib PUBLIC WITH_COMMON_SYSTEM)
endif()
# common_system is header-only, so no library linking is needed

# Add thread_system include
if(thread_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${thread_system_INCLUDE_DIR})
endif()

# Add database_system include
if(database_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${database_system_INCLUDE_DIR})
    # Also add the root directory for headers in database_system/database/
    get_filename_component(_database_system_root "${database_system_INCLUDE_DIR}" DIRECTORY)
    if(EXISTS "${_database_system_root}/database")
        target_include_directories(DatabaseServerLib PUBLIC ${_database_system_root})
    endif()
endif()

# Add network_system include
if(network_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${network_system_INCLUDE_DIR})
endif()

##################################################
# Link Libraries (Order matters for static linking!)
#
# For static libraries, the linker resolves symbols in order.
# Libraries that DEPEND on others must come BEFORE the libraries they depend on.
#
# Dependency chain:
#   DatabaseServerLib -> network_system -> thread_system
#   DatabaseServerLib -> database_system -> thread_system (optional)
#   All systems -> logger_system
#
# Therefore, correct order is:
#   1. network_system (depends on thread_system)
#   2. database_system
#   3. thread_system (depended upon by network_system)
#   4. logger_system (depended upon by all)
##################################################

# 1. Link network_system first (it depends on thread_system)
link_system_dependency(DatabaseServerLib NETWORK_SYSTEM NetworkSystem network_system messaging_system network)

# 2. Link database_system
link_system_dependency(DatabaseServerLib DATABASE_SYSTEM DatabaseSystem database_system database)

# 3. Link thread_system (network_system depends on it, so it must come after)
link_system_dependency(DatabaseServerLib THREAD_SYSTEM ThreadSystem thread_system utilities)

# 4. Link logger_system as transitive dependency (required by network_system and database_system)
# Build CMAKE_PREFIX_PATH lib paths for Windows CI
set(LOGGER_PREFIX_LIB_PATHS)
foreach(_prefix ${CMAKE_PREFIX_PATH})
    list(APPEND LOGGER_PREFIX_LIB_PATHS "${_prefix}/lib")
endforeach()

find_library(LOGGER_SYSTEM_LIB
    NAMES LoggerSystem logger_system logger
    PATHS
        ${LOGGER_PREFIX_LIB_PATHS}
        "${CMAKE_SOURCE_DIR}/../logger_system/build/lib"
        "${CMAKE_SOURCE_DIR}/../logger_system/build"
        "${CMAKE_SOURCE_DIR}/logger_system/build/lib"
        "${CMAKE_SOURCE_DIR}/logger_system/build"
        /usr/local/lib
        /usr/lib
        /opt/homebrew/lib
)
if(LOGGER_SYSTEM_LIB)
    target_link_libraries(DatabaseServerLib PUBLIC ${LOGGER_SYSTEM_LIB})
    message(STATUS "Linking logger_system from: ${LOGGER_SYSTEM_LIB}")
endif()

# Add ASIO include directory (required for network_system headers)
if(ASIO_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${ASIO_INCLUDE_DIR})
    target_compile_definitions(DatabaseServerLib PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)
endif()

# Optional: monitoring_system
if(BUILD_WITH_MONITORING_SYSTEM)
    # Define WITH_MONITORING_SYSTEM for feature_flags.h (maps to KCENON_WITH_MONITORING_SYSTEM)
    target_compile_definitions(DatabaseServerLib PUBLIC WITH_MONITORING_SYSTEM)
    if(monitoring_system_INCLUDE_DIR)
        target_include_directories(DatabaseServerLib PUBLIC ${monitoring_system_INCLUDE_DIR})
    endif()
    if(TARGET monitoring_system)
        target_link_libraries(DatabaseServerLib PUBLIC monitoring_system)
    endif()
endif()

# container_system (REQUIRED for protocol serialization)
# Define WITH_CONTAINER_SYSTEM for feature_flags.h (maps to KCENON_WITH_CONTAINER_SYSTEM)
target_compile_definitions(DatabaseServerLib PUBLIC WITH_CONTAINER_SYSTEM)
if(container_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${container_system_INCLUDE_DIR})
    # Also add the root directory for headers in container_system/core/
    get_filename_component(_container_system_root "${container_system_INCLUDE_DIR}" DIRECTORY)
    if(EXISTS "${_container_system_root}/core")
        target_include_directories(DatabaseServerLib PUBLIC ${_container_system_root})
    endif()
endif()
link_system_dependency(DatabaseServerLib CONTAINER_SYSTEM container_system ContainerSystem container)

##################################################
# Create Executable
##################################################

add_executable(database_server
    src/main.cpp
)

target_link_libraries(database_server
    PRIVATE
        DatabaseServerLib
)

set_target_properties(database_server PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Tests
##################################################

if(BUILD_TESTS)
    enable_testing()
    message(STATUS "Building database_server tests")
    add_subdirectory(tests)
endif()

##################################################
# Benchmarks
##################################################

option(BUILD_BENCHMARKS "Build performance benchmarks" OFF)

if(BUILD_BENCHMARKS)
    message(STATUS "Building database_server benchmarks")
    add_subdirectory(benchmarks)
endif()

##################################################
# Installation
##################################################

include(GNUInstallDirs)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install library
install(TARGETS DatabaseServerLib
    EXPORT DatabaseServerTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install executable
install(TARGETS database_server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

##################################################
# Build Summary
##################################################

message(STATUS "")
message(STATUS "========================================")
message(STATUS "Database Server Build Configuration:")
message(STATUS "========================================")
message(STATUS "")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Dependency Chain:")
message(STATUS "  database_server")
message(STATUS "    ├── common_system (Tier 0): ${common_system_FOUND} [REQUIRED]")
message(STATUS "    ├── thread_system (Tier 1): ${thread_system_FOUND} [REQUIRED]")
message(STATUS "    ├── database_system (Tier 2): ${database_system_FOUND} [REQUIRED]")
message(STATUS "    ├── network_system (Tier 4): ${network_system_FOUND} [REQUIRED]")
message(STATUS "    ├── monitoring_system (Tier 3): ${BUILD_WITH_MONITORING_SYSTEM} [OPTIONAL]")
message(STATUS "    └── container_system (Tier 1): ${container_system_FOUND} [REQUIRED]")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  Samples: ${BUILD_SAMPLES}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
message(STATUS "")
message(STATUS "Output Directories:")
message(STATUS "  Runtime: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Library: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Archive: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
