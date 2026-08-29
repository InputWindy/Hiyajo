# Maho

<div style="background:#143d2b;border:1px solid #3f8f63;border-radius:8px;padding:12px 16px;color:#cdeadd;">

<b>Author's note: before coding, always have the AI read AGENTS.md to learn the conventions - because the one writing code is the AI, not the programmer.</b>

</div>

## Introduction

Maho is a pure C++20 game engine whose core design revolves around **parallel dependency-graph scheduling** and **plugin architecture**. It borrows ideas from Unity DOTS: the application is decomposed into a set of single-responsibility **anonymous layers**, each expanded into task-graph nodes along an **ordered stage pipeline** (BeginFrame -> Tick -> EndFrame), and automatically orchestrated by the dependency-graph scheduler - serial when dependencies exist, parallel when none, with dependency edges acting as implicit barriers.

A feature is a plugin, and a plugin is a DLL. The engine core only provides a few generic building blocks; every capability - logging, serialization, physics, rendering - exists as a plugin, installed on demand and uninstalled by dependency.

## Features

- **Anonymous layers + stage pipeline**: `FLayerBase` only closes over itself (identity name + per-stage dependency table), does not manage dependency lifetimes; `IPipeline<TStages...>` defines an ordered stage sequence, `FLayerTaskGraph` schedules globally.
- **Dependency-graph scheduling**: node = (object name, stage), edges come from dependency tuples. A node is released immediately after all its direct dependencies complete (no stage barrier, cross-stage pipeline).
- **Dynamic install/uninstall**: `FEngineBase` loads feature DLLs via `FAssembly`; uninstall uses reverse-dependency-count min-heap greedy - a depended-on layer is refused, dependents pop first with chain uninstall.
- **Input-driven closed loop**: input handling itself is a feature (GameInputLayer), which schedules install/uninstall/exit through the engine during the Tick stage - the engine only provides scheduling capability, strategies are all plugins.
- **Optional capability composition**: `IInit`/`IShutdown`/`IMain`/`IExit` composed explicitly via `IPlugin<Caps...>`; `TSingleton<T>` is a pure marker base with no forced interface.
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

Opens the UI, enter a project name + select engine plugins (dependencies resolved automatically). Generates:

```
<Project>/
  <Project>.cproject
  Plugins/                    <- the project's own plugins
    <Project>/Public/         <- entry plugin interface (FEngineBase host)
    <Project>/Private/        <- entry plugin implementation
    <SubPlugin>/Public/       <- feature plugins (FEngineLayer feature)
  Extension/                  <- third-party plugin directory
  Intermediate/Main.cpp       <- entry (code-gen, do not edit)
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### 2. Generate Project

Double-click `<Project>.cproject` (or `Tools/generateProject.bat <Project>.cproject`):

- codegen generates `Intermediate/Generated/<Project>.gen.h` (plugin includes + expansion macros)
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

When creating a plugin, pick a template by role (`CreatePlugin.bat` -> codegen):

| Template | Role | Export | Description |
|------|------|------|------|
| `entry` | application root | `CreateEngine()` -> `FEngineBase*` | inherits `FEngineBase`, main loop + install scheduling |
| `feature` | feature layer | `CreateLayer()` -> `FEngineLayer*` | inherits `FEngineLayer`, implements BeginFrame/Tick/EndFrame |
| `engine` | pure library | none | `namespace` scope, no lifecycle |

```cpp
// entry - application root
class FMyGame : public Maho::FEngineBase
{
    MAHO_DECLARE_ENGINE(FMyGame, "MyGame.dll");
public:
    void Initialize(int, char**) override;
    void Shutdown() override;
};

