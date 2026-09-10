#include <UIView.h>

namespace Maho { namespace UI {

namespace
{
/** 深度优先按 Id 查节点（Id 在整棵树内不要求唯一，取先遇者）。 */
FUIBuilder* FindById(FUIBuilder* Node, FUIName InId)
{
	if (Node->GetId() == InId) { return Node; }
	for (const std::unique_ptr<FUIBuilder>& Child : Node->GetChildren())
	{
		if (FUIBuilder* Hit = FindById(Child.get(), InId)) { return Hit; }
	}
	return nullptr;
}
} // namespace

// -- FUIEditScope -----------------------------------------------------------------------

FUIEditScope::FUIEditScope(FUIView& InView)
	: View(&InView)
	, Lock(InView.TreeMutex)
{
}

FUIEditScope::~FUIEditScope() = default;

FUIBuilder& FUIEditScope::GetRoot() const
{
	return View->GetRoot();
}

// -- FUIView ----------------------------------------------------------------------------

FUIView::FUIView(FUIName InId, EUIOwnership InOwnership)
	: Id(InId)
	, Ownership(InOwnership)
	, RootNode(std::make_unique<FUICanvas>(InId))
{
}

FUIView::~FUIView()
{
	// 关表由所有者负责：注册表只持裸指针，撤销发布在 Shutdown。
	PendingEvents.clear();
}

FUIEditScope FUIView::Edit()
{
	return FUIEditScope(*this);
}

void FUIView::SetDisplaySize(float W, float H)
{
	DisplaySize = FUIVector2{ W, H };
}

FUIVector2 FUIView::GetDisplaySize() const
{
	return DisplaySize;
}

void FUIView::SetRenderContext(void* InContext)
{
	RenderContext = InContext;
}

void* FUIView::GetRenderContext() const
{
	return RenderContext;
}

void FUIView::SetWindowShell(bool bEnabled, std::string Title,
							 FUIVector2 DefaultPos, FUIVector2 DefaultSize,
							 EUIShellFlags Flags)
{
	WindowShell.bEnabled = bEnabled;
	WindowShell.Title = std::move(Title);
	WindowShell.DefaultPos = DefaultPos;
	WindowShell.DefaultSize = DefaultSize;
	WindowShell.Flags = Flags;
}

const FUIViewShell& FUIView::GetWindowShell() const
{
	return WindowShell;
}

void FUIView::SetShellFractions(FUIVector2 PosFraction, FUIVector2 SizeFraction)
{
	WindowShell.PosFraction = PosFraction;
	WindowShell.SizeFraction = SizeFraction;
}

void FUIView::RequestCloseWindow()
{
	std::lock_guard<std::mutex> Guard(EventMutex);
	bCloseRequested = true;
}

void FUIView::PushEvent(FUIEventRecord Record)
{
	std::lock_guard<std::mutex> Guard(EventMutex);
	PendingEvents.push_back(std::move(Record));
}

FUIBuilder* FUIView::FindPath(const std::vector<FUIName>& Path) const
{
	FUIBuilder* Node = RootNode.get();
	for (const FUIName& Step : Path)
	{
		if (Node == nullptr) { return nullptr; }
		Node = Node->FindChild(Step);
	}
	return Node;
}

FUIBuilder* FUIView::Find(FUIName InId) const
{
	std::shared_lock<std::shared_mutex> Guard(TreeMutex);
	return FindById(RootNode.get(), InId);
}

void FUIView::DrainEvents()
{
	std::vector<FUIEventRecord> Records;
	bool bClose = false;
	{
		std::lock_guard<std::mutex> Guard(EventMutex);
		Records.swap(PendingEvents);
		bClose = bCloseRequested;
		bCloseRequested = false;
	}

	for (const FUIEventRecord& Record : Records)
	{
		// 逐条查树：前一条回调可能已改动结构（故不缓存节点指针）
		FUIBuilder* Node = nullptr;
		{
			std::shared_lock<std::shared_mutex> Guard(TreeMutex);
			Node = Record.Path.empty() ? FindById(RootNode.get(), Record.Target)
									   : FindPath(Record.Path);
		}
		if (Node == nullptr) { continue; }
		if (Node->IsDisabled()) { continue; }   // 禁用节点不产生事件
		Node->BroadcastEvent(Record);
	}

	if (bClose)
	{
		WindowClosed.Broadcast(*this);
	}
}

// -- EUIShellFlags ----------------------------------------------------------------------

EUIShellFlags operator|(EUIShellFlags A, EUIShellFlags B)
{
	return static_cast<EUIShellFlags>(static_cast<std::uint32_t>(A) | static_cast<std::uint32_t>(B));
}

EUIShellFlags operator&(EUIShellFlags A, EUIShellFlags B)
{
	return static_cast<EUIShellFlags>(static_cast<std::uint32_t>(A) & static_cast<std::uint32_t>(B));
}

bool HasFlag(EUIShellFlags Value, EUIShellFlags Flag)
{
	return (static_cast<std::uint32_t>(Value) & static_cast<std::uint32_t>(Flag)) != 0;
}

}} // namespace Maho::UI
