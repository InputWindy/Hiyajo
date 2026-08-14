# Maho C++ coding standards

Vendor-neutral **source of truth** for style. Cursor `.cursor/rules/ue-coding-style.mdc` must stay a short projection of this file.

Aligned with Unreal Engine habits. Applies to all Maho engine and game C++ in this ecosystem.

## Braces (Allman)

Opening `{` and closing `}` on their **own lines**, same column:

```cpp
namespace Maho
{
	class FGameApp
	{
	public:
		bool Initialize();
	};
}
```

Forbidden (K&R / Java style):

```cpp
namespace maho {
	class GameApp {
	};
}
```

Use Allman for `if` / `for` / `while` / `switch` / functions / classes / namespaces / multi-line lambdas.

## Naming

- `UObject` and subclasses: `U` prefix (`UObject`, `UPackage`, `UResource`, …)
- Other types: `F` prefix (`FAppBase`, `FConfig`, `FObjectRef`, `FRHIResourceManager`, …)
- Interfaces: `I` prefix (`IRHI`, `IEngineExtension`)
- Enums: `E` prefix (`ERHIQueueType`, `ERHIResourceType`, `EExtensionPriority`)
- `bool` members/locals: `b` prefix (`bInitialized`)
- Members: PascalCase — **no** `m_` / `_` prefix
- Functions: PascalCase (`Initialize`, `Tick`)
- Namespaces: PascalCase (`Maho`)
- Macros / export: `MAHO_API`, `MAHO_EXPORTS`, …

### Kind must not replace Type (**mandatory**)

When naming a discriminator for “what sort of thing this is”, use **`Type`**, never **`Kind`**.

| Forbidden | Required |
|-----------|----------|
| `ERHIResourceKind` | `ERHIResourceType` |
| `GetKind()` | `GetType()` |
| member `Kind` meaning type tag | member `Type` |

Same rule for new enums / accessors / fields (`EFooKind`, `ResourceKind`, `Kind` as type tag). Do not introduce `Kind` as a synonym for `Type` to “sound nicer” or avoid a name clash — pick a clearer name (`EFooClass`, `EFooCategory`, …) if `Type` is already taken in that scope.

Allowed: unrelated English uses that are not a type-tag API (e.g. log string `"refusing menu …"`), and third-party / generated code you do not own.

## Headers vs sources

- **Templates**: implement in the header (in-class or bottom of header).
- **Non-templates**: prefer `.cpp`; headers mostly declarations.
- Short trivial accessors (`IsValid()`, one-line `return`) may stay in headers.
- `constexpr` functions stay in headers.

## Indent and types

- Indent with **Tab**
- Pointers/refs: `Type* Ptr`, `Type& Ref` (`*` / `&` with the type)
- Public headers under `Source/Public` (`Core/`, `Render/`, …) — **no** extra `Public/Maho/` nest
- Do not abuse `using namespace` in headers
- Game entry: `#include <Maho.h>` + `#include <EntryPoint.h>`, subclass `Maho::FAppBase`, implement `CreateApplication()`

## Include form

- **Public** headers (engine `Source/Public`, plugin `Source/*/Public`, shared Generated): `#include <...>`
- **Private** headers (`Source/Private`, plugin Private): `#include "..."`
- Third-party / system: angle brackets (`<imgui.h>`, `<vector>`, `<vulkan/vulkan.h>` only in Private)

## Extension source layout (**mandatory**)

All code that belongs to a built-in engine **extension** (`*System`, related private helpers, companion Layers) must live under:

```text
Source/Public/Core/Extension/<ExtensionName>/
Source/Private/Core/Extension/<ExtensionName>/
```

Rules:

- **Folder name** = the extension’s identity (same as `GetName()` / system name): `GC`, `Resource`, `Script`, `Render`, `Platform`, `WorkerPool`, `Editor`, …
- Public API header(s) and private `.cpp`/helpers for that extension stay **inside that folder** — do not dump them under `Core/Modules/`, `Core/Layer/`, or a flat `Core/Extension/*.cpp`.
- Cross-cutting docs only (`CONTRACT.md`) may stay at `Core/Extension/CONTRACT.md`.
- Include form: `#include <Core/Extension/Resource/Resource.h>` (not `<Core/Extension/Resource.h>`).

