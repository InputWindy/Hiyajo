# Maho

<div style="background:#143d2b;border:1px solid #3f8f63;border-radius:8px;padding:12px 16px;color:#cdeadd;">

<b>Author's note: before coding, always have the AI read AGENTS.md to learn the conventions - because the one writing code is the AI, not the programmer.</b>

</div>

## Introduction

Maho is a pure C++20 game engine whose core design revolves around **parallel dependency-graph scheduling** and **plugin architecture**. It borrows ideas from Unity DOTS: the application is decomposed into a set of single-responsibility **anonymous layers**, each expanded into task-graph nodes along an **ordered stage pipeline** (PreInit -> Init -> ... -> BeginFrame -> Tick -> EndFrame -> ...), and automatically orchestrated by the dependency-graph scheduler - serial when dependencies exist, parallel when none, with dependency edges acting as implicit barriers.

A feature is a plugin, and a plugin is a DLL. The engine core only provides a few generic building blocks; every capability - logging, serialization, physics, rendering - exists as a plugin, installed on demand and uninstalled by dependency.

## Features

- **Anonymous layers + stage pipeline**: `FLayerBase` only closes over itself (identity name + per-stage dependency table), does not manage dependency lifetimes; `IPipeline<TStages...>` defines an ordered stage sequence, `FLayerTaskGraph` schedules globally.
- **Dependency-graph scheduling**: node = (object name, stage), edges come from dependency tuples. A node is released immediately after all its direct dependencies complete (no stage barrier, cross-stage pipeline).
- **Dynamic install/uninstall**: `FEngineBase` loads feature DLLs via `FAssembly`; uninstall uses reverse-dependency-count min-heap greedy - a depended-on layer is refused, dependents pop first with chain uninstall.
- **Optional capability composition**: a layer mounts only the stage interfaces it implements; unimplemented stages are silently skipped by `dynamic_cast`. `TSingleton<T>` is a pure marker base with no forced interface.
- **Context-parameterized stages**: every stage method receives the scheduling context (`void Tick(FEngineBase&)`, `void Render(FRender&)`) - cross-plugin communication goes through the context, not a magic injected pointer.
- **Zero third-party dependency core**: the engine core contains no third-party library; each plugin pulls its dependencies with FetchContent in its own `.cmake`, with configurable mirror and proxy.
- **Pure generic core**: the core presets no stage enum and no application shape, delegating all of it to the application layer.

## Build Process

### 0. First Bootstrap

```bat
Setup.bat
```

Installs the engine-local Python (`Tools/python` junction -> `%LOCALAPPDATA%\Maho\python\tooling`). Prefers reusing the host's Python (with tkinter) to build a venv, otherwise downloads the python.org installer and installs silently. **Never depends on python on the system PATH**.

### 1. Create Project

```bat
CreateProject.bat
```

Opens the UI, enter a project name / parent folder / engine root / author. Creates an **empty** project — mount engine/feature plugins by hand-editing `<Project>.cproject` `Plugins`. Generates:

```
<Project>/
  <Project>.cproject
  Plugins/                    <- the project's own plugins
    <Project>/Public/         <- entry plugin interface (FEngineBase host)
    <Project>/Private/        <- entry plugin implementation
    <SubPlugin>/Public/       <- feature plugins (FLayer feature)
  Config/                     <- runtime config (DefaultEngine.ini, copied from the engine template)
  Intermediate/Main.cpp       <- entry (code-gen, do not edit)
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### 2. Generate Project

Double-click `<Project>.cproject` (or `Tools/generateProject.bat <Project>.cproject`):

- codegen generates `Intermediate/Generated/<Project>.gen.h` (per-plugin forward declarations + a guarded full-include block)
- scans engine `Plugins/`, project `Plugins/`, project `Extension/`, rewrites CMakeLists (one DLL target per plugin)
- detects the local Visual Studio (vswhere) to pick a CMake generator
- produces `<Project>.sln`

### 3. Build

Open the `.sln` in Visual Studio, or command line:

```bat
cmake -S . -B Intermediate -G "Visual Studio 17 2022" -A x64
cmake --build Intermediate --config Debug
```

Output: each plugin DLL + `EntryPoint.exe` under `Intermediate/Binaries/<Config>/`. Plugin dependency cycle detection (`MahoCheckCycle`) runs automatically before the build.

### 4. Package

```bat
package.bat
```

Opens the UI to pick platform / config, builds and copies exe + all DLLs to `Packaged/<Platform>/<Config>/`.

## Core Concepts

### Three Plugin Types

When creating a plugin, scaffold a bare layer with `CreatePlugin.bat` (or `Tools/create_plugin_ui.py`), then hand-mount the stage interfaces you need and hand-fill `.cplugin` `Dependencies`:

| Template | Role | Export | Description |
|------|------|------|------|
| `entry` | application root | `CreateEngine()` -> `FEngineBase*` | inherits `FEngineBase`, main loop + install scheduling |
| `feature` | feature layer | `CreateLayer()` -> `FLayerBase*` | inherits `FLayerBase`, implements the stage interfaces it needs |
| `engine` | pure library | none | `namespace` scope, no lifecycle |

```cpp
// entry - application root
class FMyGame : public Maho::FEngineBase
{
    MAHO_DECLARE_ENGINE(FMyGame, "MyGame.dll");
public:
    void PreMain() override;
    void PostMain() override;
};

