# Private

## 代码文件

- [Log.cpp](Log.cpp)

## 实现算法字典

Log 插件的实现集中在 `Log.cpp`——spdlog 封装。

| 函数 | 说明 |
|------|------|
| `FLog::ExecuteStage(ELogStage Stage)` | 阶段分发：`Init` → set level info；`Shutdown` → flush + `spdlog::shutdown()` |
| `SetLogLevel(ELogLevel Level)` | 把 `ELogLevel` 映射到 spdlog 级别（debug/info/warn/err/off） |
| `Debug(const char*)` | `spdlog::debug` |
| `Info(const char*)` | `spdlog::info` |
| `Warn(const char*)` | `spdlog::warn` |
| `Error(const char*)` | `spdlog::error` |

**spdlog 只在 Private**——`Public/Log.h` 不含任何 spdlog 头，避免把三方依赖传播给宿主。

## 相关文档

- [../Log.md](../Log.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层
