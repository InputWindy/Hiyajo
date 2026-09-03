# Maho — 文档索引

全仓文档导航。所有文档均为 Markdown；公开 API 页 ↔ 实现字典页经 `#fn-...` 稳定锚点互跳。

## 引擎总览

| 文档 | 内容 |
|------|------|
| [README.md](README.md) | 引擎总览：简介 / 构建流程 / 核心概念 / 示例项目 |
| [AGENTS.md](AGENTS.md) | AI agent 约定：设计约束 / 依赖链接规则（**写代码前必读**） |
| [Source/SourceDoc.md](Source/SourceDoc.md) | 源码根：Public / Private 分层 |
| [Source/Public/PublicDoc.md](Source/Public/PublicDoc.md) | Public 层：入口 + 聚合 |
| [Source/Private/PrivateDoc.md](Source/Private/PrivateDoc.md) | Private 层：实现编译单元 |

## 引擎核心（Source）

| 模块 | 概念 | 公开 API | 实现字典 |
|------|------|----------|----------|
| **Core**（编译期 + 并发基建） | [CoreDoc](Source/Public/Core/CoreDoc.md) | [CoreAPI](Source/Public/Core/CoreAPI.md) | [CoreAPI（Private）](Source/Private/Core/CoreAPI.md) |
| **Engine**（层系统） | [EngineDoc](Source/Public/Engine/EngineDoc.md) | [EngineAPI](Source/Public/Engine/EngineAPI.md) | [EngineAPI（Private）](Source/Private/Engine/EngineAPI.md) |

## 插件（Plugins）

### Common — 通用服务

| 插件 | 形态 | 文档 |
|------|------|------|
| **Archive** | 纯库（序列化） | [API](Plugins/Common/Archive/API.md) · [概念](Plugins/Common/Archive/Archive.md) · [实现](Plugins/Common/Archive/ImplAPI.md) |
| **Compress** | 纯库（zstd） | [API](Plugins/Common/Compress/API.md) · [概念](Plugins/Common/Compress/Compress.md) · [实现](Plugins/Common/Compress/ImplAPI.md) |
| **ConsoleVariable** | 单例（CVar） | [API](Plugins/Common/ConsoleVariable/API.md) · [概念](Plugins/Common/ConsoleVariable/ConsoleVariable.md) · [实现](Plugins/Common/ConsoleVariable/ImplAPI.md) |
| **Reflect** | 纯库（refl-cpp 宏） | [API](Plugins/Common/Reflect/API.md) · [概念](Plugins/Common/Reflect/Reflect.md) |
| **RHI** | 渲染服务器 | [API](Plugins/Common/RHI/API.md) · [概念](Plugins/Common/RHI/RHI.md) · [实现](Plugins/Common/RHI/ImplAPI.md) |
| **Unicode** | 纯库（编码转换） | [API](Plugins/Common/Unicode/API.md) · [概念](Plugins/Common/Unicode/Unicode.md) · [实现](Plugins/Common/Unicode/ImplAPI.md) |

### Engine — 引擎服务

| 插件 | 形态 | 文档 |
|------|------|------|
| **Asset** | 服务层（资源注册表） | [API](Plugins/Engine/Asset/API.md) · [概念](Plugins/Engine/Asset/Asset.md) · [实现](Plugins/Engine/Asset/ImplAPI.md) |
| **Config** | 服务层（INI 配置） | [API](Plugins/Engine/Config/API.md) · [概念](Plugins/Engine/Config/Config.md) · [实现](Plugins/Engine/Config/ImplAPI.md) |
| **Exception** | 服务层（异常中心） | [API](Plugins/Engine/Exception/API.md) · [概念](Plugins/Engine/Exception/Exception.md) · [实现](Plugins/Engine/Exception/ImplAPI.md) |
| **Log** | 服务层（日志） | [API](Plugins/Engine/Log/API.md) · [概念](Plugins/Engine/Log/Log.md) · [实现](Plugins/Engine/Log/ImplAPI.md) |
| **Name** | 服务层（字符串驻留） | [API](Plugins/Engine/Name/API.md) · [概念](Plugins/Engine/Name/Name.md) · [实现](Plugins/Engine/Name/ImplAPI.md) |
| **Paths** | 服务层（虚拟路径） | [API](Plugins/Engine/Paths/API.md) · [概念](Plugins/Engine/Paths/Paths.md) · [实现](Plugins/Engine/Paths/ImplAPI.md) |
| **Platform** | 服务层 + 帧阶段（窗口/事件） | [API](Plugins/Engine/Platform/API.md) · [概念](Plugins/Engine/Platform/Platform.md) · [实现](Plugins/Engine/Platform/ImplAPI.md) |
| **Render** | 渲染子系统（层 + collector + 线程） | [API](Plugins/Engine/Render/API.md) · [概念](Plugins/Engine/Render/Render.md) · [实现](Plugins/Engine/Render/ImplAPI.md) |
| **Resource** | 服务层 + IO 线程（异步资源） | [API](Plugins/Engine/Resource/API.md) · [概念](Plugins/Engine/Resource/Resource.md) · [实现](Plugins/Engine/Resource/ImplAPI.md) |
| **Script** | 服务层（Lua + sol2） | [API](Plugins/Engine/Script/API.md) · [概念](Plugins/Engine/Script/Script.md) · [实现](Plugins/Engine/Script/ImplAPI.md) |
| **Text** | 服务层（本地化） | [API](Plugins/Engine/Text/API.md) · [概念](Plugins/Engine/Text/Text.md) · [实现](Plugins/Engine/Text/ImplAPI.md) |
| **Timer** | 服务层（计时/剖析） | [API](Plugins/Engine/Timer/API.md) · [概念](Plugins/Engine/Timer/Timer.md) · [实现](Plugins/Engine/Timer/ImplAPI.md) |

## 示例项目（Example）

| 文档 | 内容 |
|------|------|
| [ExampleEngine README](Example/ExampleEngine/README.md) | 项目走读：入口插件 + Scene + DrawTriangleFeature |
| [入口插件 API](Example/ExampleEngine/Plugins/ExampleEngine/API.md) | FExampleEngine（PreMain 安装服务层） |
| [Scene API](Example/ExampleEngine/Plugins/RenderFeature/Scene/API.md) | 场景目标（SceneColor/SceneDepth） |
| [DrawTriangleFeature API](Example/ExampleEngine/Plugins/RenderFeature/DrawTriangleFeature/API.md) | 全屏三角形绘制 |
