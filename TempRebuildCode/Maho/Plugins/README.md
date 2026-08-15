# Plugins — 插件总览

引擎功能按插件组织，每个插件一个目录，独立编译成 `.dll`（共享构建）。code-gen 扫各插件的 `.cplugin` 配置表，把扩展装配进项目引擎。

## 插件清单

### Singleton（ESingletonStage，预 app 单例）

| 插件 | 扩展类 | 说明 |
|------|--------|------|
| [Log](Log/Log.cplugin) | `Maho::FLogger` | 日志（spdlog） |
| [Timer](Timer/Timer.cplugin) | `Maho::FTimer` | 时钟（chrono） |
| [Config](Config/Config.cplugin) | `Maho::FConfig` | 配置（JSON） |
| [Paths](Paths/Paths.cplugin) | `Maho::FPaths` | 路径解析（项目/引擎根） |
| [Json](Json/Json.cplugin) | `Maho::FJson` | JSON 序列化（nlohmann/json） |
| [ConsoleVariable](ConsoleVariable/ConsoleVariable.cplugin) | `Maho::FConsoleVariable` | 控制台变量注册表 |
| [Compress](Compress/Compress.cplugin) | `Maho::FCompress` | 压缩（zlib/zstd） |
| [Archive](Archive/Archive.cplugin) | `Maho::FArchive` | 序列化归档 |
| [Text](Text/Text.cplugin) | `Maho::FText` | 文本 / UTF-8 编码 |
| [Physics](Physics/Physics.cplugin) | `Maho::FPhysics` | 物理模拟库（刚体 solver） |
| [Audio](Audio/Audio.cplugin) | `Maho::FAudio` | 音频播放库（设备 + 音源） |
| [Math](Math/Math.cplugin) | `Maho::FMath` | 数学库（GLM + 数学辅助） |

### Engine（EEngineStage，引擎扩展）

| 插件 | 扩展类 | 说明 |
|------|--------|------|
| [Platform](Platform/Platform.cplugin) | `Maho::FPlatformSystem` | OS 窗口 + 输入（GLFW） |
| [RHI](RHI/RHI.cplugin) | `Maho::FRHI` | 渲染硬件接口（GPU 设备） |
| [Render](Render/Render.cplugin) | `Maho::FRenderSystem` | Render/RHI/RDG/Shader/UI |
| [Resource](Resource/Resource.cplugin) | `Maho::FResourceSystem` | 异步资源系统 + 包 IO |
| [Script](Script/Script.cplugin) | `Maho::FScriptSystem` | Lua 脚本 VM（sol2 + Lua） |
| [World](World/World.cplugin) | `Maho::FInitializationSystemGroup` | DOTS 风格 ECS world / system-group |
| [Exception](Exception/Exception.cplugin) | `Maho::FException` | 异常处理（非致命异常事件） |
| [Network](Network/Network.cplugin) | `Maho::FNetworkSystem` | 网络通信（客户端 / 服务器） |
| [Editor](Editor/Editor.cplugin) | `Maho::FEditorSystem` | 编辑器工具（ImGui 编辑器 UI） |

> 状态：全部脚手架——目录结构 + `.cplugin` + `.cmake` 已就位，源码未实现。

## 目录结构

```
<Name>/
├── <Name>.cplugin        # 插件配置表（code-gen 扫描这个）
├── <Name>.cmake          # 构建脚本（第三方依赖、编译选项）
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
				"Class": "Maho::FRenderSystem",
				"Header": "RenderSystem.h",
				"Priority": "System"
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
| `Modules[].Dependencies` | 依赖的其他插件模块 |
| `Modules[].Extension.Class` | 扩展类（`TExtension` 子类） |
| `Modules[].Extension.Header` | 扩展类所在头（聚合头内 include） |
| `Modules[].Extension.Priority` | 优先级（System / Layer / Overlay） |
| `Modules[].Extension.Stage` | 装配目标注册表：`ESingletonStage`（预 app 单例）或 `EEngineStage`（引擎） |

code-gen 汇总所有启用的插件，把 `Extension.Class` 塞进项目引擎的 `FExtensions<...>` 继承列表（见 [../Source/Public/Maho.md](../Source/Public/Maho.md) 装配方式）。

## 引用 / 拓展

- 引用某插件功能前，先读该插件的 `<Name>.md`（未实现时看 `.cplugin` 的 Description）。
- 新增插件：复制一个脚手架目录，改 `.cplugin` 的 FriendlyName / Class，`.cmake` 里写依赖拉取，源码填进 `Source/<Name>/`。
- 插件编译产物：共享构建下每个插件一个 `.dll`，通过 `MAHO_API` 导出对外符号。

## 相关文档

- [../Source/Public/Maho.md](../Source/Public/Maho.md) — 引擎核心（装配目标）
- [../Source/Public/Core/Core.md](../Source/Public/Core/Core.md) — 扩展 / 调度器基础
- [../Source/Public/Core/Extension.h](../Source/Public/Core/Extension.h) — `TExtension` 定义
