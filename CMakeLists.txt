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
#     ├── network_system (Tier 4) [REQUIRED] - TCP/QUIC server
#     ├── monitoring_system (Tier 3) [OPTIONAL] - Metrics
#     └── container_system (Tier 1) [OPTIONAL] - Serialization
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
option(BUILD_WITH_NETWORK_SYSTEM "Build with network_system integration (REQUIRED)" ON)

# Optional dependencies
option(BUILD_WITH_MONITORING_SYSTEM "Build with monitoring_system integration (OPTIONAL)" OFF)
option(BUILD_WITH_CONTAINER_SYSTEM "Build with container_system integration (OPTIONAL)" ON)

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

# container_system (OPTIONAL)
if(BUILD_WITH_CONTAINER_SYSTEM)
    message(STATUS "Searching for container_system...")
    find_system_dependency(container_system)

    if(container_system_FOUND)
        message(STATUS "container_system integration enabled")
        find_system_dependency_include(container_system)
    else()
        message(WARNING "container_system not found - integration disabled.")
        set(BUILD_WITH_CONTAINER_SYSTEM OFF CACHE BOOL "container_system not found" FORCE)
    endif()
endif()

##################################################
# Create Main Library
##################################################

add_library(DatabaseServerLib
    src/core/server_app.cpp
    src/core/server_config.cpp
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

# Add common_system include and link
if(common_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${common_system_INCLUDE_DIR})
endif()
if(TARGET common_system)
    target_link_libraries(DatabaseServerLib PUBLIC common_system)
endif()

# Add thread_system include and link
if(thread_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${thread_system_INCLUDE_DIR})
endif()
if(TARGET ThreadSystem)
    target_link_libraries(DatabaseServerLib PUBLIC ThreadSystem)
elseif(TARGET thread_system)
    target_link_libraries(DatabaseServerLib PUBLIC thread_system)
endif()

# Add network_system include and link
if(network_system_INCLUDE_DIR)
    target_include_directories(DatabaseServerLib PUBLIC ${network_system_INCLUDE_DIR})
endif()
if(TARGET NetworkSystem)
    target_link_libraries(DatabaseServerLib PUBLIC NetworkSystem)
elseif(TARGET network_system)
    target_link_libraries(DatabaseServerLib PUBLIC network_system)
endif()

# Optional: monitoring_system
if(BUILD_WITH_MONITORING_SYSTEM)
    target_compile_definitions(DatabaseServerLib PUBLIC BUILD_WITH_MONITORING_SYSTEM)
    if(monitoring_system_INCLUDE_DIR)
        target_include_directories(DatabaseServerLib PUBLIC ${monitoring_system_INCLUDE_DIR})
    endif()
    if(TARGET monitoring_system)
        target_link_libraries(DatabaseServerLib PUBLIC monitoring_system)
    endif()
endif()

# Optional: container_system
if(BUILD_WITH_CONTAINER_SYSTEM)
    target_compile_definitions(DatabaseServerLib PUBLIC BUILD_WITH_CONTAINER_SYSTEM)
    if(container_system_INCLUDE_DIR)
        target_include_directories(DatabaseServerLib PUBLIC ${container_system_INCLUDE_DIR})
    endif()
    if(TARGET ContainerSystem::container)
        target_link_libraries(DatabaseServerLib PUBLIC ContainerSystem::container)
    elseif(TARGET container_system)
        target_link_libraries(DatabaseServerLib PUBLIC container_system)
    endif()
endif()

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
    # Tests will be added in future phases
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
message(STATUS "    ├── network_system (Tier 4): ${network_system_FOUND} [REQUIRED]")
message(STATUS "    ├── monitoring_system (Tier 3): ${BUILD_WITH_MONITORING_SYSTEM} [OPTIONAL]")
message(STATUS "    └── container_system (Tier 1): ${BUILD_WITH_CONTAINER_SYSTEM} [OPTIONAL]")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  Samples: ${BUILD_SAMPLES}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
message(STATUS "")
message(STATUS "Output Directories:")
message(STATUS "  Runtime: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Library: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Archive: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
