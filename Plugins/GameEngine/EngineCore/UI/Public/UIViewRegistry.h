#pragma once

#include "UIApi.h"
#include "UIClipboard.h"
#include "UIResource.h"
#include "UITypes.h"

#include <Core/Delegate.h>

#include <Maho.h>
#include <Engine/Engine.h>

#include <cstdint>
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
 *  线程契约：注册/注销在宿主线程（安装/卸载期），读快照可来自翻译线程 —— 内部互斥保护。 */
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

	/** 锁内快照 —— 翻译线程遍历用，遍历期间不持锁（视图可在遍历中被注销）。 */
	[[nodiscard]] std::vector<FUIView*> SnapshotViews() const;

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

}} // namespace Maho::UI
