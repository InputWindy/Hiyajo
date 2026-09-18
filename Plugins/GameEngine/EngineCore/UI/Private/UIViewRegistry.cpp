#include <UIViewRegistry.h>

#include <UIView.h>

#include <Log.h>

#include <Core/Fatal.h>

#include <algorithm>
#include <string>
#include <utility>

namespace Maho { namespace UI {

namespace
{
/** 注册表自身的发布指针。**只有它必须留在文件作用域** —— 它是「发布自身」，成员化做不到
 *  （也没有意义）。它的生命期由 Initialize / Shutdown 管，不是 CRT 的 atexit。 */
FUIViewRegistry* GUIRegistry = nullptr;
}

MAHO_UI_API FUIViewRegistry* GetUIViewRegistry()
{
	return GUIRegistry;
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

// -- 能力槽 ---------------------------------------------------------------------------

FSubscriptionID FUIViewRegistry::BindResourceResolver(std::string_view Owner, FUIResourceResolver Resolver)
{
	std::scoped_lock Lock(CapabilityMutex);
	const FSubscriptionID Token = NextCapabilityToken++;
	this->Resolver = std::move(Resolver);
	ResolverOwner.assign(Owner);
	ResolverToken = Token;
	return Token;
}

void FUIViewRegistry::UnbindResourceResolver(FSubscriptionID Token)
{
	std::scoped_lock Lock(CapabilityMutex);
	// 只认自己的 token：后来的注册者覆盖先前的，旧 token 交还时不该清掉新注册者的能力。
	if (Token != 0 && Token == ResolverToken)
	{
		Resolver = {};
		ResolverOwner.clear();
		ResolverToken = 0;
	}
}

bool FUIViewRegistry::HasResourceResolver() const
{
	std::scoped_lock Lock(CapabilityMutex);
	return static_cast<bool>(Resolver);
}

FUIResolvedResource FUIViewRegistry::ResolveResource(const FUIName& Resource, bool bIsFont) const
{
	FUIResourceResolver Copy;
	{
		std::scoped_lock Lock(CapabilityMutex);
		Copy = Resolver;   // 拷贝后在锁外调用（解析器可能回头进 UI API）
	}
	if (!Copy)
	{
		FUIResolvedResource Out;
		Out.Name = Resource;
		return Out;
	}
	return Copy(Resource, bIsFont);
}

FSubscriptionID FUIViewRegistry::BindClipboardHandlers(std::string_view Owner,
	FUIClipboardGetHandler Get, FUIClipboardSetHandler Set)
{
	std::scoped_lock Lock(CapabilityMutex);
	const FSubscriptionID Token = NextCapabilityToken++;
	ClipboardGet = std::move(Get);
	ClipboardSet = std::move(Set);
	ClipboardOwner.assign(Owner);
	ClipboardToken = Token;
	return Token;
}

void FUIViewRegistry::UnbindClipboardHandlers(FSubscriptionID Token)
{
	std::scoped_lock Lock(CapabilityMutex);
	if (Token != 0 && Token == ClipboardToken)
	{
		ClipboardGet = {};
		ClipboardSet = {};
		ClipboardOwner.clear();
		ClipboardToken = 0;
	}
}

std::string FUIViewRegistry::ReadClipboard() const
{
	FUIClipboardGetHandler Get;
	{
		std::scoped_lock Lock(CapabilityMutex);
		Get = ClipboardGet;
	}
	return Get ? Get() : std::string{};
}

void FUIViewRegistry::WriteClipboard(std::string_view Text) const
{
	FUIClipboardSetHandler Set;
	{
		std::scoped_lock Lock(CapabilityMutex);
		Set = ClipboardSet;
	}
	if (Set) { Set(Text); }
}

void FUIViewRegistry::Initialize(FEngineBase& Engine, FEngineContext& Frame)
{
	std::scoped_lock Lock(Mutex);
	GUIRegistry = this;
}

void FUIViewRegistry::Shutdown(FEngineBase& Engine, FEngineContext& Frame)
{
	// 1) 能力槽：先对账，再清空。
	//    注册者若已经卸载（它死在别的 collector 的子树里 —— 比如 FExampleEditor 在 FRender 下），
	//    槽里剩下的就是**已卸载模块的闭包**；留到实例析构，就会在 DLL detach 之后调用它
	//    （实测：进程退出时 _Func_class::_Tidy 读访问冲突）。所以这里既报出来（强制可见），
	//    也清掉（防崩）。报出来 = 「注入者必须在自己的 teardown 里交还 token」不再是靠记性。
	//
	//    走 ReportError（→ stderr）而不是 Log：本层排在 FLog::Shutdown 之后，spdlog 那时已失效
	//    （实测：shutdown 阶段写进 Log 的东西一句都不会出现）。teardown 期的报告必须走 stderr，
	//    与 ResourceSystem::Shutdown 的 leftover 报告同办。
	{
		std::scoped_lock Lock(CapabilityMutex);
		if (ResolverToken != 0)
		{
			ReportError((std::string("UI: 资源解析能力由 '") + ResolverOwner
				+ "' 注入但从未交还 —— 注册者必须在自己的 teardown 里调用 UnbindUIResourceResolver").c_str());
		}
		if (ClipboardToken != 0)
		{
			ReportError((std::string("UI: 剪贴板能力由 '") + ClipboardOwner
				+ "' 注入但从未交还 —— 注册者必须在自己的 teardown 里调用 UnbindUIClipboardHandlers").c_str());
		}
		Resolver = {};
		ResolverOwner.clear();
		ResolverToken = 0;
		ClipboardGet = {};
		ClipboardSet = {};
		ClipboardOwner.clear();
		ClipboardToken = 0;
	}

	// 2) 视图表 + 上下文登记：注销是每个所有者的责任，表关掉时仍在册的视图就是所有权漏了。
	{
		std::scoped_lock Lock(Mutex);
		if (!Views.empty())
		{
			ReportError((std::string("UI: 注册表关闭时仍有 ") + std::to_string(Views.size())
				+ " 个视图在册 —— 注销是每个视图所有者的责任").c_str());
		}
		Views.clear();
		GUIRegistry = nullptr;
	}
}

}} // namespace Maho::UI
