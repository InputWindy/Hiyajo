# Script

## Code files

- [Script.h](Public/Script.h) — 脚本宿主 `FScriptSystem` + 语言中立后端接口 `IScriptLanguage` + `MAHO_LUA_*` 绑定宏族
- [ScriptApi.h](Public/ScriptApi.h) — DLL 导出宏（`MAHO_SCRIPT_API`）
- [Script.cpp](Private/Script.cpp) — 默认 Lua 后端 `FLuaLanguage`（sol2）+ 宿主实现 + `CreateLayer` 导出

## Concept — 多语言脚本宿主

多语言脚本宿主：`FScriptSystem` 只做语言注册 / 分发 / 生命周期；每个语言一个后端（`IScriptLanguage`），VM 细节、类型绑定、脚本路径全部留在后端内。**语言中立**——每个跨边界值都是不透明 `void*`（`GetState()` / `CallHandle(void*)` / `FTypeBinder = void(*)(void*)`），只有已知语言类型的调用方在 `include <sol/sol.hpp>` 之后转型。默认后端是 Lua（sol2）；Python / C# 后端可注册进同一宿主。

### 生命周期（层管线）

`FScriptSystem` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>` 的层。`Initialize` 注册默认 Lua 后端并对每个语言调 `Initialize(0, nullptr, "Scripts")`；成功后广播 `OnLanguageReady`（此时类型绑定队列已跑完）。`Shutdown` 对称拆除并清空事件。构造期声明依赖 FLog 的 IInit——脚本在 Initialize 里打日志，Log 必须先就绪。

### 语言中立后端

后端接口保持零语言类型：名字、初始化、脚本执行（`DoFile` / `LoadScriptRaw`）、函数调用（`CallGlobal` / `CallHandle`）、状态句柄（`GetState`）、类型绑定（`RegisterTypeBinder`）。`FTypeBinder` 以 `void(*)(void*)` 形式把语言状态不透明交给绑定回调。

### 加载与调用

```cpp
// 宿主加载脚本：顶层值返回为指定 sol 类型（调用点须 include <sol/sol.hpp>）
sol::table Script = Script::GetScriptSystem()->LoadScript<sol::table>("player.lua");

// 在句柄（如实体脚本表）上调用成员函数
Script::GetScriptSystem()->Call(Script, "on_update", dt);

// 全局调用
Script::GetScriptSystem()->Call("OnUpdate", dt);
```

### Lua 绑定宏（sol2）

绑定代码经 `MAHO_LUA_BIND_BEGIN/END` 类内生成 `static LuaBind(sol::state&)`，再由 `MAHO_LUA_BIND_REGISTER` 注册给 Lua 后端（Initialize 后批量跑）。表级 `MAHO_LUA_METHOD/FUNCTION/PROPERTY` 用于对命名表（如 `maho`）直接绑定。

```cpp
class FUnit
{
public:
    int HP = 0;
    void Attack();

    MAHO_LUA_BIND_BEGIN(FUnit)        // 类内，写一次类型名
        MAHO_LUA_FIELD(HP)            //   unit.hp
        MAHO_LUA_METHOD_FN(Attack)    //   unit:Attack()
    MAHO_LUA_BIND_END();
};

// main 初始化处注册一次
MAHO_LUA_BIND_REGISTER(FUnit);
```

## Third-party dependencies

- **sol2**（header-only，`<sol/sol.hpp>`；内含 Lua 5.4 运行时）——Lua 后端 VM + C++ 绑定

## Related docs

- [API.md](API.md) — API documentation
