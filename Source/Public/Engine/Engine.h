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

#define MAHO_DECLARE_ENGINE_LAYER(FeatureType, DLL)     \
public:                                                 \
	MAHO_DECLARE_LAYER(FeatureType)				\
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
class FEngineBase;

class MAHO_API IPreInit
{
public:
	virtual ~IPreInit() = default;
	virtual void PreInitialize(FEngineBase&) = 0;
};

class MAHO_API IInit
{
public:
	virtual ~IInit() = default;
	virtual void Initialize(FEngineBase&) = 0;
};

class MAHO_API IPostInit
{
public:
	virtual ~IPostInit() = default;
	virtual void PostInitialize(FEngineBase&) = 0;
};

class MAHO_API IPreShutdown
{
public:
	virtual ~IPreShutdown() = default;
	virtual void PreShutdown(FEngineBase&) = 0;
};

class MAHO_API IShutdown
{
public:
	virtual ~IShutdown() = default;
	virtual void Shutdown(FEngineBase&) = 0;
};

class MAHO_API IPostShutdown
{
public:
	virtual ~IPostShutdown() = default;
	virtual void PostShutdown(FEngineBase&) = 0;
};

/** Begin-frame capability. */
class MAHO_API IBeginFrame
{
public:
	virtual ~IBeginFrame() = default;
	virtual void BeginFrame(FEngineBase&) = 0;
};

/** Tick capability. */
class MAHO_API ITick
{
public:
	virtual ~ITick() = default;
	virtual void Tick(FEngineBase&) = 0;
};

/** End-frame capability. */
class MAHO_API IEndFrame
{
public:
	virtual ~IEndFrame() = default;
	virtual void EndFrame(FEngineBase&) = 0;
};

/** Exit capability — request a running loop (IMain) to stop. */
class MAHO_API IExit
{
public:
	virtual ~IExit() = default;
	virtual void RequestExit(FEngineBase&) = 0;
};

class MAHO_API IEngineInitPipeline : public IPipeline<IPreInit, IInit, IPostInit>
{
public:
	template <typename TStage, typename TContext>
	void Invoke(TContext& Engine)
	{
		if constexpr (std::is_same_v<TStage, IPreInit>)
		{
			static_cast<IPreInit&>(*this).PreInitialize(Engine);
		}
		else if constexpr (std::is_same_v<TStage, IInit>)
		{
			static_cast<IInit&>(*this).Initialize(Engine);
		}
		else if constexpr (std::is_same_v<TStage, IPostInit>)
		{
			static_cast<IPostInit&>(*this).PostInitialize(Engine);
		}
		else
		{
			static_assert(sizeof(TStage) == 0, "Unhandled stage — add a branch in IEngineInitPipeline::Invoke");
		}
	}
};

class MAHO_API IEngineTickPipeline: public IPipeline<IBeginFrame, ITick, IEndFrame, IExit>
{
public:
	/** stage → 方法调用的 if-constexpr 分派（TContext = FEngineBase）。 */
	template <typename TStage, typename TContext>
	void Invoke(TContext& Engine)
	{
		if constexpr (std::is_same_v<TStage, IBeginFrame>)
		{
			static_cast<IBeginFrame&>(*this).BeginFrame(Engine);
		}
		else if constexpr (std::is_same_v<TStage, ITick>)
		{
			static_cast<ITick&>(*this).Tick(Engine);
		}
		else if constexpr (std::is_same_v<TStage, IEndFrame>)
		{
			static_cast<IEndFrame&>(*this).EndFrame(Engine);
		}
		else if constexpr (std::is_same_v<TStage, IExit>)
		{
			static_cast<IExit&>(*this).RequestExit(Engine);
		}
		else
		{
			static_assert(sizeof(TStage) == 0, "Unhandled stage — add a branch in IEngineTickPipeline::Invoke");
		}
	}
};

class MAHO_API IEngineShutdownPipeline : public IPipeline<IPreShutdown,IShutdown, IPostShutdown>
{
public:
	template <typename TStage, typename TContext>
	void Invoke(TContext& Engine)
	{
		if constexpr (std::is_same_v<TStage, IPreShutdown>)
		{
			static_cast<IPreShutdown&>(*this).PreShutdown(Engine);
		}
		else if constexpr (std::is_same_v<TStage, IShutdown>)
		{
			static_cast<IShutdown&>(*this).Shutdown(Engine);
		}
		else if constexpr (std::is_same_v<TStage, IPostShutdown>)
		{
			static_cast<IPostShutdown&>(*this).PostShutdown(Engine);
		}
		else
		{
			static_assert(sizeof(TStage) == 0, "Unhandled stage — add a branch in IEngineShutdownPipeline::Invoke");
		}
	}
};

