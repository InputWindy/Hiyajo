#pragma once

#include <Core/Assembly.h>
#include <Core/Interface.h>
#include <Engine/Layer.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#define MAHO_DECLARE_ENGINE(EngineType, DLL)            \
public:                                                 \
	static Maho::FEngineBase* CreateEngine()            \
	{                                                   \
		return new EngineType();                        \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return DLL;                                     \
	}                                                   \

#define MAHO_DECLARE_FEATURE(FeatureType, DLL)          \
public:                                                 \
	static Maho::FEngineLayer* CreateLayer()            \
	{                                                   \
		return new FeatureType();                       \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return DLL;                                     \
	}                                                   \

namespace Maho
{

// ── 引擎主循环三阶段 ──────────────────────────────────────────────────────

/** Begin-frame capability. */
class MAHO_API IBeginFrame
{
public:
	virtual ~IBeginFrame() = default;
	virtual void BeginFrame() = 0;
};

/** Tick capability. */
class MAHO_API ITick
{
public:
	virtual ~ITick() = default;
	virtual void Tick() = 0;
};

/** End-frame capability. */
class MAHO_API IEndFrame
{
public:
	virtual ~IEndFrame() = default;
	virtual void EndFrame() = 0;
};

class FEngineBase;

/**
 * 引擎主循环固定管线 —— BeginFrame → Tick → EndFrame 三个无参 stage。
 * 既是 FLayerBase（可被 TaskGraph 调度，GetName 由具体 feature 提供），
 * 又是 IPipeline（stage 列表），并实现 Invoke 协议（stage → 方法调用）。
 */
class MAHO_API IEnginePipeline: public IPipeline<IBeginFrame, ITick, IEndFrame>
{
public:
	/** stage → 方法调用的 if-constexpr 分派（TContext = FEngineBase）。 */
	template <typename TStage, typename TContext>
	void Invoke(TContext& Engine)
	{
		if constexpr (std::is_same_v<TStage, IBeginFrame>)
		{
			static_cast<IBeginFrame&>(*this).BeginFrame();
		}
		else if constexpr (std::is_same_v<TStage, ITick>)
		{
			static_cast<ITick&>(*this).Tick();
		}
		else if constexpr (std::is_same_v<TStage, IEndFrame>)
		{
			static_cast<IEndFrame&>(*this).EndFrame();
		}
		else
		{
			static_assert(sizeof(TStage) == 0, "Unhandled stage — add a branch in IEnginePipeline::Invoke");
		}
	}
};

class FEngineBase;

class MAHO_API FEngineLayer : public FLayer<IEnginePipeline>
{
public:
	/** 宿主引擎（Install 时自动注入）。feature 在 stage 方法里经它调度安装/卸载/退出。 */
	FEngineBase* Owner = nullptr;
};

/**
 * 引擎锚点 —— 生命周期能力（IInit/IMain/IShutdown）+ 主循环。
 * 主循环驱动一张引擎管线图：每帧 Flush → 应用挂起管线变更 → Init → Compile
 * → Execute（BeginFrame/Tick/EndFrame 三阶段按依赖图并行调度）。
 */
class MAHO_API FEngineBase : public IPlugin<IInit, IMain, IExit, IShutdown>
{
public:
	void Initialize(int Argc, char** Argv) override { (void)Argc; (void)Argv; }
	void Shutdown() override
	{
		// 先删 feature 实例（虚析构在各自 DLL），再释放 DLL。
		Features.clear();
		Modules.clear();
	}

	int Main() final override
	{
		FThreadPool Pool;
		FLayerTaskGraph<IEnginePipeline, FEngineBase> EngineGraph(Pool, *this);

		while (true)
		{
			EngineGraph.Flush();
			FlushPendingUpdatePipelines();

			EngineGraph.Init(Pipelines);
			if (!EngineGraph.Compile())
			{
				// 依赖缺失/环 —— 跳过本帧执行，避免驱动坏图。
				continue;
			}
			EngineGraph.Execute();
			EngineGraph.Flush();

			// 输入层（如 GameInputLayer）在 Tick 里调用 RequestExit 后，
			// 本帧 Execute 已结束，此处安全检查退出标志。
			if(bIsShuttingDown.load(std::memory_order_acquire))
			{
				break;
			}
		}

		return 0;
	}

