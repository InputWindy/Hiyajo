# Log

日志单例服务——封装 spdlog 的线程安全 stdout-color logger，fmt 风格格式化输出，全引擎共享同一个 logger。

## 提供

- `FLog`：`TSingleton<FLog>` + `IPlugin<IInit, IShutdown>`，`Get()` 进程唯一访问器（定义在 Log.dll 的 Log.cpp）。
- `Logger`（public）：共享 `std::shared_ptr<spdlog::logger>`，所有引擎/服务日志都走它。
- 静态透传：`FLog::Info / Warn / Error(fmt, args...)`。
- 宏：`MAHO_LOG_CORE_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL(...)`。
- `Initialize` 解析 `--log-level=trace|debug|info|warn|error` 设级别。

## 示例

```cpp
FLog::Get().Initialize(argc, argv);
FLog::Info("init: {}", name);
FLog::Error("boom: code={}", code);
MAHO_LOG_CORE_INFO("init {}", name);
```

## 依赖

- 三方：spdlog。
- 其他插件：无。

## 相关文档

- [API.html](API.html) — API 文档
