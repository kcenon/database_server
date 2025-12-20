##################################################
# Database Server Benchmarks Configuration
##################################################

# Find benchmark library
find_package(benchmark QUIET)

if(NOT benchmark_FOUND)
    message(WARNING "Google Benchmark not found - benchmarks will not be built")
    return()
endif()

##################################################
# Gateway Benchmarks
##################################################

add_executable(gateway_benchmarks
    gateway_benchmarks.cpp
)

target_link_libraries(gateway_benchmarks
    PRIVATE
        DatabaseServerLib
        benchmark::benchmark
        benchmark::benchmark_main
        Threads::Threads
)

set_target_properties(gateway_benchmarks PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install benchmarks
install(TARGETS gateway_benchmarks
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "Gateway benchmarks configured (Phase 3.5)")
