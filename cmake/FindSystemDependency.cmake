# FindSystemDependency.cmake
# Reusable CMake module for finding system dependencies
#
# Usage:
#   include(FindSystemDependency)
#   find_system_dependency(thread_system)
#
# This will search for the dependency in:
# 1. Environment variable ${NAME}_ROOT
# 2. Standard source locations (macOS, Linux, Windows)
# 3. Relative paths (sibling directories)
# 4. CMake CONFIG mode (installed packages, vcpkg)
#
# Sets:
#   ${NAME}_FOUND - TRUE if dependency was found
#   ${NAME}_DIR - Build directory path (for CONFIG mode)

function(find_system_dependency NAME)
    # Check if already found
    if(${NAME}_FOUND)
        return()
    endif()

    # First check for existing CMake targets (super-build case)
    # Map system names to possible target names
    set(POSSIBLE_TARGETS ${NAME})

    if(${NAME} STREQUAL "thread_system")
        list(APPEND POSSIBLE_TARGETS ThreadSystem utilities thread_system)
    elseif(${NAME} STREQUAL "logger_system")
        list(APPEND POSSIBLE_TARGETS LoggerSystem logger logger_system)
    elseif(${NAME} STREQUAL "container_system")
        list(APPEND POSSIBLE_TARGETS ContainerSystem::container container_system container)
    elseif(${NAME} STREQUAL "monitoring_system")
        list(APPEND POSSIBLE_TARGETS monitoring_system MonitoringSystem)
    elseif(${NAME} STREQUAL "network_system")
        list(APPEND POSSIBLE_TARGETS NetworkSystem network_system network)
    elseif(${NAME} STREQUAL "database_system")
        list(APPEND POSSIBLE_TARGETS database_system DatabaseSystem database)
    elseif(${NAME} STREQUAL "common_system")
        list(APPEND POSSIBLE_TARGETS common_system CommonSystem common)
    endif()

    # Check for existing targets
    foreach(target ${POSSIBLE_TARGETS})
        if(TARGET ${target})
            message(STATUS "Found ${NAME} as existing target: ${target}")
            set(${NAME}_FOUND TRUE PARENT_SCOPE)
            set(${NAME}_TARGET ${target} PARENT_SCOPE)
            return()
        endif()
    endforeach()

    # Build list of search paths
    set(_SEARCH_PATHS)

    # 1. Environment variable (highest priority)
    if(DEFINED ENV{${NAME}_ROOT})
        list(APPEND _SEARCH_PATHS "$ENV{${NAME}_ROOT}")
    endif()

    # 2. Standard paths (no hardcoded user directories)
    # Workspace-relative path first (for CI environments where dependencies are in workspace root)
    list(APPEND _SEARCH_PATHS
        "${CMAKE_SOURCE_DIR}/${NAME}"
        "${CMAKE_SOURCE_DIR}/../${NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../${NAME}"
    )

    # Try to find source directory
    foreach(_path ${_SEARCH_PATHS})
        if(EXISTS "${_path}/CMakeLists.txt")
            message(STATUS "Found ${NAME} source at: ${_path}")
            set(${NAME}_DIR "${_path}/build" CACHE PATH "${NAME} build directory" FORCE)
            set(${NAME}_FOUND TRUE PARENT_SCOPE)
            set(${NAME}_SOURCE_DIR "${_path}" CACHE PATH "${NAME} source directory" FORCE)
            return()
        endif()
    endforeach()

    # Try CONFIG mode if not found in source paths
    find_package(${NAME} CONFIG QUIET)
    if(${NAME}_FOUND)
        message(STATUS "Found ${NAME} via CONFIG mode")
        set(${NAME}_FOUND TRUE PARENT_SCOPE)
        return()
    endif()

    # Not found
    message(STATUS "${NAME} not found")
    set(${NAME}_FOUND FALSE PARENT_SCOPE)
endfunction()

# Helper function to find system dependency include paths
function(find_system_dependency_include NAME)
    # Build list of include search paths
    set(_INCLUDE_PATHS)

    # 1. Environment variable (highest priority)
    if(DEFINED ENV{${NAME}_ROOT})
        list(APPEND _INCLUDE_PATHS "$ENV{${NAME}_ROOT}/include")
        list(APPEND _INCLUDE_PATHS "$ENV{${NAME}_ROOT}")
    endif()

    # 2. Workspace-relative paths FIRST (for CI and local development)
    # These take priority over installed paths to use source headers
    list(APPEND _INCLUDE_PATHS
        "${CMAKE_SOURCE_DIR}/${NAME}/include"
        "${CMAKE_SOURCE_DIR}/../${NAME}/include"
        "${CMAKE_SOURCE_DIR}/${NAME}"
        "${CMAKE_SOURCE_DIR}/../${NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../${NAME}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../${NAME}"
    )

    # 3. CMAKE_PREFIX_PATH paths (for Windows CI environments)
    foreach(_prefix ${CMAKE_PREFIX_PATH})
        list(APPEND _INCLUDE_PATHS "${_prefix}/include")
    endforeach()

    # 4. Standard installed paths (fallback for CI when deps are installed)
    list(APPEND _INCLUDE_PATHS
        "/usr/local/include"
        "/opt/homebrew/include"
    )

    # Try to find include directory
    foreach(_path ${_INCLUDE_PATHS})
        if(EXISTS "${_path}")
            set(${NAME}_INCLUDE_DIR "${_path}" PARENT_SCOPE)
            message(STATUS "Found ${NAME} include directory: ${_path}")
            return()
        endif()
    endforeach()

    # Not found
    set(${NAME}_INCLUDE_DIR "" PARENT_SCOPE)
endfunction()

# Helper function to find system dependency libraries
function(find_system_dependency_library NAME)
    # Build list of library search paths
    set(_LIB_PATHS)

    # 1. Environment variable
    if(DEFINED ENV{${NAME}_ROOT})
        list(APPEND _LIB_PATHS "$ENV{${NAME}_ROOT}/build/lib")
        list(APPEND _LIB_PATHS "$ENV{${NAME}_ROOT}/build")
    endif()

    # 2. CMAKE_PREFIX_PATH paths (for Windows CI environments)
    foreach(_prefix ${CMAKE_PREFIX_PATH})
        list(APPEND _LIB_PATHS "${_prefix}/lib")
    endforeach()

    # 3. Standard installed paths (for CI environments where dependencies are installed)
    list(APPEND _LIB_PATHS
        "/usr/local/lib"
        "/usr/lib"
        "/opt/homebrew/lib"
    )

    # 4. Workspace-relative paths (for CI environments where dependencies are in workspace root)
    list(APPEND _LIB_PATHS
        "${CMAKE_SOURCE_DIR}/${NAME}/build/lib"
        "${CMAKE_SOURCE_DIR}/../${NAME}/build/lib"
        "${CMAKE_SOURCE_DIR}/${NAME}/build"
        "${CMAKE_SOURCE_DIR}/../${NAME}/build"
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}/build/lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../${NAME}/build/lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/../${NAME}/build"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../${NAME}/build"
    )

    # Try to find library directory
    foreach(_path ${_LIB_PATHS})
        if(EXISTS "${_path}")
            set(${NAME}_LIBRARY_DIR "${_path}" PARENT_SCOPE)
            message(STATUS "Found ${NAME} library directory: ${_path}")
            return()
        endif()
    endforeach()

    # Not found
    set(${NAME}_LIBRARY_DIR "" PARENT_SCOPE)
endfunction()
