#pragma once

#include <Core/Interface.h>
#include <Engine/Layer.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

#define MAHO_DECLARE_ENGINE(EngineType, DLL)            \
public:                                                 \
	static Maho::IEngine* CreateEngine()          	    \
	{                                                   \
		return new EngineType();                        \
	}                                                   \
	static std::string_view GetModulePath()             \
	{                                                   \
		return DLL;                                     \
	}                                                   \

namespace Maho
{

// ── 引擎主循环三阶段 ──────────────────────────────────────────────────────

/** Begin-frame capability. */
class IBeginFrame
{
public:
	virtual ~IBeginFrame() = default;
	virtual void BeginFrame() = 0;
};

/** Tick capability. */
class ITick
{
public:
	virtual ~ITick() = default;
	virtual void Tick() = 0;
};

/** End-frame capability. */
class IEndFrame
{
public:
	virtual ~IEndFrame() = default;
	virtual void EndFrame() = 0;
};

class IEngine;

/**
 * 引擎主循环固定管线 —— BeginFrame → Tick → EndFrame 三个无参 stage。
 * 既是 FLayerBase（可被 TaskGraph 调度，GetName 由具体 feature 提供），
 * 又是 IPipeline（stage 列表），并实现 Invoke 协议（stage → 方法调用）。
 */
class IEnginePipeline: public IPipeline<IBeginFrame, ITick, IEndFrame>
{
public:
	/** stage → 方法调用的 if-constexpr 分派（TContext = IEngine）。 */
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

class FEngineLayer : public FLayer<IEnginePipeline>
{
};

/**
 * 引擎锚点 —— 生命周期能力（IInit/IMain/IShutdown）+ 主循环。
 * 主循环驱动一张引擎管线图：每帧 Flush → 应用挂起管线变更 → Init → Compile
 * → Execute（BeginFrame/Tick/EndFrame 三阶段按依赖图并行调度）。
 */
class IEngine : public IPlugin<IInit, IMain, IShutdown>
{
public:
	void Initialize(int Argc, char** Argv) override { (void)Argc; (void)Argv; }
	void Shutdown() override {}

	int Main() final override
	{
		FThreadPool Pool;
		FLayerTaskGraph<IEnginePipeline, IEngine> EngineGraph(Pool, *this);

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
			break;
		}

		return 0;
	}

protected:
	/** 安装语法糖：接管一个管线实例（下帧生效）。 */
	void Install(FEngineLayer* Pipeline)
	{
		PendingAdded.push_back(Pipeline);
	}
	void Uninstall(FEngineLayer* Pipeline)
	{
		PendingRemoved.push_back(Pipeline);
	}

	/** 应用挂起的安装/卸载（主循环安全点调用）。 */
	void FlushPendingUpdatePipelines()
	{
		for (FEngineLayer* P : PendingAdded)
		{
			Pipelines.push_back(P);
		}
		PendingAdded.clear();

		for (FEngineLayer* P : PendingRemoved)
		{
			Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), P), Pipelines.end());
		}
		PendingRemoved.clear();
	}

private:
	std::vector<FLayerBase*> Pipelines;          // 当前活跃管线（匿名，图只认 FLayerBase）
	std::vector<FEngineLayer*> PendingAdded;     // 挂起安装
	std::vector<FEngineLayer*> PendingRemoved;   // 挂起卸载

	std::atomic<bool> bIsShuttingDown = false;   // 引擎是否正在关闭
};

} // namespace Maho
