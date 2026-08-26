# Config

## 代码文件

- [Config.h](Config.h) — INI 配置单例 `FConfig`

## 概念——INI 配置

INI 风格配置单例服务（UE `DefaultEngine.ini` 格式）——`Load` 解析 INI 文件为 Section → Key → Value 两级表，`GetString / GetInt / GetFloat / GetBool` 按 Section/Key 读值（带默认值），`SetString` 运行时覆盖。内部分层 `std::map<std::string, FSection>`。

### FConfig —— Section/Key 读取

`TSingleton<FConfig>` + `IPlugin<IInit, IShutdown>`（Initialize/Shutdown 清 `Sections`）。解析规则：

- `[Section]` 行切换当前段；`Key = Value` 行入库（去首尾空白）。
- `;` / `#` 开头为注释；空行跳过。
- `GetBool` 接受 `true / 1 / yes / on`（大小写不敏感）。

```cpp
FConfig::Get().Load("Engine/Config/DefaultEngine.ini");
const auto GameName = FConfig::Get().GetString("/Script/Engine.Engine", "GameName");
const std::int64_t MaxFPS = FConfig::Get().GetInt("SystemSettings", "MaxFPS", 60);
FConfig::Get().SetString("SystemSettings", "MaxFPS", "120");
```

## 三方依赖

- 无（纯 std）。

## 相关文档

- [API.html](API.html) — API 文档
