# Log — API 文档

服务层：`FLog` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`（`Log.dll`）——**不是单例**，实例由引擎层系统持有、经 `GetLog()` 发布。`Initialize` 拉起 spdlog 日志器（stdout 彩色 + 轮转文件，遵守 `--log-level`）并发布 `this`；`Shutdown` flush + 撤销。spdlog 藏在 `Logger` 不完整类型后面，调用方永远看不到 spdlog 类型。

## Log.h

### ELogLevel <enum class>

日志级别——**对 spdlog level 的类型擦除**（头文件不暴露 spdlog）。

#### 枚举值

| 枚举 | 说明 |
|------|------|
| `Trace` | 跟踪（最细） |
| `Debug` | 调试 |
| `Info` | 常规信息 |
| `Warn` | 警告 |
| `Error` | 错误 |
| `Critical` | 致命级（仍不终止进程） |

### FLog <class>

日志服务层（`FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`）。六档 `Trace/Debug/Info/Warn/Error/Critical` 模板方法做 perfect-forward + fmt 编译期格式化，统一落到私有 `LogLine`。`Logger` 是 `std::shared_ptr<spdlog::logger>` 不完整类型——析构在 `Log.cpp`（完整类型可见处）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FLog, "Log.dll")` | 层声明宏（DLL 导出入口） |
| `FLog()` / `~FLog() override` | 默认构造 / 析构（析构处 `spdlog::logger` 完整类型可见） |
| `template<typename... Args> void Trace(fmt::format_string<Args...> Fmt, Args&&... A)` | 记录 Trace 级（fmt 编译期检查） |
| `template<typename... Args> void Debug(fmt::format_string<Args...> Fmt, Args&&... A)` | 记录 Debug 级 |
| `template<typename... Args> void Info(fmt::format_string<Args...> Fmt, Args&&... A)` | 记录 Info 级 |
| `template<typename... Args> void Warn(fmt::format_string<Args...> Fmt, Args&&... A)` | 记录 Warn 级 |
| `template<typename... Args> void Error(fmt::format_string<Args...> Fmt, Args&&... A)` | 记录 Error 级 |
| `template<typename... Args> void Critical(fmt::format_string<Args...> Fmt, Args&&... A)` | 记录 Critical 级 |

### GetLog <自由函数>

全局日志访问器——跨 DLL 经函数访问（**不导出裸变量**）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_LOG_API FLog* GetLog()` | 返回已初始化的 `FLog*`；`Initialize` 前 / `Shutdown` 后为 `nullptr` |

### MAHO_LOG_CORE_* <宏>

引擎核心日志语法糖（fmt 风格）——在 Log 层安装并初始化后使用。`GetLog()` 可能为 null（Initialize 前）——宏经 `MAHO_ENSURE_NOT_NULL` 保证**只报一次**并跳过。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_LOG_CORE_TRACE(...)` | `GetLog()` 非空 → `L->Trace(...)` |
| `MAHO_LOG_CORE_DEBUG(...)` | `L->Debug(...)` |
| `MAHO_LOG_CORE_INFO(...)` | `L->Info(...)` |
| `MAHO_LOG_CORE_WARN(...)` | `L->Warn(...)` |
| `MAHO_LOG_CORE_ERROR(...)` | `L->Error(...)` |
| `MAHO_LOG_CORE_CRITICAL(...)` | `L->Critical(...)` |

## LogApi.h

### MAHO_LOG_API <宏>

DLL 导出/导入宏——`MAHO_LOG_MODULE_EXPORTS` 定义时展开为 `MAHO_EXPORT`，否则为 `MAHO_IMPORT`（详见 `Core/Export.h`）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_LOG_API` | 修饰本 DLL 导出的符号（`GetLog`、`CreateLayer`） |

- [Log.md](Log.md) — 概念 · [实现字典](ImplAPI.md) — 算法
