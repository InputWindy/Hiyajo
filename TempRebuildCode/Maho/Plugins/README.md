# Plugins — 插件总览

引擎功能按插件组织，每个插件一个目录，独立编译成 `.dll`（共享构建）。code-gen 扫各插件的 `.cplugin` 配置表，把扩展装配进项目引擎。

## 插件清单

| 插件 | 扩展类 | 说明 | 状态 |
|------|--------|------|------|
| [Platform](Platform/Platform.cplugin) | `Maho::FPlatformSystem` | OS 窗口 + 输入（GLFW） | 脚手架 |
| [Render](Render/Render.cplugin) | `Maho::FRenderSystem` | Render/RHI/RDG/Shader/UI | 脚手架 |
| [Resource](Resource/Resource.cplugin) | `Maho::FResourceSystem` | 异步资源系统 + 包 IO | 脚手架 |
| [Script](Script/Script.cplugin) | `Maho::FScriptSystem` | Lua 脚本 VM（sol2 + Lua） | 脚手架 |
| [World](World/World.cplugin) | `Maho::FInitializationSystemGroup` | DOTS 风格 ECS world / system-group | 脚手架 |

> 状态"脚手架"= 目录结构 + `.cplugin` + `.cmake` 已就位，源码未实现。

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
| `Modules[].Extension.Priority` | 优先级（System / Feature 等） |

code-gen 汇总所有启用的插件，把 `Extension.Class` 塞进项目引擎的 `FExtensions<...>` 继承列表（见 [../Source/Public/Maho.md](../Source/Public/Maho.md) 装配方式）。

## 引用 / 拓展

- 引用某插件功能前，先读该插件的 `<Name>.md`（未实现时看 `.cplugin` 的 Description）。
- 新增插件：复制一个脚手架目录，改 `.cplugin` 的 FriendlyName / Class，`.cmake` 里写依赖拉取，源码填进 `Source/<Name>/`。
- 插件编译产物：共享构建下每个插件一个 `.dll`，通过 `MAHO_API` 导出对外符号。

## 相关文档

- [../Source/Public/Maho.md](../Source/Public/Maho.md) — 引擎核心（装配目标）
- [../Source/Public/Core/Core.md](../Source/Public/Core/Core.md) — 扩展 / 调度器基础
- [../Source/Public/Core/Extension.h](../Source/Public/Core/Extension.h) — `TExtension` 定义
