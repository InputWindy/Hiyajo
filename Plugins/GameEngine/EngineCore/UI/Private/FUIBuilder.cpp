// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <FUIBuilder.h>

#include "UILayoutEngine.h"
#include "UIStyleResolver.h"

#include <Core/Fatal.h>

#include <algorithm>

namespace Maho { namespace UI {

FUIBuilder::FUIBuilder(FUIName InId)
	: Id(InId)
{
}

FUIBuilder::~FUIBuilder() = default;

// -- 树 ---------------------------------------------------------------------

void FUIBuilder::ResetChildren()
{
	Children.clear();
}

bool FUIBuilder::RemoveItem(FUIName InId)
{
	if (InId.IsNone()) { return false; }
	const auto It = std::find_if(Children.begin(), Children.end(),
		[InId](const std::unique_ptr<FUIBuilder>& C) { return C->Id == InId; });
	if (It == Children.end()) { return false; }
	Children.erase(It);          // 连带其运行期状态与事件集一起析构
	return true;
}

FUIBuilder* FUIBuilder::FindChild(FUIName InId) const
{
	if (InId.IsNone()) { return nullptr; }
	for (const auto& C : Children)
	{
		if (C->Id == InId) { return C.get(); }
	}
	return nullptr;
}

FUIBuilder& FUIBuilder::operator[](FUIBlock InBlock)
{
	// 块内元素 Id 必填：块的挂载语义按 Id 复用，空 Id 无身份可言。
	for (const auto& Declared : InBlock.Nodes)
	{
		MAHO_CHECKF(Declared && !Declared->Id.IsNone(),
			"UI block: node of type '%.*s' declared with empty Id",
			static_cast<int>(Declared ? Declared->TypeName().size() : 0),
			Declared ? Declared->TypeName().data() : "");
	}

	std::vector<std::unique_ptr<FUIBuilder>> NewChildren;
	NewChildren.reserve(InBlock.Nodes.size());

	for (auto& Declared : InBlock.Nodes)
	{
		const FUIName ChildId = Declared->Id;
		MAHO_CHECKF(std::none_of(NewChildren.begin(), NewChildren.end(),
				[ChildId](const std::unique_ptr<FUIBuilder>& C) { return C->Id == ChildId; }),
			"UI block: duplicate Id in the same block");

		// 已有同 Id 同类型子节点 -> 复用该节点（保留运行期状态）
		std::unique_ptr<FUIBuilder> Slot;
		const auto It = std::find_if(Children.begin(), Children.end(),
			[ChildId](const std::unique_ptr<FUIBuilder>& C) { return C->Id == ChildId; });

		if (It != Children.end() && typeid(**It) == typeid(*Declared))
		{
			FUIBuilder& Existing = **It;
			const FUIWidgetState OldState = Existing.State;   // 运行期状态保留

			// 结构/样式/事件取声明：先递归挂载子树（声明的子节点集合 = 该节点的全量子节点）
			FUIBlock Sub;
			Sub.Nodes = std::move(Declared->Children);
			Existing.operator[](std::move(Sub));

			Existing.LayoutParams = Declared->LayoutParams;
			Existing.StyleOverride = Declared->StyleOverride;
			Existing.DragPayload = Declared->DragPayload;
			Existing.Shortcuts = Declared->Shortcuts;   // 快捷键集合取声明（空 = 未声明任何快捷键）
			Existing.bDisabled = Declared->bDisabled;
			Existing.bVisible = Declared->bVisible;
			Existing.bSelected = Declared->bSelected;
			Existing.bIsDropTarget = Declared->bIsDropTarget;
			Existing.SyncConfig(*Declared);            // 类型专属字段（Label/Value/…）取声明
			if (Declared->Events) { Existing.Events = std::move(Declared->Events); }
			Existing.State = OldState;

			Slot = std::move(*It);
		}
		else if (It != Children.end())
		{
			// 同 Id 异类型：就地替换（旧节点的状态随析构丢弃）
			Children.erase(It);
			Slot = std::move(Declared);
			Slot->Parent = this;
			Slot->Serial = AllocateSerial();
		}
		else
		{
			Slot = std::move(Declared);
			Slot->Parent = this;
			Slot->Serial = AllocateSerial();
		}

		NewChildren.push_back(std::move(Slot));
	}

	Children.swap(NewChildren);   // 未声明者随旧 vector 一起析构；顺序 = 声明顺序
	return *this;
}

std::uint32_t FUIBuilder::AllocateSerial()
{
	FUIBuilder* Root = this;
	while (Root->Parent != nullptr) { Root = Root->Parent; }
	return Root->NextSerial++;
}

// -- 状态 -------------------------------------------------------------------

bool FUIBuilder::IsDisabled() const
{
	for (const FUIBuilder* N = this; N != nullptr; N = N->Parent)
	{
		if (N->bDisabled) { return true; }
	}
	return false;
}

bool FUIBuilder::IsVisible() const
{
	for (const FUIBuilder* N = this; N != nullptr; N = N->Parent)
	{
		if (!N->bVisible) { return false; }
	}
	return true;
}

EUIState FUIBuilder::GetVisualState() const
{
	if (IsDisabled()) { return EUIState::Disabled; }
	if (State.bActive || State.bPressed) { return EUIState::Pressed; }
	if (bSelected || State.bSelected) { return EUIState::Selected; }
	if (State.bHovered) { return EUIState::Hovered; }
	return EUIState::Normal;
}

// -- 事件 -------------------------------------------------------------------

FUIEvents& FUIBuilder::EnsureEvents()
{
	if (!Events) { Events = std::make_unique<FUIEvents>(); }
	return *Events;
}

FUIEventSubscription FUIBuilder::BindClick(FUIEventHandler H)
{
	return EnsureEvents().Clicked.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindValueChanged(FUIFloatEventHandler H)
{
	return EnsureEvents().ValueChanged.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindToggled(FUIBoolEventHandler H)
{
	return EnsureEvents().Toggled.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindTextChanged(FUITextEventHandler H)
{
	return EnsureEvents().TextChanged.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindSubmitted(FUITextEventHandler H)
{
	return EnsureEvents().Submitted.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindSelectionChanged(FUIBoolEventHandler H)
{
	return EnsureEvents().SelectionChanged.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindDragDropped(FUINameEventHandler H)
{
	return EnsureEvents().DragDropped.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindPopupClosed(FUIEventHandler H)
{
	return EnsureEvents().PopupClosed.Bind(std::move(H));
}

FUIEventSubscription FUIBuilder::BindShortcut(FUITextEventHandler H)
{
	return EnsureEvents().Shortcut.Bind(std::move(H));
}

void FUIBuilder::UnbindClick(FUIEventSubscription S)
{
	if (Events) { Events->Clicked.Unbind(S); }
}

void FUIBuilder::UnbindValueChanged(FUIEventSubscription S)
{
	if (Events) { Events->ValueChanged.Unbind(S); }
}

void FUIBuilder::UnbindToggled(FUIEventSubscription S)
{
	if (Events) { Events->Toggled.Unbind(S); }
}

void FUIBuilder::UnbindTextChanged(FUIEventSubscription S)
{
	if (Events) { Events->TextChanged.Unbind(S); }
}

void FUIBuilder::UnbindSubmitted(FUIEventSubscription S)
{
	if (Events) { Events->Submitted.Unbind(S); }
}

void FUIBuilder::UnbindSelectionChanged(FUIEventSubscription S)
{
	if (Events) { Events->SelectionChanged.Unbind(S); }
}

void FUIBuilder::UnbindDragDropped(FUIEventSubscription S)
{
	if (Events) { Events->DragDropped.Unbind(S); }
}

void FUIBuilder::UnbindPopupClosed(FUIEventSubscription S)
{
	if (Events) { Events->PopupClosed.Unbind(S); }
}

void FUIBuilder::UnbindShortcut(FUIEventSubscription S)
{
	if (Events) { Events->Shortcut.Unbind(S); }
}

FUIBuilder& FUIBuilder::OnClick(FUIEventHandler H)
{
	BindClick(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnValueChanged(FUIFloatEventHandler H)
{
	BindValueChanged(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnToggled(FUIBoolEventHandler H)
{
	BindToggled(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnTextChanged(FUITextEventHandler H)
{
	BindTextChanged(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnSubmitted(FUITextEventHandler H)
{
	BindSubmitted(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnSelectionChanged(FUIBoolEventHandler H)
{
	BindSelectionChanged(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnDragDropped(FUINameEventHandler H)
{
	BindDragDropped(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnPopupClosed(FUIEventHandler H)
{
	BindPopupClosed(std::move(H));
	return *this;
}

FUIBuilder& FUIBuilder::OnShortcut(FUIKeyChord Chord, FUITextEventHandler H)
{
	if (!Chord.IsNone()) { Shortcuts.push_back(Chord); }
	BindShortcut(std::move(H));
	return *this;
}

void FUIBuilder::BroadcastEvent(const FUIEventRecord& Record)
{
	// 修饰键先落到本节点，再派发：回调里 `GetLastModifiers()` 读到的就是本次命中的键盘状态。
	State.LastModifiers = Record.Modifiers;
	if (!Events) { return; }
	switch (Record.Type)
	{
	case EUIEventType::Clicked:
		Events->Clicked.Broadcast(*this);
		break;
	case EUIEventType::ValueChanged:
		Events->ValueChanged.Broadcast(*this, Record.Value);
		break;
	case EUIEventType::Toggled:
		Events->Toggled.Broadcast(*this, Record.bFlag);
		break;
	case EUIEventType::TextChanged:
		Events->TextChanged.Broadcast(*this, Record.Text);
		break;
	case EUIEventType::Submitted:
		Events->Submitted.Broadcast(*this, Record.Text);
		break;
	case EUIEventType::SelectionChanged:
		Events->SelectionChanged.Broadcast(*this, Record.bFlag);
		break;
	case EUIEventType::DragDropped:
		Events->DragDropped.Broadcast(*this, Record.Payload);
		break;
	case EUIEventType::TreeNodeToggled:
		Events->Toggled.Broadcast(*this, Record.bFlag);
		break;
	case EUIEventType::PopupClosed:
		Events->PopupClosed.Broadcast(*this);
		break;
	case EUIEventType::Shortcut:
		Events->Shortcut.Broadcast(*this, Record.Text);
		break;
	default:
		break;
	}
}

FUIEventRecord FUIBuilder::MakeEvent(EUIEventType Type) const
{
	FUIEventRecord Record;
	Record.Type = Type;
	Record.Target = Id;

	// 根 → 本节点的 Id 链（不含根）：翻译线程构造，所有者线程按它查树
	std::vector<const FUIBuilder*> Chain;
	for (const FUIBuilder* N = this; N != nullptr; N = N->Parent)
	{
		Chain.push_back(N);
	}
	for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
	{
		if ((*It)->Parent != nullptr) { Record.Path.push_back((*It)->Id); }
	}
	return Record;
}

// -- 滚动 / 拖放 ------------------------------------------------------------

FUIBuilder& FUIBuilder::RequestScrollToBottom()
{
	bPendingToBottom = true;
	return *this;
}

FUIBuilder& FUIBuilder::SetScrollY(float InScrollY)
{
	PendingScrollY = InScrollY;
	bPendingScrollY = true;
	return *this;
}

bool FUIBuilder::ConsumeScrollRequest(bool& bOutToBottom, float& OutScrollY)
{
	bOutToBottom = bPendingToBottom;
	OutScrollY = PendingScrollY;
	const bool bAny = bPendingToBottom || bPendingScrollY;
	bPendingToBottom = false;
	bPendingScrollY = false;
	return bAny;
}

FUIBuilder& FUIBuilder::RequestKeyboardFocus()
{
	bPendingFocus = true;
	return *this;
}

bool FUIBuilder::ConsumeFocusRequest()
{
	const bool bAny = bPendingFocus;
	bPendingFocus = false;
	return bAny;
}

FUIBuilder& FUIBuilder::SetDragSource(FUIName Payload)
{
	DragPayload = Payload;
	return *this;
}

FUIBuilder& FUIBuilder::OnDropTarget(FUINameEventHandler H)
{
	BindDragDropped(std::move(H));
	bIsDropTarget = true;
	return *this;
}

// -- 翻译 -------------------------------------------------------------------

void FUIBuilder::Translate(IUITranslator& T, const FUIRect& InRect)
{
	ResolvedStyle = FUIStyleResolver::Resolve(*this, Parent ? &Parent->ResolvedStyle : nullptr);

	State.Rect = ResolveFrame(InRect, T);
	// 屏幕矩形：局部 + 当前原点（滚动区进栈会改变原点，故此处实时问后端）。
	const FUIVector2 Origin = T.GetScreenOrigin();
	State.ScreenRect = FUIRect{ Origin.X + State.Rect.X, Origin.Y + State.Rect.Y, State.Rect.W, State.Rect.H };
	if (!bVisible) { return; }

	const FUIResolvedStyle& S = ResolvedStyle;

	// 浮层节点（提示/弹层）在正常流里零尺寸：矩形空也要画内容（内容活在第二个窗口），
	// 且不参与自绘/裁剪/子节点摆放 —— 它的子树由 `PaintContent` 在弹层窗口内自行摆放。
	if (State.Rect.IsEmpty() && IsOverlayLayer())
	{
		if (IsDisabled()) { T.PushDisabled(); }
		PaintContent(T, S);
		TranslateDragDrop(T);
		if (IsDisabled()) { T.PopDisabled(); }
		return;
	}
	if (State.Rect.IsEmpty()) { return; }

	// 焦点请求在本节点开始绘制前落地：后端命中本 Id 时即请求键盘焦点（本帧生效）。
	if (ConsumeFocusRequest()) { T.SetKeyboardFocus(Id); }

	// 声明式快捷键：本帧命中的组合入队（回调仍归所有者线程）。禁用节点不响应；
	// "键盘焦点在本视图窗口 + 当前无文本输入"的守卫在后端（否则打字会误触发 Ctrl+C）。
	if (!Shortcuts.empty() && !IsDisabled())
	{
		for (const FUIKeyChord& Chord : Shortcuts)
		{
			if (!T.IsShortcutPressed(Chord)) { continue; }
			FUIEventRecord Record = MakeEvent(EUIEventType::Shortcut);
			Record.Modifiers = Chord.Mods;
			Record.Text = Chord.ToString();   // 同一节点多条快捷键时靠它区分
			T.EnqueueEvent(std::move(Record));
			break;
		}
	}

	// 右键菜单区域（`EUIInputFlags::ContextMenu`）：纯几何判定、不走 item 命中（滚动容器自己的
	// item 会被内容子窗口挡掉，而右键菜单要的恰是"整个矩形"）。命中即回写运行期状态，业务在
	// 下一帧读 `GetState().bSecondaryClicked` / `.PointerPos` 开菜单 —— 一次性，本帧先清。
	State.bSecondaryClicked = false;
	if (HasFlag(GetInputFlags(), EUIInputFlags::ContextMenu) && !IsDisabled())
	{
		FUIVector2 Pointer;
		if (T.HitTestSecondary(State.Rect, Pointer))
		{
			State.bSecondaryClicked = true;
			State.PointerPos = Pointer;
		}
	}

	if (IsDisabled()) { T.PushDisabled(); }

	PaintSelf(T, S);
	PaintContent(T, S);

	const bool bClip = LayoutParams.bClipChildren || HasFlag(GetInputFlags(), EUIInputFlags::Clip);
	if (bClip) { T.PushClip(State.Rect); }
	ArrangeChildren(T);
	if (bClip) { T.PopClip(); }

	PaintOverlay(T, S);

	// 拖放通道：翻译器侧只声明载荷/接受（控件刚提交完本帧命中项）。
	TranslateDragDrop(T);

	if (IsDisabled()) { T.PopDisabled(); }
}

void FUIBuilder::TranslateDragDrop(IUITranslator& T)
{
	const bool bSource = !DragPayload.IsNone();
	const bool bTarget = bIsDropTarget;
	if (!bSource && !bTarget) { return; }

	if (bSource)
	{
		const std::string Payload(DragPayload.ToString());
		if (T.BeginDragSource(Id, kUIPayloadType, Payload, Payload)) { T.EndDragSource(); }
	}
	if (bTarget)
	{
		std::string Payload;
		if (T.IsDropTarget(Id, kUIPayloadType, &Payload))
		{
			FUIEventRecord Record = MakeEvent(EUIEventType::DragDropped);
			Record.Payload = FUIName(Payload);
			T.EnqueueEvent(std::move(Record));   // 只入队；回调归所有者线程
		}
	}
}

FUIRect FUIBuilder::ResolveFrame(const FUIRect& Allocated, IUITranslator& T) const
{
	(void)T;
	return FUILayoutEngine::FrameRect(*this, Allocated);
}

FUIVector2 FUIBuilder::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	const FUILayout& L = LayoutParams;
	const bool bRow = L.Direction == EUIDirection::Row;

	FUIVector2 Out{ 0.f, 0.f };
	bool bFirst = true;
	for (const auto& Child : Children)
	{
		if (!Child->bVisible) { continue; }
		const FUIVector2 Size = FUILayoutEngine::Measure(*Child, T, Available);
		const FMargin& M = Child->LayoutParams.Margin;
		const FUIVector2 Ext{ Size.X + M.Left + M.Right, Size.Y + M.Top + M.Bottom };
		const float Gap = bFirst ? 0.f : L.Spacing;
		if (bRow)
		{
			Out.X += Ext.X + Gap;
			Out.Y = std::max(Out.Y, Ext.Y);
		}
		else
		{
			Out.Y += Ext.Y + Gap;
			Out.X = std::max(Out.X, Ext.X);
		}
		bFirst = false;
	}
	return Out;
}

void FUIBuilder::ArrangeChildren(IUITranslator& T)
{
	FUILayoutEngine::ArrangeChildren(*this, T);
}

EUIInputFlags FUIBuilder::GetInputFlags() const
{
	EUIInputFlags Out = EUIInputFlags::None;
	if (!DragPayload.IsNone()) { Out = Out | EUIInputFlags::DragSource; }
	if (bIsDropTarget) { Out = Out | EUIInputFlags::DropTarget; }
	return Out;
}

}} // namespace Maho::UI
