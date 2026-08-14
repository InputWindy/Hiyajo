# Maho design journal

Living status for **other AIs and humans**: what exists, why, known pitfalls, and what is still missing.  
Update this when a subsystem meaningfully changes. Module-level detail also lives in each `CONTRACT.md`.

---

## RHI (`Maho/Source/Public/Render/RHI/`)

**Status (2026-07):** Public abstraction + Vulkan skeleton shipped; clear/ImGui path still works via `GetVk*`.

- **Done**
  - Public: `RHIEnums.h`, `RHIResources.h`, `RHICommandList.h`, `RHIResourceManager.h`, extended `IRHI`
  - Logical queues always: `GetGraphicsQueue()` / `GetComputeQueue()` / `GetTransferQueue()` (never null)
  - Transfer may map to Graphics (or Compute) when no dedicated TRANSFER family — upper layer must not branch on hardware
  - VMA via vendored `Maho/ThirdParty/VulkanMemoryAllocator` + `FVulkanMemoryAllocator`
  - Manager `Acquire*` / `Release` with Desc free-list; Create* used by Manager
  - Command list record + queue `Submit` skeleton (CopyBuffer, barriers, Draw/Dispatch stubs)
- **Not done**
  - Full graphics/compute PSO creation (placeholder pipelines with null `VkPipeline` OK for compile)
  - Full `CopyBufferToTexture` regions / texture upload demo
  - Migrating ImGui off `ImGui_ImplVulkan` + `GetVk*`
- **Why**
  - Upper layers need backend-agnostic resources and G/C/T as **API promises** for later PC texture copies on Transfer
- **Pitfalls**
  - Win32 macros rename `CreateSemaphore` → use `CreateGpuSemaphore` / `DestroyGpuSemaphore`
  - `FVulkanRHI::GetVkGraphicsQueue()` is the Vk escape hatch; `IRHI::GetGraphicsQueue()` returns `FRHIQueue&`
  - Do not confuse external Unreal Engine study notes with this RHI — Maho RHI contracts live only under `Maho/Source/Public/Render/RHI/`
- **Key files**
  - Public: `RHI.h`, `RHI*`, `CONTRACT.md`
  - Private: `VulkanRHI.*`, `VulkanMemory.*`, `VulkanCommandList.*`, `VulkanResources.*`, `RHIResourceManager.cpp`
- **Next**
  - Real Hello-Triangle PSO + Transfer upload path using only Public APIs

Contract: [`../../Maho/Source/Public/Render/RHI/CONTRACT.md`](../../Maho/Source/Public/Render/RHI/CONTRACT.md)

---

## Extensions / app frame

**Status:** Built-ins are `*System` (`FPlatformSystem`, `FRenderSystem`, …), priority `System | Layer | Overlay`.  
`FRenderServer` orchestrates stages on Game; `FRHIServer` owns the RHI thread (`MahoRHI`).

- **Pitfalls**
  - Older docs may still say `F*Module` / `IModule` / `Public/Maho/` — trust code + [`Maho/Plugins/README.md`](../../Maho/Plugins/README.md)
  - Games register extensions from **generated** `Source/Generated/<Game>App.cpp` (regenerate via `.cproject`)

Contract: [`../../Maho/Source/Public/Core/Extension/CONTRACT.md`](../../Maho/Source/Public/Core/Extension/CONTRACT.md)

---

## Object / GC / refs

**Status:** Pool-allocated `UObject` graph; external handles are `FObjectRef`.

- **Pitfalls**
  - Never bare `AddRef` / `ReleaseRef` at call sites
  - Cycles: one side non-owning raw observer (no WeakRef type in current design)
  - `FObjectRef` / `FObjectWeakRef` naming in older notes — current rule is strong `FObjectRef` + raw observer for cycles (see Object CONTRACT)

Contract: [`../../Maho/Source/Public/Core/Object/CONTRACT.md`](../../Maho/Source/Public/Core/Object/CONTRACT.md)

---

## Resource system vs RHI Manager vs VMA

| Layer | Role |
|-------|------|
| `FResourceSystem` / `UResource` / `UTexture*` | Asset / UObject lifetime; **CPU** BulkData + pixels only |
| `FRHIResourceManager` | GPU `FRHI*` objects, pooling |
| VMA | Device memory (Private only) |

Do not collapse these three.

### UTexture CPU IO (2026-07)

