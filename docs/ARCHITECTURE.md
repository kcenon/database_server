> 🇰🇷 [한국어 버전](ARCHITECTURE.kr.md)

# Architecture

## Overview

Database Server is a gateway middleware that sits between client applications and physical databases (PostgreSQL, MySQL, etc.). It provides connection pooling, query routing, authentication, rate limiting, and caching as a transparent middleware layer.

### Design Goals

| Goal | Description |
|------|-------------|
| **Performance** | Sub-millisecond routing overhead, 10k+ queries/sec throughput |
| **Reliability** | Automatic health monitoring and connection recovery |
| **Security** | Token-based authentication, rate limiting, cryptographic session IDs |
| **Extensibility** | CRTP-based handlers for zero virtual dispatch overhead |
| **Observability** | Comprehensive metrics collection following monitoring_system patterns |

## System Structure

The server is organized into six modules, each with a distinct responsibility:

```
┌─────────────────────────────────────────────────────────────────┐
│                        database_server                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────┐  ┌───────────┐  ┌──────────┐  ┌──────────────┐  │
│  │   Core   │  │  Gateway  │  │ Pooling  │  │  Resilience  │  │
│  │          │  │           │  │          │  │              │  │
│  │ server_  │  │ gateway_  │  │ conn_    │  │ health_      │  │
│  │ app      │  │ server    │  │ pool     │  │ monitor      │  │
│  │ server_  │  │ query_    │  │ pool_    │  │ resilient_   │  │
│  │ config   │  │ router    │  │ metrics  │  │ connection   │  │
│  │          │  │ query_    │  │          │  │              │  │
│  │          │  │ handlers  │  │          │  │              │  │
│  │          │  │ auth_     │  │          │  │              │  │
│  │          │  │ middleware│  │          │  │              │  │
│  │          │  │ query_    │  │          │  │              │  │
│  │          │  │ cache     │  │          │  │              │  │
│  └──────────┘  └───────────┘  └──────────┘  └──────────────┘  │
│                                                                 │
│  ┌──────────┐  ┌───────────┐                                   │
│  │ Metrics  │  │  Logging  │                                   │
│  │          │  │           │                                   │
│  │ query_   │  │ console_  │                                   │
│  │ metrics_ │  │ logger    │                                   │
│  │ collector│  │           │                                   │
│  └──────────┘  └───────────┘                                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Module Summary

| Module | Namespace | Responsibility |
|--------|-----------|----------------|
| **Core** | `database_server` | Application lifecycle, configuration loading, signal handling |
| **Gateway** | `database_server::gateway` | TCP server, query protocol, routing, authentication, caching |
| **Pooling** | `database_server::pooling` | Connection pool management with priority-based scheduling |
| **Resilience** | `database_server::resilience` | Health monitoring, automatic reconnection |
| **Metrics** | `database_server::metrics` | CRTP-based zero-overhead query metrics collection |
| **Logging** | `database_server::logging` | Console logging with configurable levels |

## Core Components

### Core Module

**`server_app`** is the central orchestrator that owns and initializes all subsystems:

```
server_app
├── server_config          # Configuration file parsing
├── gateway_server         # TCP listener
├── query_router           # Query dispatch
├── connection_pool        # Database connections
├── query_cache            # Optional result cache
└── IExecutor (optional)   # Shared thread pool
```

**`server_config`** parses `config.conf` files with sections for network, pooling, authentication, rate limiting, and caching parameters.

### Gateway Module

The gateway module handles all network-facing concerns:

- **`gateway_server`**: TCP server built on `network_system::messaging_server`. Manages client sessions with authentication support.
- **`query_router`**: Routes incoming queries to the connection pool. Implements load balancing with priority-based scheduling and collects routing metrics.
- **`query_handlers`**: Seven CRTP-based handlers (select, insert, update, delete, execute, ping, batch) that process queries with zero virtual dispatch overhead. A type erasure wrapper provides runtime polymorphism when needed.
- **`auth_middleware`**: Token-based authentication with pluggable validators. Integrates rate limiting and emits audit events for security monitoring.
- **`rate_limiter`**: Sliding window algorithm with burst support and configurable block duration.
- **`query_cache`**: LRU cache with TTL-based expiration. Automatically invalidates cache entries on write operations by extracting table names from SQL statements. Thread-safe via `shared_mutex`.
- **`session_id_generator`**: Generates cryptographically secure 128-bit session IDs using hardware entropy and thread-local RNG.

#### Query Protocol

The protocol layer handles serialization of client-server messages:

```
protocol/
├── serialization_helpers.h    # Common utilities
├── header_serializer.cpp      # message_header
├── auth_serializer.cpp        # auth_token
├── param_serializer.cpp       # query_param
├── request_serializer.cpp     # query_request
└── response_serializer.cpp    # query_response
```

All serialization uses `container_system` for binary encoding, with `Result<T>` return types for type-safe error handling.

### Pooling Module

**`connection_pool`** manages database connections with:

- **Priority-based scheduling**: Four priority levels with aging to prevent starvation
- **Connection lifecycle**: Acquisition, release, health tracking, and graceful shutdown
- **Cancellation tokens**: Support for cooperative shutdown signaling
- **Pool metrics**: Active/idle counts, acquisition latency, priority-specific tracking

### Resilience Module

- **`connection_health_monitor`**: Heartbeat-based health checking with configurable intervals. Reports connection health status and success rates. Supports optional `IExecutor` for background monitoring.
- **`resilient_database_connection`**: Wraps database connections with automatic reconnection. Configurable backoff strategy and connection state tracking.

### Metrics Module

**`query_metrics_collector`** follows the CRTP pattern from `monitoring_system`:

```cpp
template <typename Derived>
class query_collector_base {
    // Zero virtual dispatch - resolved at compile time
    void collect(const query_execution& exec) {
        static_cast<Derived*>(this)->do_collect(exec);
    }
};
```

Tracks four metric categories:
- **Query execution**: Count, latency, success/failure rates, timeouts
- **Cache performance**: Hit/miss ratio, evictions
- **Pool utilization**: Active/idle connections, acquisition time
- **Session management**: Active sessions, auth events, duration

## Data Flow

### Query Request Flow

```
Client Application
    │
    ▼
