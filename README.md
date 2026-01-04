# Database Server

A high-performance database gateway middleware server that provides connection pooling, query routing, and caching capabilities.

## Overview

The Database Server is part of the kcenon unified system architecture evolution from a monolithic library to a gateway middleware architecture. It sits between client applications and physical databases, providing:

- **Connection Pooling**: Efficient connection management with priority-based scheduling
- **Query Routing**: Load balancing across multiple database connections
- **Health Monitoring**: Automatic detection and recovery of failed connections
- **Query Caching** (Optional): Result caching for frequently executed queries

## Architecture

```
┌─────────────────────────────────────────────────┐
│                database_server                   │
├─────────────────────────────────────────────────┤
│  ┌─────────────┐    ┌─────────────────────┐    │
│  │   Listener   │───▶│  Auth Middleware    │    │
│  │    (TCP)    │    │  (Rate Limiting)    │    │
│  └─────────────┘    └─────────┬───────────┘    │
│                               │                 │
│                    ┌──────────▼──────────┐     │
│                    │   Request Handler   │     │
│                    └──────────┬──────────┘     │
│                               │                 │
│                    ┌──────────▼──────────┐     │
│                    │   Query Router      │     │
│                    │   (Load Balancer)   │     │
│                    └──────────┬──────────┘     │
│                               │                 │
│  ┌─────────────┐    ┌────────▼────────┐       │
│  │ Query Cache │◀──▶│ Connection Pool │       │
│  │  (Optional) │    │                 │       │
│  └─────────────┘    └────────┬────────┘       │
│                               │                 │
└───────────────────────────────┼─────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │   Physical Database    │
                    │ (PostgreSQL, MySQL...) │
                    └───────────────────────┘
```

## Dependencies

### Required

- **common_system** (Tier 0) - Core utilities, Result<T>, logging interfaces
- **thread_system** (Tier 1) - Job scheduling, thread pool management
- **database_system** (Tier 2) - Database interfaces and types
- **network_system** (Tier 4) - TCP server implementation
- **container_system** (Tier 1) - Protocol serialization (required for query protocol messages)

### Optional

- **monitoring_system** (Tier 3) - Performance metrics and health monitoring

## Building

### Prerequisites

- CMake 3.16 or higher
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- Required system dependencies built and available

### Build Steps

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Run (optional)
./bin/database_server --help
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | ON | Build unit tests |
| `BUILD_BENCHMARKS` | OFF | Build performance benchmarks |
| `BUILD_SAMPLES` | OFF | Build sample applications |
| `BUILD_MODULES` | OFF | Build C++20 module library |
| `BUILD_WITH_MONITORING_SYSTEM` | OFF | Enable monitoring integration |
| `BUILD_WITH_CONTAINER_SYSTEM` | ON | Protocol serialization (required, build fails without it) |
| `ENABLE_COVERAGE` | OFF | Enable code coverage |

### Integration Macros

Source code uses `KCENON_WITH_*` macros (from `<kcenon/common/config/feature_flags.h>`) for integration gating:

| Macro | Preprocessor Symbol | CMake Option | Description |
|-------|---------------------|--------------|-------------|
| `KCENON_WITH_CONTAINER_SYSTEM` | `WITH_CONTAINER_SYSTEM` | `BUILD_WITH_CONTAINER_SYSTEM` | Container serialization support |
| `KCENON_WITH_MONITORING_SYSTEM` | `WITH_MONITORING_SYSTEM` | `BUILD_WITH_MONITORING_SYSTEM` | Monitoring integration |
| `KCENON_WITH_COMMON_SYSTEM` | `WITH_COMMON_SYSTEM` | Auto-detected | Common system integration |

When CMake options like `BUILD_WITH_CONTAINER_SYSTEM` are enabled, CMake defines `WITH_*` preprocessor symbols (e.g., `WITH_CONTAINER_SYSTEM`). The `feature_flags.h` header from `common_system` then maps these to `KCENON_WITH_*` macros for consistent integration gating across repositories.

### Running Tests

```bash
# Build with tests enabled (default)
cmake .. -DBUILD_TESTS=ON
cmake --build .

# Run all tests
ctest --output-on-failure

# Or run specific test executable directly
./bin/query_protocol_test
./bin/resilience_test
./bin/integration_test
./bin/rate_limiter_test
./bin/auth_middleware_test
./bin/connection_pool_test
./bin/query_cache_test
```

