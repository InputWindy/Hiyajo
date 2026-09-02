# Log（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Log.cpp

<a id="fn-log-get"></a>
### GetLog()

← [公开 API](API.md) · `FLog*`

返回全局指针 `GLog`——`Initialize` 置 `this`，`Shutdown` 置空。

```text
GetLog():
1. return GLog
```

<a id="fn-log-init"></a>
### FLog::Initialize(FEngineBase& Engine)

← [公开 API](API.md) · `void`

装配 spdlog 日志器并发布 `this`。stdout 彩色 + 轮转文件双 sink；级别取命令行 `--log-level`。

```text
Initialize(Engine):
1. ConsoleSink = stdout_color_sink_mt()
2. FileSink    = rotating_file_sink_mt("Logs/Maho.log", 5MB, 3)
3. Logger      = make_shared<logger>("Maho", { ConsoleSink, FileSink })
4. spdlog::register_logger(Logger)
5. Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v")
6. Lv = debug
7. Level = Engine.Get("log-level")            // --log-level=trace|debug|info|warn|error
8. if !Level.empty(): Lv = spdlog::level::from_str(Level)
9. Logger->set_level(Lv)
10. GLog = this
```

<a id="fn-log-shutdown"></a>
### FLog::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

回收：撤回 `GLog`，flush 全部 sink，释放日志器。

```text
Shutdown(Engine):
1. GLog = nullptr
2. spdlog::shutdown()       // flush 全部 sink
3. Logger.reset()
```

<a id="fn-log-line"></a>
### FLog::LogLine(ELogLevel Level, std::string Message)

私有——六档模板方法的公共落点。按级别把已格式化消息交给 spdlog；日志器不存在时安全跳过。

```text
LogLine(Level, Message):
1. if !Logger: return
2. switch Level:
3.   Trace:    Logger->trace(Message)
4.   Debug:    Logger->debug(Message)
5.   Info:     Logger->info(Message)
6.   Warn:     Logger->warn(Message)
7.   Error:    Logger->error(Message)
8.   Critical: Logger->critical(Message)
```

<a id="fn-log-createlayer"></a>
### CreateLayer()（extern "C"）

DLL 动态安装入口——宿主按符号名查找。委托静态 `FLog::CreateLayer()`（`MAHO_DECLARE_LAYER` 生成）。

```text
CreateLayer():
1. return Maho::FLog::CreateLayer()
```

- [Log.md](Log.md) — 概念 · [公开 API](API.md) — 签名入口