// feature - feature layer
class FRenderer : public Maho::FEngineLayer
{
    MAHO_DECLARE_LAYER(FRenderer);
public:
    void BeginFrame() override;
    void Tick() override;
    void EndFrame() override;
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

**IPipeline** defines the ordered stage sequence; **FLayer<TPipeline>** binds a layer + pipeline together; **FLayerTaskGraph** expands a set of layers into one node per stage (self-advancing + cross-object dependency) and schedules topologically.

### Engine Main Loop

`FEngineBase::Main()` drives a `FLayerTaskGraph<IEnginePipeline, FEngineBase>`:

```
while (true)
{
    Flush -> FlushPendingUpdatePipelines()   // apply pending installs/uninstalls
    Init -> Compile -> Execute -> Flush         // BeginFrame -> Tick -> EndFrame
    check the RequestExit flag
}
```

Cross-feature dependency (declared in the feature constructor):

```cpp
class FWorld : public FEngineLayer
{
    MAHO_DECLARE_LAYER(FWorld);
public:
    FWorld()
    {
        // my Tick depends on FLog's BeginFrame (compile-time)
        AddDependency<ITick, FLog, IBeginFrame>();
        // or runtime string addressing (cross-DLL)
        AddDependency(std::type_index(typeid(ITick)), "FDynLog", std::type_index(typeid(IBeginFrame)));
    }
};
```

### Dynamic Install / Uninstall

```cpp
// dynamic load + install (engine owns the DLL + instance)
Install("Renderer.dll");

// anonymous uninstall (by layer name), dependency-safe:
//   - depended on -> uninstall fails, dropped
//   - no dependencies -> min-heap greedy, dependents pop first with chain uninstall
TryUninstall("FRenderer");

// exit the main loop
RequestExit();
```

### Optional Capability Composition

The engine forces no capability; each plugin decides for itself:

- **Singleton** (`TSingleton<T>`): `Get()` process-unique (header declaration + cpp definition in each DLL)
- **Lifecycle** (`IInit`/`IShutdown`/`IMain`/`IExit`): composed explicitly via `IPlugin<Caps...>`
- **Install** (`FEngineBase`): the entry plugin is the only host, `Install`/`TryUninstall` schedule features

## Sample Project Code Composition

Take the `TestFull` project as an example, showing the engine's full usage. It dynamically installs three features (Log/World/Render), and uses an **input driver layer** (GameInput) to drive install/uninstall/exit per frame, verifying dependency-graph ordering and dynamic uninstall safety.

### Directory Structure

```
TestFull/
  TestFull.cproject              <- project manifest (engine plugins selected)
  Plugins/
    TestFull/                    <- entry plugin (entry template)
      Public/TestFull.h          <- FTestFull : FEngineBase
      Private/TestFull.cpp       <- Initialize/Shutdown + CreateEngine bridge
      TestFull.cplugin           <- dependency table
    DynLog/                      <- feature: log layer (no dependencies)
      Public/DynLog.h            <- FDynLog : FEngineLayer
      Private/DynLog.cpp         <- three-stage tracing
    DynWorld/                    <- feature: world layer
      Public/DynWorld.h          <- FDynWorld : FEngineLayer (Tick depends on DynLog.BeginFrame)
      Private/DynWorld.cpp
    DynRender/                   <- feature: render layer
      Public/DynRender.h         <- FDynRender : FEngineLayer (EndFrame depends on DynWorld.Tick)
      Private/DynRender.cpp
    GameInput/                   <- feature: input driver layer
      Public/GameInput.h         <- FGameInput : FEngineLayer
      Private/GameInput.cpp      <- Tick drives Install/TryUninstall/RequestExit per frame
  Intermediate/Main.cpp          <- entry (code-gen, do not edit)
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### Entry Plugin (TestFull)

```cpp
// TestFull.h
class FTestFull : public FEngineBase
{
    MAHO_DECLARE_ENGINE(FTestFull, "TestFull.dll");
public:
    void Initialize(int Argc, char** Argv) override;
    void Shutdown() override;
};
```

```cpp
// TestFull.cpp
void FTestFull::Initialize(int Argc, char** Argv)
{
    FLog::Get().Initialize(Argc, Argv);
    // install only the input driver layer; the rest are dynamically installed by GameInput per frame.
    Install("GameInput.dll");
}

void FTestFull::Shutdown()
{
    FLog::Get().Shutdown();   // features + DLLs already released by FEngineBase::Shutdown
}

extern "C" MAHO_TESTFULL_API Maho::FEngineBase* CreateEngine()
{
    return Maho::FTestFull::CreateEngine();
}
```

### Input Driver Layer (GameInput)

```cpp
// GameInput.cpp - simulates user input in Tick, drives install/uninstall/exit
void FGameInput::Tick()
{
    ++TickCount;
    switch (TickCount)
    {
    case 1: Owner->Install("DynLog.dll");    break;   // install one per frame
    case 2: Owner->Install("DynWorld.dll");   break;
    case 3: Owner->Install("DynRender.dll");  break;
    case 5: Owner->TryUninstall("FDynWorld"); break;   // depended on -> dropped
    case 7:
        Owner->TryUninstall("FDynWorld");              // two requests in the same frame
        Owner->TryUninstall("FDynRender");             // dependent pops first, chain uninstall
        break;
    default: Owner->RequestExit();            break;   // exit the main loop
    }
}
```

`Owner` is an `FEngineLayer` member, injected automatically by the engine as `FEngineBase*` at `Install` time - the feature schedules install/uninstall/exit through it, on the same level as other features.

### Feature Dependency Declaration (DynWorld / DynRender)

```cpp
// DynWorld.h - Tick depends on DynLog's BeginFrame (cross-DLL string addressing)
class FDynWorld : public FEngineLayer
{
    MAHO_DECLARE_LAYER(FDynWorld);
    MAHO_DECLARE_FEATURE(FDynWorld, "DynWorld.dll");
public:
    FDynWorld()
    {
        AddDependency(std::type_index(typeid(ITick)), "FDynLog", std::type_index(typeid(IBeginFrame)));
    }
    void BeginFrame() override;
    void Tick() override;
    void EndFrame() override;
};
```

```cpp
// DynRender.h - EndFrame depends on DynWorld's Tick
class FDynRender : public FEngineLayer
{
    MAHO_DECLARE_LAYER(FDynRender);
    MAHO_DECLARE_FEATURE(FDynRender, "DynRender.dll");
public:
    FDynRender()
    {
        AddDependency(std::type_index(typeid(IEndFrame)), "FDynWorld", std::type_index(typeid(ITick)));
    }
    ...
};
```

### Run Verification

```
tick 1-3 : dynamic install Log -> World -> Render
tick 4   : 3 features scheduled in parallel (dependency-free stages run in parallel)
tick 5   : request uninstall of World (depended on by Render) -> dropped
tick 6   : World still there
tick 7   : request uninstall of World + Render in the same frame -> Render pops first, World chains out
tick 8   : only Log remains
tick 9   : RequestExit -> exit
```

Full closed loop: `EntryPoint` -> `FAssembly` loads `TestFull.dll` -> `CreateEngine()` -> `FEngineBase*` -> `Initialize` (install GameInput) -> `Main` (main loop) -> GameInput.Tick drives dynamically -> dependency-graph topological scheduling -> `Shutdown`.

## Interface and Implementation Separation

The entry plugin's `Public/` only holds interfaces (grouped in folders by function); implementations go in a new plugin outside the entry, in its own `Private/`. The entry does not care how interfaces are implemented, it only schedules. Plugin Public headers do not leak third-party dependencies.
