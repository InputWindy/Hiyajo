# Script — API 文档

Script 插件 = 多语言脚本宿主（`FScriptSystem`）+ 语言中立后端接口（`IScriptLanguage`）+ 默认 Lua 后端（sol2）。宿主只做语言注册 / 分发 / 生命周期；VM 细节、类型绑定、脚本路径全部留在后端内。跨边界的值一律不透明 `void*`（调用方在已知语言类型的语境里转型）。本文档如实反映当前 `Public/` 的公开接口。

## Script.h

### GetScriptSystem <函数>

全局脚本系统访问器——跨 DLL 经函数返回 `FScriptSystem*`（非内联 static，保证进程唯一）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_SCRIPT_API FScriptSystem* GetScriptSystem()` | 返回脚本宿主实例（未初始化时为 nullptr） |

### IScriptLanguage <class（语言中立后端接口）>

每个语言一个实现（Lua / Python / C#）。宿主只经此接口驱动语言；VM 细节、类型绑定、每语言状态全部留在后端内。**语言中立**：每个跨边界值都是不透明 `void*`（调用方在已知语言类型的语境里转型）。脚本文件相对后端的脚本目录解析。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] virtual const char* GetName() const` | 语言名（"Lua"/"Python"/"CSharp"）——注册 / 选择 / 日志用 |
| `virtual bool Initialize(int Argc, char** Argv, const char* ScriptsDirectory)` | 初始化 VM；脚本文件相对 ScriptsDirectory 解析 |
| `virtual void Shutdown()` | 关闭后端 |
| `[[nodiscard]] virtual bool IsInitialized() const` | 是否已初始化 |
| `virtual bool DoFile(const char* FilePath)` | 执行脚本文件（顶层代码） |
| `virtual void* LoadScriptRaw(const char* FilePath)` | 执行文件并返回其顶层值为 `new TSolObject`（供宿主的 `LoadScript<>` 模板消费）；失败返回 nullptr |
| `virtual bool CallGlobal(const char* FunctionName)` | 调用全局函数；缺失返回 false（不报错） |
| `virtual bool CallGlobal(const char* FunctionName, float Arg0)` | 调用全局函数（一个 float 参数） |
| `virtual bool CallHandle(void* Handle, const char* FunctionName)` | 在不透明句柄上调用函数（如实体脚本表） |
| `virtual bool CallHandle(void* Handle, const char* FunctionName, float Arg0)` | 同上（一个 float 参数） |
| `[[nodiscard]] virtual void* GetState()` | 不透明语言状态指针（`sol::state*` / `PyObject*` / ...） |
| `using FTypeBinder = void (*)(void* LanguageState)` | 类型绑定器别名——语言状态不透明传入 |
| `virtual void RegisterTypeBinder(FTypeBinder Binder)` | 注册类型级绑定器；Initialize 前排队，已初始化立即执行 |

### FScriptSystem <class（脚本宿主）>

脚本宿主：管理多个语言后端（Lua/Python/C#...）。`Initialize` 拉起每个已注册语言，`Shutdown` 对称拆除。便捷模板（`LoadScript` / `Call`）转发到 **ACTIVE** 语言（第一个注册的）。

