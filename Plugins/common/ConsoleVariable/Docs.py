# -*- coding: utf-8 -*-
# ConsoleVariable 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/ConsoleVariable.h —— CVar 注册表
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ConsoleVariable.h", Title="ConsoleVariable.h —— 控制台变量注册表（服务层）",
         Desc="运行时可调参数的注册表（对齐 UE 的 `IConsoleManager` / `TAutoConsoleVariable`）："
              "静态的 `TAutoConsoleVariable<T>` 全局对象在**静态初始化期**自注册，"
              "`FConsoleVariable::Get().Find(name)` 在运行期按名字查。\n"
              "**为什么自注册**：参数属于**使用它们的那个模块**，不属于注册表。"
              "让参数对象在静态初始化期自己登记，就省掉了「注册表需要枚举所有模块的参数」"
              "这条反向依赖 —— 注册表因此保持通用，加一个新参数不需要改任何中心文件。\n"
              "**为什么值一律以字符串存放、按需解析**：控制台 / 配置文件 / 命令行给出的都是文本，"
              "存原文让同一份数据既能按 int 读也能按 float 读；也避免了「类型」在运行期被首次访问锁死。\n"
              "**为什么容量是 O(1) 单例**：注册表必须进程唯一（静态对象在任何线程 / 任何 DLL 里"
              "都要落到同一张表），因此 `Get()` 声明在头、定义在本模块的 cpp（实例跨 DLL 唯一）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Singleton.h", "`TSingleton<T>` 标记基类 —— 提供 `Get()` 的声明约定（实例定义在 cpp）")
D.Row("<Maho.h>", "引擎聚合头：`IPlugin` / `IInit` / `IShutdown` / `FEngineBase`（生命周期能力）")
D.Row("<cstdint>", "枚举基点显式定宽：`ECVarFlags` 用 `std::uint32_t`（按位标志），"
                    "`ECVarType` 用 `std::uint8_t`（取值少，省空间）")
D.Row("<functional>", "`VisitAll` 的 `std::function<void(IConsoleVariable&)>` —— 用回调而不是让调用方拿容器，"
                       "因为遍历必须在锁内进行，交出容器等于交出锁")
D.Row("<map>", "注册表本身：`名字 → 变量`（有序 ⇒ 遍历天然按名字排序，编辑器下拉框稳定）")
D.Row("<memory>", "`std::unique_ptr<IConsoleVariable>` —— 注册表独占变量所有权（变量无共享语义）")
D.Row("<string> / <string_view>", "内部拥有名字与值；外部查询一律 `string_view`（零拷贝）")
D.Row("<type_traits>", "`TAutoConsoleVariable` 里 `if constexpr` 分派取值 / 转字符串的编译期判据")
D.Row("<utility>", "`std::move` —— 构造时把默认值与描述**搬**进内部，少一次拷贝")

D.Card("枚举取值")
D.Table("枚举 · 取值", "含义")
D.Row("ECVarFlags::None = 0", "无标志")
D.Row("ECVarFlags::Cheat = 1u << 0", "仅作弊模式可改（上层策略读取，注册表本身不做拦截）")
D.Row("ECVarFlags::ReadOnly = 1u << 1", "运行期不可改：`Set` 直接忽略（**唯一的强制项**）")
D.Row("ECVarType::Int / Float / Bool / String", "值类型标签：决定 `Register` 时如何解释默认值文本")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("[[nodiscard]] constexpr bool HasFlag(ECVarFlags Flags, ECVarFlags Test)",
      "按位测试标志。**`constexpr` 的意义**：调用点可以拿它做编译期分支（例如只在 Debug 构建里"
      "声明作弊开关），而不是把判断留到运行期再进一次函数调用")

D.Card("类型 → 枚举的类型特化",
       "`TAutoConsoleVariable<T>` 需要把 C++ 类型映射成 `ECVarType` 才能注册，"
       "而 `TCVarType<T>` 就是这个映射表：主模板**只声明不定义** ⇒ 传一个没特化的类型"
       "（例如 `double`）会在编译期报「不完整类型」，而不是在运行期给出一个错标签。")
D.Table("特化", "取值")
D.Row("TCVarType<int>::Value", "`ECVarType::Int`")
D.Row("TCVarType<float>::Value", "`ECVarType::Float`")
D.Row("TCVarType<bool>::Value", "`ECVarType::Bool`")
D.Row("TCVarType<std::string>::Value", "`ECVarType::String`")

