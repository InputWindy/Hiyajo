# Core — API 文档

Core 模块 = 引擎的编译期 + 并发基础设施。零 app 假设、零第三方依赖：类型列表 / 委托事件 / 能力组合 / 单例标识 / 依赖图调度 / 线程池 / 常驻线程 / DLL 加载 / 致命路径。本文档按头文件分节，如实反映当前 `Source/Public/Core/` 的公开接口。

## TypeList.h

### TTypeList<TTypes...> <struct>

编译期有序类型列表（"类型数组"）。顺序即语义：`TTypeList<A, B>` ≠ `TTypeList<B, A>`。遍历顺序（串/并行）由调用方决定，不在本类型编码。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `Count` | `constexpr size_t` | 类型个数 |

### TCons / TAppend / TContains / TCatch / TUnionList_t <struct / alias>

列表代数：前插、后插、成员判断、多表拼接、保序去重并集。全部编译期求值，结果经 `::Type` / `_t` / `_v` 取用。

#### 接口

| 签名 | 说明 |
|------|------|
| `struct TCons<T, TList>::Type` | 把 T 前插到 TList 头部 |
| `struct TAppend<TList, TValue>::Type` / `TAppend_t` | 把 TValue 追加到 TList 尾部 |
| `struct TContains<TList, T>::value` / `TContains_v` | T 是否为 TList 的成员 |
| `struct TCatch<TLists...>::Type` | 按序拼接多个 TTypeList（`TCatch<TTypeList<A>, TTypeList<B, C>>::Type` → `TTypeList<A, B, C>`） |
| `using TUnionList_t<A, B>` | 两表并集：保序 + 去重（折叠追加，已存在则跳过） |

## Delegate.h

### TMulticastEvent<Signature> <class>

多播事件（bind + broadcast）。**非线程安全**——在拥有线程上 Broadcast；跨线程请走队列。Header-only、无状态、无 DLL 边界：消费方直接 `#include <Core/Delegate.h>`，插件的公共 API 可以把它作为成员类型暴露。

#### 接口

| 签名 | 说明 |
|------|------|
| `using FHandler = std::function<void(Args...)>` | 处理器别名 |
| `void Bind(FHandler)` | 注册一个处理器 |
| `void Broadcast(Args... Values) const` | 依次调用全部非空处理器 |
| `void RemoveAll()` | 清空全部处理器 |

## Interface.h

### IPlugin<TCapabilities...> <class（能力组合器）>

能力组合器——把全部能力 trait 作为**虚基类**安装，于是"契约 = 它承诺的接口列表"。派生类以虚继承合并能力，显式组合，不需要的对象不背生命周期接口。

#### 用法

```cpp
class FMyEngine : public virtual IPlugin<IMain, IInit, IShutdown> {};
```

### IPipeline<TStageTypes...> <class（阶段管线组合器）>

**有序**阶段管线组合器。参数顺序 = 层自己的节点顺序：`IPipeline<IInit, IMain, IShutdown>` 表示层的节点按 Init → Main → Shutdown 自推进。暴露 `TStages`（有序 TTypeList）供 TaskGraph 每阶段展开一个节点。**只携带阶段列表**；阶段→方法调用的 `Invoke` 协议由具体管线类实现。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `TStages` | `TTypeList<TStageTypes...>` | 有序阶段接口列表——TaskGraph 每阶段展开一个节点 |

## Singleton.h

### TSingleton<T> <class（CRTP 标识基类）>

**仅身份/标志基类**：没有 `Get()`、没有内联 Meyers、没有强制生命周期。每个派生单例自己声明 `static T& Get();` 并在自己的 `.cpp`（编进其 DLL）里定义——实例只存在于一个 DLL 的一个编译单元 → 跨 DLL 进程唯一（此处若用 inline static local 会在每个 include 站点 DLL 各复制一份）。`is_base_of_v<TSingleton<T>, T>` 仍可识别单例（查询遍历不变）。需要生命周期（IInit/IShutdown，在 Engine.h）时经 `IPlugin` 组合，而非无条件继承。

#### 约束

| 签名 | 说明 |
|------|------|
| `TSingleton() protected` | 受保护默认构造（仅派生可实例化） |
| `~TSingleton() protected` | 受保护析构 |
| 拷贝构造 / 拷贝赋值 `delete` | 单例不可拷贝 |
| `static T& Get()`（派生类声明，自己 cpp 定义） | 进程唯一实例入口——由派生类提供，不在本基类 |

## TaskGraph.h

### FTaskGraphDependency <struct>

命名依赖元组：`{ 依赖对象名, 依赖对象的 stage 接口 }`。`Stage = type_index(typeid(void))` 表示未设置。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `Name` | `std::string` | 依赖对象名 |
| `Stage` | `std::type_index` | 依赖对象的 stage 接口（void = 未设置） |

