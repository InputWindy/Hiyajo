# Log

## 代码文件

- [Log.h](Log.h) — 日志单例 `FLog`（封装 spdlog 的 stdout-color logger）
- [LogApi.h](LogApi.h) — 跨 DLL 导出宏（`MAHO_LOG_API`）

## 概念——日志服务

日志单例服务——封装 **spdlog** 的线程安全 stdout-color logger，fmt 风格格式化（`{}` 占位），全引擎（含其他服务插件）共享同一个 logger。`FLog` 继承 `TSingleton<FLog>` + `IPlugin<IInit, IShutdown>`：`Initialize(argc, argv)` 创建并配置 logger（识别 `--log-level=trace|debug|info|warn|error`），`Shutdown()` flush + 释放。

### FLog —— 单例 + 静态透传

- `Logger`（public）：共享 `std::shared_ptr<spdlog::logger>`，一切日志输出走它。
- 静态透传：`FLog::Info / Warn / Error(fmt, args...)`（fmt 风格，编译期检查格式串）。
- 宏糖：`MAHO_LOG_CORE_TRACE / DEBUG / INFO / WARN / ERROR / CRITICAL(...)`——直接透传到 `Logger->xxx`。

```cpp
FLog::Get().Initialize(argc, argv);
FLog::Info("init: {}", name);
FLog::Error("boom: code={}", code);
MAHO_LOG_CORE_INFO("init {}", name);
```

`Get()` 声明在头、定义在 `Log.cpp`（编进 Log.dll）——实例进程唯一。依赖插件经 Log.dll 链接 spdlog，无需自带三方。

## 三方依赖

- **spdlog**（header-only，`spdlog/spdlog.h` + stdout_color_sinks）——线程安全 stdout-color 日志。

## 相关文档

- [API.html](API.html) — API 文档