┌─────────────────┐
│  gateway_server  │  1. Accept TCP connection
│  (TCP Listener)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ auth_middleware  │  2. Validate token, check rate limit
│ (rate_limiter)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  query_router   │  3. Deserialize request, select handler
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  query_cache    │  4. Check cache (SELECT only)
│  (if enabled)   │     Hit → return cached result
└────────┬────────┘
         │ Miss
         ▼
┌─────────────────┐
│ connection_pool │  5. Acquire connection (priority-based)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Physical Database│  6. Execute query
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ query_cache     │  7. Store result (SELECT) or
│                 │     invalidate (INSERT/UPDATE/DELETE)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  query_router   │  8. Serialize response, collect metrics
└────────┬────────┘
         │
         ▼
Client Application
```

### Write Query Invalidation

When a write operation (INSERT, UPDATE, DELETE) is processed, the cache extracts table names from the SQL statement and invalidates all cached entries for those tables. This ensures cache consistency without requiring manual invalidation.

## Thread Model

### Concurrency Design

The server uses a combination of threading strategies:

| Component | Threading Model | Synchronization |
|-----------|----------------|-----------------|
| `gateway_server` | Event-driven (network_system) | Session map with mutex |
| `query_router` | Async execution via IExecutor | Lock-free metrics collection |
| `connection_pool` | Condition variable for acquisition | Mutex-protected pool state |
| `query_cache` | Reader-writer pattern | `shared_mutex` (read-heavy workload) |
| `health_monitor` | Periodic background task | Atomic health status |
| `rate_limiter` | Per-client tracking | Mutex per client bucket |
| `session_id_gen` | Thread-local RNG | No synchronization needed |

### IExecutor Integration

Components optionally accept an `IExecutor` for unified thread management:

```
server_app
    │
    ├── set_executor(executor)
    │       │
    │       ├── query_router.set_executor()
    │       │       └── Async query execution
    │       │
    │       └── connection_health_monitor(executor)
    │               └── Background health checks
    │
    └── (no executor)
            └── Falls back to std::async
