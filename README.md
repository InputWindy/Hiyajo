# Maho — Minimal Game Engine Shell (脚手架)

> **Maho is a pure engine scaffold.** It provides only the minimal instruction set that every game needs: application lifecycle, platform abstraction, render hardware interface, and a framework for composing higher-level systems. Everything else is built in the project.

## Design Philosophy

![Engine Architecture](Doc/Engine/diagrams/engine_architecture.png)

### What stays in the engine (shell)

Only **infrastructure that every game needs**, regardless of genre or rendering approach:

| Module | Role | Why it stays |
|--------|------|-------------|
| `FAppBase` + Extension Framework | Application lifecycle, plugin registry | Every game needs boot/shutdown orchestration |
| `FPlatformSystem` | Window creation, input, file I/O, timers | Platform abstraction is infrastructure, not game logic |
| `FRenderSystem` / `FRenderServer` | Render thread orchestration, async GPU upload | Engine must perceive rendering exists (even if empty) |
| **RHI** (`FRHI*`) | Vulkan backend: resources, queues, command lists | GPU abstraction layer — no game should touch `vulkan.h` |
| **RDG** (`FRDGBuilder`) | Declarative render graph: pass scheduling, resource lifetime | Declarative rendering framework for any pipeline |
| **Shader Pipeline** | GLSL parsing → SPIR-V compilation, lazy compilation | Shader language + compiler are engine-level concepts |
| `FImGuiSystem` | Dear ImGui integration (Vulkan backend) | RHI-level UI infrastructure, not a game "system" |
| `TAsyncTransferServer` | Templated async worker thread + import/export | Generic async transfer pattern |
| `FThreadedServer` | Single persistent worker thread, FIFO queue | Base async primitive |

### What moves to the project (game)

Any **opinionated system** that makes choices about how the game world works:

| System | Location in project | Why it moved |
|--------|-------------------|-------------|
| **UObject / GC** | `Source/Game/Object/` + `Source/Game/System/GC/` | Object model is a game architecture choice |
| **Resource System** | `Source/Game/System/Resource/` | Asset pipeline is game-specific |
| **WorkerPool** | `Source/Game/System/WorkerPool/` | Job system strategy is game-specific |
| **Script (Lua)** | `Source/Game/System/Script/` | Scripting language is a game choice |
| **ECS** | `Source/Game/ECS/` | Data model is a game architecture choice |
| **World / Level** | `Source/Game/World/` | Scene organization is game-specific |
| **Editor UI** | `Source/Game/Editor/` | Editor chrome is per-project |

## Core Capabilities

### 1. Extension Framework (`IEngineExtension`)

The engine shell orchestrates extensions through a priority-ordered stage pipeline. Extensions are **auto-discovered** from `Source/Game/` by code generation — no manual registration needed.

```
EExtensionPriority:
  System  → Platform, Render (always first)
  Layer   → GC, Resource, WorkerPool, World
  Overlay → Script, Editor (always last)

EEngineStage:
  PreInit → Init → PostInit → Tick → PreShutdown → Shutdown
```

Each extension implements:

```cpp
class IEngineExtension
{
    const char* GetName() const override;
    bool ExecuteStage(EEngineStage Stage) override;  // Called for every stage
    bool IsIdle() const override;                     // For shutdown coordination
};
```

### 2. Render Server + Render Features

The engine provides `FRenderServer` — a **render thread orchestrator** that owns the RHI queue and drives `IRenderFeature` execution. Features declare which pipeline stages they participate in:

```cpp
class IRenderFeature
{
    void ExecuteStage(ERenderPipelineStage Stage, FRDGBuilder& GraphBuilder) override;
};
```

```
ERenderPipelineStage:
  PreRender → BasePass → Translucent → PostProcess → Present
```

`FRenderServer` also inherits `TAsyncTransferServer` for async CPU→GPU resource upload (textures, meshes, skeletons, animations).

### 3. RHI — Render Hardware Interface

Backend-agnostic GPU abstraction. Public headers contain **no `vulkan.h`** — upper layers never see the backend.

