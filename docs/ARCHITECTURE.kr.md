> 🇺🇸 [English Version](ARCHITECTURE.md)

# 아키텍처

## 개요

Database Server는 클라이언트 애플리케이션과 물리적 데이터베이스(PostgreSQL, MySQL 등) 사이에 위치하는 게이트웨이 미들웨어입니다. 투명한 미들웨어 계층으로서 커넥션 풀링, 쿼리 라우팅, 인증, Rate Limiting, 캐싱 기능을 제공합니다.

### 설계 목표

| 목표 | 설명 |
|------|------|
| **성능** | 1ms 미만의 라우팅 오버헤드, 10k+ 쿼리/초 처리량 |
| **신뢰성** | 자동 헬스 모니터링 및 커넥션 복구 |
| **보안** | 토큰 기반 인증, Rate Limiting, 암호학적 세션 ID |
| **확장성** | 가상 디스패치 오버헤드 제로의 CRTP 기반 핸들러 |
| **관측성** | monitoring_system 패턴을 따르는 종합적인 메트릭 수집 |

## 시스템 구조

서버는 각각 명확한 책임을 가진 6개 모듈로 구성됩니다:

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

### 모듈 요약

| 모듈 | 네임스페이스 | 책임 |
|------|-------------|------|
| **Core** | `database_server` | 애플리케이션 생명주기, 설정 로딩, 시그널 처리 |
| **Gateway** | `database_server::gateway` | TCP 서버, 쿼리 프로토콜, 라우팅, 인증, 캐싱 |
| **Pooling** | `database_server::pooling` | 우선순위 기반 스케줄링의 커넥션 풀 관리 |
| **Resilience** | `database_server::resilience` | 헬스 모니터링, 자동 재연결 |
| **Metrics** | `database_server::metrics` | CRTP 기반 제로 오버헤드 쿼리 메트릭 수집 |
| **Logging** | `database_server::logging` | 설정 가능한 레벨의 콘솔 로깅 |

## 핵심 컴포넌트

### Core 모듈

**`server_app`**은 모든 하위 시스템을 소유하고 초기화하는 중앙 오케스트레이터입니다:

```
server_app
├── server_config          # 설정 파일 파싱
├── gateway_server         # TCP 리스너
├── query_router           # 쿼리 디스패치
├── connection_pool        # 데이터베이스 커넥션
├── query_cache            # 선택적 결과 캐시
└── IExecutor (optional)   # 공유 스레드 풀
```

**`server_config`**는 네트워크, 풀링, 인증, Rate Limiting, 캐싱 파라미터 섹션이 포함된 `config.conf` 파일을 파싱합니다.

### Gateway 모듈

Gateway 모듈은 모든 네트워크 대면 관심사를 처리합니다:

- **`gateway_server`**: `network_system::messaging_server` 기반의 TCP 서버. 인증 지원이 포함된 클라이언트 세션을 관리합니다.
- **`query_router`**: 수신 쿼리를 커넥션 풀로 라우팅합니다. 우선순위 기반 스케줄링의 로드 밸런싱을 구현하고 라우팅 메트릭을 수집합니다.
- **`query_handlers`**: 7개의 CRTP 기반 핸들러(select, insert, update, delete, execute, ping, batch)가 가상 디스패치 오버헤드 없이 쿼리를 처리합니다. 동적 디스패치가 필요한 경우 타입 소거 래퍼가 런타임 다형성을 제공합니다.
- **`auth_middleware`**: 플러거블 검증기를 사용한 토큰 기반 인증. Rate Limiting을 통합하고 보안 모니터링을 위한 감사 이벤트를 발행합니다.
- **`rate_limiter`**: 버스트 지원과 설정 가능한 차단 지속 시간이 포함된 슬라이딩 윈도우 알고리즘.
- **`query_cache`**: TTL 기반 만료가 포함된 LRU 캐시. SQL 문에서 테이블 이름을 추출하여 쓰기 작업 시 캐시 항목을 자동으로 무효화합니다. `shared_mutex`를 통한 스레드 안전.
- **`session_id_generator`**: 하드웨어 엔트로피와 스레드 로컬 RNG를 사용하여 암호학적으로 안전한 128비트 세션 ID를 생성합니다.

#### Query Protocol

프로토콜 계층은 클라이언트-서버 메시지의 직렬화를 처리합니다:

```
protocol/
├── serialization_helpers.h    # 공통 유틸리티
├── header_serializer.cpp      # message_header
├── auth_serializer.cpp        # auth_token
├── param_serializer.cpp       # query_param
├── request_serializer.cpp     # query_request
└── response_serializer.cpp    # query_response
```

모든 직렬화는 `container_system`을 사용한 바이너리 인코딩을 사용하며, 타입 안전 오류 처리를 위해 `Result<T>` 반환 타입을 사용합니다.

### Pooling 모듈

**`connection_pool`**은 다음 기능으로 데이터베이스 커넥션을 관리합니다:

- **우선순위 기반 스케줄링**: 기아 방지를 위한 에이징이 포함된 4단계 우선순위 레벨
- **커넥션 생명주기**: 획득, 반환, 헬스 추적, 정상 종료
- **취소 토큰**: 협력적 종료 시그널링 지원
- **풀 메트릭**: 활성/유휴 카운트, 획득 지연, 우선순위별 추적

### Resilience 모듈

- **`connection_health_monitor`**: 설정 가능한 간격의 하트비트 기반 헬스 체크. 커넥션 헬스 상태와 성공률을 보고합니다. 백그라운드 모니터링을 위한 선택적 `IExecutor`를 지원합니다.
- **`resilient_database_connection`**: 자동 재연결로 데이터베이스 커넥션을 래핑합니다. 설정 가능한 백오프 전략과 커넥션 상태 추적.

### Metrics 모듈

**`query_metrics_collector`**는 `monitoring_system`의 CRTP 패턴을 따릅니다:

```cpp
template <typename Derived>
class query_collector_base {
    // 제로 가상 디스패치 - 컴파일 타임에 해결
    void collect(const query_execution& exec) {
        static_cast<Derived*>(this)->do_collect(exec);
    }
};
```

4가지 메트릭 카테고리를 추적합니다:
- **쿼리 실행**: 카운트, 지연, 성공/실패율, 타임아웃
- **캐시 성능**: Hit/Miss 비율, 퇴거
- **풀 활용도**: 활성/유휴 커넥션, 획득 시간
- **세션 관리**: 활성 세션, 인증 이벤트, 지속 시간

## 데이터 흐름

### 쿼리 요청 흐름

```
Client Application
    │
    ▼
┌─────────────────┐
│  gateway_server  │  1. TCP 연결 수락
│  (TCP Listener)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ auth_middleware  │  2. 토큰 검증, Rate Limit 확인
│ (rate_limiter)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  query_router   │  3. 요청 역직렬화, 핸들러 선택
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  query_cache    │  4. 캐시 확인 (SELECT만 해당)
│  (if enabled)   │     Hit → 캐시된 결과 반환
└────────┬────────┘
         │ Miss
         ▼
┌─────────────────┐
│ connection_pool │  5. 커넥션 획득 (우선순위 기반)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Physical Database│  6. 쿼리 실행
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ query_cache     │  7. 결과 저장 (SELECT) 또는
│                 │     무효화 (INSERT/UPDATE/DELETE)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  query_router   │  8. 응답 직렬화, 메트릭 수집
└────────┬────────┘
         │
         ▼
Client Application
```

### 쓰기 쿼리 무효화

쓰기 작업(INSERT, UPDATE, DELETE)이 처리되면 캐시는 SQL 문에서 테이블 이름을 추출하고 해당 테이블의 모든 캐시 항목을 무효화합니다. 이를 통해 수동 무효화 없이 캐시 일관성을 보장합니다.

## 스레드 모델

### 동시성 설계

서버는 다양한 스레딩 전략을 조합하여 사용합니다:

| 컴포넌트 | 스레딩 모델 | 동기화 |
|----------|------------|--------|
| `gateway_server` | 이벤트 기반 (network_system) | mutex 보호 세션 맵 |
| `query_router` | IExecutor를 통한 비동기 실행 | lock-free 메트릭 수집 |
| `connection_pool` | 획득 시 condition variable | mutex 보호 풀 상태 |
| `query_cache` | 리더-라이터 패턴 | `shared_mutex` (읽기 중심 워크로드) |
| `health_monitor` | 주기적 백그라운드 작업 | 원자적 헬스 상태 |
| `rate_limiter` | 클라이언트별 추적 | 클라이언트 버킷별 mutex |
| `session_id_gen` | 스레드 로컬 RNG | 동기화 불필요 |

### IExecutor 통합

컴포넌트는 통합 스레드 관리를 위해 선택적으로 `IExecutor`를 받습니다:

```
server_app
    │
    ├── set_executor(executor)
    │       │
    │       ├── query_router.set_executor()
    │       │       └── 비동기 쿼리 실행
    │       │
    │       └── connection_health_monitor(executor)
    │               └── 백그라운드 헬스 체크
    │
    └── (executor 없음)
            └── std::async로 폴백
```

이를 통해 모든 컴포넌트에서 단일 스레드 풀을 공유하여 효율적인 리소스 활용이 가능합니다.

### 스레드 안전 보장

- 별도 문서가 없는 한 모든 공개 API는 스레드 안전합니다
- 내부 상태는 적절한 동기화 프리미티브로 보호됩니다
- 세션 ID는 경합을 방지하기 위해 스레드 로컬 RNG를 사용합니다
- 메트릭 수집은 가능한 경우 원자적 연산을 사용합니다

## 의존성

### 생태계 의존성 그래프

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

### 의존성 역할

| 의존성 | Tier | database_server에서의 역할 |
|--------|------|-----------------------------|
| `common_system` | 0 | 오류 처리를 위한 `Result<T>`, 비동기 실행을 위한 `IExecutor`, 로깅 인터페이스 |
| `thread_system` | 1 | 작업 스케줄링, 스레드 풀 관리, 적응형 작업 큐 |
| `container_system` | 1 | 쿼리 프로토콜 메시지의 바이너리 직렬화 |
| `database_system` | 2 | 데이터베이스 백엔드 인터페이스, 커넥션 타입, 쿼리 실행 |
| `network_system` | 4 | TCP 서버 구현 (`messaging_server`) |
| `monitoring_system` | 3 | 선택적 메트릭 내보내기 및 헬스 엔드포인트 통합 |

## C++20 모듈 구조

프로젝트는 컴파일 시간 개선과 캡슐화를 위해 C++20 모듈을 지원합니다:

```
kcenon.database_server                    # 주 모듈 인터페이스
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

각 파티션은 공개 API만 내보내어 명확한 모듈 경계를 제공하고 의도하지 않은 내부 의존성을 방지합니다.

## 설계 패턴

### CRTP (Curiously Recurring Template Pattern)

제로 오버헤드 다형성을 위해 두 영역에서 사용됩니다:

1. **쿼리 핸들러**: 7개의 핸들러 타입(select, insert, update, delete, execute, ping, batch)이 CRTP 기반 클래스를 상속합니다. 동적 디스패치가 필요한 경우 타입 소거 래퍼가 런타임 다형성을 제공합니다.

2. **메트릭 수집기**: `query_collector_base<Derived>`는 `monitoring_system`에서 확립된 패턴을 따라 메트릭 수집의 컴파일 타임 디스패치를 가능하게 합니다.

### Result<T> 패턴

모든 프로토콜 동작과 실패 가능한 함수는 `common_system`의 `Result<T>`를 반환하여 다음을 제공합니다:
- 예외 없는 타입 안전 오류 전파
- 모든 모듈에서 일관된 오류 처리
- 조합 가능한 오류 체인

### 설정 기반 아키텍처

서버 동작은 코드 변경 없이 `config.conf`를 통해 완전히 설정 가능합니다:
- 네트워크 파라미터 (호스트, 포트, TLS, 최대 커넥션)
- 풀 크기 (최소/최대 커넥션, 타임아웃)
- 인증 및 Rate Limiting 정책
- 캐시 파라미터 (최대 항목, TTL, 크기 제한)

## 보안 고려사항

### 인증 흐름

1. 클라이언트가 TCP로 연결하고 첫 번째 메시지에 `auth_token`을 전송
2. `auth_middleware`가 플러거블 검증기를 사용하여 토큰 검증
3. Rate Limiter가 클라이언트별 요청 빈도 확인
4. 성공 시, 안전한 세션 ID가 생성되어 커넥션에 연결
5. 이후 요청은 상태 유지 작업을 위해 세션을 참조

### Session ID 보안

세션 ID는 128비트 암호학적 무작위성을 사용합니다:
- `std::random_device`(하드웨어 엔트로피)에서 두 개의 64비트 값
- 스레드별로 시드되는 스레드 로컬 `std::mt19937_64`
- 출력: 32자 소문자 16진수 문자열
- 검증: 100k 샘플에서 문자당 3.5비트 이상의 엔트로피

---

*버전: 1.0.0 | 최종 업데이트: 2026-02-22*
