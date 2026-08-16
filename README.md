# TempRebuildCode — Maho 引擎重构临时工区

所有重构后的引擎类和接口实现放在这里（头文件名也重新设计）。全部完成后，手动整体替换 `Maho/` 源代码。

## 目录结构

```text
TempRebuildCode/
├── Maho/                          # 重构后的引擎
│   ├── CMakeLists.txt              # 引擎 DLL 入口
│   ├── Maho.cmake                  # 核心三方库 FetchContent
│   ├── Source/
│   │   ├── Public/                 # Core/ + Engine.h + EntryPoint* + Maho.h
│   │   └── Private/                # Core/Fatal.cpp
│   └── Plugins/                    # 插件（自包含：.cplugin + .cmake + settings.json + Source）
├── Build/                          # CMake 框架（CMake 模块 + workspace + Templates）
├── Tools/                          # Python 工具框架
└── README.md                       # 本文件
```

## 重构状态

### 核心（已完成）

- `Core/`：`TypeList` / `Topology` / `Delegate` / `Extension` / `Fatal` / `Export` / `Async`（`FThreadPool` + `FThreadedServer` + `IRunable`/`ICommandLine`）
- `Engine.h`：`EToolStage` / `EEngineStage` / `FToolkitBase` / `FEngineBase`（`MainLoop` + `RequestShutdown` + `GApp`）
- `EntryPoint*`：`MahoMain`（IDE/引擎）+ `MahoCLIMain`（CLI），5 平台 shim

### 插件

| 类别 | 状态 |
|------|------|
| Tool（EToolStage） | 12 已实现（Log/Timer/Config/Paths/Json/ConsoleVariable/Archive/Unicode/Name/Text/Math/Asset），4 脚手架（Compress/Physics/Audio/CommandParser） |
| Engine（EEngineStage） | 4 已实现（Platform/Resource/Exception/PluginManager），4 脚手架（RHI/Render/Network/Editor），2 空（Script/World） |

详见 [Maho/Plugins/README.md](Maho/Plugins/README.md)。

### 工具链

- `Tools/maho_tools.py`：create_project / create_plugin / codegen_game_app / codegen_plugin_dependencies / git clone 进度流式输出
- 三方库拉取：逐插件 `settings.json` 镜像源 + `MAHO_GIT_PROXY_PREFIX` 透明代理前缀

## 编码规范

遵循根目录 [AGENTS.md](../AGENTS.md)：F/I/E/T/b 前缀、Allman 括号、Tab 缩进、英文注释、Public 只放接口、聚合头与模块同名、模板元编程优先、依赖声明用 `Core/Topology.h`（编译期拓扑排序 + 环检测）。

## 相关文档

- [Maho/Source/Public/Maho.md](Maho/Source/Public/Maho.md) — 引擎核心
- [Maho/Source/Public/Core/Core.md](Maho/Source/Public/Core/Core.md) — 基础设施
- [Maho/Source/Public/Core/Async/Async.md](Maho/Source/Public/Core/Async/Async.md) — 并行模型
- [Maho/Plugins/README.md](Maho/Plugins/README.md) — 插件总览
