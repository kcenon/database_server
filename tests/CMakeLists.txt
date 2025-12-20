##################################################
# Database Server Tests Configuration
##################################################

# Enable testing
enable_testing()

# Find test dependencies
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    find_package(gtest QUIET)
endif()

##################################################
# Gateway Library (Standalone for testing)
##################################################

# Create a standalone gateway library for testing without full DatabaseServerLib
add_library(GatewayLib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gateway/query_protocol.cpp
)

target_include_directories(GatewayLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link common_system for Result<T>
if(common_system_INCLUDE_DIR)
    target_include_directories(GatewayLib PUBLIC ${common_system_INCLUDE_DIR})
endif()
if(TARGET common_system)
    target_link_libraries(GatewayLib PUBLIC common_system)
endif()

# Link container_system if available
if(BUILD_WITH_CONTAINER_SYSTEM)
    target_compile_definitions(GatewayLib PUBLIC BUILD_WITH_CONTAINER_SYSTEM)
    # Try different include paths for container_system
    if(container_system_INCLUDE_DIR)
        target_include_directories(GatewayLib PUBLIC ${container_system_INCLUDE_DIR})
    endif()
    if(container_system_SOURCE_DIR)
        target_include_directories(GatewayLib PUBLIC ${container_system_SOURCE_DIR})
    endif()
    # Fallback to relative path
    target_include_directories(GatewayLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../container_system)

    # Link container_system library
    if(TARGET ContainerSystem::container)
        target_link_libraries(GatewayLib PUBLIC ContainerSystem::container)
    elseif(TARGET container_system)
        target_link_libraries(GatewayLib PUBLIC container_system)
    else()
        # Fallback: link directly to static library
        # Build CMAKE_PREFIX_PATH lib paths for Windows CI
        set(CONTAINER_PREFIX_LIB_PATHS)
        foreach(_prefix ${CMAKE_PREFIX_PATH})
            list(APPEND CONTAINER_PREFIX_LIB_PATHS "${_prefix}/lib")
        endforeach()

        find_library(CONTAINER_SYSTEM_LIB
            NAMES container_system ContainerSystem
            PATHS
                ${CONTAINER_PREFIX_LIB_PATHS}
                ${container_system_SOURCE_DIR}/build/lib
                ${CMAKE_CURRENT_SOURCE_DIR}/../../container_system/build/lib
                /usr/local/lib
            NO_DEFAULT_PATH
        )
        if(CONTAINER_SYSTEM_LIB)
            target_link_libraries(GatewayLib PUBLIC ${CONTAINER_SYSTEM_LIB})
            message(STATUS "Linked container_system library: ${CONTAINER_SYSTEM_LIB}")
        else()
            message(WARNING "container_system library not found")
        endif()
    endif()
endif()

set_target_properties(GatewayLib PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

##################################################
# Query Protocol Unit Tests (Phase 3.1)
##################################################

if(GTest_FOUND OR gtest_FOUND)
    # Create unit test executable
    add_executable(query_protocol_test
        query_protocol_test.cpp
    )

    # Link dependencies - handle different target names
    if(GTest_FOUND)
        target_link_libraries(query_protocol_test PRIVATE
            GatewayLib
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(query_protocol_test PRIVATE
            GatewayLib
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    # Set properties
    set_target_properties(query_protocol_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Add test
    add_test(NAME QueryProtocolTests COMMAND query_protocol_test)

    # Enable test discovery
    include(GoogleTest)

    # Sanitizer builds need longer timeout
    if(CMAKE_CXX_FLAGS MATCHES "fsanitize")
        set(TEST_TIMEOUT 300)
    else()
        set(TEST_TIMEOUT 60)
    endif()

    gtest_discover_tests(query_protocol_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Query protocol tests configured (Phase 3.1)")
else()
    message(WARNING "GTest not found - query protocol tests will not be built")
endif()
