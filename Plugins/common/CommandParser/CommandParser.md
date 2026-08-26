# CommandParser

## 代码文件

- [CommandParser.h](CommandParser.h) — 命令行解析单例 `FCommandParser`

## 概念——命令行解析

命令行参数解析单例服务——把 `argc/argv` 解析成 **key → value 存储**，供启动期配置查询。`--key value` 与 `--key=value` 两种形式都支持；裸 flag（无值）记为 `true`；非连字符参数（positional）忽略。底层用 **CLI11**（引擎三方，header-only）做真正的 tokenize / 引号值处理。

### FCommandParser —— key-value 存储

`TSingleton<FCommandParser>` + `IPlugin<IInit, IShutdown>`。`Initialize(argc, argv)` 即 `Parse`（幂等，后写覆盖）；`Shutdown` 清空。查询 API：`Has / Get / GetBool / GetInt`（bool 接受 `true/1/yes/on`；int 解析失败回 0），`GetAll()` 拿全部键值对。

```cpp
FCommandParser::Get().Initialize(argc, argv);
const std::string Name = FCommandParser::Get().Get("name");
const bool Verbose = FCommandParser::Get().GetBool("verbose");
```

内部把每个 `-key` 规范化为 `--key` 长选项后交给 CLI11 逐个声明解析，坏输入不 abort（尽力读回已解析部分）。

## 三方依赖

- **CLI11**（`CLI/CLI.hpp`，引擎三方 header-only）——选项 tokenize 与引号值处理。

## 相关文档

- [API.html](API.html) — API 文档
