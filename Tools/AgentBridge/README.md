# Maho AgentBridge

AgentBridge is a loopback-only Node.js service used by the existing Maho editor
Agent panel. It preserves the legacy chat API and Agent Protocol v1 while
providing Agent Core with a selectable, capability-negotiated `WorldAdapter`.

Agent Core v0.4.1 adds **World Adapter Capability Negotiation and the Minimal
World Profile** on top of the v0.4 remote protocol. It keeps the v0.3 Generic AI Provider architecture: real model
planning is not coupled to Cursor SDK, and MockProvider, DeepSeek, generic
OpenAI-compatible Chat Completions, and the optional CursorProvider implement
one internal Provider contract.

Maho v0.4.2 adds an optional standalone C++ World Adapter Harness for protocol
and transport conformance. Its in-memory Stub Backend does **not** connect to
the real C++ game world. Agent Core does not expose shell commands, file tools,
Lua execution, C++ reflection, pointers, WebSockets, rendering, physics, or
multiplayer features.

## Architecture

The v0.4.1 request path is:

```text
natural language
  -> AgentService
  -> ProviderRegistry
       -> MockProvider
       -> OpenAICompatibleProvider -> DeepSeek preset
       -> CursorProvider
  -> structured ToolCall
  -> ToolRegistry + Ajv validation
  -> Session WorldAdapter capability validation
  -> CommandExecutor
  -> WorldAdapterFactory
       -> MockWorldAdapter -> MockWorld + UndoJournal
       -> RemoteWorldAdapter -> Maho World Adapter Protocol v1
            -> optional C++ Harness -> bounded queue -> Stub Backend
  -> authoritative ToolResult + ChangeSet
  -> HTTP response + JSONL audit log
```

`server.mjs` only loads configuration, creates the service objects, registers
the HTTP router, starts the server, and coordinates shutdown. Business logic is
under `src/`.

Provider code can only plan ToolCalls. It cannot access MockWorld or a remote
transport. `CommandExecutor` remains the only Agent Core execution path, and
the selected WorldAdapter is authoritative for snapshots, revisions, changes,
transactions, idempotency, and undo.

The existing `@cursor/sdk` dependency is retained. Version 1.0.26 exposes
`customTools`; `CursorProvider` uses those callbacks only to capture internal
ToolCalls and runs the SDK in plan mode. The callback does not execute world
operations. The SDK is dynamically imported only when Cursor is selected.

OpenAICompatibleProvider uses the Node.js 22 built-in `fetch` and standard Chat
Completions `/chat/completions`; no additional HTTP SDK is required. DeepSeek
is a preset over this implementation, not a separate network stack.

Ajv is a direct dependency because tool contracts are published and compiled
as JSON Schema. Every v1 tool argument schema rejects additional properties.

## Requirements and install

- Node.js **22.13 or newer** (`@cursor/sdk@1.0.26` requires this)
- npm

```powershell
cd Tools\AgentBridge
npm install
npm test
npm run eval
npm run eval:remote
npm run eval:remote:minimal
npm run smoke:remote
```

The remote eval and smoke commands start an isolated fake world server on a
random loopback port. `eval:remote:minimal` runs five cases against the
three-tool Minimal World Profile. They do not contact a real external service.

All default tests use `node:test`, random loopback ports, local fake HTTP
servers, independent Sessions, and offline world adapters. No DeepSeek/Cursor API key,
external network service, C++ game, or MyGame checkout is required.

The verified v0.4.1 install reports zero dependency vulnerabilities. This task
does not run `npm audit fix --force` or upgrade unrelated dependencies.

## Provider selection

Selection order is deterministic:

1. `MAHO_AGENT_MOCK=1` forces MockProvider.
2. `MAHO_AI_PROVIDER` selects an explicit Provider.
3. Without an explicit Provider, legacy `CURSOR_API_KEY` selects Cursor.
4. Otherwise AgentBridge uses MockProvider.

`DEEPSEEK_API_KEY` alone never selects DeepSeek. Real Provider failures never
fall back to Mock, because fallback would hide configuration, authentication,
or model failures.

Supported Provider IDs:

- `mock` — deterministic and offline; the default.
- `deepseek` — the DeepSeek preset over OpenAICompatibleProvider.
- `openai-compatible` — a caller-configured Chat Completions endpoint.
- `cursor` — the retained optional Cursor SDK implementation.

See [AI Provider Architecture](docs/AI_PROVIDER_ARCHITECTURE.md) for the
contract and lifecycle, and [DeepSeek Setup](docs/DEEPSEEK_SETUP.md) for real
API configuration.

## World adapter selection

World selection is independent of Provider selection:

