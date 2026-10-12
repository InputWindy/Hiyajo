#pragma once

#include "UIApi.h"
#include "UIClipboard.h"
#include "UIResource.h"
#include "UITypes.h"

#include <Core/Delegate.h>

#include <Maho.h>
#include <Engine/Engine.h>

#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Maho { namespace UI {

class FUIView;
class FUIViewRegistry;

/** 注册表访问器 —— 与 `GetLog()` 同形（`Plugins/Common/Log/Public/Log.h:28`）：跨 DLL 走导出
 *  **函数**，不导出裸变量。层 Init 后非空、Shutdown 后置空，取用前判空。
 *
 *  它是本插件**唯一**的自由函数，也是唯一的入口：拿到它之后的一切 —— 视图表、能力槽、
 *  游戏上下文登记 —— 都走它自己的成员方法。状态属于这个 frame，访问姿势也只有一种。 */
MAHO_UI_API FUIViewRegistry* GetUIViewRegistry();

/** 跨 DLL 视图注册表 —— 与 `FLog` 同形：**没有 `static Get()`**，本身是一个层（只有 IInit/IShutdown），
 *  由宿主 Install。Initialize 里发布 `this`，Shutdown 里清空并撤发布。
 *
 *  所有权归各视图所有者（编辑器面板 / 游戏侧系统）：本表只持有裸指针，从不删除别人注册的视图。
 *  线程契约：注册/注销在宿主线程（安装/卸载期），读快照可来自翻译线程 —— 内部互斥保护。
 *
 *  ── 注销与遍历的屏障（否则就是 use-after-free）────────────────────────────────────────────────
 *  表不拥有视图，所以它**无法**阻止所有者在注销之后立刻析构；它能给的唯一保证因此必须写在
 *  时间上：**一次遍历（translation pass）在飞时，`UnregisterView` 返回之前会等它归零** —— 于是
 *  "`UnregisterView` 返回后即可安全析构" 成为契约，`SnapshotViews()` 交出的裸指针在**本次**遍历
 *  内一定仍然有效（实测过的反例：翻译线程在遍历中撞上一个已析构的视图，从释放内存里读出来的
 *  `Shell.Title` 是"size>0 但首字节为 0"，于是 `ImGui::Begin` 拿到空窗口名，断言炸在
 *  `imgui.cpp:7404`）。
 *
 *  规矩只有一条：**遍历前先取 `FScopedUITranslationPass`，再取快照**（顺序不能反 —— 先快照后取
 *  屏障的话，两步之间发生的注销仍然会让指针失效）。 */
class FUIViewRegistry : public FFrameExtension, public IPipeline<IInit, IShutdown>
{
public:
	MAHO_DECLARE_FRAME(FUIViewRegistry);

	FUIViewRegistry();
	~FUIViewRegistry() override;

	FUIViewRegistry(const FUIViewRegistry&) = delete;
	FUIViewRegistry& operator=(const FUIViewRegistry&) = delete;

	void RegisterView(FUIView& View);
	void UnregisterView(FUIView& View);

	/** 锁内快照 —— 翻译线程遍历用。**取它之前必须先取 `FScopedUITranslationPass`**，否则遍历中
	 *  的视图可能被所有者析构（见类注释里的屏障）。快照本身不持锁：注册可以照常进行，只有"注销
	 *  + 析构"需要等遍历归零。 */
	[[nodiscard]] std::vector<FUIView*> SnapshotViews() const;

	/** 一次翻译遍历的开始 / 结束（用 `FScopedUITranslationPass` 而不是直接调）。 */
	void BeginTranslationPass();
	void EndTranslationPass();

	[[nodiscard]] FUIView* FindView(FUIName Id) const;

	// 这里曾经还有一个「游戏侧 ImGui 上下文」槽（`void*`：UIFeature 发布，FUISystem 取走再打到
	// 自己的视图上）。它已删除 —— 视图真正需要的只是「归哪个翻译循环」这个**归属**，而不是后端
	// 地址。现在由 `FUIView::SetRenderScope(FUIName)` 表达：登记方与翻译方各自声明自己的作用域名，
	// 于是既不需要中转，也没有任何一方持有别人的上下文。

