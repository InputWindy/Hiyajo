#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Script 插件的文档内容。

运行器（Tools/plugin_docs.py）把 docs_builder 作为变量 D 注入本文件 —— 只声明，不 import。
"""

# ══════════════════════════════════════════════════════════════════════════════
# Public/ScriptApi.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ScriptApi.h", Title="ScriptApi.h —— 插件导出标记",
         Desc="只有导出标记 `MAHO_SCRIPT_API`。当前只有 `GetScriptSystem()` 这一个符号用到它 —— "
              "但那一个就足够重要：全局访问器必须**跨 DLL 唯一**，"
              "所以它按「头里声明、cpp 里定义」导出，而不是 header-inline（那只会在每个 DLL 里各留一份）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 原语；引擎 core 是插件唯一的下层依赖")

D.Macro("MAHO_SCRIPT_API", "MAHO_EXPORT / MAHO_IMPORT",
        "构建 Script.dll 时（`MAHO_SCRIPT_MODULE_EXPORTS`）取导出，其余消费者取导入")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Script.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Script.h", Title="Script.h —— 脚本宿主 + 语言后端接口",
         Desc="分两层：**接口** `IScriptLanguage`（每种语言一个实现）与**宿主** `FScriptSystem`"
              "（管多个后端、把调用转给当前活动的那一个）。宿主**只通过接口**驱动语言，"
              "所以 VM 细节、类型绑定、每语言的状态全部关在后端内部 —— 加一门语言不需要动宿主。\n"
              "语言中立是靠**不透明 `void*`** 实现的：所有跨界值都是裸指针，"
              "调用方在「已知语言类型」的上下文里自己转回去（例如 Lua 侧 `sol::state*`）。"
              "代价是这里没有类型安全，收益是引擎 core 不必认识 sol2 / CPython / .NET，"
              "也解释了为什么 `LoadScript<>` / `Call<>` 是**模板**：类型转换发生在调用点，"
              "而调用点是唯一能包含 `<sol/sol.hpp>` 的地方。\n"
              "另外两条边界约定：脚本文件相对**后端的 scripts 目录**解析（不由引擎拼路径），"
              "而每帧调用（`Call`）在缺函数时**返回 false 而不是报错** —— "
              "可选回调是脚本的常态，把它当异常会逼每个脚本都写空函数。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Delegate.h", "`TMulticastEvent`：语言就绪广播")
D.Row("Core/Interface.h", "`IPipeline` —— `FScriptSystem` 的阶段序列靠它")
D.Row("<Maho.h>", "`FFrameExtension` + `MAHO_DECLARE_FRAME`（帧身份）")
D.Row("Engine/Engine.h", "stage 接口与 `FEngineBase`")
D.Row("ScriptApi.h", "本插件的导出标记（`MAHO_SCRIPT_API`）")
D.Row("<memory> / <string> / <vector>", "后端所有权（`unique_ptr`）与集合容器。"
                                        "**刻意不包含 sol2 / 任何具体语言的头** —— 这正是接口能保持中立的原因")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_SCRIPT_API FScriptSystem* GetScriptSystem()",
      "全局脚本系统访问器。**跨 DLL 走函数**：头里只有声明，定义在 `Private/Script.cpp` —— "
      "否则每个包含本头的 DLL 都会自带一份，出现「两个脚本系统」。返回指针而非引用，"
      "是为了「尚未就绪」也能合法表达")

D.Class("IScriptLanguage", Desc="语言后端接口：一门语言一个实现（Lua / Python / C#）。"
                                "**全部成员都是纯虚**，宿主因此可以完全中立地驱动它："
                                "不认识的只有实现，不认识的没有接口。\n"
                                "两种调用粒度刻意分开：`CallGlobal` 打全局作用域，"
                                "`CallHandle` 打在任意不透明句柄上（实体脚本表、命名空间表…）—— "
                                "游戏对象要挂自己的脚本实例，只打全局作用域是不够的。")
D.SetAccess("public")
D.Interface("virtual ~IScriptLanguage() = default",
            "虚析构：宿主用 `unique_ptr<IScriptLanguage>` 持有后端（注册即接管所有权）")
D.Interface("[[nodiscard]] virtual const char* GetName() const = 0",
            "语言名（`\"Lua\"` / `\"Python\"` / `\"CSharp\"`）。用于注册、按名查找与日志 —— "
            "**返回裸 `const char*` 而不是 `std::string`**：跨 DLL 边界返回 STL 字符串会带上分配器"
            "与析构归属的麻烦，而语言名是静态字面量，本来就不需要所有权")
D.Interface("virtual bool Initialize(int Argc, char** Argv, const char* ScriptsDirectory) = 0",
            "初始化 VM：`Argc`/`Argv` 透传给脚本运行时（脚本可以看命令行），"
            "`ScriptsDirectory` 是脚本文件的解析基准。成功与否由返回值给出，不抛异常")
D.Interface("virtual void Shutdown() = 0", "拆掉 VM（与 `Initialize` 对称）")
D.Interface("[[nodiscard]] virtual bool IsInitialized() const = 0",
            "是否已初始化。宿主据此决定「类型绑定排队等初始化」还是「立刻执行」")
D.Interface("virtual bool DoFile(const char* FilePath) = 0",
            "执行一个脚本文件（顶层代码）。路径相对后端的 scripts 目录")
D.Interface("virtual void* LoadScriptRaw(const char* FilePath) = 0",
            "执行文件并把**顶层返回值**以 `new TSolObject`（堆分配、后端语言类型的对象）交出去；"
            "失败返回 nullptr。所有权交给调用方（`LoadScript<>` 拷走内容后 delete）—— "
            "用裸指针是因为「语言类型」在接口层不可知，只能约定「新分配的对象」")
D.Interface("virtual bool CallGlobal(const char* FunctionName) = 0",
            "调全局函数（无参）。函数不存在返回 false，**且不报错**（可选回调是常态）")
D.Interface("virtual bool CallGlobal(const char* FunctionName, float Arg0) = 0",
            "调全局函数（一个 float 参数）—— 最常见的逐帧回调形态（`OnUpdate(dt)`），"
            "所以为它准备了不带模板的重载")
D.Interface("virtual bool CallHandle(void* Handle, const char* FunctionName) = 0",
            "在不透明句柄上（如实体脚本表）调函数（无参）")
D.Interface("virtual bool CallHandle(void* Handle, const char* FunctionName, float Arg0) = 0",
            "在不透明句柄上调函数（一个 float 参数）")
D.Interface("[[nodiscard]] virtual void* GetState() = 0",
            "语言状态的不透明指针（`sol::state*` / `PyObject*` / …）。"
            "**它是绑定代码的入口**：有了它，绑定侧可以包含语言头并在自己的上下文里转回真实类型")
D.Interface("using FTypeBinder = void (*)(void* LanguageState)",
            "类型绑定函数指针。**用裸函数指针而不是 `std::function`**：绑定注册发生在静态初始化期"
            "（宏展开处），此时不该依赖任何堆分配或跨模块的 std::function 布局")
D.Interface("virtual void RegisterTypeBinder(FTypeBinder Binder) = 0",
            "注册一个类型级绑定器：未初始化时入队（等 `Initialize` 后批量跑），已初始化则立即执行 —— "
            "这让「宏写在任何文件里」都能正确工作，而不依赖静态初始化顺序")

D.Class("FScriptSystem", Base="FFrameExtension + IPipeline<IPreInit, IInit, IPostInit, "
                              "IPreShutdown, IShutdown, IPostShutdown>",
        Desc="脚本宿主：管多个语言后端，并把便利接口转发给**活动语言**（第一个注册的）。"
             "**它只挂初始化/关闭这几个阶段，没有 Tick** —— 因为脚本不会被引擎自动调用："
             "是谁想调（游戏逻辑、编辑器面板）谁就调 `Call` / `DoFile`，"
             "这使脚本的调用时机由调用方掌握，而不是被帧循环强加一遍。\n"
             "后端所有权在宿主：`RegisterLanguage` 即接管 `unique_ptr`，"
             "`Shutdown` 时按相反顺序逐个拆 —— 语言之间的依赖（如 Python 里嵌 Lua 表）"
             "于是有一个确定的拆除次序，而不是随静态析构乱序。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FScriptSystem)",
            "帧身份与工厂符号：`FFrameBuilder` 按 DLL 路径装载它的凭据")
D.Interface("FScriptSystem()",
            "构造：空的语言表。后端是靠 `RegisterLanguage` 逐个装进来的 —— "
            "宿主本身不认识任何具体语言")
D.Interface("using FOnLanguageReady = TMulticastEvent<void(IScriptLanguage&)>",
            "「某语言已就绪」事件类型。载体会拿到后端引用（不是指针）—— "
            "语言在后端表里不会中途消失，直到 `Shutdown`")
D.Interface("void RegisterLanguage(IScriptLanguage* Language)",
            "装入一个语言后端（**宿主接管所有权**）；按名字幂等 —— "
            "重复注册同名语言不会出现两个活动后端")
D.Interface("[[nodiscard]] IScriptLanguage* GetLanguage(const char* Name) const",
            "按名字查后端；未注册返回 nullptr")
D.Interface("[[nodiscard]] IScriptLanguage* GetActive() const",
            "当前活动后端（第一个注册的）；一个都没有时 nullptr。"
            "「第一个注册的」是刻意的简单规则：多语言共存时语义可预测，"
            "切换活动语言不必引入一套优先级配置")
D.Interface("void RegisterTypeBinder(const char* LanguageName, IScriptLanguage::FTypeBinder Binder)",
            "把绑定器注册到**指定名字**的后端上（`MAHO_*_BIND_REGISTER` 展开到这里）。"
            "按名字而不是按指针，所以宏在静态初始化期运行也安全")
D.Interface("[[nodiscard]] FOnLanguageReady& GetOnLanguageReady()",
            "取就绪事件以挂订阅者（返回引用以便 `+=` 绑定）")
D.Interface("template <typename TSolObject> [[nodiscard]] TSolObject LoadScript(const char* FilePath)",
            "加载脚本文件并返回顶层值（通常是表）。**调用方给出语言类型** —— "
            "调用点必须包含 `<sol/sol.hpp>`；类型转换因此发生在唯一知道类型的那个模块里。"
            "活动语言为空或后端没返回值时给出空构造值。"
            "实现按值返回：`LoadScriptRaw` 交出的堆对象被拷走、随即 delete —— "
            "语言对象跨 DLL 传递时不留下跨模块的 delete")
D.Interface("template <typename TSolHandle> bool Call(TSolHandle& Handle, const char* FunctionName)",
            "在任意句柄（实体脚本实例、命名空间表…）上调用函数：转给活动语言的 `CallHandle`。"
            "句柄类型由调用方给出，理由与 `LoadScript` 相同")
D.Interface("template <typename TSolHandle> bool Call(TSolHandle& Handle, const char* FunctionName, "
            "float Arg0)",
            "同上，带一个 float 参数（逐帧回调的常用形态）")
D.Interface("[[nodiscard]] bool Call(const char* FunctionName)",
            "在活动语言上调用全局函数（无参）。**非模板重载** —— 这条路径不需要语言类型，"
            "所以不包含 sol2 的代码也能用它")
D.Interface("[[nodiscard]] bool Call(const char* FunctionName, float Arg0)",
            "在活动语言上调用全局函数（一个 float 参数）")
D.Interface("[[nodiscard]] bool DoFile(const char* FilePath)",
            "在活动语言上执行脚本文件")
D.Interface("[[nodiscard]] void* TryGetState()",
            "活动语言的不透明状态（`sol::state*` / `PyObject*` / …）；用于绑定代码。"
            "名字里的 `Try` 是在提示「可能没有活动语言」")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "阶段入口：把每个已注册后端带起来（含把排队的类型绑定器跑完）")
D.Interface("void PostInitialize(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void PreShutdown(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "阶段入口：逐个 `Shutdown` 后端并释放（与初始化对称）。"
            "顺序在这里由宿主单点决定，避免「谁先析构」变成静态初始化顺序问题")
D.Interface("void PostShutdown(FEngineBase&) override {}", "阶段入口（空实现）")
D.Field("std::vector<std::unique_ptr<IScriptLanguage>> Languages",
        "后端表，**宿主拥有**（注册即接管）。`vector` 保持注册顺序 —— "
        "「第一个即活动语言」这条规则靠的就是它")
D.Field("IScriptLanguage* Active = nullptr", "活动语言（第一个注册的）的裸观察指针；所有权在 `Languages` 里")
D.Field("FOnLanguageReady OnLanguageReady", "语言就绪广播（`GetOnLanguageReady` 返回它的引用）")

# ── Lua 绑定宏 ────────────────────────────────────────────────────────────────

D.Macro("MAHO_LUA_METHOD", "(Table).set_function(#Method, &Class::Method)",
        "把 C++ 成员函数绑到 Lua 表上（sol2 的 `set_function`，自动转 snake_case）。"
        "写成员函数名一次即可，名字由 `#Method` 串化 —— 不会出现「改名后 Lua 侧名字没跟着改」")

D.Macro("MAHO_LUA_FUNCTION", "(Table).set_function(Name, Fn)",
        "绑一个自由函数 / lambda 到 Lua 表上（名字由调用方显式给出）")

D.Macro("MAHO_LUA_PROPERTY", "(Table).set_property(Name, Getter, __VA_ARGS__)",
        "绑 getter/setter 属性；**省略 setter 即只读** —— 省略是语法层面的，"
        "不需要「只读标志」这种运行期约定")

D.Macro("MAHO_LUA_BIND_BEGIN", "static void LuaBind(sol::state& _Lua) { sol::usertype<Type> _Meta = "
                               "_Lua.new_usertype<Type>(#Type); (void)_Meta; {",
        "类内绑定的开始：建 usertype 并把 `_Lua` / `_Meta` 放进作用域，供下面几条宏使用。"
        "**展开成一个 static 成员函数**，因此名字只写一次、绑定代码就贴在类体里，"
        "不会和成员声明分家")

D.Macro("MAHO_LUA_FIELD", "_Meta[#Field] = &Type::Field;",
        "直接暴露成员字段：Lua 侧 `unit.field` 读写。仅适用于公开字段")

D.Macro("MAHO_LUA_PROPERTY_MEMBER", "_Meta[Name] = sol::property(&Type::Getter, &Type::Setter);",
        "暴露 getter/setter 对：Lua 侧 `unit.name` 读写。这是包住私有字段的正确方式 —— "
        "字段本身不出现在绑定里")

D.Macro("MAHO_LUA_METHOD_FN", "_Meta[#Method] = &Type::Method;",
        "暴露成员函数：Lua 侧 `unit:Method()`。**与字段访问的区别就在这个冒号**（`_Meta[#Method]` 是函数槽）")

D.Macro("MAHO_LUA_BIND_END", "} }",
        "类内绑定的结束：关掉内层作用域与函数体")

D.Macro("MAHO_LUA_BIND_REGISTER", "Maho::Script::GetScriptSystem()->RegisterTypeBinder(\"Lua\", "
                                  "[](void* _LuaState) { Type::LuaBind(*static_cast<sol::state*>(_LuaState)); })",
        "把类的 `LuaBind` 注册到 Lua 后端（在任意一处调用一次，例如 main 的 Initialize）。"
        "**两种时机都覆盖**：Lua 已初始化则立刻执行，否则入队等初始化后批量跑 —— "
        "所以注册点在静态初始化期也不会失效")
