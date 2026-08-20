# Public

## 代码文件

- [Log.h](Log.h)
- [LogApi.h](LogApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FLog : TExtensionList<FLog>` | 日志单例（纯单例，无 Main/IAssembly） |
| `FLog::ExecuteStage(ELogStage)` | 阶段分发（Init / Shutdown） |
| `ELogStage` | 本插件自定义 drive stage |
| `ELogLevel` | 日志级别（Debug/Info/Warn/Error/Off） |
| `Debug/Info/Warn/Error(const char*)` | 全局日志函数 |
| `SetLogLevel(ELogLevel)` | 设置级别 |

**Public 头不泄露 spdlog**——所有 API 用 `const char*`，spdlog 实现留在 Private。

## 相关文档

- [../Log.md](../Log.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典
