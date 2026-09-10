#pragma once

#include "UIApi.h"
#include "UITypes.h"

#include <Maho.h>
#include <Engine/Engine.h>

#include <mutex>
#include <vector>

namespace Maho { namespace UI {

class FUIView;
class FUIViewRegistry;

/** 注册表访问器 —— 与 `GetLog()` 同形（`Plugins/Common/Log/Public/Log.h:28`）：跨 DLL 走导出
 *  **函数**，不导出裸变量。层 Init 后非空、Shutdown 后置空，取用前判空。 */
MAHO_UI_API FUIViewRegistry* GetUIViewRegistry();

/** 游戏侧 UI 上下文登记（opaque 指针，本 DLL 不认识 ImGui）。
 *
 *  上下文的所有者（游戏 UI 渲染特性 `FUIFeature`）在创建上下文后 `SetUIGameRenderContext(this)`
 *  发布、销毁前再置空；游戏侧视图的所有者（`FUISystem`）建视图时取此值 `FUIView::SetRenderContext`，
 *  从而**不需要** `UISystem → UIFeature` 的构建依赖（那会成环：`UIFeature` 已依赖 `UISystem`）。
 *  取用前判空：为空 = 上下文还没建好（视图所有者下一帧再试注册）。 */
MAHO_UI_API void SetUIGameRenderContext(void* Context);
MAHO_UI_API void* GetUIGameRenderContext();

/** 跨 DLL 视图注册表 —— 与 `FLog` 同形：**没有 `static Get()`**，本身是一个层（只有 IInit/IShutdown），
 *  由宿主 Install。Initialize 里发布 `this`，Shutdown 里清空并撤发布。
 *
 *  所有权归各视图所有者（编辑器面板 / 游戏侧系统）：本表只持有裸指针，从不删除别人注册的视图。
 *  线程契约：注册/注销在宿主线程（安装/卸载期），读快照可来自翻译线程 —— 内部互斥保护。 */
class FUIViewRegistry : public FLayer<IInit, IShutdown>
{
public:
	MAHO_DECLARE_LAYER(FUIViewRegistry);

	FUIViewRegistry();
	~FUIViewRegistry() override;

	FUIViewRegistry(const FUIViewRegistry&) = delete;
	FUIViewRegistry& operator=(const FUIViewRegistry&) = delete;

	void RegisterView(FUIView& View);
	void UnregisterView(FUIView& View);

	/** 锁内快照 —— 翻译线程遍历用，遍历期间不持锁（视图可在遍历中被注销）。 */
	[[nodiscard]] std::vector<FUIView*> SnapshotViews() const;

	[[nodiscard]] FUIView* FindView(FUIName Id) const;

private:
	// -- engine init/shutdown stages (scheduler-only) --
	void Initialize(FEngineBase& Engine) override;   // 发布 this
	void Shutdown(FEngineBase& Engine) override;     // 清空 + 撤发布

	mutable std::mutex Mutex;
	std::vector<FUIView*> Views;
};

}} // namespace Maho::UI