```cpp
Script::GetScriptSystem()->Initialize(0, nullptr);  // 启动全部后端
Script::GetScriptSystem()->DoFile("main.lua");      // 宿主加载脚本
Script::GetScriptSystem()->Call("OnUpdate", dt);    // 宿主逐帧驱动
```

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FScriptSystem, "Script.dll")` | 层声明（`CreateLayer` 导出，宿主按符号名查找动态安装） |
| `FScriptSystem()` | 构造（构造期声明依赖 FLog 的 IInit——日志必须先初始化） |
| `using FOnLanguageReady = TMulticastEvent<void(IScriptLanguage&)>` | 每个语言 `Initialize` 成功后广播（绑定队列已跑完） |
| `void RegisterLanguage(IScriptLanguage* Language)` | 安装语言后端（宿主接管所有权）；按 `GetName()` 幂等——重名丢弃新实例 |
| `[[nodiscard]] IScriptLanguage* GetLanguage(const char* Name) const` | 按名查找后端；未注册返回 nullptr |
| `[[nodiscard]] IScriptLanguage* GetActive() const` | ACTIVE 后端（第一个注册的）；无则 nullptr |
| `void RegisterTypeBinder(const char* LanguageName, IScriptLanguage::FTypeBinder Binder)` | 在命名后端上注册类型绑定器（`MAHO_*_BIND_REGISTER` 调用） |
| `FOnLanguageReady& GetOnLanguageReady()` | 语言就绪事件访问器 |
| `template<typename TSolObject> [[nodiscard]] TSolObject LoadScript(const char* FilePath)` | 加载脚本文件并返回其顶层值（通常为表）。调用方给出 sol 类型——调用点须 include `<sol/sol.hpp>`；文件缺失或 active 语言无返回值时返回空类型值 |
| `template<typename TSolHandle> bool Call(TSolHandle& Handle, const char* FunctionName)` | 在任意句柄上调用函数（实体脚本实例 / 命名空间表 / ...）——`Call` 的句柄版本，解析到给定句柄而非全局作用域 |
| `template<typename TSolHandle> bool Call(TSolHandle& Handle, const char* FunctionName, float Arg0)` | 同上（一个 float 参数） |
| `[[nodiscard]] bool Call(const char* FunctionName)` | 在 active 语言上调用全局函数 |
| `[[nodiscard]] bool Call(const char* FunctionName, float Arg0)` | 同上（一个 float 参数） |
| `[[nodiscard]] bool DoFile(const char* FilePath)` | 在 active 语言上执行脚本文件 |
| `[[nodiscard]] void* TryGetState()` | active 语言的不透明状态（`sol::state*` / `PyObject*` / ...） |

#### 约束（层管线阶段）

`FScriptSystem : FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`——仅 `Initialize` / `Shutdown` 实现（scheduler-only），其余阶段为空。

### MAHO_LUA_* <宏族>

Lua 绑定糖（仅 Lua 后端；sol2 绑定代码用，须可见 `<sol/sol.hpp>`）。

#### 表级绑定

| 宏 | 语义 |
|------|------|
| `MAHO_LUA_METHOD(Table, Class, Method)` | 在 Lua 表上注册 C++ 成员函数（sol2 `set_function`，snake_case 自动） |
| `MAHO_LUA_FUNCTION(Table, Name, Fn)` | 在 Lua 表上注册 C++ 自由函数 / lambda |
| `MAHO_LUA_PROPERTY(Table, Name, Getter, ...)` | 注册属性（getter + setter；省略 setter 为只读） |

#### 类内绑定（Begin/End 包裹，展开为 `static void LuaBind(sol::state&)`）

```cpp
class FUnit
{
public:
    int HP = 0;
    void Attack();

    MAHO_LUA_BIND_BEGIN(FUnit)        // 类内写一次类型名
        MAHO_LUA_FIELD(HP)            //   unit.hp
        MAHO_LUA_METHOD_FN(Attack)    //   unit:Attack()
    MAHO_LUA_BIND_END();
};
```

| 宏 | 语义 |
|------|------|
| `MAHO_LUA_BIND_BEGIN(Type)` | 类内开始：建 usertype + 暴露绑定上下文（`_Lua` / `_Meta` 对内层宏可见） |
| `MAHO_LUA_FIELD(Type, Field)` | 直接成员字段——点访问 `unit.field`（读写） |
| `MAHO_LUA_PROPERTY_MEMBER(Type, Name, Getter, Setter)` | getter/setter 属性——点访问 `unit.name`（读写） |
| `MAHO_LUA_METHOD_FN(Type, Method)` | 成员函数——点访问 `unit:Method()` |
| `MAHO_LUA_BIND_END()` | 类内结束：关闭绑定块 |
| `MAHO_LUA_BIND_REGISTER(Type)` | 把类生成的 `static LuaBind(sol::state&)` 注册给 Lua 后端；Lua Initialize 后批量执行（或已初始化立即执行）。调用一次即可（如 main 的 Initialize） |

## ScriptApi.h

### MAHO_SCRIPT_API <宏>

DLL 导出/导入宏（模块边界）。构建 Script.dll 时 `MAHO_EXPORT`，消费方 `MAHO_IMPORT`。

- [Script.md](Script.md) — 概念 · [实现字典](ImplAPI.md) — 算法
