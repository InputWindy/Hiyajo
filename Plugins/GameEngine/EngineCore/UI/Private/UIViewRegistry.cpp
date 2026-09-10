#include <UIViewRegistry.h>

#include <UIView.h>

#include <Log.h>

#include <algorithm>

namespace Maho { namespace UI {

namespace
{
FUIViewRegistry* GUIRegistry = nullptr;   // 文件作用域，不导出
void* GGameRenderContext = nullptr;       // 游戏侧上下文登记（仅中转，不在本 DLL 内解引用）
}

MAHO_UI_API FUIViewRegistry* GetUIViewRegistry()
{
	return GUIRegistry;
}

MAHO_UI_API void SetUIGameRenderContext(void* Context)
{
	GGameRenderContext = Context;
}

MAHO_UI_API void* GetUIGameRenderContext()
{
	return GGameRenderContext;
}

FUIViewRegistry::FUIViewRegistry()
{
	// 诊断路径要用的 Log 必须先就位（与 Init 图并发性质一致：任何在 IInit 里写日志的层
	// 都依赖 FLog.IInit）。
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();
}

FUIViewRegistry::~FUIViewRegistry() = default;

void FUIViewRegistry::RegisterView(FUIView& View)
{
	std::scoped_lock Lock(Mutex);
	Views.push_back(&View);
}

void FUIViewRegistry::UnregisterView(FUIView& View)
{
	std::scoped_lock Lock(Mutex);
	Views.erase(std::remove(Views.begin(), Views.end(), &View), Views.end());
}

std::vector<FUIView*> FUIViewRegistry::SnapshotViews() const
{
	std::scoped_lock Lock(Mutex);
	return Views;
}

FUIView* FUIViewRegistry::FindView(FUIName Id) const
{
	std::scoped_lock Lock(Mutex);
	for (FUIView* View : Views)
	{
		if (View != nullptr && View->GetId() == Id) { return View; }
	}
	return nullptr;
}

void FUIViewRegistry::Initialize(FEngineBase& Engine)
{
	std::scoped_lock Lock(Mutex);
	GUIRegistry = this;
}

void FUIViewRegistry::Shutdown(FEngineBase& Engine)
{
	std::scoped_lock Lock(Mutex);
	// 注销是每个所有者的责任：表关掉时仍在册的视图就是所有权漏了。
	MAHO_IF_NOT_NULL(GetLog(), L) { L->Warn("UI: 注册表关闭时仍有 {} 个视图在册", Views.size()); }
	Views.clear();
	GUIRegistry = nullptr;
	// 上下文登记与表同生命周期：表关掉后不再有任何翻译入口，留着指针只会是悬垂读源。
	GGameRenderContext = nullptr;
}

}} // namespace Maho::UI