	/** 请求主循环在下一帧边界退出。 */
	void RequestExit() final override { bIsShuttingDown.store(true, std::memory_order_release); }

	/** 取回活跃 feature 实例（只读，供子类/外部查询）。 */
	const std::vector<std::unique_ptr<FEngineLayer>>& GetFeatures() const
	{
		return Features;
	}

	/** 安装语法糖：接管一个管线实例（下帧生效）。 */
	void Install(FEngineLayer* Pipeline)
	{
		if (Pipeline != nullptr)
		{
			Pipeline->Owner = this;
		}
		PendingAdded.push_back(Pipeline);
	}

	/**
	 * 经 FAssembly 动态加载一个 feature DLL 并安装（下帧生效）。
	 * 引擎持有 FAssembly + FEngineLayer 的 unique_ptr（Modules/Features），
	 * 卸载时引擎自行 delete 实例并 FreeLibrary。
	 */
	void Install(std::string_view DllPath, const char* FactorySymbol = "CreateLayer")
	{
		auto Asm = std::make_unique<FAssembly>(DllPath);
		if (!Asm->IsLoaded())
		{
			return;
		}

		using CreateFn = FEngineLayer* (*)();
		auto Create = Asm->GetProcAs<CreateFn>(FactorySymbol);
		if (Create == nullptr)
		{
			return;
		}

		auto Layer = std::unique_ptr<FEngineLayer>(Create());
		if (!Layer)
		{
			return;
		}

		Install(Layer.get());
		Modules.push_back(std::move(Asm));
		Features.push_back(std::move(Layer));
	}

	/**
	 * 随机卸载请求 —— 无条件记入待卸载集合（不即时校验）。
	 *
	 * 安全点 FlushUnload 先应用 PendingAdded，再用全部 pending remove 建
	 * 小顶堆贪心卸载：同帧内 A 可能被 B 挡住，但若 B 也在卸载请求里，B 先
	 * 弹出卸载后 A 的依赖数归零、随之弹出 —— 一次请求内连锁卸载。
	 */
	void RequestUninstall(FEngineLayer* Pipeline)
	{
		if (Pipeline != nullptr)
		{
			PendingRemoveRequests.insert(Pipeline);
		}
	}

	/**
	 * 匿名卸载请求 —— 按 layer 名（GetName()）寻址卸载。等价 RequestUninstall，
	 * 只是按名字而非指针。找不到同名活跃层则忽略。
	 */
	void TryUninstall(std::string_view LayerName)
	{
		for (FLayerBase* L : Pipelines)
		{
			if (L->GetName() == LayerName)
			{
				RequestUninstall(static_cast<FEngineLayer*>(L));
				return;
			}
		}
	}

protected:
	/** 应用挂起的安装/卸载（主循环安全点调用）。 */
	void FlushPendingUpdatePipelines()
	{
		for (FEngineLayer* P : PendingAdded)
		{
			Pipelines.push_back(P);
		}
		PendingAdded.clear();

		FlushUnload();   // 小顶堆贪心卸载（随机卸载请求的批量应用）
	}

	/** 引擎持有 feature 实例 + DLL；卸载时一并 delete + FreeLibrary。 */
	void DeleteUnloaded(FEngineLayer* Layer)
	{
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I].get() == Layer)
			{
				// 先 delete feature（虚析构在 DLL），再释放 DLL。
				Features[I].reset();
				if (I < Modules.size())
				{
					Modules[I].reset();
				}
				return;
			}
		}
	}

