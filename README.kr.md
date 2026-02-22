> 🇺🇸 [English Version](README.md)

# Database Server

클라이언트 애플리케이션과 물리적 데이터베이스 사이에서 커넥션 풀링, 쿼리 라우팅, 캐싱 기능을 제공하는 고성능 데이터베이스 게이트웨이 미들웨어 서버입니다.

## 개요

Database Server는 kcenon 통합 시스템 아키텍처에서 모놀리식 라이브러리를 게이트웨이 미들웨어 아키텍처로 전환하는 과정의 일부입니다. 클라이언트 애플리케이션과 물리적 데이터베이스 사이에 위치하여 다음 기능을 제공합니다:

- **커넥션 풀링**: 우선순위 기반 스케줄링을 통한 효율적인 커넥션 관리
- **쿼리 라우팅**: 다수의 데이터베이스 커넥션에 대한 로드 밸런싱
- **헬스 모니터링**: 장애 커넥션의 자동 감지 및 복구
- **쿼리 캐싱** (선택): 자주 실행되는 쿼리 결과 캐싱

## 아키텍처

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

## 의존성

### 필수

- **common_system** (Tier 0) - 핵심 유틸리티, Result<T>, 로깅 인터페이스
- **thread_system** (Tier 1) - 작업 스케줄링, 스레드 풀 관리
- **database_system** (Tier 2) - 데이터베이스 인터페이스 및 타입
- **network_system** (Tier 4) - TCP 서버 구현
- **container_system** (Tier 1) - 프로토콜 직렬화 (쿼리 프로토콜 메시지에 필요)

### 선택

- **monitoring_system** (Tier 3) - 성능 메트릭 및 헬스 모니터링

## 빌드

### 사전 요구사항

- CMake 3.16 이상
- C++20 호환 컴파일러 (GCC 10+, Clang 12+, MSVC 2019+)
- 필수 시스템 의존성이 빌드되어 사용 가능한 상태

### 빌드 단계

```bash
# 빌드 디렉터리 생성
mkdir build && cd build

# 설정
cmake ..

# 빌드
cmake --build .

# 실행 (선택)
./bin/database_server --help
```

### CMake 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `BUILD_TESTS` | ON | 단위 테스트 빌드 |
| `BUILD_BENCHMARKS` | OFF | 성능 벤치마크 빌드 |
| `BUILD_SAMPLES` | OFF | 샘플 애플리케이션 빌드 |
| `BUILD_MODULES` | OFF | C++20 모듈 라이브러리 빌드 |
| `BUILD_WITH_MONITORING_SYSTEM` | OFF | 모니터링 통합 활성화 |
| `BUILD_WITH_CONTAINER_SYSTEM` | ON | 프로토콜 직렬화 (필수, 비활성화 시 빌드 실패) |
| `ENABLE_COVERAGE` | OFF | 코드 커버리지 활성화 |

### 통합 매크로

소스 코드는 kcenon 생태계 전반에서 통합 게이팅을 위해 `KCENON_WITH_*=1` 통합 매크로를 사용합니다:

| 매크로 | CMake 옵션 | 설명 |
|--------|------------|------|
| `KCENON_WITH_CONTAINER_SYSTEM=1` | `BUILD_WITH_CONTAINER_SYSTEM` | Container 직렬화 지원 |
| `KCENON_WITH_MONITORING_SYSTEM=1` | `BUILD_WITH_MONITORING_SYSTEM` | 모니터링 통합 |
| `KCENON_WITH_COMMON_SYSTEM=1` | 자동 감지 | Common system 통합 |

CMake는 해당 `BUILD_WITH_*` 옵션이 활성화될 때 `KCENON_WITH_*=1` 전처리기 매크로를 직접 정의합니다. 이 통합 매크로 시스템은 중간 매핑 없이 모든 kcenon 저장소에서 일관된 통합 게이팅을 보장합니다.

### 테스트 실행

```bash
# 테스트 활성화하여 빌드 (기본값)
cmake .. -DBUILD_TESTS=ON
cmake --build .

# 전체 테스트 실행
ctest --output-on-failure

# 또는 특정 테스트 실행 파일 직접 실행
./bin/query_protocol_test
./bin/resilience_test
./bin/integration_test
./bin/rate_limiter_test
./bin/auth_middleware_test
./bin/connection_pool_test
./bin/query_cache_test
```

