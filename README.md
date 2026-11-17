# Multithreaded HTTP Server & Load Balancer (C++17)

**At a glance:** HTTP/1.1 server + round-robin load balancer in C++17 on raw POSIX sockets, thread pool, zero dependencies.
**22,199 requests/sec at 11 ms p99** (keep-alive, 100 concurrent), **0 failed** across 50,000 requests.
Load balancer splits 30,000 requests exactly evenly (10,000 / 10,000 / 10,000) across 3 backends.

A concurrent HTTP/1.1 static-file server with a built-in round-robin load
balancer, written in modern C++17 using only the standard library and POSIX
sockets. No third-party dependencies beyond `pthread`.

- **Thread-pool architecture** — a fixed set of workers pull connection tasks
  from a shared `std::queue` guarded by `std::mutex` + `std::condition_variable`.
- **RAII everywhere** — every socket lives in a move-only `Socket` wrapper that
  closes its descriptor in the destructor. No raw `new`/`delete`.
- **Round-robin reverse proxy** — a second mode fronts N backend servers and
  relays bytes bidirectionally with `poll()`.
- **Graceful shutdown** — `SIGINT`/`SIGTERM` drain the queue, join all workers,
  and print metrics.
- Compiles warning-clean under `-std=c++17 -Wall -Wextra -Werror -pedantic`.

---

## Architecture

### Server mode

```
                         ┌──────────────────────────────────────────┐
   clients               │              http_server                 │
  ┌───────┐   accept     │   ┌───────────────┐                      │
  │  ab   │──────────────┼──▶│  accept loop  │  poll(listen_fd,500ms)│
  │ curl  │              │   │ (main thread) │  checks g_stop flag   │
  │  ...  │              │   └───────┬───────┘                      │
  └───────┘              │           │ submit(task)                 │
                         │           ▼                              │
                         │   ┌───────────────┐  shared task queue   │
                         │   │  ThreadPool   │  ┌──┬──┬──┬──┐        │
                         │   │  mutex + cv   │  │t │t │t │t │        │
                         │   └──┬──┬──┬──┬───┘  └──┴──┴──┴──┘        │
                         │      │  │  │  │   worker threads          │
                         │      ▼  ▼  ▼  ▼                           │
                         │   handle_connection():                   │
                         │     read → parse → resolve file          │
                         │     → build response → write → log       │
                         │     (keep-alive loop, 5s idle timeout)   │
                         │            │                             │
                         │            ▼   ./public/*                │
                         │   ┌──────────────────┐                   │
                         │   │ static file root │ (realpath-guarded)│
                         │   └──────────────────┘                   │
                         └──────────────────────────────────────────┘
```

### Load-balancer mode

```
                    ┌───────────────────────────────┐        ┌──────────────┐
  ┌───────┐         │   http_server --load-balancer │  RR #0 │ backend :8081│
  │ client│────────▶│  accept loop → ThreadPool     │───────▶│ (http_server)│
  └───────┘  :8080  │                               │        └──────────────┘
                    │  handle_connection():         │  RR #1 ┌──────────────┐
                    │   port = backends[i++ % N]    │───────▶│ backend :8082│
                    │   connect_local(port)         │        └──────────────┘
                    │   relay both ways via poll()  │  RR #2 ┌──────────────┐
                    │   until EOF / 5s idle         │───────▶│ backend :8083│
                    └───────────────────────────────┘        └──────────────┘
```

### Source layout

```
project/
├── src/
│   ├── main.cpp            # CLI parsing, signal setup, mode dispatch
│   ├── config.{h,cpp}      # argv -> Config; --help text
│   ├── socket.{h,cpp}      # RAII Socket + net helpers (listen/connect/rw)
│   ├── thread_pool.{h,cpp} # worker pool over a shared task queue
│   ├── http_request.{h,cpp}# request line + headers + body parser
│   ├── http_response.{h,cpp}# response builder, status text, MIME types
│   ├── server.{h,cpp}      # accept loop + static-file connection handler
│   ├── load_balancer.{h,cpp}# round-robin reverse proxy + relay
│   └── logger.{h,cpp}      # thread-safe access log + metrics
├── public/                 # static file root (index.html, hello.txt)
├── CMakeLists.txt
└── README.md
```

