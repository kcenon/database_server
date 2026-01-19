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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gateway/protocol/header_serializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gateway/protocol/auth_serializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gateway/protocol/param_serializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gateway/protocol/request_serializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gateway/protocol/response_serializer.cpp
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
    # Define KCENON_WITH_CONTAINER_SYSTEM=1 for unified macro system
    target_compile_definitions(GatewayLib PUBLIC KCENON_WITH_CONTAINER_SYSTEM=1)
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

    ##################################################
    # Session ID Security Tests
    ##################################################

    # Create session ID test executable
    add_executable(session_id_test
        session_id_test.cpp
    )

    # Link dependencies
    target_link_libraries(session_id_test PRIVATE
        DatabaseServerLib
    )

    # Link GTest - handle different target names
    if(GTest_FOUND)
        target_link_libraries(session_id_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(session_id_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    # Set properties
    set_target_properties(session_id_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Add test
    add_test(NAME SessionIdTests COMMAND session_id_test)

    gtest_discover_tests(session_id_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Session ID security tests configured")

    ##################################################
    # Resilience Unit Tests (Phase 2)
    ##################################################

    # Create resilience test executable
    add_executable(resilience_test
        resilience_test.cpp
    )

    # Link dependencies
    target_link_libraries(resilience_test PRIVATE
        DatabaseServerLib
    )

    # Link GTest - handle different target names
    if(GTest_FOUND)
        target_link_libraries(resilience_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(resilience_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    # Set properties
    set_target_properties(resilience_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Add test
    add_test(NAME ResilienceTests COMMAND resilience_test)

    gtest_discover_tests(resilience_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Resilience tests configured (Phase 2)")

    ##################################################
    # Integration Tests (Phase 3.5)
    ##################################################

    # Create integration test executable
    add_executable(integration_test
        integration_test.cpp
    )

    # Integration tests need the full DatabaseServerLib
    target_link_libraries(integration_test PRIVATE
        DatabaseServerLib
    )

    # Link GTest - handle different target names
    if(GTest_FOUND)
        target_link_libraries(integration_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(integration_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    # Set properties
    set_target_properties(integration_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Add test
    add_test(NAME IntegrationTests COMMAND integration_test)

    # Longer timeout for integration tests
    if(CMAKE_CXX_FLAGS MATCHES "fsanitize")
        set(INTEGRATION_TEST_TIMEOUT 600)
    else()
        set(INTEGRATION_TEST_TIMEOUT 120)
    endif()

    gtest_discover_tests(integration_test
        PROPERTIES
            TIMEOUT ${INTEGRATION_TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 120
    )

    message(STATUS "Integration tests configured (Phase 3.5)")

    ##################################################
    # Rate Limiter Unit Tests
    ##################################################

    add_executable(rate_limiter_test
        rate_limiter_test.cpp
    )

    target_link_libraries(rate_limiter_test PRIVATE
        DatabaseServerLib
    )

    if(GTest_FOUND)
        target_link_libraries(rate_limiter_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(rate_limiter_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    set_target_properties(rate_limiter_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_test(NAME RateLimiterTests COMMAND rate_limiter_test)

    gtest_discover_tests(rate_limiter_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Rate limiter tests configured")

    ##################################################
    # Auth Middleware Unit Tests
    ##################################################

    add_executable(auth_middleware_test
        auth_middleware_test.cpp
    )

    target_link_libraries(auth_middleware_test PRIVATE
        DatabaseServerLib
    )

    if(GTest_FOUND)
        target_link_libraries(auth_middleware_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(auth_middleware_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    set_target_properties(auth_middleware_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_test(NAME AuthMiddlewareTests COMMAND auth_middleware_test)

    gtest_discover_tests(auth_middleware_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Auth middleware tests configured")

    ##################################################
    # Connection Pool Unit Tests
    ##################################################

    add_executable(connection_pool_test
        connection_pool_test.cpp
    )

    target_link_libraries(connection_pool_test PRIVATE
        DatabaseServerLib
    )

    if(GTest_FOUND)
        target_link_libraries(connection_pool_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(connection_pool_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    set_target_properties(connection_pool_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_test(NAME ConnectionPoolTests COMMAND connection_pool_test)

    gtest_discover_tests(connection_pool_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Connection pool tests configured")

    ##################################################
    # Query Cache Unit Tests
    ##################################################

    add_executable(query_cache_test
        query_cache_test.cpp
    )

    target_link_libraries(query_cache_test PRIVATE
        DatabaseServerLib
    )

    if(GTest_FOUND)
        target_link_libraries(query_cache_test PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
    else()
        target_link_libraries(query_cache_test PRIVATE
            gtest
            gtest_main
            Threads::Threads
        )
    endif()

    set_target_properties(query_cache_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_test(NAME QueryCacheTests COMMAND query_cache_test)

    gtest_discover_tests(query_cache_test
        PROPERTIES
            TIMEOUT ${TEST_TIMEOUT}
        DISCOVERY_TIMEOUT 60
    )

    message(STATUS "Query cache tests configured")

else()
    message(WARNING "GTest not found - tests will not be built")
endif()
