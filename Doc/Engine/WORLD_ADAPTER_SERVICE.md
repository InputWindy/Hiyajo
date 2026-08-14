# Maho World Adapter Service

## Status and scope

Agent Core v0.4.2 adds an optional C++ World Adapter Protocol v1 service and a
standalone process named `MahoWorldAdapterHarness`. The harness proves the
cross-language boundary used by the existing Node.js `RemoteWorldAdapter`:

```text
Node RemoteWorldAdapter
  -> IPv4 loopback HTTP
  -> FWorldAdapterHttpTransport worker
  -> FWorldAdapterService
  -> FWorldAdapterCommandQueue
  -> Harness main-thread pump
  -> FStubWorldBackend
```

This is deliberately not an `FWorld` integration. `FStubWorldBackend` is
harness-only and test-only in-memory state; it is not the engine's entity,
Actor, Component, Scene, Level, Transform, Render, RHI, resource, physics, or
undo architecture. The harness does not create `FApp`, start an editor or game,
or register itself with normal Maho runtime startup.

The protocol version remains `1.0`. The authoritative protocol sources are:

- `Tools/AgentBridge/docs/MAHO_WORLD_ADAPTER_PROTOCOL_V1.md`
- `Tools/AgentBridge/src/world/world-adapter-contract.mjs`
- `Tools/AgentBridge/src/world/remote-world-adapter.mjs`
- `Tools/AgentBridge/tests/fixtures/world-adapter-v1/`

## Build targets and options

Both options default to `OFF`:

| Option | Effect |
| --- | --- |
| `MAHO_BUILD_WORLD_ADAPTER` | Builds `MahoWorldAdapter` and `MahoWorldAdapterHarness` |
| `MAHO_BUILD_WORLD_ADAPTER_TESTS` | Enables testing, enables the adapter, builds `MahoWorldAdapterTests`, and registers it with CTest |

The targets are:

| Target | Role |
| --- | --- |
| `MahoWorldAdapter` / `Maho::WorldAdapter` | Static protocol, queue, backend, service, and transport library |
| `MahoWorldAdapterHarness` | Standalone loopback service using only the Stub Backend |
| `MahoWorldAdapterTests` | Lightweight unit, fixture, concurrency, HTTP, and lifecycle test executable |

When the options are off, the harness and HTTP transport are not built, no
listener is created, and `Maho::Engine` and `Maho::Modules` retain their current
public semantics.

Example configure and serial Debug build:

```powershell
cmake -S Build -B Intermediate `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_GENERATOR_INSTANCE="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools" `
  -DMAHO_BUILD_SHARED=ON `
  -DMAHO_BUILD_WORLD_ADAPTER=ON `
  -DMAHO_BUILD_WORLD_ADAPTER_TESTS=ON

cmake --build Intermediate --config Debug --parallel 1
ctest --test-dir Intermediate -C Debug --output-on-failure
```

## Source boundaries

The component lives under `Maho/WorldAdapter/`, outside the engine source glob.
Its public DTOs own strings, arrays, and JSON values. They never retain an
`FWorld`, `UObject`, `FObjectRef`, HTTP request, socket, iterator, container
reference, or JSON document reference.

- `Protocol`: owning DTOs, structured protocol errors, strict JSON parsing,
  validation, serialization, and canonical payload fingerprints.
- `Core`: the typed backend interface, bounded command queue, request state,
  waiting, cancellation, and shutdown behavior.
- `Stub`: the harness-only in-memory backend, revision, UUID generation, and
  bounded idempotency cache.
- `Transport`: the narrow loopback HTTP server. HTTP and Winsock types do not
  cross into the Backend interface.
- `Service`: owns and connects Backend, Queue, and Transport.
- `Harness`: owns the Service and runs the only backend pump thread.
- `Tests`: a small internal test runner; no external test framework is added.

## DTO validation and golden fixtures

The validator explicitly checks required and unknown fields, JSON types,
string/array/body limits, UUIDs, non-negative safe integer revisions and
generations, finite Transform values, object-valued `args`, tool names,
capability dependencies, Minimal Profile limits, and request/response
correlation. Parse or validation exceptions are caught and converted into safe
structured errors before crossing service, thread, or HTTP boundaries.

`MahoWorldAdapterTests` reads every file directly from
`Tools/AgentBridge/tests/fixtures/world-adapter-v1/`. CMake supplies the fixture
root through `MAHO_WORLD_ADAPTER_FIXTURE_ROOT`; the fixtures are not copied.
Canonical JSON processing makes object field order irrelevant.

The harness advertises exactly the existing Minimal World Profile:

```json
{
  "supports_atomic_transactions": false,
  "supports_dry_run": false,
  "supports_undo": false,
  "supports_idempotency": true,
  "max_tool_calls": 1,
  "supported_tools": [
    "world.get_summary",
    "entity.spawn_primitive",
    "entity.set_transform"
  ]
}
```

## Backend, queue, and thread model

`IWorldAdapterBackend` exposes typed capabilities, snapshot, execute, undo, and
shutdown operations. It has no HTTP, Node.js, or socket dependency. After the
service reads capabilities during initialization, world operations are called
only by the pump thread.

HTTP workers parse and validate a request, create an owning command envelope,
enqueue it, and wait for a finite result. The queue supports multiple producers,
has configurable bounded capacity, and has one pump owner. It never holds its
mutex while the backend executes or while HTTP I/O occurs. The harness main
thread repeatedly calls `FWorldAdapterService::Pump`; it never waits for an HTTP
worker.

The request state machine is:

```text
queued -> executing -> completed
   |          |
   |          +-> timed_out -> completed authoritatively
   +-> timed_out / cancelled
```

- A queued timeout marks the command timed out; the pump skips it, no revision
  changes, and no success is cached.
- An executing timeout does not interrupt the Backend. The authoritative result
  and idempotency entry are stored when the short command finishes. A retry of
  the same request ID replays that result without executing again.
- A disconnected client cannot cause a second execution. A write already in
  progress remains authoritative; a command not yet started may be cancelled.
- A full queue returns a structured backpressure response. Queue waits always
  have a deadline, and shutdown wakes every waiter.

## Stub state, revision, and idempotency

`FStubWorldBackend` stores small per-session/per-world maps of primitive DTOs.
Entity IDs are generated RFC 4122 version-4 UUID strings, never pointer values;
generation is `1`. Snapshot copies ordinary DTOs and exposes no internal
container state.

The initial revision is `0`. Health, snapshot, summary, failures, queued
timeouts, and replay do not change it. Successful spawn and transform each
increment it once. Write responses carry the actual before/after revisions.
Undo returns `UNDO_NOT_AVAILABLE`, creates no token, and changes no state.

Idempotency is scoped by `(session_id, world_id, request_id, operation)`. A
canonical object-order-independent fingerprint distinguishes payloads. A match
returns the stored authoritative response with `replayed=true`; a mismatch
returns a request-ID conflict without execution. The configurable cache has a
finite default size and LRU eviction. Concurrent duplicate requests converge on
one queue state and one backend execution. The cache is process-local and is
not persisted after shutdown.

## HTTP and authentication

The transport exposes only:

- `GET /world-adapter/v1/health`
- `POST /world-adapter/v1/snapshot`
- `POST /world-adapter/v1/execute`
- `POST /world-adapter/v1/undo`

It always binds the IPv4 loopback address `127.0.0.1`; there is no `0.0.0.0`
configuration and no firewall manipulation. Port `8770` is the default and
port `0` requests an OS-selected loopback port. The fixed worker pool and
accepted-connection queue are bounded. Requests use strict content type and
header handling, a 1 MiB body limit, and a 4 MiB response limit. Logs do not
contain request bodies, bearer tokens, or Authorization headers.

Authentication is optional for local development. Configure it with the
`MAHO_WORLD_AUTH_TOKEN` environment variable or the harness `--auth-token`
argument. The environment variable is preferred because command-line arguments
may be visible to process-inspection tools. Token comparison examines the full
received and expected values; the token is not emitted in ready output, errors,
or logs.

The server is intentionally narrow and Windows-only in v0.4.2. It uses the
Windows SDK Winsock server API and does not access a LAN or the public Internet.

## Lifecycle and shutdown

Service initialization validates configuration, creates Backend/Queue/Transport,
binds loopback, and only then begins accepting. A bind or initialization failure
rolls back owned objects. Shutdown is idempotent:

```text
BeginShutdown
  -> stop accepting
  -> close queued connections
  -> cancel queued commands and wake waiters
  -> allow an executing short command to finish
Shutdown
  -> join accept and worker threads
  -> shut down Backend
  -> destroy Transport, Queue, and Backend
```

The destructor does not throw. Transport lifetime never exceeds Service, and
Backend lifetime extends through the final authoritative command completion.
Tests cover repeated shutdown, bind rollback, worker joins, and immediate port
reuse.

## Harness and cross-language smoke

The Debug harness is normally at:

```text
Binaries/Win64/Debug/MahoWorldAdapterHarness.exe
```

Supported arguments:

```text
--port <0-65535>
--queue-capacity <1-65536>
--request-timeout-ms <1-600000>
--idempotency-capacity <1-65536>
--auth-token <token>
```

At startup it writes exactly one machine-readable line like:

```text
MAHO_WORLD_ADAPTER_READY {"host":"127.0.0.1","port":12345,"protocol":"1.0"}
```

Write `shutdown` followed by a newline to stdin for a clean exit. Console Ctrl-C
also begins shutdown. No public shutdown endpoint is added.

The Node.js smoke launches this executable on a random port and uses the
existing `RemoteWorldAdapter` contract implementation:

```powershell
cd Tools\AgentBridge
$env:MAHO_WORLD_ADAPTER_HARNESS = "C:\path\to\MahoWorldAdapterHarness.exe"
npm run smoke:maho-cpp
```

It verifies health, Minimal Profile negotiation, snapshots, summary, spawn,
transform, request-ID replay, unsupported undo, revisions, UUIDs, process
cleanup, and port release. It does not select an AI Provider, use a fake remote
server, fall back to `MockWorldAdapter`, or contact any non-loopback address.

## Dependencies, offline behavior, and v0.5

No new downloadable third-party dependency is added. `MahoWorldAdapter` reuses
the repository's pinned nlohmann/json 3.11.3 headers under `Maho/ThirdParty`
(MIT license), plus C++ standard library threads and the Windows SDK `ws2_32`
system library. Enabling WorldAdapter therefore introduces no new fetch and has
no new offline-build requirement. Disabled builds do not compile or link the
transport.

For v0.5, replace the Stub Backend with an explicit, opt-in backend that maps
owning DTOs to a real `FWorld` on its owning thread. That work must define the
real entity/Transform boundary, lifetime and shutdown integration, and game or
editor registration. The protocol DTOs, strict validation, queue, request state,
transport security, and cross-language tests should remain the boundary unless
the separately versioned protocol requires a change. v0.4.2 does not begin any
of that integration.