D.Enum("ECVarFlags", Base="std::uint32_t",
       Desc="变量标志位（UE `ECVF_*` 风格）。**为什么用位标志而不是多个 bool**："
            "标志集合会随需求增长，位掩码让「加一个标志」不改变任何已有签名。"
            "注意 `ReadOnly` 是本插件**唯一真正强制**的标志，`Cheat` 只作信息供上层判断。")

D.Enum("ECVarType", Base="std::uint8_t",
       Desc="变量的值类型标签。**为什么需要它**：容器里存的是文本，"
            "得有东西记下「这个键按什么类型解释」；枚举而不是字符串，是为了让非法类型无法表达。")

D.Class("IConsoleVariable",
        Desc="查表返回的接口 —— 调用方只依赖它，不依赖具体实现。\n"
             "**为什么既给 `GetString` 又给 `GetInt/GetFloat/GetBool`**："
             "每个调用点只关心一种形状，让它在接口处就转换完，调用方不必自己写解析；"
             "同时也把「解析失败怎么办」这件事收进实现里（当前是取默认值 0 / false / 空串）。")
D.SetAccess("public")
D.Interface("virtual ~IConsoleVariable() = default",
            "虚析构 —— 变量由注册表以 `unique_ptr<IConsoleVariable>` 持有并多态删除")
D.Interface("[[nodiscard]] virtual std::string_view GetName() const = 0",
            "变量名（`string_view`：名字一注册就不再变动，返回视图不必拷贝）")
D.Interface("[[nodiscard]] virtual std::string_view GetDescription() const = 0",
            "说明文本（编辑器的提示 / 自动补全用）")
D.Interface("[[nodiscard]] virtual ECVarFlags GetFlags() const = 0",
            "标志位（上层据此决定是否允许修改 / 是否显示）")
D.Interface("[[nodiscard]] virtual int GetInt() const = 0", "按整数读取（解析失败取 0）")
D.Interface("[[nodiscard]] virtual float GetFloat() const = 0", "按浮点读取（解析失败取 0）")
D.Interface("[[nodiscard]] virtual bool GetBool() const = 0",
            "按布尔读取（解析失败取 false）")
D.Interface("[[nodiscard]] virtual std::string GetString() const = 0",
            "按字符串读取 —— 返回 `std::string` 而非 `string_view`："
            "值可以随时被 `Set` 改写，视图会立刻悬空")
D.Interface("virtual void Set(std::string_view Value) = 0",
            "从字符串赋值（内部解析）；**带 `ReadOnly` 标志时静默忽略** —— "
            "「不可改」是策略而不是错误，不制造异常路径")

D.Class("FConsoleVariable", Base="TSingleton<FConsoleVariable>, IPlugin<IInit, IShutdown>",
        Desc="注册表本体。`TSingleton` 提供 `Get()` 的声明约定、`IPlugin<IInit, IShutdown>` "
             "声明它需要的生命周期 stage（**注册表在 `Initialize` 之后可用，`Shutdown` 清空**）。\n"
             "**为什么名字到接口的映射用 `map` 而不是哈希表**：变量数量在几十个量级，"
             "有序容器带来的「遍历顺序稳定」对编辑器枚举更值钱。\n"
             "**`VisitAll` 的回调契约**：访问期间持有注册表锁，回调里**不得**再回查注册表"
             "（那是同一把锁的重入）。")
D.SetAccess("public")
D.Interface("static FConsoleVariable& Get()",
            "进程唯一的访问点：**声明在这里、定义在 `Private/ConsoleVariable.cpp`** ⇒ "
            "实例属于本 DLL，所有消费方拿到的是同一个对象（放在头里 inline 会变成「每个 DLL 一份」）")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "进入可用状态（注册表在 `Initialize` 之后可查）")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "清空注册表 —— **为什么清空**：变量对象是各模块的静态数据，"
            "但表里的 `unique_ptr` 必须在本模块卸载前释放，否则销毁顺序反转会 double free")
D.Interface("[[nodiscard]] IConsoleVariable* Find(std::string_view Name)",
            "按名字查；不存在返回 `nullptr`（**不隐式创建**：拼错名字应该立刻可见，"
            "而不是悄悄多出一个空变量）")
