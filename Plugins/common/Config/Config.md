# Config

INI 风格配置单例（UE DefaultEngine.ini 格式）——`Load` 解析 INI 文件，按 Section/Key 读值，运行时 `SetString` 覆盖。

## 提供

- `FConfig`：`TSingleton<FConfig>` + `IPlugin<IInit, IShutdown>`。
  - `Load(Path)`：解析 INI 文件。
  - `GetString / GetInt / GetFloat / GetBool(Section, Key, Default)`：读值（带默认值）。
  - `SetString(Section, Key, Value)`：运行时覆盖。
  - `HasSection / HasKey`：存在性查询。

## 示例

```cpp
FConfig::Get().Load("Engine/Config/DefaultEngine.ini");
const auto GameName = FConfig::Get().GetString("/Script/Engine.Engine", "GameName");
```

## 依赖

- 三方：无。
- 其他插件：无。