**현재 테스트 커버리지:**
- Query Protocol 테스트 (45개)
  - 쿼리 타입 및 상태 코드 변환
  - 메시지 헤더 구성
  - 인증 토큰 검증 및 만료
  - 쿼리 파라미터 값 타입
  - 쿼리 요청/응답 직렬화
  - 잘못된 입력에 대한 오류 처리

- Resilience 테스트 (29개)
  - 헬스 상태 구조 및 성공률 계산
  - 헬스 체크 설정 기본값 및 커스터마이징
  - 재연결 설정 검증
  - 커넥션 상태 열거형 문자열 변환
  - null 백엔드 처리를 위한 커넥션 헬스 모니터
  - 복원력 있는 데이터베이스 커넥션 엣지 케이스

- Integration 테스트 (Phase 3.5)
  - 인증 미들웨어 인증 흐름
  - 부하 상태에서의 Rate Limiting 동작
  - 쿼리 라우터 메트릭 추적
  - 동시 요청 처리
  - 전체 파이프라인 처리량 테스트
  - 오류 처리 시나리오

- Session ID 보안 테스트
  - 형식 검증 (32자 16진수 문자열)
  - 100,000개 샘플에 대한 고유성 검증
  - 엔트로피 추정 (문자당 3.5비트 이상)
  - 동시 생성 시 스레드 안전성
  - 성능 검증 (생성당 1μs 미만)

- Rate Limiter 테스트
  - 슬라이딩 윈도우 알고리즘 정확성
  - 버스트 처리 및 설정
  - 차단 지속 시간 동작
  - 동시 접근 안전성
  - 클라이언트 독립성 검증

- Auth Middleware 테스트
  - 토큰 검증 성공/실패 케이스
  - 토큰 만료 처리
  - Rate Limiting 통합
  - 감사 이벤트 발행
  - 메트릭 수집
  - 세션 관리

- Connection Pool 테스트
  - 풀 설정 및 초기화
  - 커넥션 획득 및 반환
  - 풀 소진 및 타임아웃 처리
  - 정상 종료 동작
  - 헬스 체크 기능
  - 우선순위 기반 메트릭 추적
  - 동시 접근 안전성

- Query Cache 테스트 (32개)
  - 캐시 설정 기본값 및 커스터마이징
  - 기본 캐시 동작 (get, put, clear)
  - LRU 퇴거 정책 검증
  - TTL 기반 만료 처리
  - 테이블 기반 캐시 무효화
  - SQL 및 파라미터 기반 캐시 키 생성
  - 캐시 메트릭 (적중, 미스, 퇴거)
  - 크기 제한 적용
  - 동시 접근 시 스레드 안전성

### C++20 모듈

이 프로젝트는 컴파일 시간 개선과 더 나은 캡슐화를 위해 C++20 모듈을 지원합니다.

**요구사항:**
- Clang 16.0+ 또는 GCC 14.0+ 또는 MSVC 19.29+
- CMake 3.28+ 권장 (최적의 모듈 지원)

**모듈 빌드:**

```bash
cmake .. -DBUILD_MODULES=ON
cmake --build .
```

**모듈 구조:**

```
kcenon.database_server              # 주 모듈 인터페이스
├── :core                           # 서버 앱 및 설정
├── :gateway                        # 쿼리 프로토콜 및 라우팅
├── :pooling                        # 커넥션 풀 관리
├── :resilience                     # 헬스 모니터링 및 복구
└── :metrics                        # CRTP 기반 메트릭 수집
```

**사용법:**

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

**파티션 임포트:**

```cpp
// 게이트웨이 컴포넌트만 임포트
import kcenon.database_server:gateway;

// 게이트웨이 타입 사용
database_server::gateway::query_request request("SELECT * FROM users",
                                                  database_server::gateway::query_type::select);
```

### 벤치마크 실행

```bash
# 벤치마크 활성화하여 빌드
cmake .. -DBUILD_BENCHMARKS=ON
cmake --build .

# 벤치마크 실행
./bin/gateway_benchmarks
```