D.Interface("void VisitAll(const std::function<void(IConsoleVariable&)>& Visitor) const",
            "按名字顺序遍历全部变量（编辑器自动补全用它枚举已知名字）。"
            "回调在**持有注册表锁**的情况下运行 ⇒ 回调内不得再进注册表")
D.Interface("IConsoleVariable* Register(std::string_view Name, ECVarType Type, std::string DefaultValue, std::string_view Description, ECVarFlags Flags)",
            "登记一个变量并返回其接口（`TAutoConsoleVariable` 的构造流程调用它）。"
            "参数多为按值 —— 注册表要把名字 / 默认值 / 描述都**存下来**，按值传让调用方选择搬移")
D.SetAccess("protected")
D.Interface("friend TSingleton<FConsoleVariable>", "只许 `TSingleton` 的 `Get()` 构造实例")
D.Interface("FConsoleVariable() = default",
            "protected 构造：实例只能经 `Get()` 取得（受控的进程唯一性）")
D.Field("std::map<std::string, std::unique_ptr<IConsoleVariable>> Registry",
        "名字 → 变量。**有序 + 独占所有权**：遍历稳定，且表的销毁顺序与变量自身的生命周期一致")

D.Struct("TCVarType<T>",
         Desc="C++ 类型 → `ECVarType` 的映射表：主模板**只声明**，四个特化各给出一个 `Value`。\n"
              "**为什么故意不给主模板下定义**：用未支持的类型实例化 `TAutoConsoleVariable<T>` 时"
              "应当**编译失败**（不完整类型的报错就在出错那一行），"
              "而不是在运行期给出一个错误的值类型标签，让问题推迟到读值时才暴露。")
D.SetAccess("public")
D.Field("static constexpr ECVarType Value",
        "各特化提供的类型标签（int → Int、float → Float、bool → Bool、std::string → String）")

D.Class("TAutoConsoleVariable<T>",
        Desc="静态控制台变量 —— 与 UE 的 `TAutoConsoleVariable` 同名同用法："
             "在**静态初始化期**构造即完成注册，之后直接 `GetValue()` / `Set()`：\n"
             "`static TAutoConsoleVariable<int> CVarMaxFPS(\"r.MaxFPS\", 60, \"Max FPS\");`\n"
             "`const int MaxFPS = CVarMaxFPS.GetValue();`\n"
             "**为什么持有接口指针而不是自己存值**：值的唯一副本在注册表里，"
             "`Find(name)` 与 `GetValue()` 因此永远读同一份数据 —— "
             "否则「按名字改」与「按对象改」会变成两个不同步的真值来源。\n"
             "**为什么不支持拷贝**（未声明拷贝语义）：变量是静态单例式对象，"
             "复制一个「名字 + 句柄」对没有意义。")
D.SetAccess("public")
D.Interface("TAutoConsoleVariable(std::string_view InName, T Default, std::string_view Description, ECVarFlags Flags = ECVarFlags::None)",
            "构造即注册：走 `FConsoleVariable::Get().Register(...)`，"
            "`TCVarType<T>::Value` 提供类型标签、`ToString(Default)` 把默认值转成文本。"
            "**注意静态初始化顺序**：注册表由 `Get()` 保证存在，因此这里不依赖别的全局对象的构造顺序")
D.Interface("[[nodiscard]] T GetValue() const",
            "按 `T` 取值：`if constexpr` 在编译期选出 `GetInt/GetFloat/GetBool/GetString` 中的一个 —— "
            "没有运行期类型分派，也没有装箱")
D.Interface("void Set(T Value)",
            "按 `T` 赋值：先 `ToString` 再走接口 `Set`（于是 `ReadOnly` 的忽略行为对两种改法一致）")
D.Interface("[[nodiscard]] std::string_view GetName() const",
            "本变量自己的名字（连表都不用查）")
D.SetAccess("private")
D.Interface("[[nodiscard]] static std::string ToString(const T& Value)",
            "把值规范成存进注册表的文本：`std::string` 原样、`bool` 走 \"true\"/\"false\""
            "（**不是 1/0**，让人手写的配置文件也读得懂）、其余走 `std::to_string`")
D.Field("std::string Name",
        "自己的名字副本（构造时用，避免每次去表里反查）")
D.Field("IConsoleVariable* Handle = nullptr",
        "注册时拿到的接口指针 —— **裸指针而非智能指针**：所有权在注册表，"
        "这里只是「用」，持有 `unique_ptr` 会造成二次释放")