- `mock` is the default and requires no network service.
- `remote` is selected only by `MAHO_WORLD_ADAPTER=remote`.
- A selected remote adapter never falls back to mock after configuration,
  health, transport, protocol, validation, or execution failures.

Remote health capabilities may truthfully disable atomic batches, dry-run, or
undo and may advertise a non-empty subset of the eight ToolRegistry names.
AgentService exposes only that stable subset to the selected Provider, while
CommandExecutor independently rejects unsupported Provider output before any
world call. Non-atomic adapters are limited to one ToolCall per request; a
batch is never split. Unsupported dry-run is not converted to real execution,
and unsupported undo returns `UNDO_NOT_AVAILABLE` without a remote undo call.

The Minimal World Profile supports only:

- `world.get_summary`
- `entity.spawn_primitive`
- `entity.set_transform`

It declares `max_tool_calls=1`, no atomic transactions, no dry-run, no undo,
and required request-id idempotency. This is still World Adapter Protocol v1.

The remote protocol is a development integration boundary, not a public
Internet API. Remote URLs are restricted to `127.0.0.1`, `localhost`, or
`::1` by default.
A non-loopback URL requires both `MAHO_WORLD_ALLOW_NON_LOOPBACK=1` and a
non-empty `MAHO_WORLD_AUTH_TOKEN`. The token is sent only as a bearer
authorization header and is excluded from metadata, CLI output, and audit
records.

See [World Adapter Architecture](docs/WORLD_ADAPTER_ARCHITECTURE.md) and
[Maho World Adapter Protocol v1](docs/MAHO_WORLD_ADAPTER_PROTOCOL_V1.md).

## Maho C++ World Adapter Harness

The optional v0.4.2 harness is built from the repository root. It is a protocol
and lifecycle integration process, not MyGame and not a real `FWorld`:

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

Run the cross-language smoke against the real harness executable:

```powershell
cd Tools\AgentBridge
$env:MAHO_WORLD_ADAPTER_HARNESS = "C:\path\to\Binaries\Win64\Debug\MahoWorldAdapterHarness.exe"
npm run smoke:maho-cpp
```

The smoke starts the harness on a random IPv4 loopback port, exercises the
existing `RemoteWorldAdapter`, closes the process even after a failure, and
checks that the port was released. It never selects a Provider and has no fake
server or MockWorld fallback. If bearer authentication is required, set the
same `MAHO_WORLD_AUTH_TOKEN` for the harness and Node adapter; it is never
printed in the ready line.

See [Maho World Adapter Service](../../Doc/Engine/WORLD_ADAPTER_SERVICE.md) for
the C++ targets, DTO validation, queue/thread model, timeout and idempotency
semantics, shutdown order, dependencies, and platform limits.

## Run

Default:

```powershell
cd Tools\AgentBridge
npm start
```

Legacy-compatible explicit command:

```powershell
node server.mjs --port 8765 --cwd C:\path\to\MyGame
```

Force deterministic Mock mode:

```powershell
$env:MAHO_AGENT_MOCK = "1"
npm start
```

Without an explicit Provider or `CURSOR_API_KEY`, Mock mode is selected
automatically. `--api-key` and `--api-key-file` are retained for compatibility
with the existing C++ client and select Cursor.
Their precedence is:

1. `--api-key`
2. `--api-key-file`
3. `CURSOR_API_KEY`

## CLI demo

The CLI creates its own Session, WorldAdapter, ToolRegistry, CommandExecutor,
AgentService, and selected Provider. It does not start the AgentBridge HTTP
server or occupy a port. With no Provider or world environment variables it
uses MockProvider + MockWorldAdapter and remains fully offline:

```powershell
cd Tools\AgentBridge
npm run demo
```

Remote CLI demo against the included fake server:

```powershell
# Window 1
npm run world:fake

# Window 2
$env:MAHO_WORLD_ADAPTER = "remote"
$env:MAHO_WORLD_BASE_URL = "http://127.0.0.1:8770"
npm run demo
```

Set `MAHO_WORLD_FAKE_PROFILE=minimal` in Window 1 to run the same fake server
with the three-tool Minimal World Profile; `full` remains the default.

Enter natural-language commands directly. Built-in commands are:

- `/help`
- `/world` for a formatted snapshot
- `/entities` for a compact entity list
- `/undo` through the existing Agent/Undo path
- `/reset` for a new isolated Session
- `/exit`

Ctrl+C exits normally. Command errors are displayed without crashing the
CLI. Startup shows the selected adapter, supported tool count, and its
atomic/dry-run/undo capability flags, Provider, model, Mock/real mode, and
`Thinking: disabled`; it never prints a Key or token.

Select DeepSeek:

