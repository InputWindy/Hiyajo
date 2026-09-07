# Log

## Code files

- [Log.h](Public/Log.h) — 日志层头：`ELogLevel` / `FLog` + `MAHO_LOG_CORE_*` 宏
- [LogApi.h](Public/LogApi.h) — DLL 导出宏 `MAHO_LOG_API`
- [Log.cpp](Private/Log.cpp) — spdlog 日志器装配 + `LogLine` 分发 + 跨 DLL 访问器 `GetLog`

## Concept - fmt-Formatted Logging Behind an Incomplete Type

Log 是引擎的**格式化日志通道**。它把 spdlog 完整藏在 `std::shared_ptr<spdlog::logger>` 不完整类型后面——头文件只前向声明 `spdlog::logger`，全部 spdlog 类型只出现在 `Log.cpp`。调用方只接触 `GetLog()->Info("{}", v)` 这类 fmt 风格接口。

### 1. 装配（Initialize）

`Initialize` 构造两条 sink：

- **stdout 彩色** sink（`stdout_color_sink_mt`）。
- **轮转文件** sink（`Logs/Maho.log`，5MB × 3）——GUI 子系统应用没有控制台，文件是持久日志目的地。

日志器名 `"Maho"`，格式 `[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，级别从引擎命令行 `--log-level` 读取（`Engine.Get("log-level")`，缺省 `debug`）。

### 2. 使用（模板方法 + 宏）

六档模板方法做 perfect-forward + fmt 编译期检查；`MAHO_LOG_CORE_*` 宏在 `GetLog()` 可能为 null 的场景（Log 层 Initialize 前）安全跳过。

```cpp
#include <Log.h>

using namespace Maho;

// 直接经访问器（须先装层）
if (FLog* L = GetLog())
{
    L->Info("init {}", name);
}

// 宏版本（null 安全，仅报一次）
MAHO_LOG_CORE_INFO("boot");
MAHO_LOG_CORE_ERROR("boom: code={}", code);
```

### 3. 收尾（Shutdown）

`Shutdown` 撤回 `GLog`、`spdlog::shutdown()`（flush 全部 sink）、释放 `Logger`。

## Third-party dependencies

- spdlog（`stdout_color_sink_mt` / `rotating_file_sink_mt`，fmt 随附）——完整类型只出现在 `Log.cpp`。

## Related docs

- [API.md](API.md) - API documentation
- [ImplAPI.md](ImplAPI.md) - 实现算法字典
- [EngineDoc.md](../../../Source/Public/Engine/EngineDoc.md) - 层架构
