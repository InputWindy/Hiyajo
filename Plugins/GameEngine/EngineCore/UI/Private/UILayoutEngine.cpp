// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "UILayoutEngine.h"

#include "UIStyleResolver.h"
#include <algorithm>
#include <vector>

namespace Maho { namespace UI {

namespace
{
/** 本节点有效内边距：`FUILayout::Padding` 非零则取它，否则取解析样式的 Padding。 */
FMargin EffectivePadding(const FUIBuilder& Node)
{
	const FMargin& P = Node.GetLayout().Padding;
	if (P.Left != 0.f || P.Top != 0.f || P.Right != 0.f || P.Bottom != 0.f) { return P; }
	return Node.GetResolvedStyle().Padding;
}

FUIRect Shrink(const FUIRect& R, const FMargin& M)
{
	const float W = std::max(0.f, R.W - M.Left - M.Right);
	const float H = std::max(0.f, R.H - M.Top - M.Bottom);
	return { R.X + M.Left, R.Y + M.Top, W, H };
}
} // namespace

FUIRect FUILayoutEngine::FrameRect(const FUIBuilder& Node, const FUIRect& Allocated)
{
	return Shrink(Allocated, Node.GetLayout().Margin);
}

FUIRect FUILayoutEngine::ContentRect(const FUIBuilder& Node, const FUIRect& Frame)
{
	return Shrink(Frame, EffectivePadding(Node));
}

FUIVector2 FUILayoutEngine::Measure(FUIBuilder& Node, IUITranslator& T, const FUIVector2& Available)
{
	const FUILayout& L = Node.GetLayout();

	// 浮层节点（提示 / 弹层）在正常流里**不占位**（见 `FUIBuilder::IsOverlayLayer` 契约）：
	// 矩形可为空，内容由 `PaintContent` 画在第二个窗口里。类型默认样式带的内边距
	// （`FUIPopup` = 10/8）不能在这里加上 —— 否则浮层会在同级流里吃掉一块真实空间，
	// 把同一容器内 `Fill` 兄弟的份额挤小（先例：控制台弹层把日志面板的 Fill 顶掉一截）。
	if (Node.IsOverlayLayer())
	{
		Node.State.ContentSize = FUIVector2{ 0.f, 0.f };
		return FUIVector2{ 0.f, 0.f };
	}

	// 量之前先把本节点样式解析到最新：`MeasureContent` 与 `EffectivePadding` 都读解析结果，
	// 而逐帧重建的节点（列表行 / 面包屑 / 树项）在本帧翻译前没有任何缓存 —— 不在这里解析，
	// 量出来的就是"零内边距"尺寸，与随后自绘所用的样式（类型默认，按钮 = 10/5）不一致：
	// 量 32×14 的按钮，画的时候标签区被内边距裁到 12×4，只剩一条。
	// 父节点在 `FUIBuilder::Translate` 里先解析了自己的样式，故逐级继承取父已解析值仍成立。
	Node.ResolvedStyle = FUIStyleResolver::Resolve(
		Node, Node.GetParent() != nullptr ? &Node.GetParent()->ResolvedStyle : nullptr);

	const FUIVector2 Content = Node.MeasureContent(T, Available);

	const auto Solve = [](const FUILength& Len, float ContentLen, float AvailLen)
	{
		switch (Len.Mode)
		{
		case EUISizeMode::Fixed:    return Len.Value;
		case EUISizeMode::Fill:     return AvailLen;
		case EUISizeMode::Fraction: return AvailLen * Len.Value;
		default:                    return ContentLen;
		}
	};

	const FMargin Pad = EffectivePadding(Node);
	FUIVector2 Out{
		Solve(L.Width, Content.X, Available.X) + Pad.Left + Pad.Right,
		Solve(L.Height, Content.Y, Available.Y) + Pad.Top + Pad.Bottom
	};
	Node.State.ContentSize = Out;
	return Out;
}

void FUILayoutEngine::ArrangeChildren(FUIBuilder& Node, IUITranslator& T)
{
	ArrangeIn(Node, T, ContentRect(Node, Node.State.Rect));
}

void FUILayoutEngine::ArrangeIn(FUIBuilder& Node, IUITranslator& T, const FUIRect& InContent)
{
	const FUILayout& L = Node.GetLayout();
	const FUIRect Content = InContent;
	const bool bRow = L.Direction == EUIDirection::Row;

	const float MainTotal  = bRow ? Content.W : Content.H;
	const float CrossTotal = bRow ? Content.H : Content.W;

	struct FSlot
	{
		FUIBuilder* Node = nullptr;
		FMargin     Margin{};
		float       MainLen = 0.f;    // 内容主轴长（不含 Margin）
		float       CrossLen = 0.f;
		bool        bFill = false;
		bool        bOverlay = false; // 浮层（零尺寸、内容活在第二个窗口）：不占流内空间
	};

	std::vector<FSlot> Slots;
	Slots.reserve(Node.Children.size());

	float UsedMain = 0.f;
	int   FillCount = 0;
	// 参与流内排布的槽数：浮层不算 —— 它零尺寸却仍是同级的一员，若让它占一份间距，同一容器内
	// 最后一个可见控件之后就会凭空多出一段横向间隙（先例：控制台命令框下方那条顶不满的空白）。
	int   FlowCount = 0;
	for (const auto& Child : Node.Children)
	{
		if (!Child->bVisible) { continue; }

		const FUILayout& CL = Child->Layout();
		const FUIVector2 Measured = Measure(*Child, T, FUIVector2{ Content.W, Content.H });
		const FUILength& Main = bRow ? CL.Width : CL.Height;
		const FUILength& Cross = bRow ? CL.Height : CL.Width;

		FSlot Slot;
		Slot.Node = Child.get();
		Slot.Margin = CL.Margin;
		Slot.bOverlay = Child->IsOverlayLayer();
		if (!Slot.bOverlay) { ++FlowCount; }

		const float MeasuredMain = bRow ? Measured.X : Measured.Y;
		const float MeasuredCross = bRow ? Measured.Y : Measured.X;
		const float MainMargin = bRow ? (CL.Margin.Left + CL.Margin.Right) : (CL.Margin.Top + CL.Margin.Bottom);
		const float CrossMargin = bRow ? (CL.Margin.Top + CL.Margin.Bottom) : (CL.Margin.Left + CL.Margin.Right);

		switch (Main.Mode)
		{
		case EUISizeMode::Fixed:
			Slot.MainLen = Main.Value;
			UsedMain += Slot.MainLen;
			break;
		case EUISizeMode::Fraction:
			Slot.MainLen = MainTotal * Main.Value;
			UsedMain += Slot.MainLen;
			break;
		case EUISizeMode::Fill:
			Slot.bFill = true;
			++FillCount;
			break;
		default:
			Slot.MainLen = MeasuredMain - MainMargin;
			UsedMain += Slot.MainLen;
			break;
		}

		switch (Cross.Mode)
		{
		case EUISizeMode::Fixed:
			Slot.CrossLen = Cross.Value;
			break;
		case EUISizeMode::Fraction:
			Slot.CrossLen = CrossTotal * Cross.Value;
			break;
		case EUISizeMode::Fill:
			Slot.CrossLen = std::max(0.f, CrossTotal - CrossMargin);
			break;
		default:
			Slot.CrossLen = std::max(0.f, MeasuredCross - CrossMargin);
			break;
		}

		const EUIAlign Align = bRow ? L.VerticalAlign : L.HorizontalAlign;
		if (Align == EUIAlign::Stretch)
		{
			Slot.CrossLen = std::max(0.f, CrossTotal - CrossMargin);
		}
		Slot.CrossLen = std::min(Slot.CrossLen, std::max(0.f, CrossTotal - CrossMargin));

		Slots.push_back(Slot);
	}

	const float TotalSpacing = L.Spacing * static_cast<float>(FlowCount > 0 ? FlowCount - 1 : 0);
	const float Remain = std::max(0.f, MainTotal - TotalSpacing - UsedMain);
	const float FillEach = FillCount > 0 ? Remain / static_cast<float>(FillCount) : 0.f;

	float MainPos = 0.f;
	for (FSlot& Slot : Slots)
	{
		if (Slot.bFill) { Slot.MainLen = FillEach; }

		const bool bRowSlot = bRow;
		const float MainMargin = bRowSlot ? (Slot.Margin.Left + Slot.Margin.Right)
										  : (Slot.Margin.Top + Slot.Margin.Bottom);
		const float CrossMargin = bRowSlot ? (Slot.Margin.Top + Slot.Margin.Bottom)
										   : (Slot.Margin.Left + Slot.Margin.Right);
		const float CrossBegin = bRowSlot ? Slot.Margin.Top : Slot.Margin.Left;

		const EUIAlign Align = bRowSlot ? L.VerticalAlign : L.HorizontalAlign;
		float CrossOffset = 0.f;
		switch (Align)
		{
		case EUIAlign::Center: CrossOffset = (CrossTotal - Slot.CrossLen - CrossMargin) * 0.5f; break;
		case EUIAlign::End:    CrossOffset = CrossTotal - Slot.CrossLen - CrossMargin; break;
		default:               CrossOffset = 0.f; break;
		}

		const float AllocMain  = Slot.MainLen + MainMargin;
		const float AllocCross = Slot.CrossLen + CrossMargin;

		const FUIRect Alloc = bRowSlot
			? FUIRect{ Content.X + MainPos, Content.Y + CrossOffset + CrossBegin, AllocMain, AllocCross }
			: FUIRect{ Content.X + CrossOffset + CrossBegin, Content.Y + MainPos, AllocCross, AllocMain };

		Slot.Node->Translate(T, Alloc);
		// 浮层之后不推进间距：它不占位，也就不该在自己身后留下一段空档（它常常是同级最后一项）。
		MainPos += Slot.bOverlay ? AllocMain : (AllocMain + L.Spacing);
	}
}

}} // namespace Maho::UI
