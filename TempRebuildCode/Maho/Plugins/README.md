# Plugins — 插件总览

引擎功能按插件组织，每个插件一个目录，独立编译成 `.dll`（共享构建）。code-gen 扫各插件的 `.cplugin` 配置表，把扩展装配进项目引擎。

## 插件清单

状态：✅ 已实现 · ⬜ 脚手架（结构就位，实现 TODO）· 🈳 空目录

### Tool（EToolStage，预 app 工具包）

| 插件 | 扩展类 | 状态 | 说明 |
|------|--------|------|------|
| [Log](Log/Log.cplugin) | `Maho::Log::FLogger` | ✅ | 日志（spdlog） |
| [Timer](Timer/Timer.cplugin) | `Maho::Timer::FTimer` | ✅ | 栈 profiler + `FGameClock` |
| [Config](Config/Config.cplugin) | `Maho::Config::FConfig` | ✅ | INI 解析（DefaultEngine.ini 风格） |
| [Paths](Paths/Paths.cplugin) | `Maho::Paths::FPaths` | ✅ | 虚拟路径映射（别名 → 物理目录） |
| [Json](Json/Json.cplugin) | `Maho::Json::FJson` | ✅ | nlohmann/json（header-only 包装） |
| [ConsoleVariable](ConsoleVariable/ConsoleVariable.cplugin) | `Maho::ConsoleVariable::FConsoleVariable` | ✅ | UE 风格 CVar（`IConsoleVariable` + `TAutoConsoleVariable<T>`） |
| [Compress](Compress/Compress.cplugin) | `Maho::Compress::FCompress` | ⬜ | 压缩（zlib/zstd） |
| [Archive](Archive/Archive.cplugin) | `Maho::Archive::FArchiveSystem` | ✅ | 二进制序列化（`FArchive` + `FMemoryReader/Writer` + `ISerialize`） |
| [Unicode](Unicode/Unicode.cplugin) | `Maho::Unicode::FUnicode` | ✅ | 文本编码（utfcpp，UTF-8/16/32） |
| [Name](Name/Name.cplugin) | `Maho::Name::FNamePool` | ✅ | interned 字符串标识符池（`FName`） |
| [Text](Text/Text.cplugin) | `Maho::Text::FTextManager` | ✅ | 本地化文本（文化感知，依赖 Json） |
| [Physics](Physics/Physics.cplugin) | `Maho::Physics::FPhysics` | ⬜ | 物理模拟库（刚体 solver） |
| [Audio](Audio/Audio.cplugin) | `Maho::Audio::FAudio` | ⬜ | 音频播放库（设备 + 音源） |
| [Math](Math/Math.cplugin) | `Maho::Math::FMath` | ✅ | 数学库（GLM + helper，header-only） |
| [CommandParser](CommandParser/CommandParser.cplugin) | `Maho::CommandParser::FCommandParser` | ⬜ | 命令行参数解析（`ICommandLine` 实现） |
| [Asset](Asset/Asset.cplugin) | `Maho::Asset::FAssetRegistry` | ✅ | 资产路径 + 注册表（逻辑路径 → 物理文件 + 类型 + 依赖，依赖 Paths） |

### Engine（EEngineStage，引擎扩展）

| 插件 | 扩展类 | 状态 | 说明 |
|------|--------|------|------|
| [Platform](Platform/Platform.cplugin) | `Maho::Platform::FPlatformSystem` | ✅ | 平台抽象（GLFW 窗口 + EGL headless，`MAHO_HEADLESS` 开关） |
| [RHI](RHI/RHI.cplugin) | `Maho::RHI::FRHI` | ⬜ | 渲染硬件接口（GPU 设备，`FThreadedServer`） |
| [Render](Render/Render.cplugin) | `Maho::Render::FRenderSystem` | ⬜ | Render/RHI/RDG/Shader/UI（依赖 Platform） |
| [Resource](Resource/Resource.cplugin) | `Maho::Resource::FResourceSystem` | ✅ | 异步资源系统 + 类型化 Import/Export（`FThreadedServer`，依赖 Paths/Name） |
| [Script](Script/Script.cplugin) | `Maho::Script::FScriptSystem` | 🈳 | Lua 脚本 VM（sol2 + Lua，依赖 Resource） |
| [World](World/World.cplugin) | `Maho::World::FInitializationSystemGroup` | 🈳 | DOTS 风格 ECS world / system-group |
| [Exception](Exception/Exception.cplugin) | `Maho::Exception::FException` | ✅ | 异常处理（非致命异常多播事件） |
| [Network](Network/Network.cplugin) | `Maho::Network::FNetworkSystem` | ⬜ | 网络通信（客户端 / 服务器） |
| [Editor](Editor/Editor.cplugin) | `Maho::Editor::FEditorSystem` | ⬜ | 编辑器工具（ImGui 编辑器 UI） |
| [PluginManager](PluginManager/PluginManager.cplugin) | `Maho::PluginManager::FPluginManager` | ✅ | 运行时插件管理器（动态 DLL 装卸） |