**Current Test Coverage:**
- Query Protocol Tests (45 tests)
  - Query types and status code conversions
  - Message header construction
  - Auth token validation and expiration
  - Query param value types
  - Query request/response serialization
  - Error handling for invalid inputs

- Resilience Tests (29 tests)
  - Health status structure and success rate calculation
  - Health check configuration defaults and customization
  - Reconnection configuration validation
  - Connection state enum string conversion
  - Connection health monitor with null backend handling
  - Resilient database connection edge cases

- Integration Tests (Phase 3.5)
  - Auth middleware authentication flow
  - Rate limiting behavior under load
  - Query router metrics tracking
  - Concurrent request handling
  - Full pipeline throughput tests
  - Error handling scenarios

- Session ID Security Tests
  - Format validation (32-character hex string)
  - Uniqueness verification across 100,000 samples
  - Entropy estimation (> 3.5 bits per character)
  - Thread-safety with concurrent generation
  - Performance validation (< 1μs per generation)

- Rate Limiter Tests
  - Sliding window algorithm correctness
  - Burst handling and configuration
  - Block duration behavior
  - Concurrent access safety
  - Client independence verification

- Auth Middleware Tests
  - Token validation success/failure cases
  - Token expiration handling
  - Rate limiting integration
  - Audit event emission
  - Metrics collection
  - Session management

- Connection Pool Tests
  - Pool configuration and initialization
  - Connection acquisition and release
  - Pool exhaustion and timeout handling
  - Graceful shutdown behavior
  - Health check functionality
  - Priority-based metrics tracking
  - Concurrent access safety

- Query Cache Tests (32 tests)
  - Cache configuration defaults and customization
  - Basic cache operations (get, put, clear)
  - LRU eviction policy verification
  - TTL-based expiration handling
  - Table-based cache invalidation
  - Cache key generation from SQL and parameters
  - Cache metrics (hits, misses, evictions)
  - Size limit enforcement
  - Thread safety with concurrent access

### C++20 Modules

The project supports C++20 modules for improved compilation times and better encapsulation.

**Requirements:**
- Clang 16.0+ or GCC 14.0+ or MSVC 19.29+
- CMake 3.28+ recommended for best module support

**Building with Modules:**

```bash
cmake .. -DBUILD_MODULES=ON
cmake --build .
```

**Module Structure:**

```
kcenon.database_server              # Primary module interface
├── :core                           # Server app and configuration
├── :gateway                        # Query protocol and routing
├── :pooling                        # Connection pool management
└── :resilience                     # Health monitoring and recovery
```

**Usage:**

```cpp
import kcenon.database_server;

int main() {
    database_server::server_app app;

    if (auto result = app.initialize("config.yaml"); !result) {
        return 1;
    }

    return app.run();
}
```

**Partition Imports:**

```cpp
// Import only gateway components
import kcenon.database_server:gateway;

// Use gateway types
database_server::gateway::query_request request("SELECT * FROM users",
                                                  database_server::gateway::query_type::select);
```

### Running Benchmarks

```bash
# Build with benchmarks enabled
cmake .. -DBUILD_BENCHMARKS=ON
cmake --build .

# Run benchmarks
./bin/gateway_benchmarks
```

**Performance Benchmarks (Phase 3.5):**
- Router routing overhead measurement (target: < 1ms)
- Query throughput benchmarks (target: 10k+ queries/sec)
- Auth middleware validation throughput
- Rate limiter performance
- Serialization/deserialization throughput
- Latency distribution (p50, p90, p99)
- Concurrent access patterns

## Configuration

The server can be configured using a configuration file (default: `config.conf`):

