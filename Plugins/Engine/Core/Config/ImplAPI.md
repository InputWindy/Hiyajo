# Config（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Config.cpp

<a id="fn-config-get"></a>
### GetConfig()

← [公开 API](API.md) · `FConfig*`

返回全局指针 `GConfig`——`Initialize` 置 `this`，`Shutdown` 置空。

```text
GetConfig():
1. return GConfig
```

<a id="fn-config-platform"></a>
### GetPlatformName()（内部）

匿名命名空间，仅被 `Initialize` 引用。编译期宏 → 平台 INI 覆盖名。

```text
GetPlatformName():
1. _WIN32        -> "Windows"
2. __ANDROID__   -> "Android"
3. __APPLE__     -> "IOS"      // 含 tvOS，暂不区分
4. __linux__     -> "Linux"
5. else          -> "Unknown"
```

<a id="fn-config-init"></a>
### FConfig::Initialize(FEngineBase& Engine)

← [公开 API](API.md) · `void`

发布 `this`，清空旧配置，加载默认 + 平台覆盖，把 `[ConsoleVariables]` 推进 CVar 注册表。

```text
Initialize(Engine):
1. GConfig = this
2. Sections.clear()
3. Load("Config/DefaultEngine.ini")
4. Load("Config/" + GetPlatformName() + ".ini")      // 后加载，覆盖默认
5. for (Key, Value) in Sections["ConsoleVariables"]:
6.   Var = ConsoleVariable::FConsoleVariable::Get().Find(Key)
7.   if Var: Var->Set(Value)
```

<a id="fn-config-shutdown"></a>
### FConfig::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

回收：撤回 `GConfig`，清空配置表。

```text
Shutdown(Engine):
1. GConfig = nullptr
2. Sections.clear()
```

<a id="fn-config-trim"></a>
### Trim(std::string_view S)（内部）

匿名命名空间，仅被 `Load` 引用。剥掉字符串首尾的空白（空格 / 制表 / 回车）。

```text
Trim(S):
1. First = S.find_first_not_of(" \t\r")
2. if First == npos: return ""
3. Last  = S.find_last_not_of(" \t\r")
4. return S[First .. Last]
```

<a id="fn-config-load"></a>
### FConfig::Load(std::string_view Path)

← [公开 API](API.md) · `bool`

解析一个 INI 文件并合并进 `Sections`。逐行：跳过空 / 注释，`[Section]` 切段，`Key=Value` 写入当前段。

```text
Load(Path):
1. Stream = ifstream(Path)
2. if !Stream: return false
3. CurrentSection = ""; Line
4. while getline(Stream, Line):
5.   T = Trim(Line)
6.   if T 空 或 T 以 ';' 或 '#' 开头: continue
7.   if T 以 '[' 开头 且 以 ']' 结尾:
8.     CurrentSection = T[1 .. size-2]; continue
9.   Eq = T.find('=')
10.  if Eq == npos: continue
11.  Sections[CurrentSection][Trim(T[0..Eq))] = Trim(T[Eq+1..])
12. return true
```

<a id="fn-config-getstring"></a>
### FConfig::GetString(std::string_view Section, std::string_view Key) const

← [公开 API](API.md) · `std::optional<std::string>`

读字符串；缺段 / 缺键返回 `nullopt`。

```text
GetString(Section, Key):
1. SecIt = Sections.find(string(Section))
2. if SecIt == end: return nullopt
3. KeyIt = SecIt->second.find(string(Key))
4. if KeyIt == end: return nullopt
5. return KeyIt->second
```

<a id="fn-config-getint"></a>
### FConfig::GetInt(std::string_view Section, std::string_view Key, std::int64_t Default) const

← [公开 API](API.md) · `std::int64_t`

取字符串后 `stoll`；缺失或解析失败回落 `Default`。

```text
GetInt(Section, Key, Default):
1. V = GetString(Section, Key)
2. if !V: return Default
3. try return stoll(*V) catch(...) return Default
```

<a id="fn-config-getfloat"></a>
### FConfig::GetFloat(std::string_view Section, std::string_view Key, double Default) const

← [公开 API](API.md) · `double`

取字符串后 `stod`；缺失或解析失败回落 `Default`。

```text
GetFloat(Section, Key, Default):
1. V = GetString(Section, Key)
2. if !V: return Default
3. try return stod(*V) catch(...) return Default
```

<a id="fn-config-getbool"></a>
### FConfig::GetBool(std::string_view Section, std::string_view Key, bool Default) const

← [公开 API](API.md) · `bool`

值小写后匹配真值集合；缺失返回 `Default`。

```text
GetBool(Section, Key, Default):
1. V = GetString(Section, Key)
2. if !V: return Default
3. Lower = tolower(*V)
4. return Lower in {"true","1","yes","on"}
```

<a id="fn-config-setstring"></a>
### FConfig::SetString(std::string_view Section, std::string_view Key, std::string Value)

← [公开 API](API.md) · `void`

运行时写入 / 覆盖一个键（隐式创建段）。

```text
SetString(Section, Key, Value):
1. Sections[string(Section)][string(Key)] = move(Value)
```

<a id="fn-config-hassection"></a>
### FConfig::HasSection(std::string_view Section) const

← [公开 API](API.md) · `bool`

段是否存在。

```text
HasSection(Section):
1. return Sections.find(string(Section)) != Sections.end()
```

<a id="fn-config-haskey"></a>
### FConfig::HasKey(std::string_view Section, std::string_view Key) const

← [公开 API](API.md) · `bool`

段内键是否存在——直接委托 `GetString`。

```text
HasKey(Section, Key):
1. return GetString(Section, Key).has_value()
```

<a id="fn-config-createlayer"></a>
### CreateLayer()（extern "C"）

DLL 动态安装入口——宿主按符号名查找。委托静态 `FConfig::CreateLayer()`（`MAHO_DECLARE_LAYER` 生成）。

```text
CreateLayer():
1. return Maho::Config::FConfig::CreateLayer()
```

- [Config.md](Config.md) — 概念 · [公开 API](API.md) — 签名入口