	// -- 能力槽（别的层注入）-------------------------------------------------------------
	// 别的层把能力（资源解析器 / 剪贴板读写）注入到这里。**槽是本 frame 的成员，不是文件级
	// static** —— 这一点是整件事的关键：注入者死在 FrameGraph 的调度里（FExampleEditor 在
	// FRender 的子树里 teardown），若槽挂在 CRT 的 atexit 上，两个时刻毫无关联：注册者先死、
	// 槽后析构，析构就会调用已卸载模块的闭包（实测：进程退出时 _Func_class::_Tidy 读访问冲突）。
	// 落在本 frame 上，两个时刻就都进了图的调度：本 frame 的 IShutdown 先清槽并报出未撤销的
	// 注册者，实例析构随后发生，必然早于任何 DLL detach。
	//
	// 凭据：每次 Bind 返回一个 token，只有持 token 的一方（或本 frame 自己）能撤销它 ——
	// 后来的注册者覆盖先前的，旧 token 不会误撤销新注册者。
	[[nodiscard]] FSubscriptionID BindResourceResolver(std::string_view Owner, FUIResourceResolver Resolver);
	void UnbindResourceResolver(FSubscriptionID Token);
	[[nodiscard]] bool HasResourceResolver() const;
	/** 拷贝解析器后在锁外调用（解析器允许回头再进 UI 的公开 API）。 */
	[[nodiscard]] FUIResolvedResource ResolveResource(const FUIName& Resource, bool bIsFont) const;

	[[nodiscard]] FSubscriptionID BindClipboardHandlers(std::string_view Owner,
		FUIClipboardGetHandler Get, FUIClipboardSetHandler Set);
	void UnbindClipboardHandlers(FSubscriptionID Token);
	[[nodiscard]] std::string ReadClipboard() const;
	void WriteClipboard(std::string_view Text) const;

private:
	// -- engine init/shutdown stages (scheduler-only) --
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;   // 发布 this
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;     // 清槽 + 清空 + 撤发布

	mutable std::mutex Mutex;
	std::vector<FUIView*> Views;

	/** 遍历屏障：在飞的遍历数 + 等它归零的条件变量。与 `Mutex` 分开，是因为语义不同 ——
	 *  `Mutex` 保护表，这把保护"没人正在用表里曾经存在的指针"。 */
	std::mutex PassMutex;
	std::condition_variable PassCv;
	int PassesInFlight = 0;

	/** 等所有在飞遍历结束。**持有遍历的线程自己不等待**（那样会自锁）：遍历中注销自己的视图是
	 *  合法用法，此时它自己的遍历仍在飞，但它已经不在表里、也不会再被本次遍历访问到第二次。 */
	void WaitForPassesToDrain();

	/** 与视图表分开一把锁：注入来自别的层，解析在帧内高频，两者互不相干。 */
	mutable std::mutex CapabilityMutex;

	FUIResourceResolver Resolver;
	std::string         ResolverOwner;
	FSubscriptionID     ResolverToken = 0;

	FUIClipboardGetHandler ClipboardGet;
	FUIClipboardSetHandler ClipboardSet;
	std::string            ClipboardOwner;
	FSubscriptionID        ClipboardToken = 0;

	FSubscriptionID NextCapabilityToken = 1;
};

/** 一次翻译遍历的作用域守卫 —— 必须在取快照**之前**构造。
 *
 *  它存在的唯一目的：让 `UnregisterView` 知道"有人正拿着表里的裸指针"，从而把"注销返回"与
 *  "遍历结束"排成顺序。顺序写反（先快照、后取屏障）等于没写 —— 两步之间发生的注销仍会让指针失效。 */
class FScopedUITranslationPass
{
public:
	explicit FScopedUITranslationPass(FUIViewRegistry& InRegistry) noexcept
		: Registry(InRegistry)
	{
		Registry.BeginTranslationPass();
	}

	~FScopedUITranslationPass()
	{
		Registry.EndTranslationPass();
	}

	FScopedUITranslationPass(const FScopedUITranslationPass&) = delete;
	FScopedUITranslationPass& operator=(const FScopedUITranslationPass&) = delete;

private:
	FUIViewRegistry& Registry;
};

}} // namespace Maho::UI