- **Three logical queues**: Graphics, Compute, Transfer (always present, Transfer may fall back to Graphics)
- **Resource Manager**: `Acquire*` / `Release` with descriptor-free-lists (not ad-hoc `Create*`)
- **Dynamic Rendering**: `VK_KHR_dynamic_rendering` — no `VkFramebuffer` objects
- **Bindless Descriptors**: `VK_EXT_descriptor_indexing` for flexible descriptor management
- **VMA**: GPU memory allocation via vendored `VulkanMemoryAllocator`

```cpp
// How upper layers talk to RHI
FRHIResourceManager& Mgr = RHIServer.GetResourceManager();
FRHIBufferRef VBO = Mgr.AcquireVertexBuffer(Desc);
FRHITextureRef Tex = Mgr.AcquireTexture2D(Desc);

FRHICommandList& CL = RHIServer.GetGraphicsQueue().BeginCommandList();
CL.CopyBuffer(Src, Dst, Size);
CL.Submit();
```

### 4. RDG — Render Dependency Graph

Declarative rendering framework. You declare **what** a pass reads/writes, and RDG handles resource transitions, lifetimes, and scheduling:

```cpp
FRDGBuilder GB;
FRDGPassParameters Params;
Params.ColorAttachments[0] = GB.CreateTexture("SceneColor", Desc);
Params.DepthStencil = GB.CreateTexture("Depth", Desc);

GB.AddRasterPass("BasePass", Params, [](FRHICommandList& CL)
{
    CL.SetViewport(0, 0, W, H);
    CL.SetPipeline(MyPSO);
    CL.Draw(3, 1, 0, 0);
});

GB.Execute();
```

### 5. Shader Pipeline

GLSL source → SPIR-V binary, with Unity-style authoring extensions:

- **`.shader` format**: `Properties{}`, `SubShader{}`, `Pass{}` blocks
- **Semantic-based vertex input**: `in vec3 a_Position : POSITION` (not `layout(location=N)`)
- **`a2v` / `v2f` structs**: Standard inter-stage communication
- **Render states in shader**: `Cull`, `ZWrite`, `Blend` declared in `Pass`
- **Lazy compilation**: Shaders are compiled on first use (not at boot), enabling real-time shader editing
- **Compiler**: glslang → SPIR-V + reflection JSON

### 6. Async Transfer Infrastructure

Two levels of async primitives, forming an inheritance chain:

![Async Infrastructure](Doc/Engine/diagrams/async_infrastructure.png)

`FTransferHandle` is a **lightweight status token** — only query InProgress / Failed / Succeeded. No GPU objects or result data on the handle.

## Extension by the Project

### EngineExtension discovery

Place your extension classes under `Source/Game/` with the correct directory hint for priority:

```
Source/Game/
├── System/GC/GCSystem.h       → EExtensionPriority::System
├── System/Resource/...        → EExtensionPriority::System
├── World/WorldLayer.h         → EExtensionPriority::Layer
├── Script/ScriptLayer.h       → EExtensionPriority::Overlay
└── Editor/EditorLayer.h       → EExtensionPriority::Overlay
```

Codegen (`maho_tools.py`) scans these directories and generates `Hiyajo-ProjectApp.cpp` with all `RegisterExtension<>` calls. **No manual registration.**

### RenderFeature discovery

Place your render features under `Source/Render/`:

```
Source/Render/
├── TriangleBasePassFeature.h  → Auto-registered by codegen
├── ShadowPassFeature.h        → (future)
└── PostProcessFeature.h       → (future)
```

The codegen injects `Server.RegisterFeature<FTriangleBasePassFeature>()` into `PostInitialize()`.

### Render resource exporters

To bridge game-side `UResource` types (project) with engine-side `FRenderServer` (engine), specialize `TRenderResourceExporter` in `Source/Render/RenderResourceExporters.cpp`:

```cpp
template <>
struct TRenderResourceExporter<UTexture>
{
    static bool TryExport(const UTexture& Texture, FRenderServer& Server);
};
```

