# Script（Private）— 实现算法字典

cpp 侧每个关键函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。核心是默认 Lua 后端 `FLuaLanguage`（sol2）+ 宿主 `FScriptSystem` 的生命周期 / 注册 / 分发。

## Script.cpp

`FLuaLanguage` 是文件内匿名定义的默认后端类（sol2，`#define SOL_ALL_SAFETIES_ON 1`）。`FImpl` 持有 `sol::state`；脚本路径解析统一经内部 `ResolveScriptPath`（相对路径拼在脚本目录后，绝对路径直通）。

<a id="fn-resolvepath"></a>
### ResolveScriptPath(const std::string& ScriptsDirectory, const char* FilePath)（内部）

匿名命名空间，仅被 DoFile/LoadScriptRaw 引用。

```text
ResolveScriptPath(ScriptsDirectory, FilePath):
1. if FilePath 是绝对路径: return FilePath
2. return (ScriptsDirectory / FilePath)
```

### FLuaLanguage（默认 Lua 后端）

`FLuaLanguage : IScriptLanguage`——sol2 实现的 Lua 后端。语言状态是 `sol::state`（经 `GetState()` 以 `void*` 露出）；脚本目录默认 `"Scripts"`；`package.path` 配成 `<ScriptsDir>/?.lua; <ScriptsDir>/?/init.lua`。

<a id="fn-lua-init"></a>
### FLuaLanguage::Initialize(int Argc, char** Argv, const char* InScriptsDirectory)

← [公开 API](API.md) · `bool`

打开基础库、建脚本目录、配 `package.path`，然后跑掉排队的类型绑定器。幂等。

```text
Initialize(Argc, Argv, InScriptsDirectory):
1. if bInitialized: return true
2. ScriptsDirectory = InScriptsDirectory 非空 ? 它 : "Scripts"
3. Impl = new FImpl
4. Lua.open_libraries(base, package, coroutine, string, table, math, utf8)
5. create_directories(ScriptsDirectory)
6. Lua["package"]["path"] = ScriptsDir/?.lua; ScriptsDir/?/init.lua
7. bInitialized = true
8. for Binder in PendingTypeBinders: Binder(&Lua)   // 排队的绑定批量执行
9. PendingTypeBinders.clear()
```

<a id="fn-lua-shutdown"></a>
### FLuaLanguage::Shutdown()

← [公开 API](API.md) · `void`

清空排队绑定 + 释放 `sol::state`。不打日志（关停顺序相对 Log 不保证）。

```text
Shutdown():
1. PendingTypeBinders.clear()
2. Impl.reset(); bInitialized = false
```

<a id="fn-lua-dofile"></a>
### FLuaLanguage::DoFile(const char* FilePath)

← [公开 API](API.md) · `bool`

执行脚本文件（顶层代码）。路径解析 → 校验文件存在 → `safe_script_file`（保护调用），失败记错误返回 false。

```text
DoFile(FilePath):
1. if 未初始化或路径无效: return false
2. Resolved = ResolveScriptPath(...)
3. if 非普通文件: warn "file not found"; return false
4. Result = Lua.safe_script_file(Resolved)
5. if !Result.valid(): error(Result.what()); return false
6. return true
```

<a id="fn-lua-loadraw"></a>
### FLuaLanguage::LoadScriptRaw(const char* FilePath)

← [公开 API](API.md) · `void*`

执行文件并返回其顶层值为 `new sol::object`（供宿主 `LoadScript<>` 模板消费）；脚本无返回值 / 缺失 / 失败返回 nullptr。

```text
LoadScriptRaw(FilePath):
1. if 未初始化或路径无效: return nullptr
2. Resolved = ResolveScriptPath(...)；非普通文件 -> return nullptr
3. Result = Lua.safe_script_file(Resolved)；无效 -> return nullptr
4. Top = Result.get<sol::object>()
5. if Top.valid(): return new sol::object(Top)
6. warn "返回无值"; return nullptr
```

<a id="fn-lua-call"></a>
### FLuaLanguage::CallGlobal(const char*, float) / CallHandle(void*, const char*, float)

← [公开 API](API.md) · `bool`

