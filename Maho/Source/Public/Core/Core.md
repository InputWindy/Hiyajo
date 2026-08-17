# Core — 引擎基础设施

Maho 的编译期基础设施 + 并发基础设施。**零 app 假设、零三方依赖**——插件和应用形态全部建立在它之上。

## 子模块

| 子模块 | 职责 |
|--------|------|
| [TypeList.h](TypeList.h) | 编译期类型列表 `TTypeList` + 运行时遍历 `ForEach` |
| [Topology.h](Topology.h) | 依赖声明（`TDependsOn`/`TDependsPack`/`TPackConcat`）+ 编译期拓扑排序 / 环检测 / 分层 / 逆序 |
| [Delegate.h](Delegate.h) | 单播 `TDelegate` / 多播 `TMulticastDelegate` + 句柄 `FDelegateHandle` |
| [Extension.h](Extension.h) | `IExtension<TStage>`（运行时接口）+ `TExtension`（单例 CRTP）+ 并行调度器 + `FExtensions` 装配 |
| [Assembly.h](Assembly.h) | `FAssembly` 动态加载原语（句柄 + 符号探测，纯加载无解释） |
| [Fatal.h](Fatal.h) | 致命路径 `ReportFatal` + `InstallFatalHandlers`（零依赖崩溃兜底） |
| [Export.h](Export.h) | DLL 导出宏 `MAHO_API` |
| [Async/](Async/Async.md) | `IRunable`（`MainLoop` + `RequestShutdown` + `GApp`）+ `FThreadPool` + `FThreadedServer` |

## 依赖关系

```
TypeList ──→ Topology ──→ Extension ──→ Async/ThreadPool
                │
Delegate（独立）  Fatal（独立）  Assembly（独立）  Async/Runable（独立）
```

分层：

- **`TypeList`** 最底层（类型 + 遍历）；`Topology`/`Extension` 依赖它。
- **`Extension`** 依赖 `Topology`（拓扑）+ `Async`（线程池）。
- **`Assembly`** 只依赖 `Export` —— 动态加载原语，不认识插件/manifest/工厂。
- **`Fatal`** 只依赖 `Export` —— 崩溃时刻零依赖。
- **`Runable`** 定义 `ICommandLine`/`IRunable`/`GApp` —— 可运行契约 + 停机请求。

## 关键设计

### IExtension<TStage> 是统一运行时接口

```cpp
template <typename TStage>
class IExtension
{
public:
	virtual ~IExtension() = default;
	virtual bool ExecuteStage(TStage Stage) = 0;
};
```

一切可被驱动的东西（插件、toolkit 聚合、engine 聚合）都实现它。`TStage` 是模板参数——core 不知道 stage 长什么样，stage 枚举由 Toolkit/Engine 插件定义。

### FAssembly 是纯加载原语

```cpp
FAssembly A("MyGame.dll");
A.Load();                                    // LoadLibrary / dlopen
auto Create = A.GetProc<F*()>("CreateX");    // 符号探测
A.Unload();                                  // FreeLibrary / dlclose
```

怎么解释探测到的符号，是消费方（AssemblyImporter / 薄 launcher）的事。

### 依赖声明编译期解析

```cpp
struct FChildDependencies
{
	using FDependsPack = typename TPackConcat<
		typename TResolveDependsPack<FParent>::Type,     // 父的依赖
		TDependsPack<TDependsOn<Init, TTypeList<FC>>>    // 自己的
	>::Type;
};
```

## 相关文档

- [Core.h](Core.h) — 聚合头
- [Core.html](Core.html) — API 文档
- [Async/Async.md](Async/Async.md) — 并行模型
- [../Maho.md](../Maho.md) — 引擎核心
