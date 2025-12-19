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
│  │   Listener   │───▶│   Request Handler   │    │
│  │ (TCP/QUIC)  │    │                     │    │
│  └─────────────┘    └─────────┬───────────┘    │
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
- **network_system** (Tier 4) - TCP/QUIC server implementation

### Optional

- **monitoring_system** (Tier 3) - Performance metrics and health monitoring
- **container_system** (Tier 1) - Binary protocol serialization

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
| `BUILD_SAMPLES` | OFF | Build sample applications |
| `BUILD_WITH_MONITORING_SYSTEM` | OFF | Enable monitoring integration |
| `BUILD_WITH_CONTAINER_SYSTEM` | ON | Enable container serialization |
| `ENABLE_COVERAGE` | OFF | Enable code coverage |

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

### Phase 2: Core Migration (Current)
- [x] Migrate connection_pool from database_system
  - [x] connection_priority enum with 4 priority levels
  - [x] pool_metrics with priority-specific tracking
  - [x] connection_pool with adaptive job queue integration
- [x] Migrate resilience logic from database_system
  - [x] connection_health_monitor with heartbeat-based health tracking
  - [x] resilient_database_connection with automatic reconnection
- [x] Update namespace to database_server::pooling and database_server::resilience
- [x] Add database_system as required dependency

### Phase 3: Network Gateway Implementation
- [ ] Implement TCP/QUIC Listener
- [ ] Define and implement Query Protocol
- [ ] Implement Query Routing & Load Balancing
- [ ] (Optional) Implement Query Result Cache

## License

BSD 3-Clause License - see [LICENSE](LICENSE) for details.

## Related Projects

- [common_system](../common_system) - Core utilities and interfaces
- [thread_system](../thread_system) - Threading and job scheduling
- [network_system](../network_system) - Network communication
- [database_system](../database_system) - Database client library
