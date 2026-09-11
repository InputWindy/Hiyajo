// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include <Widgets/FUIPanel.h>

#include "UILayoutEngine.h"
#include "WidgetStyle.h"

#include <algorithm>

namespace Maho { namespace UI {

namespace
{
constexpr float kCloseBoxSize = 14.f;
constexpr float kCloseBoxPad  = 4.f;
} // namespace

FUIPanel::FUIPanel(FUIName InId)
	: FUIBuilder(InId)
{
	if (!GetId().IsNone())
	{
		// 关闭框不是节点：给它一个与本面板 Id 绑定的内部命中 Id（同名面板互不串号）
		CloseId = FUIName(std::string(GetId().ToString()) + "#close");
	}
}

FUIPanel::~FUIPanel() = default;

std::string_view FUIPanel::TypeName() const
{
	return "FUIPanel";
}

const FUIStyle& FUIPanel::TypeDefaultStyle() const
{
	static Detail::FTypeStyleCache Cache;
	return Cache.Get([](FUIStyle& S)
	{
		const FUITheme& Th = GetUITheme();
		S[EUIState::Normal].Fill       = Th.PanelFill;
		S[EUIState::Normal].Stroke     = Th.PanelStroke;
		S[EUIState::Normal].Text       = Th.Text;
		S[EUIState::Normal].StrokeWidth = Th.StrokeWidth;
		S[EUIState::Normal].Radius     = Th.Radius;
		S[EUIState::Normal].Padding    = FMargin(8.f, 6.f);
		S[EUIState::Normal].FontSize   = Th.FontSize;

		S[EUIState::Hovered].Fill   = Th.ControlFill;
		S[EUIState::Hovered].Stroke = Th.Border;
		S[EUIState::Hovered].Text   = Th.Text;

		S[EUIState::Pressed].Fill   = Th.ControlPress;
		S[EUIState::Pressed].Stroke = Th.Border;
		S[EUIState::Pressed].Text   = Th.Text;

		S[EUIState::Selected].Fill   = Th.PanelFill;
		S[EUIState::Selected].Stroke = Th.Accent;
		S[EUIState::Selected].Text   = Th.Text;

		S[EUIState::Disabled].Fill = Th.PanelFill;
		S[EUIState::Disabled].Text = Th.TextDisabled;
	});
}

FUIPanel& FUIPanel::SetAnchor(float InX, float InY, float InW, float InH)
{
	AnchorX = InX;
	AnchorY = InY;
	AnchorW = InW;
	AnchorH = InH;
	bHasAnchor = true;
	return *this;
}

float FUIPanel::UsedHeaderHeight() const
{
	if (Chrome != EUIPanelChrome::Full || Title.empty()) { return 0.f; }
	return HeaderHeight;
}

FUIRect FUIPanel::ResolveFrame(const FUIRect& Allocated, IUITranslator& T) const
{
	if (!bHasAnchor) { return FUIBuilder::ResolveFrame(Allocated, T); }

	// 锚点比例按本帧显示区解析（旧 `FUIWidget` 语义：与父布局分配无关）
	const FUIRect Display = T.GetDisplayRect();
	return FUIRect{ Display.X + Display.W * AnchorX, Display.Y + Display.H * AnchorY,
					Display.W * AnchorW,       Display.H * AnchorH };
}

FUIRect FUIPanel::BodyRect() const
{
	const FUIRect Frame = GetRect();
	const FMargin& P = GetResolvedStyle().Padding;
	const float Header = UsedHeaderHeight();
	return FUIRect{ Frame.X + P.Left, Frame.Y + P.Top + Header,
					std::max(0.f, Frame.W - P.Left - P.Right),
					std::max(0.f, Frame.H - P.Top - P.Bottom - Header) };
}

FUIVector2 FUIPanel::MeasureContent(IUITranslator& T, const FUIVector2& Available) const
{
	FUIVector2 Out = FUIBuilder::MeasureContent(T, Available);
	Out.Y += UsedHeaderHeight();
	return Out;
}

void FUIPanel::PaintSelf(IUITranslator& T, const FUIResolvedStyle& S)
{
	if (Chrome != EUIPanelChrome::Full) { return; }

	T.DrawRect(GetRect(), S);

	if (Title.empty()) { return; }

	const FUIRect Frame = GetRect();
	const FMargin& P = GetResolvedStyle().Padding;
	const float Header = UsedHeaderHeight();
	const FUIRect Hdr{ Frame.X + P.Left, Frame.Y + P.Top,
					   std::max(0.f, Frame.W - P.Left - P.Right), Header };

	// 表头底色：主题中性色（状态由标题文字色与关闭框体现）
	FUIResolvedStyle HeadStyle = S;
	HeadStyle.Fill   = GetUITheme().ControlFill;
	HeadStyle.Stroke = GetUITheme().Border;
	HeadStyle.Radius = std::max(0.f, S.Radius - 1.f);
	T.DrawRect(Hdr, HeadStyle);

	const float TitleRight = bClosable ? (Hdr.W - kCloseBoxSize - kCloseBoxPad * 2.f) : Hdr.W;
	const FUIRect TitleRect{ Hdr.X + 6.f, Hdr.Y, std::max(0.f, TitleRight - 6.f), Hdr.H };
	const FUIResolvedResource Font = T.ResolveFont(S.Font, S.FontSize);
	T.DrawText(TitleRect, Title, Font, S.FontSize, S, EUITextAlign::Left);

	if (!bClosable || CloseId.IsNone()) { return; }

	const FUIRect Box{ Hdr.X + Hdr.W - kCloseBoxSize - kCloseBoxPad,
					   Hdr.Y + (Hdr.H - kCloseBoxSize) * 0.5f, kCloseBoxSize, kCloseBoxSize };
	const FUIHitResult Hit = T.WidgetButton(CloseId, Box, FUIResolvedStyle{});
	if (Hit.bClicked) { Detail::Enqueue(*this, T, EUIEventType::Clicked); }

	FUIResolvedStyle IconStyle = S;
	IconStyle.Text = Hit.bHovered ? GetUITheme().Accent : S.Text;
	const FUIResolvedResource Icon = T.ResolveTexture(GetUITheme().IconClose);
	if (Icon.bValid) { T.DrawIcon(Box, Icon, IconStyle); }
	else { T.DrawText(Box, "x", Font, S.FontSize, IconStyle, EUITextAlign::Center); }
}

void FUIPanel::ArrangeChildren(IUITranslator& T)
{
	if (!bScrollable)
	{
		FUILayoutEngine::ArrangeIn(*this, T, BodyRect());
		return;
	}

	IUITranslator::FUIScrollRequest Request;
	bool bToBottom = false;
	float ScrollY = 0.f;
	if (ConsumeScrollRequest(bToBottom, ScrollY))
	{
		Request.bToBottom = bToBottom;
		Request.bSetScrollY = !bToBottom;
		Request.ScrollY = ScrollY;
	}

	const FUIRect Body = BodyRect();
	const float ContentH = MeasureContent(T, FUIVector2{ Body.W, 0.f }).Y;

	T.BeginScrollRegion(GetId(), Body, Request);
	FUILayoutEngine::ArrangeIn(*this, T, FUIRect{ 0.f, 0.f, Body.W, ContentH });
	const IUITranslator::FUIScrollInfo Info = T.EndScrollRegion();

	FUIWidgetState& State = MutableState();
	State.ScrollY = Info.ScrollY;
	State.ScrollMaxY = Info.ScrollMaxY;
}

EUIInputFlags FUIPanel::GetInputFlags() const
{
	EUIInputFlags Out = FUIBuilder::GetInputFlags();
	if (bScrollable) { Out = Out | EUIInputFlags::Scroll | EUIInputFlags::Clip; }
	if (bContextMenu) { Out = Out | EUIInputFlags::ContextMenu; }
	return Out;
}

void FUIPanel::SyncConfig(const FUIBuilder& Declared)
{
	const FUIPanel* Other = dynamic_cast<const FUIPanel*>(&Declared);
	if (Other == nullptr) { return; }
	Title = Other->Title;
	Chrome = Other->Chrome;
	bClosable = Other->bClosable;
	bScrollable = Other->bScrollable;
	bContextMenu = Other->bContextMenu;
	bHasAnchor = Other->bHasAnchor;
	AnchorX = Other->AnchorX;
	AnchorY = Other->AnchorY;
	AnchorW = Other->AnchorW;
	AnchorH = Other->AnchorH;
	HeaderHeight = Other->HeaderHeight;
}

}} // namespace Maho::UI