**성능 벤치마크 (Phase 3.5):**
- 라우터 라우팅 오버헤드 측정 (목표: < 1ms)
- 쿼리 처리량 벤치마크 (목표: 10k+ 쿼리/초)
- 인증 미들웨어 검증 처리량
- Rate Limiter 성능
- 직렬화/역직렬화 처리량
- 지연 시간 분포 (p50, p90, p99)
- 동시 접근 패턴

## 설정

서버는 설정 파일(기본: `config.conf`)을 사용하여 구성할 수 있습니다:

```conf
# 서버 식별
name=my_database_server

# 네트워크 설정
network.host=0.0.0.0
network.port=5432
network.enable_tls=false
network.max_connections=100
network.connection_timeout_ms=30000

# 로깅
logging.level=info
logging.enable_console=true

# 커넥션 풀 (Phase 2)
pool.min_connections=5
pool.max_connections=50
pool.idle_timeout_ms=60000
pool.health_check_interval_ms=30000

# 인증 (Phase 3.4)
auth.enabled=true
auth.validate_on_each_request=false
auth.token_refresh_window_ms=300000

# Rate Limiting (Phase 3.4)
rate_limit.enabled=true
rate_limit.requests_per_second=100
rate_limit.burst_size=200
rate_limit.window_size_ms=1000
rate_limit.block_duration_ms=60000

# 쿼리 캐시 (Phase 3)
cache.enabled=true
cache.max_entries=10000
cache.ttl_seconds=300
cache.max_result_size_bytes=1048576
cache.enable_lru=true
```

## 사용법

```bash
# 기본 설정으로 실행
./database_server

# 사용자 정의 설정 파일로 실행
./database_server -c /path/to/config.conf

# 도움말 표시
./database_server --help

# 버전 표시
./database_server --version
```

## 개발 로드맵

### Phase 1: 스캐폴딩 및 의존성 설정
- [x] 기본 디렉터리 구조 생성
- [x] 의존성과 함께 CMakeLists.txt 구성
- [x] 기본 server_app 인터페이스 구현
- [x] main.cpp 진입점 생성
- [x] 설정 로딩 추가

### Phase 2: 코어 마이그레이션 (완료)
- [x] database_system에서 connection_pool 마이그레이션
  - [x] 4단계 우선순위 레벨의 connection_priority 열거형
  - [x] 우선순위별 추적이 가능한 pool_metrics
  - [x] 적응형 작업 큐 통합의 connection_pool
  - [x] 서버 측 커넥션 풀 타입 (`connection_types.h`)
    - connection_pool_config, connection_stats, connection_wrapper
    - connection_pool_base 추상 인터페이스
    - connection_pool 구현
- [x] database_system에서 resilience 로직 마이그레이션
  - [x] 하트비트 기반 헬스 추적의 connection_health_monitor
  - [x] 자동 재연결이 가능한 resilient_database_connection
- [x] namespace를 database_server::pooling 및 database_server::resilience로 업데이트
- [x] database_system을 필수 의존성으로 추가

> **참고**: database_system Phase 4.3부터 클라이언트 측 커넥션 풀링이 제거되었습니다
> (Client Library Diet 이니셔티브). 커넥션 풀링은 이제 database_server 미들웨어를 통해
> 서버 측에서 처리됩니다. 클라이언트 애플리케이션은 ProxyMode(`set_mode_proxy()`)를
> 사용하여 database_server를 통해 연결해야 합니다.

