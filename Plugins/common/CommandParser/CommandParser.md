# CommandParser

命令行参数解析单例——把 argc/argv 解析成 key-value 存储；`--key value` 与 `--key=value` 都支持，位置参数存到 positional key。

## 提供

- `FCommandParser`：`TSingleton<FCommandParser>` + `IPlugin<IInit, IShutdown>`。
  - `Parse(argc, argv)`：解析（幂等，后写覆盖）。
  - `Has(Key)` / `Get(Key)` / `GetBool(Key)` / `GetInt(Key)`：查询。
  - `GetAll()`：全部 key→value 对；`Clear()`：清空。

## 示例

```cpp
FCommandParser::Get().Initialize(argc, argv);
const std::string Name = FCommandParser::Get().Get("name");
const bool Verbose = FCommandParser::Get().GetBool("verbose");
```

## 依赖

- 三方：CLI11（引擎三方，header-only）。
- 其他插件：无。