> `socket.{h,cpp}` and `logger.{h,cpp}` are shared utilities used by both the
> server and the load balancer; they factor the RAII socket wrapper and logging
> out of the per-mode code.

---

## Build

### With CMake (recommended)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# binary at ./build/http_server
```

Debug build (same strict warnings, `-g -O0`):

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

### Directly with g++

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 -pthread \
    src/*.cpp -o http_server
```

Compiles with **zero warnings** under the required flag set.

---

## Run

### Server mode

```bash
./http_server --port 8080 --root ./public --threads 4
curl -i  http://localhost:8080/
curl -I  http://localhost:8080/          # HEAD: headers only, no body
curl -i  http://localhost:8080/hello.txt
curl -i -X POST --data 'name=heli' http://localhost:8080/submit
```

### Load-balancer mode

Start backends, then the balancer in front of them:

```bash
./http_server --port 8081 --root ./public --threads 4 &
./http_server --port 8082 --root ./public --threads 4 &
./http_server --port 8083 --root ./public --threads 4 &
./http_server --load-balancer --frontend-port 8080 --backends 8081,8082,8083 --threads 8

curl http://localhost:8080/     # each request round-robins to the next backend
```

### Help

```bash
./http_server --help
```

Shut down either mode with `Ctrl-C` (`SIGINT`); the queue drains, workers join,
and a metrics summary is printed.

---

## Feature checklist

| Requirement | Where |
| --- | --- |
| HTTP/1.1 over POSIX sockets, GET + HEAD | `server.cpp`, `http_request.cpp` |
| Static files from configurable root | `HttpServer::resolve_file` |
| 200 / 400 / 403 / 404 / 405 / 500 with headers | `http_response.cpp` |
| Malformed requests handled (no crash) | `parse_request` → 400 |
| Thread pool (`thread`/`mutex`/`condition_variable`/`queue`) | `thread_pool.cpp` |
| Configurable worker count (default 4) | `--threads`, `Config` |
| Graceful shutdown: drain queue, join threads | `ThreadPool::shutdown` |
| Round-robin load balancer on frontend port | `load_balancer.cpp` |
| Non-blocking forwarding (`poll`) | `LoadBalancer::handle_connection` |
| RAII sockets, `unique_ptr`/`shared_ptr`, no raw new/delete | `Socket`, `make_shared` |
| `EINTR` + partial read/write handling | `net::read_some` / `write_all` |
| CLI args + `--help` | `config.cpp` |
| Access log: IP, method, path, status, latency | `Logger::access` |
| Metrics on shutdown | `Logger::print_metrics` |
| `SO_REUSEADDR` | `net::create_listener` |
| `SIGPIPE` ignored + `MSG_NOSIGNAL` | `main.cpp`, `net::write_all` |
| 5s idle timeout | `SO_RCVTIMEO` + `poll` timeout |
| POST body parsing (bonus) | `parse_request`, `build_response` |

Path traversal (`GET /../../../etc/passwd`) is rejected with **403** via
`realpath()` prefix-checking against the canonical root.

---

## Performance benchmarks

Measured with ApacheBench (`ab`) on this repo's container. **The machine has a
single vCPU (`nproc == 1`)**, so these are conservative single-core numbers;
multi-core hosts scale further.

### 1. Direct server — 100 concurrent, connection-per-request

```
ab -c 100 -n 30000 http://localhost:8080/     # --threads 4
```