### Phase 3: 네트워크 게이트웨이 구현 (현재)
- [x] Query Protocol 정의 및 구현
  - [x] query_types.h: 쿼리 타입 및 상태 코드 열거형
  - [x] query_protocol.h: 요청/응답 메시지 구조
  - [x] container_system을 사용한 직렬화
  - [x] 메시지 검증용 단위 테스트 (45개)
  - [x] 모듈러 구조로 리팩터링 ([#32](https://github.com/kcenon/database_server/issues/32)):
    - `protocol/serialization_helpers.h`: 공통 유틸리티
    - `protocol/header_serializer.cpp`: message_header
    - `protocol/auth_serializer.cpp`: auth_token
    - `protocol/param_serializer.cpp`: query_param
    - `protocol/request_serializer.cpp`: query_request
    - `protocol/response_serializer.cpp`: query_response
- [x] TCP Listener 구현
  - [x] gateway_server: kcenon::network::core::messaging_server를 사용한 TCP 서버
  - [x] 인증 지원이 포함된 클라이언트 세션 관리
- [x] 쿼리 라우팅 및 로드 밸런싱 구현
  - [x] query_router: 쿼리를 커넥션 풀로 라우팅
  - [x] 우선순위 기반 스케줄링 통합
  - [x] 성능 모니터링을 위한 메트릭 수집
  - [x] 게이트웨이 및 쿼리 라우터와 server_app 통합
  - [x] 가상 디스패치 오버헤드 제로의 CRTP 기반 쿼리 핸들러 ([#48](https://github.com/kcenon/database_server/issues/48))
- [x] 인증 미들웨어 추가 (Phase 3.4)
  - [x] auth_middleware: 플러거블 검증기를 사용한 토큰 검증
  - [x] rate_limiter: 버스트 지원이 포함된 슬라이딩 윈도우 Rate Limiting
  - [x] 보안 이벤트에 대한 감사 로깅
  - [x] gateway_server 요청 파이프라인과의 통합
- [x] 통합 테스트 및 벤치마크 작성 (Phase 3.5)
  - [x] 전체 쿼리 흐름에 대한 통합 테스트
  - [x] 오류 처리 시나리오 테스트
  - [x] 성능 벤치마크 (목표: 10k+ 쿼리/초)
  - [x] 지연 시간 측정 (목표: < 1ms 라우팅 오버헤드)
- [x] 쿼리 결과 캐시 구현 ([#30](https://github.com/kcenon/database_server/issues/30))
  - [x] 설정 가능한 최대 항목 수의 LRU 퇴거 정책
  - [x] 캐시된 결과에 대한 TTL 기반 만료
  - [x] 쓰기 작업 시 자동 캐시 무효화
  - [x] 대상 무효화를 위한 SQL 테이블 이름 추출
  - [x] shared_mutex를 사용한 스레드 안전 구현
  - [x] 종합적인 캐시 메트릭
- [x] IExecutor 인터페이스 통합 ([#45](https://github.com/kcenon/database_server/issues/45))
  - [x] 백그라운드 작업을 위한 선택적 IExecutor 주입
  - [x] Resilience 모듈: IExecutor를 통한 헬스 모니터링
  - [x] Gateway 모듈: IExecutor를 통한 비동기 쿼리 실행
  - [x] Executor 미제공 시 std::async로 폴백
  - [x] server_app에서 중앙화된 executor 관리

### Phase 4: 기반 시스템 개선 통합 ([#50](https://github.com/kcenon/database_server/issues/50))
- [x] IExecutor 인터페이스 통합 ([#45](https://github.com/kcenon/database_server/issues/45))
  - [x] 게이트웨이, 풀링, 복원력 모듈에서 통합 비동기 실행
  - [x] std::async 폴백이 포함된 선택적 IExecutor 주입
- [x] Result<T> 패턴 채택 ([#46](https://github.com/kcenon/database_server/issues/46))
  - [x] 게이트웨이 쿼리 처리에서 타입 안전 오류 처리
  - [x] 모든 프로토콜 핸들러에서 일관된 Result<T> 반환
  - [x] 캐시 및 라우터 동작에서 Result<T> 반환
- [x] C++20 모듈 마이그레이션 ([#47](https://github.com/kcenon/database_server/issues/47))
  - [x] 주 모듈: `kcenon.database_server`
  - [x] 파티션: `:core`, `:gateway`, `:pooling`, `:resilience`, `:metrics`
  - [x] 모듈 임포트를 사용한 깔끔한 의존성 관리
- [x] 프로토콜 핸들러용 CRTP 패턴 ([#48](https://github.com/kcenon/database_server/issues/48))
  - [x] 쿼리 처리에서 가상 디스패치 오버헤드 제로
  - [x] 7개 CRTP 핸들러: select, insert, update, delete, execute, ping, batch
  - [x] 런타임 다형성을 위한 타입 소거 래퍼
- [x] CRTP 기반 쿼리 메트릭 수집기 ([#49](https://github.com/kcenon/database_server/issues/49))
  - [x] `query_collector_base` CRTP 템플릿
  - [x] 메트릭: 쿼리 실행, 캐시 성능, 풀 활용도, 세션
  - [x] monitoring_system 패턴과의 통합

## 계획된 기능

다음 기능들이 향후 릴리스에 계획되어 있습니다:

| 기능 | 설명 | 상태 |
|------|------|------|
| QUIC 프로토콜 | 내장 TLS를 사용한 고성능 UDP 기반 전송 | 계획됨 |
| 쿼리 결과 캐시 | TTL 및 LRU 퇴거가 포함된 SELECT 쿼리 결과 인메모리 캐시 | ✅ 완료 ([#30](https://github.com/kcenon/database_server/issues/30)) |
| IExecutor 통합 | common_system IExecutor 인터페이스를 사용한 통합 비동기 실행 | ✅ 완료 ([#45](https://github.com/kcenon/database_server/issues/45)) |
| Result<T> 패턴 | common_system Result<T>를 사용한 타입 안전 오류 처리 | ✅ 완료 ([#46](https://github.com/kcenon/database_server/issues/46)) |
| C++20 모듈 | 파티션 모듈 구조의 모듈러 컴파일 | ✅ 완료 ([#47](https://github.com/kcenon/database_server/issues/47)) |
| CRTP 쿼리 핸들러 | 쿼리 처리에서 가상 디스패치 오버헤드 제로 | ✅ 완료 ([#48](https://github.com/kcenon/database_server/issues/48)) |
| CRTP 메트릭 수집기 | monitoring_system 패턴을 따르는 제로 오버헤드 쿼리 메트릭 수집 | ✅ 완료 ([#49](https://github.com/kcenon/database_server/issues/49)) |

## Executor 통합

서버는 중앙화된 스레드 관리를 위해 `common_system`의 선택적 `IExecutor` 주입을 지원합니다. 이를 통해 컴포넌트 간에 단일 스레드 풀을 공유하여 효율적인 리소스 활용이 가능합니다.

### 사용 예제

```cpp
#include <kcenon/database_server/server_app.h>
#include <kcenon/thread/adapters/common_executor_adapter.h>
#include <kcenon/thread/core/thread_pool.h>

// 서버 앱 생성
database_server::server_app app;
app.initialize("config.conf");

// 공유 executor 생성 (선택)
auto pool = std::make_shared<kcenon::thread::thread_pool>("shared_executor", 4);
pool->start();
auto executor = kcenon::thread::adapters::common_executor_factory::create_from_thread_pool(pool);

// 통합 스레드 관리를 위한 executor 주입
app.set_executor(executor);

// 서버 시작 - 비동기 쿼리와 헬스 모니터링이 공유 executor를 사용
app.run();
```

### IExecutor 지원 컴포넌트

| 컴포넌트 | 메서드 | 설명 |
|----------|--------|------|
| `server_app` | `set_executor()` | 중앙화된 executor 관리 |
| `query_router` | `set_executor()` | 비동기 쿼리 실행 |
| `connection_health_monitor` | 생성자 | 백그라운드 헬스 모니터링 |
| `resilient_database_connection` | 생성자 | 헬스 모니터로 전파 |

Executor가 제공되지 않으면 컴포넌트는 백그라운드 작업을 위해 자동으로 `std::async`로 폴백합니다.

## 메트릭 수집

서버는 제로 오버헤드 수집을 위해 monitoring_system의 수집기 패턴을 따르는 CRTP 기반 메트릭 수집기를 포함합니다.

### 메트릭 카테고리

| 카테고리 | 메트릭 | 설명 |
|----------|--------|------|
| 쿼리 실행 | total/success/failed/timeout, latency | 쿼리 성능 추적 |
| 캐시 성능 | hits/misses, hit ratio, evictions | 캐시 효율성 모니터링 |
| 풀 활용도 | active/idle 커넥션, acquisition time | 커넥션 풀 상태 |
| 세션 관리 | active sessions, auth events, duration | 세션 생명주기 추적 |

### 사용 예제

```cpp
#include <kcenon/database_server/metrics/query_metrics_collector.h>

using namespace database_server::metrics;

// 글로벌 수집기 획득
auto& collector = get_query_metrics_collector();

// 설정으로 초기화
collector.initialize({
    {"enabled", "true"},
    {"track_query_types", "true"}
});

// 쿼리 실행 기록
query_execution exec;
exec.query_type = "select";
exec.latency_ns = 1500000;  // 1.5ms
exec.success = true;
collector.collect_query_metrics(exec);

// 캐시 동작 기록
cache_stats cache;
cache.hit = true;
collector.collect_cache_metrics(cache);

// 모니터링을 위한 메트릭 획득
const auto& metrics = collector.get_metrics();
double avg_latency = metrics.query_metrics.avg_query_latency_ms();
double cache_hit_ratio = metrics.cache_metrics.cache_hit_ratio();
```

### Monitoring System 통합

```cpp
// 모니터링 통합 초기화
initialize_monitoring_integration("my_database_server");

// 커스텀 모니터링 시스템을 위한 내보내기 콜백 설정
set_metrics_export_callback([](const std::vector<monitoring_metric>& metrics) {
    for (const auto& m : metrics) {
        // 모니터링 시스템으로 내보내기
        push_to_prometheus(m.name, m.value, m.tags);
    }
});

// 현재 메트릭 내보내기
export_metrics_to_monitoring();

// 헬스 엔드포인트용 메트릭 획득
auto health_metrics = get_metrics_for_health_endpoint();
```

## 보안

### Session ID 생성

세션 ID는 암호학적으로 안전한 방법을 사용하여 생성됩니다:

- **128비트 엔트로피**: 예측 불가능성을 위해 두 개의 64비트 랜덤 값 사용
- **하드웨어 시딩**: 하드웨어 엔트로피를 위해 `std::random_device` 사용
- **스레드 안전**: 스레드 로컬 RNG로 경합 및 예측 가능성 방지
- **형식**: 32자 소문자 16진수 문자열

세션 ID 형식은 예측 가능한 타임스탬프-카운터 패턴에서 안전한 랜덤 값으로 변경되었습니다. 이전 형식(`session_TIMESTAMP_COUNTER`)은 ID 예측을 통한 세션 하이재킹에 취약했습니다.

### 보안 모범 사례

1. **인증**: 프로덕션 환경에서 `auth.enabled=true` 활성화
2. **Rate Limiting**: 남용 방지를 위해 적절한 Rate Limit 구성
3. **TLS**: 암호화된 연결을 위해 TLS 활성화 (`network.enable_tls=true`)
4. **로깅**: 프로덕션 환경에서 전체 세션 ID 로깅 방지

## 라이선스

BSD 3-Clause License - 자세한 내용은 [LICENSE](LICENSE)를 참조하세요.

## CI/CD

이 프로젝트는 database_system 파이프라인과 일치하는 종합적인 CI/CD 워크플로우를 포함합니다:

### 핵심 워크플로우

| 워크플로우 | 설명 | 상태 |
|-----------|------|------|
| `ci.yml` | 멀티 플랫폼 빌드 (Ubuntu, macOS, Windows) | 활성 |
| `integration-tests.yml` | 커버리지 포함 통합 테스트 | 활성 |
| `coverage.yml` | Codecov 코드 커버리지 | 활성 |

### 품질 게이트

| 워크플로우 | 설명 | 상태 |
|-----------|------|------|
| `static-analysis.yml` | clang-tidy 및 cppcheck 분석 | 활성 |
| `sanitizers.yml` | ASan, TSan, UBSan 검사 | 활성 |

### 보안 및 문서

| 워크플로우 | 설명 | 상태 |
|-----------|------|------|
| `dependency-security-scan.yml` | Trivy 취약점 스캐닝 | 활성 |
| `sbom.yml` | SBOM 생성 (CycloneDX, SPDX) | 활성 |
| `build-Doxygen.yaml` | API 문서 생성 | 활성 |

### 성능

| 워크플로우 | 설명 | 상태 |
|-----------|------|------|
| `benchmarks.yml` | 성능 회귀 테스트 | 활성 |

## 관련 프로젝트

- [common_system](../common_system) - 핵심 유틸리티 및 인터페이스
- [thread_system](../thread_system) - 스레딩 및 작업 스케줄링
- [network_system](../network_system) - 네트워크 통신
- [database_system](../database_system) - 데이터베이스 클라이언트 라이브러리