## 目录结构

```
<Name>/
├── <Name>.cplugin        # 插件配置表（code-gen 扫描这个）
├── <Name>.cmake          # 构建脚本（第三方依赖、编译选项、镜像源）
├── settings.json         # 插件自包含配置（mirrors 镜像源等）
├── Content/              # 资源
└── Source/<Name>/
    ├── Public/           # 聚合头 + 对外接口
    └── Private/          # 实现
```

聚合头 `<Name>.h` 位于 `Source/<Name>/Public/`，与该目录下的 `<Name>.md`（概念）+ `<Name>.html`（API）构成"一个头 + 两份文档"。

## 配置表格式（`.cplugin`）

```json
{
	"FileVersion": 1,
	"FriendlyName": "Render",
	"Description": "Render/RHI/RDG/Shader/UI extension.",
	"Category": "Engine",
	"EnabledByDefault": true,
	"Modules": [
		{
			"Name": "Render",
			"Type": "Runtime",
			"Dependencies": ["Platform"],
			"Extension": {
				"Class": "Maho::Render::FRenderSystem",
				"Header": "RenderSystem.h",
				"Priority": "System",
				"Stage": "EEngineStage"
			}
		}
	]
}
```

字段：

| 字段 | 说明 |
|------|------|
| `Modules[].Name` | 模块名（编译目标） |
| `Modules[].Type` | Runtime / Editor 等 |
| `Modules[].Dependencies` | 依赖的其他插件模块（构建层链接顺序） |
| `Modules[].Extension.Class` | 扩展类（`TExtension` 子类） |
| `Modules[].Extension.Header` | 扩展类所在头（聚合头内 include） |
| `Modules[].Extension.Priority` | 优先级（System / Layer / Overlay） |
| `Modules[].Extension.Stage` | 装配目标：`EToolStage`（工具包）或 `EEngineStage`（引擎） |

code-gen 汇总所有启用的插件，把 `Extension.Class` 分进 toolkit / engine 两个 `FExtensions<...>` 继承列表（见 [../Source/Public/Maho.md](../Source/Public/Maho.md) 装配方式）。

## 依赖的两层语义

- **构建层**（`.cplugin` `Dependencies`）：CMake 链接顺序 + code-gen 同步 `<Name>.gen.h`。
- **调度层**（`FDependsPack`，`.gen.h`）：只对**同 stage** 依赖有意义——工具→工具或引擎→引擎，拓扑排序在各自调度器内生效。跨 stage（引擎依赖工具）由工具包先跑完保证顺序，引擎侧不需要声明。

## 引用 / 拓展

- 引用某插件功能前，先读该插件的 `<Name>.md`（未实现时看 `.cplugin` 的 Description）。
- 新增插件：复制一个脚手架目录，改 `.cplugin` 的 FriendlyName / Class，`.cmake` 里写依赖拉取，源码填进 `Source/<Name>/`。
- 插件编译产物：共享构建下每个插件一个 `.dll`，通过 `MAHO_<NAME>_API` 导出对外符号。
- 引擎插件（`EEngineStage`）源码里带 `CreateExtension()` 自由函数 + `IAdapter` 类（动态 DLL 装载用）；工具插件同样带工厂函数。

## 相关文档

- [../Source/Public/Maho.md](../Source/Public/Maho.md) — 引擎核心（装配目标）
- [../Source/Public/Core/Core.md](../Source/Public/Core/Core.md) — 扩展 / 调度器基础
- [../Source/Public/Core/Extension.h](../Source/Public/Core/Extension.h) — `TExtension` 定义