// feature - feature layer (mounts only the stage interfaces it needs)
class FRenderer : public Maho::FLayer<Maho::IBeginFrame, Maho::ITick, Maho::IEndFrame>
{
    MAHO_DECLARE_LAYER(FRenderer, "Renderer.dll");
public:
    void BeginFrame(Maho::FEngineBase&) override;
    void Tick(Maho::FEngineBase&) override;
    void EndFrame(Maho::FEngineBase&) override;
};
```

### Anonymous Layers + Stage Pipeline

**FLayerBase** only closes over itself:

```cpp
class FLayerBase
{
    virtual std::string_view GetName() const = 0;   // stable identity name (topology key)
    const FDependencyTable& GetDependencies() const; // per-stage dependency table
protected:
    template <typename TMyStage, typename TDepObj, typename TDepStage>
    void AddDependency();   // compile-time dependency: this at TMyStage depends on TDepObj at TDepStage
    void AddDependency(type_index, string_view, type_index);  // runtime dependency (cross-DLL string addressing)
};
```

**IPipeline** defines the ordered stage sequence; **FLayer<TPipelines...>** binds a layer + pipelines together; **FLayerTaskGraph** expands a set of layers into one node per stage (self-advancing + cross-object dependency) and schedules topologically.

### Engine Main Loop

`FEngineBase::Main()` drives a `FLayerTaskGraph`:

```
Init graph (IPreInit -> IInit -> IPostInit, once) -> Compile -> Execute -> Flush
Tick loop (per frame):
    Flush -> FlushPendingUpdatePipelines()      // apply pending installs/uninstalls
    -> rebuild Tick graph (IBeginFrame -> Tick -> EndFrame -> IExit)
    -> Compile -> Execute -> check the RequestExit flag
Shutdown graph (IPreShutdown -> IShutdown -> IPostShutdown, once) -> release features + DLLs
```

Cross-feature dependency (declared in the feature constructor):

```cpp
class FWorld : public FLayer<ITick>
{
    MAHO_DECLARE_LAYER(FWorld, "World.dll");
public:
    FWorld()
    {
        // my Tick depends on FLog's BeginFrame (compile-time)
        AddDependency<ITick, FLog, IBeginFrame>();
        // or runtime string addressing (cross-DLL)
        AddDependency(std::type_index(typeid(ITick)), "FLog", std::type_index(typeid(IBeginFrame)));
    }
};
```

### Dynamic Install / Uninstall / Reload

```cpp
// dynamic load + install (engine owns the DLL + instance)
Install("Renderer.dll");

// anonymous uninstall (by layer name), dependency-safe:
//   - depended on -> uninstall fails, dropped
//   - no dependencies -> min-heap greedy, dependents pop first with chain uninstall
TryUninstall("FRenderer");

// hot reload: uninstall at the next safe point, re-install the same DLL the frame after
Reload("FRenderer");

// exit the main loop
RequestExit();
```

Install/uninstall are recorded into pending sets and applied at the next safe point (`FlushPendingUpdatePipelines`), which broadcasts `OnLayersChanged` so the host re-expands its cached graph. Failures are loud, not silent:

- layers are always loaded by name (`Install("X.dll")`) -- there is no raw-pointer install;
- a duplicate layer name (one instance per name) is refused;
- a layer whose declared dependency is not yet installed is refused (**deps first**) -- a failed install propagates to its dependents, mirroring uninstall's "depended-on is refused";
- a load / symbol / factory failure reports the reason;
- a throwing stage method is reported non-fatally and its downstreams are still released.

### Optional Capability Composition

A layer mounts **only** the stage interfaces it implements - unimplemented stages are silently skipped (`dynamic_cast` inside the dispatch specialization). The engine forces no capability; each plugin decides for itself:

- **Singleton** (`TSingleton<T>`): `Get()` process-unique (header declaration + cpp definition in each DLL)
- **Lifecycle** (`IPreInit`/`IInit`/... / `IPreShutdown`/`IShutdown`/...): mounted as stage interfaces on a `FLayer`
- **Install** (`FEngineBase`): the entry plugin is the only host, `Install`/`TryUninstall` schedule features

## Sample Project (ExampleEngine)

The `Example/ExampleEngine` project is the engine's full usage sample - a **real rendering project** (not a synthetic input-driven demo). It mounts the engine service layers, brings up the Vulkan render subsystem, and draws a triangle into a scene target.

### Directory Structure

```
ExampleEngine/
  ExampleEngine.cproject              <- project manifest (engine plugins selected)
  Plugins/
    ExampleEngine/                    <- entry plugin (entry template)
      Public/ExampleEngine.h          <- FExampleEngine : FEngineBase
      Private/ExampleEngine.cpp       <- PreMain installs the engine service layers
      ExampleEngine.cplugin           <- dependency table
    RenderFeature/                    <- render feature plugins
      Scene/                          <- scene feature: owns shared scene color/depth targets
        Public/Scene.h                <- FScene : FLayer<IBeginRender, IRender, IEndRender, IPresent>
      DrawTriangleFeature/            <- triangle feature: compiles shaders + draws
        Public/DrawTriangleFeature.h  <- FDrawTriangleFeature : FLayer<IRender>
  Intermediate/Generated/ExampleEngine.gen.h  <- codegen output (forward decls + include block)
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### Entry Plugin (ExampleEngine)