- **Done:** `UTexture` base + `UTexture2D` / `3D` / `Cube` / `CubeArray` / `2DArray`; `EResourceType` texture variants; Private `TextureImageCodec` Import/Export via `TResourceIOTraits`.
- **Raster:** Win32 **WIC** (png/jpg/… → RGBA8). `MAHO_WITH_OPENIMAGEIO` default **OFF** (FetchContent deferred).
- **KTX2:** **libktx** when KTX-Software sources are present (`MAHO_KTX_SOURCE_DIR` or `Intermediate/_deps/ktx_software-src`; optional `MAHO_FETCH_LIBKTX=ON`). Needs Git Bash (`BASH_EXECUTABLE`) and `enable_language(C)`. Without sources, configure still succeeds (WIC-only). OIIO does not stably decode KTX2.
- **Path hints:** `.cube.` / `.cubemap.` / `.cubearray.` / `.3d.` / `.2darray.` select subtype when TypeHint unknown; plain `.ktx2` defaults to `UTexture2D`.
- **Render proxy / transfer (2026-08):** Client/ThreadedServer/Exporter — `FTransferHandle` (status only) + `FRenderServer::QueueResourceUpload<T>` / `RequestResourceDestroy`. Exporters specialized in `RenderServer.cpp` for `UTexture`(+dims), `UStaticMesh`, `USkeleton`, `UAnimation`. Async GPU upload via fence poll (`IsFenceSignaled`); no Flush/WaitForFence on Game or ExecuteFrame hot path (Boot placeholders excepted). Registries + defaults: Texture / Mesh / Skeleton / Animation. Join key = `FResourceSystem::MakeResourceCatalogKey` (SoftPath). Prefab/Material/AnimationGraph: no Render proxy.
- **Not done:** Sampler/material bind of proxies; mesh draw path; Cube/3D/BC upload; skin streams on mesh snapshot; OIIO wired build; Game↔disk rewrite to `FTransferHandle` (kept as KickImport/`FObjectRef`).
- **Pitfalls:** Game `U*` must never hold GPU handles; cube KTX without a name hint may import as `UTexture2D` (dimension warn); draw must use `Find*OrDefault` until transfer Succeeded.

### Model / Prefab CPU IO (2026-08)

- **Done (Phase 1):** `UMaterial` / `UStaticMesh` / `USkeleton` / `UAnimation` / `UAnimationGraph` / `UPrefab`; `EResourceType` + `EModelAxis` / `EModelHandedness`.
- **Codec:** Private `MeshModelCodec` — Assimp → `FDecodedModelScene` → Apply siblings in package; Prefab `DocumentJson` holds **Metadata** (coordinate system) peer to Meshes / Skeleton / AnimationGraph SoftPaths.
- **Assimp:** default `MAHO_FETCH_ASSIMP=ON` pulls **v5.3.1** (CMake ≥3.22 not required; 5.4+ needs 3.22) on first configure if no local tree; or set `MAHO_ASSIMP_SOURCE_DIR`. `ASSIMP_WARNINGS_AS_ERRORS=OFF` forced (MSVC C4819 in clipper). `MAHO_FETCH_ASSIMP=OFF` without sources → decode disabled.
- **Entry:** `TResourceIOTraits<UPrefab>` matches fbx/gltf/glb/obj/…; one BulkData job creates the whole scene package contents.
- **Not done (Phase 2):** Graph authoring UI, playback, skinning upload, RHI mesh, coordinate auto-remap, Prefab re-export to FBX.

---

## ImGui

Still **Dear ImGui official backends** (`ImGui_ImplGlfw` + `ImGui_ImplVulkan`), borrowing Vulkan handles from `FVulkanRHI::GetVk*`.  
Not on `FRHICommandList`. Planned migration later; do not “fix” in drive-by RHI work.

- **Multi-viewport enabled** (`ImGuiConfigFlags_ViewportsEnable`): undocked panels can leave the main OS window.
- Per-frame: Game `UpdatePlatformWindows` (GLFW); MahoRHI `SubmitRenderPlatformWindows` / `RenderPlatformWindowsDefault` (Vulkan). No Game-thread Flush for viewports.
- Viewport `Renderer_Create/Destroy/SetWindowSize` marshaled to MahoRHI with sync Flush (rare).
- Cost: create/resize secondary windows still sync; per-frame draw stays async with 3-frame overlap.

## Editor UI (game project, not engine)

Editor chrome (`FEditorLayer`, `FEditorUIRegistry`, `FAgentChatClient`) lives in the **game project** under `Source/Editor/` when `GAME_WITH_EDITOR` is on. Engine only provides ImGui under `Render/UI/`.

- **Law:** game `Source/Editor/CONTRACT.md`
- Shell owns geometry / DockSpace; registry owns contributions + Catalog separators
- Temporary Details = `DockPanel` + `bTransient` + `OpenDockPanel` — **not** Modal
- Access: `TryGetEditorUIRegistry(FApp&)` or `GetExtension<FEditorLayer>()->GetUIRegistry()`
- Runtime game HUD is **out of scope** (future `FGameUI`)

---

## World Adapter service (`Maho/WorldAdapter/`)

**Status (2026-08):** Agent Core v0.4.2 ships an optional Windows loopback
service library, standalone harness, strict Protocol v1 validation, bounded
command queue, Stub Backend, CTest suite, and Node/C++ conformance smoke.

- **Done:** typed owning DTOs; direct Node golden-fixture coverage; Minimal
  World Profile; main-thread backend pump; queued/executing timeout semantics;
  bounded LRU request-ID idempotency; authoritative revision; loopback Winsock
  HTTP with optional bearer token; startup rollback and idempotent shutdown.
- **Not done:** real `FWorld`, game/editor registration, production entities or
  Transform, undo, dry-run, atomic batches, Render/RHI/resources/physics.
- **Pitfalls:** `FStubWorldBackend` is harness/test scaffolding, never a real
  world. Normal Maho builds and games must not auto-start the service. Keep all
  Maho builds serial with `--parallel 1` in this worktree.
- **Next:** v0.5 may add a separately opt-in real-world Backend on the owning
  world thread without weakening Protocol v1 validation or transport limits.

Design and usage: [`WORLD_ADAPTER_SERVICE.md`](WORLD_ADAPTER_SERVICE.md)

---

## How to update this journal

When you finish a meaningful slice: update **Status / Done / Not done / Pitfalls / Next** for that section, and mirror a one-line Status in the module `CONTRACT.md`.