```

This allows sharing a single thread pool across all components for efficient resource utilization.

### Thread Safety Guarantees

- All public APIs are thread-safe unless documented otherwise
- Internal state is protected by appropriate synchronization primitives
- Session IDs use thread-local RNG to avoid contention
- Metrics collection uses atomic operations where possible

## Dependencies

### Ecosystem Dependency Graph

```
                    ┌───────────────┐
                    │ common_system │  Tier 0
                    │  (Result<T>,  │
                    │   IExecutor)  │
                    └───────┬───────┘
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
     ┌────────────┐  ┌───────────┐  ┌──────────────┐
     │thread_system│  │container_ │  │database_system│  Tier 1-2
     │ (job queue, │  │system     │  │ (DB backend, │
     │  pool)      │  │(serialize)│  │  interfaces) │
     └──────┬─────┘  └─────┬─────┘  └──────┬───────┘
            │               │               │
            ▼               │               │
     ┌──────────────┐      │               │
     │network_system│      │               │
     │ (TCP server) │ Tier 4               │
     └──────┬───────┘      │               │
            │               │               │
            ▼               ▼               ▼
     ┌─────────────────────────────────────────┐
     │           database_server               │
     │  (gateway middleware)                   │
     └─────────────────────────────────────────┘

     Optional: monitoring_system (Tier 3) for metrics export
```

### Dependency Roles

| Dependency | Tier | Role in database_server |
|------------|------|------------------------|
| `common_system` | 0 | `Result<T>` for error handling, `IExecutor` for async execution, logging interfaces |
| `thread_system` | 1 | Job scheduling, thread pool management, adaptive job queues |
| `container_system` | 1 | Binary serialization for query protocol messages |
| `database_system` | 2 | Database backend interfaces, connection types, query execution |
| `network_system` | 4 | TCP server implementation (`messaging_server`) |
| `monitoring_system` | 3 | Optional metrics export and health endpoint integration |

## C++20 Module Structure

The project supports C++20 modules for improved compilation times and encapsulation:

```
kcenon.database_server                    # Primary module interface
│
├── kcenon.database_server:core           # server_app, server_config
├── kcenon.database_server:gateway        # gateway_server, query_router,
│                                         # query_handlers, auth_middleware,
│                                         # query_cache, query_protocol
├── kcenon.database_server:pooling        # connection_pool, connection_types,
│                                         # connection_priority, pool_metrics
├── kcenon.database_server:resilience     # connection_health_monitor,
│                                         # resilient_database_connection
└── kcenon.database_server:metrics        # query_metrics_collector,
                                          # query_collector_base
```

Each partition exports only its public API, providing clear module boundaries and preventing unintended internal dependencies.

## Design Patterns

### CRTP (Curiously Recurring Template Pattern)

Used in two areas for zero-overhead polymorphism:

1. **Query Handlers**: Seven handler types (select, insert, update, delete, execute, ping, batch) inherit from a CRTP base. A type erasure wrapper provides runtime polymorphism when dynamic dispatch is needed.

2. **Metrics Collector**: `query_collector_base<Derived>` enables compile-time dispatch for metrics collection, following the pattern established by `monitoring_system`.

### Result<T> Pattern

All protocol operations and fallible functions return `Result<T>` from `common_system`, providing:
- Type-safe error propagation without exceptions
- Consistent error handling across all modules
- Composable error chains

### Configuration-Driven Architecture

Server behavior is fully configurable via `config.conf` without code changes:
- Network parameters (host, port, TLS, max connections)
- Pool sizing (min/max connections, timeouts)
- Authentication and rate limiting policies
- Cache parameters (max entries, TTL, size limits)

## Security Considerations

### Authentication Flow

1. Client connects via TCP and sends an `auth_token` in the first message
2. `auth_middleware` validates the token using a pluggable validator
3. Rate limiter checks per-client request rate
4. On success, a secure session ID is generated and associated with the connection
5. Subsequent requests reference the session for stateful operations

### Session ID Security

Session IDs use 128-bit cryptographic randomness:
- Two 64-bit values from `std::random_device` (hardware entropy)
- Thread-local `std::mt19937_64` seeded per-thread
- Output: 32-character lowercase hexadecimal string
- Verified: > 3.5 bits entropy per character across 100k samples

---

*Version: 1.0.0 | Last updated: 2026-02-22*