Examples:

| OK | Forbidden |
|----|-----------|
| `Extension/Resource/ResourceIO.cpp` | `Core/Modules/ResourceIO.cpp` |
| `Extension/Script/LuaObjectReflect.cpp` | `Core/Layer/LuaObjectReflect.cpp` |
| `Extension/GC/GC.cpp` | `Extension/GC.cpp` (flat) |

Codegen outputs stay under `Source/Generated/` (e.g. `LuaReflectBindings.gen.h`) — those are **not** extension sources.

## Comments

- All code comments (`//`, `/* */`, `/** */`) must be **English**
- Chinese explanations belong in `Doc/` or chat — **not** in `.h` / `.cpp`

## Client / ThreadedServer / Exporter transfer (**mandatory**)

Applies when **large blobs** move between two modules and one side is active, the other passive. Model it as **client (master) / server (slave)** with `FThreadedServer` as the transport.

| Role | Duty |
|------|------|
| Client | Starts the transfer; owns or produces CPU payload |
| Server | Receives, registers, materializes local resources |
| Transport | `FThreadedServer` (or its façade); queue + handles only — not business codecs in the header |

### Rules

1. **Unified template Import / Export** — the template argument is the **exporter/importer type**, not a resource enum and not a code-gen registry of exporters.
2. **Exporter/importer specializations live in the server `.cpp`** (exception to “templates only in headers”: *explicit specializations* for this pattern belong in the server translation unit so Assimp/WIC/RHI stay out of public headers).
3. **Non-blocking** — Import/Export return immediately. No hot-path `Flush` of the transport server and no `WaitForFence` on the client thread (Boot/Shutdown may Flush).
4. **`FTransferHandle` is extremely light** — its **only** job is to query transfer status: **InProgress / Failed / Succeeded**. Forbidden on the handle: `GetCatalogKey`, `GetProxy`, `GetResult`, GPU pointers, payload bytes.
5. **Resource identity stays on the client** (e.g. `FSoftObjectPath`). The handle only answers “did this transfer finish?”. Draw code uses SoftPath/CatalogKey to find a proxy; if not ready, use a **default placeholder** resource.
6. On submit, **payload ownership moves** into the transport/server (CPU snapshot); the client must not assume shared mutable buffers after Import returns.
7. **Small per-frame state sync** (e.g. scene primitives + poses) is **not** this pattern — use a value packet (`FSceneUpdatePacket` → render `FScene`), not Import/Export + `FTransferHandle`.

### Shape (illustrative)

```cpp
enum class ETransferState : std::uint8_t
{
	InProgress,
	Failed,
	Succeeded,
};

struct FTransferHandle
{
	[[nodiscard]] ETransferState GetState() const;
	[[nodiscard]] bool IsInProgress() const { return GetState() == ETransferState::InProgress; }
	[[nodiscard]] bool HasFailed() const { return GetState() == ETransferState::Failed; }
	[[nodiscard]] bool HasSucceeded() const { return GetState() == ETransferState::Succeeded; }
};

// Façade on the server / transport
template <typename TExporter>
FTransferHandle Import(/* request */);

template <typename TImporter>
FTransferHandle Export(/* request */);
```

Game → Render resource upload is the same idea: `QueueResourceUpload(const TResource&)` with `TResource : UResource`, dispatching to `TRenderResourceExporter<T>` specialized in `RenderServer.cpp` (or companion `.cpp`). Do **not** copy the old `QueueTextureUpload` + `RHIServer.Flush()` + `WaitForFence` path.

Cursor / Agent note: [`.cursor/rules/client-server-transfer.md`](../../.cursor/rules/client-server-transfer.md) (promote to `.mdc` + `alwaysApply: true` when possible).

## Related hard constraints

- Object refs: [`../../Maho/Source/Public/Core/Object/CONTRACT.md`](../../Maho/Source/Public/Core/Object/CONTRACT.md)
- RHI surface: [`../../Maho/Source/Public/Render/RHI/CONTRACT.md`](../../Maho/Source/Public/Render/RHI/CONTRACT.md)