```cpp
// ExampleEngine.h
class FExampleEngine : public FEngineBase
{
    MAHO_DECLARE_ENGINE(FExampleEngine, "ExampleEngine.dll");
public:
    void PreMain() override;
    void PostMain() override;
};
```

```cpp
// ExampleEngine.cpp
void FExampleEngine::PreMain()
{
    // Engine service layers installed up front; the window drives the engine
    // loop and FPlatform requests exit when the window is closed.
    Install("Log.dll");
    Install("Config.dll");
    Install("Platform.dll");
    Install("Resource.dll");
    Install("Script.dll");
    Install("Render.dll");
}

extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
    return Maho::FExampleEngine::CreateEngine();
}
```

### RenderFeature: Scene + DrawTriangleFeature

**Scene** owns the shared scene targets (color/depth) across frames. Its `Render` clears them; `Present` blits the color target to the swapchain backbuffer.

```cpp
// Scene.h - scene feature, mounts all four render stages
class FScene : public FLayer<IBeginRender, IRender, IEndRender, IPresent>
{
    MAHO_DECLARE_LAYER(FScene, "Scene.dll");
    ...
};
```

**DrawTriangleFeature** mounts only `IRender`. It lazily compiles an embedded fullscreen-triangle GLSL (VS + FS via `FShaderCompilerServer`), builds a graphics pipeline with dynamic rendering, and draws into the scene color target - after Scene's clear (declared dependency).

```cpp
// DrawTriangleFeature.h
class FDrawTriangleFeature : public FLayer<IRender>
{
    MAHO_DECLARE_LAYER(FDrawTriangleFeature, "DrawTriangleFeature.dll");
public:
    void Render(FRender& R) override;
    ...
};
```

The cross-feature dependency is declared in the constructor, so the triangle draws after the scene clears:

```cpp
FDrawTriangleFeature::FDrawTriangleFeature()
{
    AddDependency(std::type_index(typeid(IRender)), "FScene", std::type_index(typeid(IRender)));
}
```

### Run Verification

```
EntryPoint.exe ExampleEngine.dll
```

- `PreMain` installs Log/Config/Platform/Resource/Script/Render; `FPlatform` polls events each frame and requests exit when the window closes.
- `FRender` (a render subsystem) schedules its own render features (`IBeginRender -> IRender -> IEndRender -> IPresent`) on its own render thread, pipelined across frames.
- Log output goes to both console and `Logs/Maho.log` (rolling).

Full closed loop: `EntryPoint` -> `FAssembly` loads `ExampleEngine.dll` -> `CreateEngine()` -> `FEngineBase*` -> `PreMain` (install services) -> `Main` (main loop, dependency-graph scheduling) -> render subsystem drives Scene/DrawTriangleFeature -> `PostMain` -> `Shutdown`.

## Interface and Implementation Separation

The entry plugin's `Public/` only holds interfaces (grouped in folders by function); implementations go in a new plugin outside the entry, in its own `Private/`. The entry does not care how interfaces are implemented, it only schedules. Plugin Public headers do not leak third-party dependencies (e.g. `Log.h` hides spdlog behind an incomplete type + type-erased level; RHI's backend is private to its DLL).

## Docs

- [Docs.md](Docs.md) - full repository documentation index
- [Source/SourceDoc.md](Source/SourceDoc.md) - source root (layers)
- [Source/Public/Core/CoreAPI.md](Source/Public/Core/CoreAPI.md) - Core infrastructure API
- [Source/Public/Engine/EngineAPI.md](Source/Public/Engine/EngineAPI.md) - layer system API
- [Example/ExampleEngine/README.md](Example/ExampleEngine/README.md) - sample project walkthrough