// 引擎拓展基类
class MAHO_API FEngineLayer
	: public FLayer<
		IEngineInitPipeline, 
		IEngineTickPipeline, 
		IEngineShutdownPipeline>
{
public:
	// 默认空实现 —— feature 按需 override 关心的 stage（其余静默 no-op）。
	virtual void PreInitialize(FEngineBase&) {}
	virtual void Initialize(FEngineBase&) {}
	virtual void PostInitialize(FEngineBase&) {}

	virtual void BeginFrame(FEngineBase&) {}
	virtual void Tick(FEngineBase&) {}
	virtual void EndFrame(FEngineBase&) {}
	virtual void RequestExit(FEngineBase&) {}

	virtual void PreShutdown(FEngineBase&) {}
	virtual void Shutdown(FEngineBase&) {}
	virtual void PostShutdown(FEngineBase&) {}
};

// 引擎基类
class MAHO_API FEngineBase
{
public:
	virtual void ParseCommandLine(int Argc, char** Argv);

	virtual void PreMain() = 0;

	virtual void PostMain() = 0;

	int Main();

public:
	/** 取回活跃 feature 实例（只读，供子类/外部查询）。 */
	const std::vector<std::unique_ptr<FEngineLayer>>& GetLayers() const
	{
		return Features;
	}

	/** 请求主循环在下一帧边界退出。 */
	void RequestExit();

	/** 启动参数（IInit stage 经此取，如 Log 的 --log-level）。 */
	[[nodiscard]] int GetLaunchArgc() const { return LaunchArgc; }
	[[nodiscard]] char** GetLaunchArgv() const { return LaunchArgv; }

	/** 按层名（GetName()）查找活跃 feature 实例；找不到返回 nullptr。 */
	FEngineLayer* FindLayer(std::string_view LayerName);

	/** 安装语法糖：接管一个管线实例（下帧生效）。 */
	void Install(FEngineLayer* Pipeline);

	/**
	 * 经 FAssembly 动态加载一个 feature DLL 并安装（下帧生效）。
	 * 引擎持有 FAssembly + FEngineLayer 的 unique_ptr（Modules/Features），
	 * 卸载时引擎自行 delete 实例并 FreeLibrary。
	 */
	void Install(std::string_view DllPath, const char* FactorySymbol = "CreateLayer");

	/**
	 * 随机卸载请求 —— 无条件记入待卸载集合（不即时校验）。
	 *
	 * 安全点 FlushUnload 先应用 PendingAdded，再用全部 pending remove 建
	 * 小顶堆贪心卸载：同帧内 A 可能被 B 挡住，但若 B 也在卸载请求里，B 先
	 * 弹出卸载后 A 的依赖数归零、随之弹出 —— 一次请求内连锁卸载。
	 */
	void RequestUninstall(FEngineLayer* Pipeline);

	/**
	 * 匿名卸载请求 —— 按 layer 名（GetName()）寻址卸载。等价 RequestUninstall，
	 * 只是按名字而非指针。找不到同名活跃层则忽略。
	 */
	void TryUninstall(std::string_view LayerName);

protected:
	/** 应用挂起的安装/卸载（主循环安全点调用）。 */
	void FlushPendingUpdatePipelines();

	/** 引擎持有 feature 实例 + DLL；卸载时一并 delete + FreeLibrary。 */
	void DeleteUnloaded(FEngineLayer* Layer);

private:
	/** 重建反向依赖计数：layer 名 → 被依赖数（活跃层全量重算）。 */
	void RebuildReverseDeps();

	/**
	 * 小顶堆贪心卸载 —— 用全部 pending remove 请求建堆（按被依赖数），每次
	 * 卸一个被依赖数为 0 的层，更新其依赖者的计数并重入堆，直到堆顶被依赖
	 * 数 > 0（剩下的都还被依赖，放弃）。依赖者的请求在同一批内连锁完成。
	 */
	void FlushUnload();

private:
	std::vector<FLayerBase*> Pipelines;          // 当前活跃管线（匿名，图只认 FLayerBase）
	std::vector<FEngineLayer*> PendingAdded;     // 挂起安装

	std::set<FEngineLayer*>    PendingRemoveRequests;  // 随机卸载请求集合
	std::map<std::string, int> ReverseDepCount;        // layer 名 → 被依赖数

	std::vector<std::unique_ptr<FAssembly>>     Modules;   // DLL 保活（move-only）
	std::vector<std::unique_ptr<FEngineLayer>>  Features;  // feature 实例所有权

	int     LaunchArgc = 0;
	char**  LaunchArgv = nullptr;

	std::atomic<bool> bIsShuttingDown = false;   // 引擎是否正在关闭

	FThreadPool Pool;
};

} // namespace Maho
