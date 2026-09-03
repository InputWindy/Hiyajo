# Config

## Code files

- [Config.h](Public/Config.h) — 配置层头：`FConfig`
- [ConfigApi.h](Public/ConfigApi.h) — DLL 导出宏 `MAHO_CONFIG_API`
- [Config.cpp](Private/Config.cpp) — INI 解析 + 读取 API 实现 + 跨 DLL 访问器 `GetConfig`

## Concept - INI Section-Key Configuration

Config 用 UE `DefaultEngine.ini` 风格的**段键**模型组织配置：文件由 `[Section]` 头与 `Key=Value` 行组成；`;`/`#` 开头为注释；行首尾空白被剥掉。`GetConfig()->GetString("/Script/Engine.Engine", "GameName")` 读取。

### 1. 加载（Load / Initialize）

`Load(Path)` 把文件合并进现有 `Sections`，**后加载覆盖先加载**。引擎初始化时 `FConfig::Initialize` 依次加载：

1. `Config/DefaultEngine.ini`（通用默认）。
2. `Config/<Platform>.ini`（平台覆盖，如 `Windows.ini` / `Android.ini` / `IOS.ini` / `Linux.ini`）——后加载，同名键胜出。

之后把 `[ConsoleVariables]` 段里每个键值推进 CVar 注册表（`ConsoleVariable::FConsoleVariable::Get().Find(Key)` → `Var->Set(Value)`），即"INI 键名 == CVar 名"。

### 2. 读取（Get*）

- `GetString` 直接返回键值（`std::optional`，缺段 / 缺键为 `nullopt`）。
- `GetInt` / `GetFloat` 先取字符串再 `stoll`/`stod`，解析失败回落到默认值。
- `GetBool` 把值小写后与 `true`/`1`/`yes`/`on` 匹配（其余为假）。

```cpp
#include <Config.h>

using namespace Maho;

if (Config::FConfig* C = Config::GetConfig())
{
    auto GameName = C->GetString("/Script/Engine.Engine", "GameName");
    if (GameName) { /* "Maho" */ }

    bool bMotionBlur = C->GetBool("/Script/Engine.RendererSettings", "r.MotionBlur", true);
    C->SetString("/Script/Engine.Engine", "GameName", "Maho2");
}
```

## Third-party dependencies

- None (pure std).

## Related docs

- [API.md](API.md) - API documentation
- [ImplAPI.md](ImplAPI.md) - 实现算法字典
- [EngineDoc.md](../../../Source/Public/Engine/EngineDoc.md) - 层架构
