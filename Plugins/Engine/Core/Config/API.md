# Config — API 文档

服务层：`FConfig` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`（`Config.dll`）。UE `DefaultEngine.ini` 格式的 INI 配置引擎——`Load` 解析 INI 文件，`GetString/GetInt/GetFloat/GetBool` 读取，`SetString` 运行时覆盖。`Initialize` 自动加载默认 + 平台覆盖配置，并把 `[ConsoleVariables]` 段推进 CVar 注册表。

## Config.h

### FConfig <class>

INI 风格配置层——按 **Section（段）** 组织键值对。内部 `Sections`（`std::map<std::string, FSection>`，`FSection = std::map<std::string, std::string>`）。读取 API 为 const 查询；写入（`Load`/`SetString`）应发生在拥有线程上。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FConfig, "Config.dll")` | 层声明宏（DLL 导出入口） |
| `bool Load(std::string_view Path)` | 从路径解析一个 INI 文件，合并进现有配置（后加载的键覆盖）；文件打不开返回 `false` |
| `std::optional<std::string> GetString(std::string_view Section, std::string_view Key) const` | 读字符串；缺段 / 缺键返回 `nullopt` |
| `std::int64_t GetInt(std::string_view Section, std::string_view Key, std::int64_t Default = 0) const` | 读整数（`stoll`）；缺失或解析失败返回 `Default` |
| `double GetFloat(std::string_view Section, std::string_view Key, double Default = 0.0) const` | 读浮点（`stod`）；缺失或解析失败返回 `Default` |
| `bool GetBool(std::string_view Section, std::string_view Key, bool Default = false) const` | 读布尔；值小写后 `true`/`1`/`yes`/`on` 为真，其余为假；缺失返回 `Default` |
| `void SetString(std::string_view Section, std::string_view Key, std::string Value)` | 运行时写入 / 覆盖一个键（隐式创建段） |
| `bool HasSection(std::string_view Section) const` | 段是否存在 |
| `bool HasKey(std::string_view Section, std::string_view Key) const` | 段内键是否存在（等价 `GetString(...).has_value()`） |

### GetConfig <自由函数>

全局配置访问器——`Initialize` 置 `this`，`Shutdown` 置空。跨 DLL 经函数访问。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_CONFIG_API FConfig* GetConfig()` | 返回已初始化的 `FConfig*`；`Initialize` 前 / `Shutdown` 后为 `nullptr` |

## ConfigApi.h

### MAHO_CONFIG_API <宏>

DLL 导出/导入宏——`MAHO_CONFIG_MODULE_EXPORTS` 定义时展开为 `MAHO_EXPORT`，否则为 `MAHO_IMPORT`（详见 `Core/Export.h`）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_CONFIG_API` | 修饰本 DLL 导出的符号（`GetConfig`、`CreateLayer`） |

- [Config.md](Config.md) — 概念 · [实现字典](ImplAPI.md) — 算法