| Metric | Value |
| --- | --- |
| Concurrency | 100 |
| Complete requests | 30000 |
| **Failed requests** | **0** |
| Requests/sec | 10,723 |
| Mean latency (per request) | 9.3 ms |
| p50 / p90 / p99 / p100 | 9 / 11 / 14 / 17 ms |
| Transfer rate | 8.8 MB/s |

### 2. Through the load balancer — 100 concurrent, 3 backends

```
ab -c 100 -n 30000 http://localhost:8080/     # LB 8 thr -> 3 backends x 4 thr
```

| Metric | Value |
| --- | --- |
| Complete requests | 30000 |
| **Failed requests** | **0** |
| Requests/sec | 5,055 |
| p50 / p99 | 19 / 24 ms |
| Backend distribution | 10000 / 10000 / 10000 (exactly even) |

### 3. Keep-alive — 100 concurrent

```
ab -k -c 100 -n 50000 http://localhost:8080/  # --threads 128
```

| Metric | Value |
| --- | --- |
| Complete requests | 50000 |
| **Failed requests** | **0** |
| Keep-Alive requests | 49,542 |
| Requests/sec | 22,199 |
| p50 / p99 | 4 / 11 ms |

### Concurrency model (important)

This is a **thread-per-active-connection pool**. A worker owns a connection for
that connection's lifetime, which is the natural fit for the required *5-second
idle timeout* and blocking I/O model.

- **Short-lived requests** (`Connection: close`, the `ab` default): throughput is
  bounded by CPU, *not* by thread count. 100 (and 200) concurrent connections
  ran with **0 failures on just 4 threads** — extra connections queue in the
  listen backlog + task queue and drain in microseconds.
- **Keep-alive connections** stay bound to a worker until they close or hit the
  5s idle timeout. To serve *T* simultaneously-active keep-alive connections,
  run with `--threads >= T` (e.g. `--threads 128` for the run above). This is
  the classic trade-off of a blocking thread-pool design; an `epoll`/reactor
  event loop would decouple connection count from thread count at the cost of
  much more complex code.

---

## What I learned

- **RAII makes socket lifetime boring — in a good way.** Once every `fd` lives in
  a move-only `Socket`, "who closes this and when" stops being a question:
  scope exit and `std::vector`/`shared_ptr` destruction do it exactly once.
  Passing an accepted socket into a `std::function` task forced a real design
  choice — `std::function` must be copyable, so ownership is handed to a
  `shared_ptr<Socket>` the task moves out of.
- **Signals are their own little world.** A handler may only touch a
  `volatile sig_atomic_t`, so the accept loop `poll()`s with a 500 ms timeout
  and re-checks the flag rather than trying to be woken by the signal directly.
  Deliberately *not* using `SA_RESTART` lets blocking calls return `EINTR` so
  shutdown is responsive.
- **The network never gives you all the bytes at once.** Every `recv`/`send`
  needs an `EINTR` retry, `send` must loop over partial writes, and a request
  parser has to distinguish *malformed* (→ 400) from *not finished yet*
  (→ read more). Getting the "incomplete vs bad" split right is most of a robust
  HTTP parser.
- **`SIGPIPE` will kill you silently.** Writing to a peer that already hung up
  raises `SIGPIPE` and terminates the process unless you ignore it *and* pass
  `MSG_NOSIGNAL` — belt and suspenders.
- **Path traversal is a canonicalization problem.** String-prefix checks on the
  raw path are not enough; resolving both the root and the target with
  `realpath()` and comparing canonical prefixes is what actually stops
  `/../../etc/passwd`.
- **"100 concurrent connections" and "thread pool" pull in different
  directions.** A blocking pool trivially handles 100 short requests but caps
  *simultaneous keep-alive* connections at the thread count — a limit worth
  understanding and stating rather than hiding.

---

## Possible extensions

LRU file cache, TLS via OpenSSL, an `epoll` reactor to decouple connections from
threads, health checks / least-connections balancing, and a JSON config file
parser. The current code keeps to the stated constraint of **no dependencies
beyond `pthread` and the C++ standard library**.
