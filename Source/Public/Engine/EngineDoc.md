<!-- mahogen -->
# Engine

## 代码文件

- [ModuleManager.h](ModuleManager.h) — 模块加载/引用计数（DLL 生命周期）
- [Schedulers.h](Schedulers.h) — 串行 / 并行调度器
- [ThreadPool.h](ThreadPool.h)
<!-- mahogen end -->

## 概念——模块系统与调度策略

Engine 层放**具体调度策略**和**模块加载管理器**。核心 `Core/` 只给契约（`FInstance` 根、`FModuleInstance` 约定、`IScheduler`），串/并行与模块生命周期的实现都在这里。

### 模块系统

模块 = "DLL + 工厂"的可装载代码单元。三块拼：

| 概念 | 层 | 职责 |
|------|------|------|
| `FInstance` | Core | DLL 实例根（虚析构，`delete` 走 DLL） |
| `FModuleInstance` | Core | 类型约定：`static GetModulePath()` 指向 DLL |
| `FModuleManager<TExtensions>` | Engine | `TSingleton`：加载 DLL、构造实例、引用计数卸载 |

应用用 `FModuleManager<FXxx>` 按类型加载，再驱动返回的实例：

```cpp
// FExtensions = 项目全部模块类型（编译期扫描表 + code-gen）
struct FGameEngine : Parallel::FParallelScheduler<FExtensions>
{
	std::vector<FInstance*> Instances;

	int Main(int, char**)
	{
		// 1. 加载全部实例
		ForEach<FExtensions>(*this, [&](auto Tag) {
			using T = typename decltype(Tag)::Type;
			if (FInstance* S = FModuleManager<FExtensions>::Get().template Load<T>())
				Instances.push_back(S);
		});

		// 2. Query 过滤接口 → 并行驱动
		using FTicks = decltype(Query<FExtensions>().Select<ITick>().Cast<ITick>());
		this->template Execute<FTicks>(Instances, [](ITick& T) { T.Tick(); });

		// 3. 收尾：实例先死（虚析构在 DLL）→ 模块后卸
		for (FInstance* I : Instances) delete I;
		FModuleManager<FExtensions>::Get().UnloadIdle();
		return 0;
	}
};
```

**生命周期顺序是硬约束**：实例的虚表/析构都在 DLL 代码段，先 Unload DLL 再用实例 = 用后释放。所以实例销毁必须先于模块卸载——这交给宿主（`delete` 全死在 DLL 卸载之前）。

### 调度策略

**`FParallelScheduler`** —— 并行：持有 `FThreadPool`。`Run` 投线程池；`Execute` = 外层 level 串行 + 内层线程池并行（barrier 跨层同步）。

**`FThreadPool`** —— 线程池：

- 构造 0 线程；首次 `Run` 懒启动到 `min(任务数, hardware_concurrency)`
- 15 任务 5 核 → 5+5+5 分批，对外透明
- `Run` 带 barrier（atomic + cv）；任务异常 `try/catch` 记录 `exception_ptr`、保证 barrier 释放、跑完 `rethrow`

## 相关文档

- [../Core/CoreDoc.md](../Core/CoreDoc.md) — 核心基础设施
- [EngineAPI.html](EngineAPI.html) — API 文档