private:
	/** 重建反向依赖计数：layer 名 → 被依赖数（活跃层全量重算）。 */
	void RebuildReverseDeps()
	{
		ReverseDepCount.clear();

		// 活跃层名全量初始化为 0（无依赖/无被依赖的层也在内）。
		for (FLayerBase* L : Pipelines)
		{
			ReverseDepCount[std::string(L->GetName())] = 0;
		}
		for (FEngineLayer* L : PendingAdded)
		{
			ReverseDepCount[std::string(L->GetName())] = 0;
		}

		// 累加：每个活跃层声明的依赖 → 被依赖者计数 +1。
		for (FLayerBase* L : Pipelines)
		{
			for (const auto& [Stage, Deps] : L->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					ReverseDepCount[Dep.Name] += 1;
				}
			}
		}
		for (FEngineLayer* L : PendingAdded)
		{
			for (const auto& [Stage, Deps] : L->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					ReverseDepCount[Dep.Name] += 1;
				}
			}
		}
	}

	/**
	 * 小顶堆贪心卸载 —— 用全部 pending remove 请求建堆（按被依赖数），每次
	 * 卸一个被依赖数为 0 的层，更新其依赖者的计数并重入堆，直到堆顶被依赖
	 * 数 > 0（剩下的都还被依赖，放弃）。依赖者的请求在同一批内连锁完成。
	 */
	void FlushUnload()
	{
		if (PendingRemoveRequests.empty())
		{
			return;
		}
		RebuildReverseDeps();

		// name → 活跃层指针（仅 Pipelines；PendingAdded 已在本函数前应用）。
		std::map<std::string, FEngineLayer*> ByName;
		for (FLayerBase* L : Pipelines)
		{
			ByName[std::string(L->GetName())] = static_cast<FEngineLayer*>(L);
		}

		// 小顶堆：只装"已请求卸载"的层 (被依赖数, name)。
		using HeapEntry = std::pair<int, std::string>;
		auto Cmp = [](const HeapEntry& A, const HeapEntry& B)
		{
			return A.first > B.first;   // min-heap
		};
		std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(Cmp)> Heap(Cmp);
		for (FEngineLayer* L : PendingRemoveRequests)
		{
			const std::string Name = std::string(L->GetName());
			Heap.push({ ReverseDepCount[Name], Name });
		}

		while (!Heap.empty())
		{
			const auto [Count, Name] = Heap.top();
			Heap.pop();

			// 过期条目：计数已被更早的弹出更新过。
			if (ReverseDepCount[Name] != Count)
			{
				continue;
			}
			auto It = ByName.find(Name);
			if (It == ByName.end())
			{
				continue;   // 不在活跃集（已被卸载）。
			}
			FEngineLayer* Layer = It->second;
			if (!PendingRemoveRequests.count(Layer))
			{
				continue;   // 已处理。
			}
			if (Count > 0)
			{
				break;   // 被依赖 → 堆后只会更大，放弃剩余请求。
			}
			// 安全卸载 Layer：移出管线 + 移出请求集合。
			Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), Layer), Pipelines.end());
			ByName.erase(Name);
			PendingRemoveRequests.erase(Layer);

			// 先更新 Layer 依赖者的被依赖数（-1），重入堆以触发连锁卸载 ——
			// 必须在 delete Layer 之前读 GetDependencies()。
			for (const auto& [Stage, Deps] : Layer->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					const int NewCount = ReverseDepCount[Dep.Name] - 1;
					ReverseDepCount[Dep.Name] = NewCount;
					Heap.push({ NewCount, Dep.Name });
				}
			}

			// 最后 delete 实例 + FreeLibrary。
			DeleteUnloaded(Layer);
		}

		// 放弃本批无法安全卸载的剩余请求（不跨帧追认）。
		PendingRemoveRequests.clear();
	}

private:
	std::vector<FLayerBase*> Pipelines;          // 当前活跃管线（匿名，图只认 FLayerBase）
	std::vector<FEngineLayer*> PendingAdded;     // 挂起安装

	std::set<FEngineLayer*>    PendingRemoveRequests;  // 随机卸载请求集合
	std::map<std::string, int> ReverseDepCount;        // layer 名 → 被依赖数

	std::vector<std::unique_ptr<FAssembly>>     Modules;   // DLL 保活（move-only）
	std::vector<std::unique_ptr<FEngineLayer>>  Features;  // feature 实例所有权

	std::atomic<bool> bIsShuttingDown = false;   // 引擎是否正在关闭
};

} // namespace Maho