```conf
# Server identification
name=my_database_server

# Network settings
network.host=0.0.0.0
network.port=5432
network.enable_tls=false
network.max_connections=100
network.connection_timeout_ms=30000

# Logging
logging.level=info
logging.enable_console=true

# Connection pool (Phase 2)
pool.min_connections=5
pool.max_connections=50
pool.idle_timeout_ms=60000
pool.health_check_interval_ms=30000

# Authentication (Phase 3.4)
auth.enabled=true
auth.validate_on_each_request=false
auth.token_refresh_window_ms=300000

# Rate Limiting (Phase 3.4)
rate_limit.enabled=true
rate_limit.requests_per_second=100
rate_limit.burst_size=200
rate_limit.window_size_ms=1000
rate_limit.block_duration_ms=60000

# Query Cache (Phase 3)
cache.enabled=true
cache.max_entries=10000
cache.ttl_seconds=300
cache.max_result_size_bytes=1048576
cache.enable_lru=true
```

## Usage

```bash
# Run with default configuration
./database_server

# Run with custom configuration file
./database_server -c /path/to/config.conf

# Show help
./database_server --help

# Show version
./database_server --version
```

## Development Roadmap

### Phase 1: Scaffolding & Dependency Setup
- [x] Create basic directory structure
- [x] Configure CMakeLists.txt with dependencies
- [x] Implement basic server_app interface
- [x] Create main.cpp entry point
- [x] Add configuration loading

### Phase 2: Core Migration (Completed)
- [x] Migrate connection_pool from database_system
  - [x] connection_priority enum with 4 priority levels
  - [x] pool_metrics with priority-specific tracking
  - [x] connection_pool with adaptive job queue integration
  - [x] Server-side connection pool types (`connection_types.h`)
    - connection_pool_config, connection_stats, connection_wrapper
    - connection_pool_base abstract interface
    - connection_pool implementation
- [x] Migrate resilience logic from database_system
  - [x] connection_health_monitor with heartbeat-based health tracking
  - [x] resilient_database_connection with automatic reconnection
- [x] Update namespace to database_server::pooling and database_server::resilience
- [x] Add database_system as required dependency

> **Note**: As of database_system Phase 4.3, client-side connection pooling was
> removed (Client Library Diet initiative). Connection pooling is now handled
> server-side via database_server middleware. Client applications should use
> ProxyMode (`set_mode_proxy()`) to connect through database_server.