```powershell
$env:MAHO_AI_PROVIDER = "deepseek"
$env:DEEPSEEK_API_KEY = "replace_me"
npm run demo
```

Select Cursor:

```powershell
$env:MAHO_AI_PROVIDER = "cursor"
$env:CURSOR_API_KEY = "replace_me"
npm run demo
```

Select a generic OpenAI-compatible endpoint:

```powershell
$env:MAHO_AI_PROVIDER = "openai-compatible"
$env:MAHO_AI_BASE_URL = "https://provider.example/v1"
$env:MAHO_AI_MODEL = "provider-model"
$env:MAHO_AI_API_KEY = "replace_me"
npm run demo
```

Do not place real Key values in checked-in files or shell history.

## Behavior evaluations

Run all checked-in deterministic behavior cases with:

```powershell
npm run eval
```

Every scenario uses MockProvider with a fresh Session and MockWorldAdapter. No Cursor
API key, model service, or external network is used. JSON files under
`evals/cases/` define single- and multi-turn conversations separately from the
runner. The suite checks replies, tool names/counts, revision changes, undo
creation, final entity counts, transforms/properties, clarification, and
no-world-change behavior. A failure prints its scenario and turn with expected
and actual values and exits nonzero.

See [evals/README.md](evals/README.md) for the case format and current coverage.

## Session entity references

Each Session owns a small, in-memory reference context containing the last
created entity, last referenced entity, last query result IDs, and at most 20
recent entity IDs. New Sessions never inherit another Session's context.

Target resolution is deterministic:

1. an explicit `entity_id`;
2. a unique explicitly mentioned entity name;
3. a unique explicitly mentioned primitive type such as "the cube";
4. the last referenced entity that still exists;
5. the last created entity that still exists.

Deleted or undone-away entities are removed from the context. Every reference
is checked against the current authoritative snapshot before use. Duplicate names or
primitive matches, missing pronoun targets, and other uncertain references
produce an assistant clarification with no ToolCall, revision change, or undo
token.

## MockProvider behavior

MockProvider remains deterministic, offline, and independent of the HTTP
router. It is a rule-based test provider, **not** a general natural-language
model.

Supported bounded expressions include:

- create `cube`, `sphere`, `cylinder`, and `plane`;
- red, green, blue, and white primitives;
- Chinese and English variants such as `生成一个红色方块`,
  `来个红色立方体`, `创建一个 red cube`, and `create a red cube`;
- list entities and query/delete by unique name or ID;
- references including `它`, `刚才那个`, `刚生成的`, and `it`;
- absolute position and scale;
- relative right/left movement on X and up/down movement on Z;
- double/half scale, color changes, hide/show, and latest undo;
- one-call compound spawns with initial position, scale, and/or color.

Requests for excessive entity counts, local files, JavaScript, PowerShell, or
system commands return no tools. Out-of-range coordinates and illegal scales
are rejected without bypassing the existing JSON Schemas.

## Environment variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `MAHO_AGENT_HOST` | `127.0.0.1` | Listen host; only `127.0.0.1` and `::1` are accepted |
| `MAHO_AGENT_PORT` | `8765` | Listen port; `--port` takes precedence |
| `MAHO_AGENT_MOCK` | automatic | `1` forces MockProvider |
| `MAHO_AGENT_DATA_DIR` | `Tools/AgentBridge/.runtime` | JSONL audit/runtime directory |
| `MAHO_WORLD_ADAPTER` | `mock` | World adapter ID: `mock` or explicit `remote` |
| `MAHO_WORLD_BASE_URL` | `http://127.0.0.1:8770` | Credential-free remote base URL |
| `MAHO_WORLD_TIMEOUT_MS` | `5000` | Per-remote-world-request timeout |
| `MAHO_WORLD_AUTH_TOKEN` | empty | Optional bearer token; required for non-loopback |
| `MAHO_WORLD_ADAPTER_HARNESS` | empty | Executable path required by `npm run smoke:maho-cpp` |
| `MAHO_WORLD_ALLOW_NON_LOOPBACK` | `0` | Set to `1` only with a bearer token |
| `CURSOR_API_KEY` | empty | Cursor SDK key; absence selects Mock mode |
| `MAHO_AI_PROVIDER` | selection rules above | `mock`, `deepseek`, `openai-compatible`, or `cursor` |
| `MAHO_AI_API_KEY` | empty | Generic Key; takes precedence over `DEEPSEEK_API_KEY` for DeepSeek |
| `MAHO_AI_BASE_URL` | Provider preset | Required for generic OpenAI-compatible |
| `MAHO_AI_MODEL` | Provider preset | Required for generic OpenAI-compatible |
| `MAHO_AI_TIMEOUT_MS` | `30000` | Per-model-request timeout |
| `MAHO_AI_MAX_RETRIES` | `1` | Retry count after the first model request |
| `MAHO_AI_TEMPERATURE` | `0` | Chat Completions temperature |
| `MAHO_AI_FINALIZE` | `true` | Enable one supported finalization request |
| `MAHO_AI_MAX_TOOL_CALLS` | `16` | Maximum planned ToolCalls |
| `DEEPSEEK_API_KEY` | empty | DeepSeek compatibility Key |