### FTaskGraphNode <struct>

基础图节点——**纯拓扑**。子类扩展它携带执行载荷。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `Name` | `std::string` | 对象身份（如 "World"） |
| `Stage` | `std::type_index` | 阶段（void = 未设置） |
| `Dependencies` | `std::vector<FTaskGraphDependency>` | 边：(name, stage) → 本节点 |

### FTaskGraph <class>

依赖图调度器。节点 = (对象, 阶段) 对，边来自每个节点的依赖元组。**一个节点在所有直接依赖完成后立即就绪并释放——无阶段屏障**（图对 stage 无感，跨阶段管线天然成立）。

生命周期：`Init`（装载拓扑）→ `Compile`（接线 + 环/缺失检测）→ `Execute`（异步拓扑派发）→ `Flush`（屏障）。执行协议经虚函数 `ExecuteNode` 委托给子类——基类节点只携带 `{Name, Stage, Dependencies}`，子类定义自己的节点（持有任意回调/上下文）并在 `ExecuteNode` 里转回派生类型。

#### 接口

| 签名 | 说明 |
|------|------|
| `explicit FTaskGraph(FThreadPool&)` | 绑定执行线程池 |
| `void Init(std::vector<FNode*> Nodes)` | 装载完整节点集（仅拓扑数据）；可重复调用 |
| `bool Compile()` | 接线边 + 检测环/缺失依赖；失败返回 false |
| `void Execute()` | 把就绪节点派发给线程池（异步——调 Flush 同步） |
| `void Flush()` | 阻塞到图排空（池 barrier） |
| `void Reset()` | 复用当前已编译图（重置每节点 pending 计数） |
| `virtual void ExecuteNode(FNode*) protected` | 执行协议钩子——子类把 FNode 转回派生节点并驱动回调；跑在池 worker 上（须线程安全） |

## ThreadPool.h

### FThreadPool <class>

固定规模线程池（常驻 worker + FIFO 任务队列）。`Submit` 入队即返（任务无序并发执行）；`Flush` 是锁步 barrier——阻塞到**本调用前已提交的所有任务全部完成**（不只是出队）。Worker 懒启动（首次 Submit），永不收缩；`NumThreads = 0` → `hardware_concurrency()`。任务必须线程安全。

#### 接口

| 签名 | 说明 |
|------|------|
| `explicit FThreadPool(uint32_t NumThreads = 0)` | 0 → `std::thread::hardware_concurrency()`（下限 1）；构造零 worker，懒启动 |
| `~FThreadPool()` | 置停止位 + join 全部 worker |
| `void Submit(std::function<void()>)` | 入队一个任务，立即返回 |
| `void Flush()` | barrier：排队一个空任务，等待 `PendingCount == 0`（真正"执行完"，非"出队"） |
| `uint32_t GetNumThreads() const` | 线程数上限 |

实现细节：`EnsureThreads`（懒增长，private）、`WorkerLoop`（worker 主循环，异常兜底到 `ReportFatal`）。

## ThreadedServer.h

### FThreadedServer <class>