### Phase 3: Network Gateway Implementation (Current)
- [x] Define and implement Query Protocol
  - [x] query_types.h: Query type and status code enums
  - [x] query_protocol.h: Request/response message structures
  - [x] Serialization using container_system
  - [x] Unit tests for message validation (45 tests)
  - [x] Refactored into modular structure (see [#32](https://github.com/kcenon/database_server/issues/32)):
    - `protocol/serialization_helpers.h`: Common utilities
    - `protocol/header_serializer.cpp`: message_header
    - `protocol/auth_serializer.cpp`: auth_token
    - `protocol/param_serializer.cpp`: query_param
    - `protocol/request_serializer.cpp`: query_request
    - `protocol/response_serializer.cpp`: query_response
- [x] Implement TCP Listener
  - [x] gateway_server: TCP server using kcenon::network::core::messaging_server
  - [x] Client session management with authentication support
- [x] Implement Query Routing & Load Balancing
  - [x] query_router: Routes queries to connection pool
  - [x] Priority-based scheduling integration
  - [x] Metrics collection for performance monitoring
  - [x] server_app integration with gateway and query router
  - [x] CRTP-based query handlers for zero virtual dispatch overhead ([#48](https://github.com/kcenon/database_server/issues/48))
- [x] Add authentication middleware (Phase 3.4)
  - [x] auth_middleware: Token validation with pluggable validators
  - [x] rate_limiter: Sliding window rate limiting with burst support
  - [x] Audit logging for security events
  - [x] Integration with gateway_server request pipeline
- [x] Write integration tests and benchmarks (Phase 3.5)
  - [x] Integration tests for complete query flow
  - [x] Error handling scenario tests
  - [x] Performance benchmarks (target: 10k+ queries/sec)
  - [x] Latency measurement (target: < 1ms routing overhead)
- [x] Implement Query Result Cache ([#30](https://github.com/kcenon/database_server/issues/30))
  - [x] LRU eviction policy with configurable max entries
  - [x] TTL-based expiration for cached results
  - [x] Automatic cache invalidation on write operations
  - [x] SQL table name extraction for targeted invalidation
  - [x] Thread-safe implementation with shared_mutex
  - [x] Comprehensive cache metrics
- [x] IExecutor Interface Integration ([#45](https://github.com/kcenon/database_server/issues/45))
  - [x] Optional IExecutor injection for background tasks
  - [x] Resilience module: health monitoring via IExecutor
  - [x] Gateway module: async query execution via IExecutor
  - [x] Fallback to std::async when executor not provided
  - [x] Centralized executor management in server_app

## Planned Features

The following features are planned for future releases:

| Feature | Description | Status |
|---------|-------------|--------|
| QUIC Protocol | High-performance UDP-based transport with built-in TLS | Planned |
| Query Result Cache | In-memory cache for SELECT query results with TTL and LRU eviction | ✅ Completed ([#30](https://github.com/kcenon/database_server/issues/30)) |
| CRTP Query Handlers | Zero virtual dispatch overhead for query processing | ✅ Completed ([#48](https://github.com/kcenon/database_server/issues/48)) |

## Executor Integration

The server supports optional `IExecutor` injection from `common_system` for centralized thread management. This allows sharing a single thread pool across components for efficient resource utilization.

### Usage Example

```cpp
#include <kcenon/database_server/server_app.h>
#include <kcenon/thread/adapters/common_executor_adapter.h>
#include <kcenon/thread/core/thread_pool.h>

// Create server app
database_server::server_app app;
app.initialize("config.conf");

// Create shared executor (optional)
auto pool = std::make_shared<kcenon::thread::thread_pool>("shared_executor", 4);
pool->start();
auto executor = kcenon::thread::adapters::common_executor_factory::create_from_thread_pool(pool);

// Inject executor for unified thread management
app.set_executor(executor);

// Start server - async queries and health monitoring will use shared executor
app.run();
```

### Components with IExecutor Support

| Component | Method | Description |
|-----------|--------|-------------|
| `server_app` | `set_executor()` | Centralized executor management |
| `query_router` | `set_executor()` | Async query execution |
| `connection_health_monitor` | Constructor | Background health monitoring |
| `resilient_database_connection` | Constructor | Propagates to health monitor |

When no executor is provided, components automatically fall back to `std::async` for background tasks.

## Security

### Session ID Generation

Session IDs are generated using cryptographically secure methods:

- **128-bit entropy**: Uses two 64-bit random values for unpredictability
- **Hardware seeding**: Uses `std::random_device` for hardware entropy
- **Thread-safe**: Thread-local RNG prevents contention and predictability
- **Format**: 32-character lowercase hexadecimal string

The session ID format changed from predictable timestamp-counter pattern to secure random values. Old format (`session_TIMESTAMP_COUNTER`) was vulnerable to session hijacking through ID prediction.

### Security Best Practices

1. **Authentication**: Enable `auth.enabled=true` in production
2. **Rate Limiting**: Configure appropriate rate limits to prevent abuse
3. **TLS**: Enable TLS for encrypted connections (`network.enable_tls=true`)
4. **Logging**: Avoid logging full session IDs in production environments

## License

BSD 3-Clause License - see [LICENSE](LICENSE) for details.

## CI/CD

The project includes comprehensive CI/CD workflows matching the database_system pipeline:

### Core Workflows

| Workflow | Description | Status |
|----------|-------------|--------|
| `ci.yml` | Multi-platform build (Ubuntu, macOS, Windows) | Active |
| `integration-tests.yml` | Integration tests with coverage | Active |
| `coverage.yml` | Code coverage with Codecov | Active |

### Quality Gates

| Workflow | Description | Status |
|----------|-------------|--------|
| `static-analysis.yml` | clang-tidy and cppcheck analysis | Active |
| `sanitizers.yml` | ASan, TSan, UBSan checks | Active |

### Security & Documentation

| Workflow | Description | Status |
|----------|-------------|--------|
| `dependency-security-scan.yml` | Trivy vulnerability scanning | Active |
| `sbom.yml` | SBOM generation (CycloneDX, SPDX) | Active |
| `build-Doxygen.yaml` | API documentation generation | Active |

### Performance

| Workflow | Description | Status |
|----------|-------------|--------|
| `benchmarks.yml` | Performance regression testing | Active |

## Related Projects

- [common_system](../common_system) - Core utilities and interfaces
- [thread_system](../thread_system) - Threading and job scheduling
- [network_system](../network_system) - Network communication
- [database_system](../database_system) - Database client library