CPU snapshots (`FTextureCpuSnapshot`, `FMeshCpuSnapshot`, etc.) are defined in the engine's `<Render/ResourceSnapshots.h>` and converted in the project's `ResourceSnapshotConverters.cpp`.

## Key Invariants

1. **Public = `#include <...>`**, Private = `#include "..."` — no `Public/Maho/` nesting
2. **Allman braces**, Tab indent, English comments
3. **`U*` types are CPU-only** — no `FRHI*` or Vulkan handles on game objects
4. **Never hand-edit** `Maho/Source/Generated/**` or `Intermediate/Generated/**`
5. **No `vulkan.h`** in Public headers — RHI backends are opaque
6. **Pass `FObjectRef`**, never raw `UObject*` across module boundaries

## Repository Map

```text
Maho/                              # Engine repo root
├── README.md                       # YOU ARE HERE
├── AGENTS.md                       # AI agent entry point
├── Maho/                          # Engine DLL sources
│   ├── Source/Public/              # Public API
│   │   ├── Core/
│   │   │   ├── App.h               # FAppBase — application lifecycle
│   │   │   ├── Engine.h            # FEngine
│   │   │   ├── Extension/          # IEngineExtension, FLayer, EExtensionPriority
│   │   │   ├── Server/             # FThreadedServer, TAsyncTransferServer
│   │   │   └── Export.h            # MAHO_API / MAHO_OBJECT macros
│   │   └── Render/
│   │       ├── RHI/                # IRHI, FRHI*, CONTRACT.md
│   │       ├── RDG/                # FRDGBuilder, FRDGPass
│   │       ├── Sequencer/          # IRenderFeature, ERenderPipelineStage
│   │       ├── RenderServer.h      # FRenderServer
│   │       ├── RenderServerTypes.h # FScene, FTransferHandle
│   │       └── UI/ImGuiSystem.h    # FImGuiSystem
│   ├── Source/Private/             # Engine internals
│   │   ├── Core/App.cpp, Engine.cpp
│   │   ├── Core/Extension/
│   │   │   ├── Platform/           # FPlatformSystem
│   │   │   └── Render/             # FRenderSystem
│   │   └── Render/
│   │       ├── RHI/VulkanRHI.*     # Vulkan backend (VMA, dynamic rendering)
│   │       ├── RDG/RDGBuilder.cpp
│   │       ├── RenderServer.cpp
│   │       ├── ShaderCompiler.cpp  # GLSL → SPIR-V
│   │       └── UI/ImGuiSystem.cpp  # ImGui Vulkan integration
│   ├── Source/Generated/           # codegen (gitignored)
│   │   ├── ObjectReflectTypes.gen.*
│   │   ├── ResourceTypes.gen.*
│   │   └── LuaReflectBindings.gen.*
│   ├── ThirdParty/                 # nlohmann, VMA header, fonts
│   └── Plugins/                    # Optional .cplugin modules
├── Build/                          # CMake toolchain + templates
├── Tools/                          # setup, generateProject, codegen
│   ├── maho_tools.py               # Extension/Feature auto-scan + codegen
│   └── object_reflect_codegen.py   # UObject reflection codegen
└── Doc/Engine/                     # Architecture docs, coding standards
```

## Quick Start

```bat
# First time
setup.bat                           # Install local Python
createProject.bat                   # Create a game project

# Development
# Double-click .cproject → generates .sln with auto-registered extensions
# Build in Visual Studio
```

## Documentation

| Document | Purpose |
|----------|---------|
| [AGENTS.md](AGENTS.md) | AI agent first stop — project map, invariants |
| [Doc/Engine/DESIGN_JOURNAL.md](Doc/Engine/DESIGN_JOURNAL.md) | Subsystem status, design rationale, pitfalls |
| [Doc/Engine/CODING_STANDARDS.md](Doc/Engine/CODING_STANDARDS.md) | Full C++ coding standard |
| [Doc/Engine/引擎架构设计.html](Doc/Engine/引擎架构设计.html) | Architecture overview diagram |
| [`**/CONTRACT.md`](Maho/Source/Public/Render/RHI/CONTRACT.md) | Module-level laws and invariants |