The legacy Cursor SDK JSONL store remains under
`<--cwd>/Saved/Agent/cursor-sdk-store` to avoid changing the existing editor
behavior. Agent Core audit data uses `MAHO_AGENT_DATA_DIR`.
`Tools/AgentBridge/.runtime/` is ignored by Git.

Request bodies are limited to 1 MiB by default. The server never listens on a
non-loopback address.

Generic OpenAI-compatible mode requires all of `MAHO_AI_BASE_URL`,
`MAHO_AI_MODEL`, and `MAHO_AI_API_KEY`. It receives no DeepSeek-specific
fields. DeepSeek defaults to `https://api.deepseek.com` and
`deepseek-v4-flash`, both overridable by generic variables. Agent Core v0.4.1
always disables thinking mode.

## Offline and real-network commands

These commands are offline and never make a real model request:

```powershell
npm test
npm run eval
npm run eval:remote
npm run eval:remote:minimal
npm run smoke:remote
npm run smoke:maho-cpp  # requires the built C++ Harness path
```

These commands are explicit, optional real DeepSeek network operations and may
incur API charges:

```powershell
npm run smoke:deepseek
npm run eval:deepseek
```

Both real commands exit nonzero before any request when no DeepSeek Key is
configured. They are not part of `npm test`, `npm run eval`, or default CI.

## APIs

Legacy APIs, unchanged:

- `GET /health`
- `POST /chat`
- `GET /events?after=<id>`
- `POST /shutdown`

Agent Core v1 APIs:

- `GET /v1/health`
- `POST /v1/sessions`
- `POST /v1/agent/run`
- `POST /v1/tools/execute`
- `GET /v1/world/snapshot`
- `POST /v1/history/undo`
- `GET /v1/events`

The full request, response, revision, idempotency, batch, and undo contracts are
documented in [docs/AGENT_PROTOCOL_V1.md](docs/AGENT_PROTOCOL_V1.md).

## Quick Mock verification

Start the service in one PowerShell window:

```powershell
$env:MAHO_AGENT_MOCK = "1"
$env:MAHO_AGENT_PORT = "8765"
npm start
```

Use another PowerShell window:

```powershell
$base = "http://127.0.0.1:8765"
$session = Invoke-RestMethod -Method Post -Uri "$base/v1/sessions" `
  -ContentType "application/json" -Body "{}"

$requestId = [guid]::NewGuid().ToString()
$runBody = @{
  session_id = $session.session_id
  request_id = $requestId
  message = "生成一个红色立方体"
  expected_revision = 0
} | ConvertTo-Json
$run = Invoke-RestMethod -Method Post -Uri "$base/v1/agent/run" `
  -ContentType "application/json" -Body $runBody

Invoke-RestMethod -Method Get `
  -Uri "$base/v1/world/snapshot?session_id=$($session.session_id)"

$undoBody = @{
  session_id = $session.session_id
  request_id = [guid]::NewGuid().ToString()
  expected_revision = $run.world_revision
  undo_token = $run.undo_token
} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri "$base/v1/history/undo" `
  -ContentType "application/json" -Body $undoBody

Invoke-RestMethod -Method Post -Uri "$base/shutdown" `
  -ContentType "application/json" -Body "{}"
```

## Current integration limits

- State in the included Mock and fake implementations is in memory and
  disappears when the process exits.
- Each Session owns exactly one selected WorldAdapter.
- The included fake remote server is test scaffolding, not a production engine.
- Entities are primitives only: `cube`, `sphere`, `cylinder`, or `plane`.
- Properties are restricted to `color`, `visible`, and `label`.
- Rotation uses three Euler-angle numbers.
- Full-profile undo is limited to the latest successful write transaction;
  the Minimal World Profile has no undo.
- MockWorldAdapter uses an in-memory UndoJournal internally; RemoteWorldAdapter
  retains only opaque undo tokens returned by the remote world.
- The v0.4.2 C++ Harness uses a Stub Backend and is not production `FWorld` or
  game integration.
- Session state and reference context are not persisted.
- Provider finalization is limited to one text-only request; it cannot produce
  another executable ToolCall.
- Thinking mode, streaming, recursive agent loops, automatic Provider routing,
  and automatic fallback are not supported.