全局调用：查函数存在（`HasFunction`）→ 保护调用，无参或单 float。句柄调用：把 `void*` 当 `sol::table*`，查表内字段是否为 `sol::function` → 保护调用。任一失败记错误返回 false。

```text
CallGlobal(Name[, Arg0]):
1. if 未初始化或函数不存在: return false
2. Fn = Lua[Name]（protected）
3. Result = Arg0 有无 ? Fn() : Fn(Arg0)
4. !valid -> error; return false;  else return true

CallHandle(Handle, Name[, Arg0]):
1. if 任一无效: return false
2. Table = *static_cast<sol::table*>(Handle)
3. Obj = Table[Name]; if !Obj.is<sol::function>(): return false
4. Result = Fn() / Fn(Arg0)（protected）; !valid -> error; false; else true
```

<a id="fn-lua-regbinder"></a>
### FLuaLanguage::RegisterTypeBinder(FTypeBinder Binder)

← [公开 API](API.md) · `void`

未初始化时排队；已初始化立即执行。

```text
RegisterTypeBinder(Binder):
1. if !Binder: return
2. if 未初始化: PendingTypeBinders.push_back(Binder)
3. else: Binder(&Lua)
```

### FScriptSystem（宿主）

`FScriptSystem` 全局实例指针 `GScriptSystem`；`GetScriptSystem()` 返回它。

<a id="fn-host-init"></a>
### FScriptSystem::Initialize(FEngineBase& Engine)

← [公开 API](API.md) · `void`

注册默认 Lua 后端 → 逐语言 `Initialize(0, nullptr, "Scripts")`（argc/argv 不再下传，语言改经引擎读配置）→ 成功者广播 `OnLanguageReady` → 设 `GScriptSystem`。

```text
Initialize(Engine):
1. RegisterLanguage(new FLuaLanguage())
2. for Language in Languages:
     if Language->Initialize(0, nullptr, "Scripts"):
         OnLanguageReady.Broadcast(*Language)
3. GScriptSystem = this
```

<a id="fn-host-shutdown"></a>
### FScriptSystem::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

清全局指针 → 对称关停全部后端 → 清空注册表 → 清空事件。

```text
Shutdown(Engine):
1. GScriptSystem = nullptr
2. for Language: Language->Shutdown()
3. Languages.clear(); Active = nullptr
4. OnLanguageReady.RemoveAll()
```

<a id="fn-host-reglang"></a>
### FScriptSystem::RegisterLanguage(IScriptLanguage* Language)

← [公开 API](API.md) · `void`

安装后端（宿主接管所有权）；按 `GetName()` 幂等——重名丢弃新实例。第一个注册的成为 ACTIVE。

```text
RegisterLanguage(Language):
1. if !Language: return
2. for Existing in Languages:
     if Existing->GetName() == Language->GetName(): delete Language; return   // 重名幂等
3. if Active == nullptr: Active = Language
4. Languages.emplace_back(Language)
```

<a id="fn-host-regbinder"></a>
### FScriptSystem::RegisterTypeBinder(const char* LanguageName, IScriptLanguage::FTypeBinder Binder)

← [公开 API](API.md) · `void`

按名找后端，转发给该后端的 `RegisterTypeBinder`。

```text
RegisterTypeBinder(LanguageName, Binder):
1. Lang = GetLanguage(LanguageName)
2. if Lang: Lang->RegisterTypeBinder(Binder)
```

<a id="fn-host-dispatch"></a>
### FScriptSystem::Call / Call(float) / DoFile / TryGetState

← [公开 API](API.md) · `bool` / `void*`

全部转发到 ACTIVE 后端（空则 false/nullptr）。

```text
Call(Name[, Arg0])      = Active ? Active->CallGlobal(Name[, Arg0]) : false
DoFile(FilePath)        = Active ? Active->DoFile(FilePath)          : false
TryGetState()           = Active ? Active->GetState()                : nullptr
```

### CreateLayer()（C 导出）

`extern "C" MAHO_SCRIPT_API Maho::FLayerBase* CreateLayer()`——宿主按符号名查找的层工厂，经 `FScriptSystem::CreateLayer()` 返回层实例。

- [Script.md](Script.md) — 概念 · [公开 API](API.md) — 签名入口