专用常驻 worker：一个持久线程 + FIFO **串行**任务队列。给需要私有常开线程 + 串行命令队列的**长期角色**用（渲染线程、IO 加载线程……）——**不是**瞬时并行任务（那种用 FThreadPool）。子类可覆写 `OnInitialize` / `OnShutdown` / `GetThreadName` 做角色化配置；`Flush()` 是 FIFO barrier。

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual ~FThreadedServer()` | 析构时自动 Shutdown |
| `bool Initialize()` | 启动专用 worker；幂等；`OnInitialize` 失败返回 false |
| `void Shutdown()` | 停止 + join worker；幂等 |
| `bool IsRunning() const` | worker 是否在运行（原子读） |
| `void Submit(std::function<void()>)` | 入队一个任务（非阻塞、FIFO、串行执行） |
| `void Flush()` | barrier：阻塞到本调用前提交的所有任务执行完 |
| `virtual bool OnInitialize() protected` | 线程启动前回调；返回 false 中止启动 |
| `virtual void OnShutdown() protected` | 线程 join 后回调 |
| `virtual const char* GetThreadName() const protected` | 默认 `"ThreadedServer"` |

## Assembly.h

### FModuleDeleter <struct>

OS 模块句柄的自定义删除器——DLL 用 `FreeLibrary` / `dlclose` 释放而非 `delete`。FAssembly 独占该句柄，析构时释放。

#### 接口

| 签名 | 说明 |
|------|------|
| `void operator()(void* Handle) const noexcept` | 释放句柄（nullptr 安全；Win: FreeLibrary / 其他: dlclose） |

### FAssembly <class>

动态加载的代码单元——OS 模块句柄 + 符号查找。**纯加载原语**：不认识插件/manifest/工厂，如何解释已加载模块（探测哪些符号、含义如何）全由消费方决定。所有权：句柄唯一所有者（unique_ptr → 只移不拷）；宿主必须在所有由它构造的实例存活期间保持 FAssembly 加载——先卸载再使用虚表/析构是 use-after-free。

#### 接口

| 签名 | 说明 |
|------|------|
| `FAssembly() = default` | 空构造（未加载） |
| `explicit FAssembly(std::string_view Path)` | 构造即 Load |
| `bool Load(std::string_view Path)` | 从路径加载（Win: LoadLibraryA / 其他: dlopen RTLD_NOW）；失败返回 false（先 Unload 旧句柄） |
| `void Unload()` | 释放句柄（FreeLibrary / dlclose）；可重复调用 |
| `bool IsLoaded() const` | 当前是否持有有效句柄 |
| `void* GetProcAddress(const char* Name) const` | 原始符号查找；缺失或未加载 → nullptr |
| `template<typename TFunction> TFunction GetProcAs(const char* Name) const` | 把原始符号 reinterpret_cast 成类型化**函数指针** |

## Fatal.h

### ReportFatal / ReportError / InstallFatalHandlers <function>

统一致命路径 + 崩溃兜底（零依赖）。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[noreturn]] void ReportFatal(const char* Message)` | 致命路径：stderr + `Saved/Logs/Fatal.log`，然后 abort |
| `void ReportError(const char* Message)` | 非致命错误：stderr + `Saved/Logs/Fatal.log`，不 abort |
| `void InstallFatalHandlers()` | 安装 std::terminate 兜底（进程入口最先调用一次） |

### MAHO_CHECK / MAHO_CHECKF / MAHO_VERIFY / MAHO_ENSURE / MAHO_ENSURE_NOT_NULL <宏>

UE 风格断言宏（do-while 包裹）。

| 宏 | 语义 |
|------|------|
| `MAHO_CHECK(Expr)` | 硬不变量：假 → `ReportFatal`（崩）；Shipping 下表达式被编译掉（要保留副作用用 VERIFY） |
| `MAHO_CHECKF(Expr, Fmt, ...)` | 硬不变量 + 格式化消息（`snprintf` 进 512B 局部缓冲） |
| `MAHO_VERIFY(Expr)` | 同 CHECK，但表达式**总是求值**（副作用保留） |
| `MAHO_ENSURE(Expr)` | 软不变量：假 → `ReportError` **只报一次**（static bool），继续执行 |
| `MAHO_ENSURE_NOT_NULL(PtrExpr, Name)` | 软空保护：`MAHO_ENSURE(PtrExpr != nullptr)` 后 `for (auto* Name = PtrExpr; ...)`——非空才执行 |

## Export.h

### MAHO_API / MAHO_EXPORT / MAHO_IMPORT <宏>

DLL 导出/导入（UE 风格模块边界）。五平台：Windows + Xbox（`__declspec`），Linux + Android + iOS（visibility）。Xbox 基于 MSVC 但不定 `_WIN32`，故显式检查 `_MSC_VER`。未定义 `MAHO_BUILD_SHARED` 时全部展开为空。另关掉 MSVC 4251 告警。

### MAHO_IF_NOT_NULL <宏>

空保护语句——指针表达式**只求值一次**，非空才执行语句（绑定名是局部量，不会被重复求值）。用于经全局访问器（如 `GetLog()`）到达的可选服务：

```cpp
MAHO_IF_NOT_NULL(::Maho::GetLog(), L)
{
    L->Info("ready");
}
```

## Core.h

### Core.h <聚合头>

Core 模块聚合入口：include 它即可使用整个 Core 模块的编译期 + 并发基础设施。

#### 包含

| 头文件 | 说明 |
|--------|------|
| `<Core/TypeList.h>` | 类型列表代数 |
| `<Core/Delegate.h>` | 多播事件 |
| `<Core/Singleton.h>` | 单例标识基类 |
| `<Core/TaskGraph.h>` | 依赖图调度 |
| `<Core/Assembly.h>` | DLL 加载原语 |
| `<Core/Fatal.h>` | 致命路径兜底 |

注：`Interface.h` / `ThreadPool.h` / `ThreadedServer.h` / `Export.h` 不在聚合内，按需单独 include。

- [CoreDoc.md](CoreDoc.md) — 概念 · [实现字典](../../Private/Core/CoreAPI.md) — 算法 · [Engine API](../Engine/EngineAPI.md) — 层系统
