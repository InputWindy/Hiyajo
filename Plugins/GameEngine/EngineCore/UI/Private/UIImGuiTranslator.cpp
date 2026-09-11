#include "UIImGuiTranslator.h"

#include <UITheme.h>

// 弹层置顶用了一个 imgui.h 未公开的原语（`BringWindowToDisplayFront`：把窗口挪到 `g.Windows`
// 末尾 = 本帧最后画）。公开 API 里没有"只置顶、不动焦点"的写法，见 `BeginPopup` 内的注释。
#include <imgui_internal.h>

#include <Log.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Maho { namespace UI {

namespace
{
/** 占位图案的灰阶（资源未就绪时的一致性缺省外观）。 */
const FUIColor kPlaceholderGray{ 0.5f, 0.5f, 0.5f, 0.35f };

bool NearlyEqual(float A, float B)
{
	return std::fabs(A - B) < 1e-4f;
}
} // namespace

// -- 坐标与颜色 --------------------------------------------------------------------------

ImVec2 FImGuiTranslator::ScreenMin(const FUIRect& Local) const
{
	return ImVec2(Origin.X + Local.X, Origin.Y + Local.Y);
}

ImVec2 FImGuiTranslator::ScreenMax(const FUIRect& Local) const
{
	return ImVec2(Origin.X + Local.X + Local.W, Origin.Y + Local.Y + Local.H);
}

FUIRect FImGuiTranslator::ToScreen(const FUIRect& Local) const
{
	return FUIRect{ Origin.X + Local.X, Origin.Y + Local.Y, Local.W, Local.H };
}

ImU32 FImGuiTranslator::ToColor(const FUIColor& C)
{
	return ImGui::GetColorU32(ImVec4(C.R, C.G, C.B, C.A));
}

// -- 视图进出 ----------------------------------------------------------------------------

void FImGuiTranslator::BeginView(FUIView& InView, const FUIRect& DisplayRect)
{
	View = &InView;
	TextureCache.clear();
	FontCache.clear();
	OriginStack.clear();
	DisabledDepth = 0;
	PendingFocusId = 0;   // 焦点请求只在提出它的那一帧有效，不留到下一帧

	// 原点 = 窗口内容区左上（面板路径含窗口内边距；叠加层路径内边距为 0）。
	Origin = FUIVector2{ ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y };
	Display = DisplayRect;
}

void FImGuiTranslator::EndView(FUIView& InView)
{
	(void)InView;
	while (DisabledDepth > 0)
	{
		ImGui::EndDisabled();
		--DisabledDepth;
	}
	OriginStack.clear();
	TextureCache.clear();
	FontCache.clear();
	View = nullptr;
}

void FImGuiTranslator::EnqueueEvent(FUIEventRecord Record)
{
	// 修饰键在入队这一刻采集：此刻的键盘状态正是命中那一刻的状态（树回调要跨帧才跑到）。
	const ImGuiIO& IO = ImGui::GetIO();
	if (IO.KeyShift) { Record.Modifiers = Record.Modifiers | EUIModifiers::Shift; }
	if (IO.KeyCtrl)  { Record.Modifiers = Record.Modifiers | EUIModifiers::Ctrl; }
	if (IO.KeyAlt)   { Record.Modifiers = Record.Modifiers | EUIModifiers::Alt; }

	// 翻译线程只入队：回调由视图所有者的 `DrainEvents()` 执行（跨线程写规则不破）。
	if (View != nullptr) { View->PushEvent(std::move(Record)); }
}

// -- 资源 --------------------------------------------------------------------------------

FUIResolvedResource FImGuiTranslator::ResolveFont(FUIName Font, float Size)
{
	const float Snapped = SnapFontSize(Size);
	const std::uint32_t Key = Font.GetId() ^ (static_cast<std::uint32_t>(Snapped * 16.f) << 20);
	if (const auto It = FontCache.find(Key); It != FontCache.end()) { return It->second; }

	FUIResolvedResource Out;
	Out.Name = Font;
	if (void* Native = FindUIFont(Context, Font, Snapped))
	{
		Out.bValid = true;
		Out.NativeHandle = reinterpret_cast<std::uintptr_t>(Native);
		FontCache.emplace(Key, Out);
		return Out;
	}

	// 未烘 / 未登记的字体：用后端缺省字体绘制，且只记一条诊断（不逐帧刷屏）。
	static bool bWarned = false;
	if (!bWarned)
	{
		bWarned = true;
		MAHO_IF_NOT_NULL(GetLog(), L) { L->Warn("UI: 字体引用未登记，回退缺省字体（图集按主题字体×档位启动烘制）"); }
	}
	Out.bValid = false;
	FontCache.emplace(Key, Out);
	return Out;
}

FUIResolvedResource FImGuiTranslator::ResolveTexture(FUIName Texture)
{
	if (Texture.IsNone()) { return FUIResolvedResource{}; }
	if (const auto It = TextureCache.find(Texture.GetId()); It != TextureCache.end())
	{
		return It->second;
	}
	FUIResolvedResource Out = ResolveUIResource(Texture, false);
	TextureCache.emplace(Texture.GetId(), Out);
	return Out;
}

ImFont* FImGuiTranslator::FontOf(const FUIResolvedResource& Font, float Size) const
{
	(void)Size;
	if (Font.bValid && Font.NativeHandle != 0)
	{
		return reinterpret_cast<ImFont*>(Font.NativeHandle);
	}
	return ImGui::GetFont();   // 缺省字体
}

// -- 测量 --------------------------------------------------------------------------------

FUIVector2 FImGuiTranslator::MeasureText(std::string_view Text, const FUIResolvedResource& Font, float Size)
{
	ImFont* F = FontOf(Font, Size);
	const float UseSize = (Font.bValid && Size > 0.f) ? SnapFontSize(Size) : ImGui::GetFontSize();
	const ImVec2 S = F->CalcTextSizeA(UseSize, FLT_MAX, 0.f, Text.data(), Text.data() + Text.size());
	return FUIVector2{ S.x, S.y };
}

FUIVector2 FImGuiTranslator::MeasureIcon(const FUIResolvedResource& Icon, float Size)
{
	if (!Icon.bValid)
	{
		return FUIVector2{ Size, Size };   // 占位方块按请求边长
	}
	const float W = static_cast<float>(Icon.Width > 0 ? Icon.Width : static_cast<std::uint32_t>(Size));
	const float H = static_cast<float>(Icon.Height > 0 ? Icon.Height : static_cast<std::uint32_t>(Size));
	const float Scale = (W > 0.f) ? (Size / W) : 1.f;
	return FUIVector2{ W * Scale, H * Scale };
}

// -- 基元 --------------------------------------------------------------------------------

void FImGuiTranslator::PushDisabled()
{
	ImGui::BeginDisabled(true);
	++DisabledDepth;
}

void FImGuiTranslator::PopDisabled()
{
	if (DisabledDepth > 0)
	{
		ImGui::EndDisabled();
		--DisabledDepth;
	}
}

void FImGuiTranslator::PushClip(const FUIRect& Rect)
{
	ImGui::PushClipRect(ScreenMin(Rect), ScreenMax(Rect), true);
}

void FImGuiTranslator::PopClip()
{
	ImGui::PopClipRect();
}

void FImGuiTranslator::DrawRect(const FUIRect& Rect, const FUIResolvedStyle& S)
{
	if (Rect.IsEmpty()) { return; }
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ScreenMin(Rect);
	const ImVec2 Max = ScreenMax(Rect);

	if (S.Fill.A > 0.f)
	{
		Draw->AddRectFilled(Min, Max, ToColor(S.Fill), S.Radius);
	}
	if (S.StrokeWidth > 0.f && S.Stroke.A > 0.f)
	{
		Draw->AddRect(Min, Max, ToColor(S.Stroke), S.Radius, ImDrawFlags_None, S.StrokeWidth);
	}
}

void FImGuiTranslator::DrawText(const FUIRect& Rect, std::string_view Text,
								const FUIResolvedResource& Font, float Size,
								const FUIResolvedStyle& S, EUITextAlign Align)
{
	if (Text.empty() || Rect.IsEmpty()) { return; }

	ImFont* F = FontOf(Font, Size);
	const float UseSize = (Font.bValid && Size > 0.f) ? SnapFontSize(Size) : ImGui::GetFontSize();
	const ImVec2 TextSize = F->CalcTextSizeA(UseSize, FLT_MAX, 0.f, Text.data(), Text.data() + Text.size());

	float X = Rect.X;
	if (Align == EUITextAlign::Center) { X += (Rect.W - TextSize.x) * 0.5f; }
	else if (Align == EUITextAlign::Right) { X += Rect.W - TextSize.x; }
	const float Y = Rect.Y + (Rect.H - TextSize.y) * 0.5f;

	ImDrawList* Draw = ImGui::GetWindowDrawList();
	Draw->PushClipRect(ScreenMin(Rect), ScreenMax(Rect), true);
	Draw->AddText(F, UseSize, ImVec2(Origin.X + X, Origin.Y + Y), ToColor(S.Text),
				  Text.data(), Text.data() + Text.size());
	Draw->PopClipRect();
}

void FImGuiTranslator::DrawIcon(const FUIRect& Rect, const FUIResolvedResource& Icon, const FUIResolvedStyle& S)
{
	if (Rect.IsEmpty()) { return; }
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ScreenMin(Rect);
	const ImVec2 Max = ScreenMax(Rect);

	if (Icon.bValid && Icon.NativeHandle != 0)
	{
		Draw->AddImage(static_cast<ImTextureID>(Icon.NativeHandle), Min, Max, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
					   ToColor(S.Text));
		return;
	}
	// 资源未就绪：占位方块（不阻塞布局，资源就绪后自动接续）
	FUIColor Placeholder = kPlaceholderGray;
	Placeholder.A *= S.Text.A;
	Draw->AddRectFilled(Min, Max, ToColor(Placeholder), 2.f);
}

void FImGuiTranslator::DrawImage(const FUIRect& Rect, const FUIResolvedResource& Texture,
								 const FUIColor& Tint, const FUIVector2& UV0, const FUIVector2& UV1)
{
	if (Rect.IsEmpty()) { return; }
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ScreenMin(Rect);
	const ImVec2 Max = ScreenMax(Rect);
	if (!Texture.bValid || Texture.NativeHandle == 0)
	{
		FUIColor Placeholder = kPlaceholderGray;
		Placeholder.A *= Tint.A;
		Draw->AddRectFilled(Min, Max, ToColor(Placeholder), 2.f);
		return;
	}
	Draw->AddImage(static_cast<ImTextureID>(Texture.NativeHandle), Min, Max, ImVec2(UV0.X, UV0.Y), ImVec2(UV1.X, UV1.Y),
				   ToColor(Tint));
}

// -- 命中 --------------------------------------------------------------------------------

FUIHitResult FImGuiTranslator::HitTestItem(FUIName Id, const FUIRect& Local, bool bHitTest,
										   bool bAllowHoverWhileActive)
{
	FUIHitResult Out;
	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Local));

	const float W = std::max(Local.W, 1.f);
	const float H = std::max(Local.H, 1.f);
	if (PendingFocusId == Id.GetId())
	{
		ImGui::SetKeyboardFocusHere();
		PendingFocusId = 0;
	}
	if (bHitTest)
	{
		ImGui::InvisibleButton("##hit", ImVec2(W, H));
		// 悬停位默认受"同窗口活跃项"过滤（见头文件说明）；扩选类控件要的是"指针在我上面"
		// 这个纯几何真值，故按需放行 —— 活跃项自己那条路不受影响。
		Out.bHovered = bAllowHoverWhileActive
			? ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
			: ImGui::IsItemHovered();
		Out.bPressed = ImGui::IsItemActive();
		Out.bClicked = ImGui::IsItemClicked();
		Out.bReleased = ImGui::IsItemDeactivated();
		Out.bDragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f);
		if (ImGui::IsItemActive()) { FocusedId = Id.GetId(); }
	}
	else
	{
		ImGui::Dummy(ImVec2(W, H));
	}
	ImGui::PopID();
	if (bDebugDraw && (Out.bHovered || Out.bPressed))
	{
		const FUIColor DebugColor = Out.bPressed ? FUIColor{ 1.f, 0.4f, 0.2f, 1.f } : FUIColor{ 0.2f, 1.f, 0.6f, 1.f };
		DebugDrawRect(Local, DebugColor);
	}
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetButton(FUIName Id, const FUIRect& Rect, const FUIResolvedStyle& S)
{
	const FUIHitResult Out = HitTestItem(Id, Rect, true);
	DrawRect(Rect, S);
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetCheckbox(FUIName Id, const FUIRect& Rect, bool& bValue,
											  const FUIResolvedStyle& S)
{
	const FUIHitResult Out = HitTestItem(Id, Rect, true);
	if (Out.bClicked) { bValue = !bValue; }
	DrawRect(Rect, S);
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetSliderFloat(FUIName Id, const FUIRect& Rect, float& Value,
												 float Min, float Max, std::string_view Format,
												 const FUIResolvedStyle& S)
{
	(void)S;
	FUIHitResult Out;
	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Rect));
	ImGui::SetNextItemWidth(std::max(Rect.W, 1.f));
	const std::string Fmt = Format.empty() ? std::string("%.3f") : std::string(Format);
	const float Old = Value;
	if (ImGui::SliderFloat("##slider", &Value, Min, Max, Fmt.c_str()))
	{
		Out.bClicked = true;
	}
	Out.bHovered = ImGui::IsItemHovered();
	Out.bPressed = ImGui::IsItemActive();
	if (!NearlyEqual(Old, Value)) { Out.bDragging = ImGui::IsItemActive(); }
	ImGui::PopID();
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetInputText(FUIName Id, const FUIRect& Rect, std::string& Text,
											   std::string_view Hint, std::size_t MaxLength,
											   bool bMultiline, const FUIResolvedStyle& S)
{
	(void)S;
	FUIHitResult Out;
	std::vector<char> Buffer(1024, '\0');
	const std::size_t CopyLen = std::min(Text.size(), Buffer.size() - 1);
	std::memcpy(Buffer.data(), Text.data(), CopyLen);

	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Rect));
	// 键盘焦点请求（`FUIBuilder::RequestKeyboardFocus`）：真值与 `HitTestItem` 同一约定 ——
	// 只在提出它的那一帧有效，且必须**紧贴**下一个 item 之前发出（否则焦点落到别的控件上）。
	// 输入框是唯一"焦点即生命周期"的控件：不消费这个请求，编辑器"选完候选把焦点交回输入框"
	// 的路径就是死代码（旧版靠 `IsItemActive()` 回读，新树不逐帧回写活跃位）。
	if (PendingFocusId == Id.GetId())
	{
		ImGui::SetKeyboardFocusHere();
		PendingFocusId = 0;
	}
	bool bEnter = false;
	if (bMultiline)
	{
		ImGui::InputTextMultiline("##text", Buffer.data(), Buffer.size(),
								  ImVec2(std::max(Rect.W, 1.f), std::max(Rect.H, 1.f)));
	}
	else
	{
		const std::string HintCopy(Hint);
		ImGui::SetNextItemWidth(std::max(Rect.W, 1.f));
		// 回车提交：返回值**只**指回车（文本改动由下面的内容比较判定），两者可同帧并发。
		bEnter = ImGui::InputTextWithHint("##text", HintCopy.c_str(), Buffer.data(), Buffer.size(),
										  ImGuiInputTextFlags_EnterReturnsTrue);
	}
	Out.bHovered = ImGui::IsItemHovered();
	Out.bPressed = ImGui::IsItemActive();
	if (ImGui::IsItemActive()) { FocusedId = Id.GetId(); }

	std::string NewText(Buffer.data());
	if (MaxLength > 0 && NewText.size() > MaxLength) { NewText.resize(MaxLength); }
	if (NewText != Text)
	{
		Text = std::move(NewText);
		Out.bClicked = true;
	}
	if (bEnter) { Out.bSubmitted = true; }
	ImGui::PopID();
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetSelectable(FUIName Id, const FUIRect& Rect, bool bSelected,
												const FUIResolvedStyle& S)
{
	const FUIHitResult Out = HitTestItem(Id, Rect, true, /*bAllowHoverWhileActive*/ true);
	DrawRect(Rect, S);
	(void)bSelected;
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetDragFloat(FUIName Id, const FUIRect& Rect, float* Values,
											   int Components, float Speed, std::string_view Format,
											   const FUIResolvedStyle& S)
{
	(void)S;
	FUIHitResult Out;
	if (Values == nullptr || Components < 1) { return Out; }

	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Rect));
	ImGui::SetNextItemWidth(std::max(Rect.W, 1.f));
	const std::string Fmt = Format.empty() ? std::string("%.3f") : std::string(Format);
	bool bChanged = false;
	// v_min == v_max：不夹取（与 ImGui 的 DragFloatN 语义一致：只受 Speed 影响）。
	switch (Components)
	{
	case 2:  bChanged = ImGui::DragFloat2("##drag", Values, Speed, 0.f, 0.f, Fmt.c_str()); break;
	case 3:  bChanged = ImGui::DragFloat3("##drag", Values, Speed, 0.f, 0.f, Fmt.c_str()); break;
	case 4:  bChanged = ImGui::DragFloat4("##drag", Values, Speed, 0.f, 0.f, Fmt.c_str()); break;
	default: bChanged = ImGui::DragFloat("##drag", Values, Speed, 0.f, 0.f, Fmt.c_str()); break;
	}
	Out.bHovered = ImGui::IsItemHovered();
	Out.bPressed = ImGui::IsItemActive();
	if (bChanged) { Out.bClicked = true; }
	if (ImGui::IsItemActive()) { FocusedId = Id.GetId(); }
	ImGui::PopID();
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetColorEdit(FUIName Id, const FUIRect& Rect, float* RGBA,
											   const FUIResolvedStyle& S)
{
	(void)S;
	FUIHitResult Out;
	if (RGBA == nullptr) { return Out; }

	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Rect));
	ImGui::SetNextItemWidth(std::max(Rect.W, 1.f));
	const bool bChanged = ImGui::ColorEdit4("##color", RGBA, ImGuiColorEditFlags_AlphaBar);
	Out.bHovered = ImGui::IsItemHovered();
	Out.bPressed = ImGui::IsItemActive();
	if (bChanged) { Out.bClicked = true; }
	if (ImGui::IsItemActive()) { FocusedId = Id.GetId(); }
	ImGui::PopID();
	return Out;
}

FUIHitResult FImGuiTranslator::WidgetCollapsingHeader(FUIName Id, const FUIRect& Rect, bool& bOpen,
													  const FUIResolvedStyle& S)
{
	FUIHitResult Out;
	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Rect));
	if (bOpen) { ImGui::SetNextItemOpen(true, ImGuiCond_Always); }
	ImGui::CollapsingHeader("##header", bOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
	Out.bHovered = ImGui::IsItemHovered();
	Out.bPressed = ImGui::IsItemActive();
	if (ImGui::IsItemClicked()) { Out.bClicked = true; }
	if (ImGui::IsItemToggledOpen()) { bOpen = !bOpen; }
	ImGui::PopID();
	DrawRect(Rect, S);
	return Out;
}

// -- 滚动 --------------------------------------------------------------------------------

bool FImGuiTranslator::BeginScrollRegion(FUIName Id, const FUIRect& Rect, const FUIScrollRequest& Request)
{
	ImGui::PushID(static_cast<int>(Id.GetId()));
	ImGui::SetCursorScreenPos(ScreenMin(Rect));
	const bool bVisible = ImGui::BeginChild("##scroll", ImVec2(std::max(Rect.W, 1.f), std::max(Rect.H, 1.f)),
											ImGuiChildFlags_None, ImGuiWindowFlags_None);
	// 区域内原点 = 子窗口内容起点（滚动量已由 ImGui 计进内容偏移）
	OriginStack.push_back(Origin);
	Origin = FUIVector2{ ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y };
	ScrollStack.push_back(Request);   // 贴底要等内容摆完再请求，留到 EndScrollRegion
	(void)bVisible;
	return true;
}

FImGuiTranslator::FUIScrollInfo FImGuiTranslator::EndScrollRegion()
{
	const FUIScrollRequest Request = ScrollStack.empty() ? FUIScrollRequest{} : ScrollStack.back();
	if (!ScrollStack.empty()) { ScrollStack.pop_back(); }
	// 内容已摆完：此刻才知道可滚动范围，贴底/置量才有效。
	if (Request.bToBottom) { ImGui::SetScrollHereY(1.f); }
	else if (Request.bSetScrollY) { ImGui::SetScrollY(Request.ScrollY); }

	FUIScrollInfo Info;
	Info.ScrollY = ImGui::GetScrollY();
	Info.ScrollMaxY = ImGui::GetScrollMaxY();
	ImGui::EndChild();
	if (!OriginStack.empty())
	{
		Origin = OriginStack.back();
		OriginStack.pop_back();
	}
	ImGui::PopID();
	return Info;
}

// -- 弹出层 ------------------------------------------------------------------------------

bool FImGuiTranslator::BeginTooltip(FUIName Id, const FUIRect& Anchor, bool bFollowMouse)
{
	(void)Id;
	if (bFollowMouse)
	{
		const ImVec2 Mouse = ImGui::GetIO().MousePos;
		ImGui::SetNextWindowPos(ImVec2(Mouse.x + 16.f, Mouse.y + 16.f));
	}
	else
	{
		ImGui::SetNextWindowPos(ScreenMin(Anchor));
	}
	ImGui::BeginTooltip();
	// 提示窗是独立窗口：原点切到它的内容区，出栈时恢复（与弹层同一约定）。
	OriginStack.push_back(Origin);
	Origin = FUIVector2{ ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y };
	return true;
}

void FImGuiTranslator::EndTooltip()
{
	ImGui::EndTooltip();
	if (!OriginStack.empty())
	{
		Origin = OriginStack.back();
		OriginStack.pop_back();
	}
}

bool FImGuiTranslator::BeginPopup(FUIName Id, bool bOpen, const FUIRect& Anchor, bool bModal)
{
	const std::string Name = "##uiPopup" + std::to_string(Id.GetId());
	const bool bWasOpen = OpenPopups[Id.GetId()];
	if (bOpen && !bWasOpen) { ImGui::OpenPopup(Name.c_str()); }
	OpenPopups[Id.GetId()] = bOpen;

	// 锚点摆放：默认贴锚点**下沿**（自上而下生长），上一帧实测高度放不下就翻到锚点**上沿**。
	// 必须自己翻：`SetNextWindowPos` 一旦被调用，ImGui 就不再跑它那套自动翻转策略，
	// 于是贴着屏幕底部停靠的面板里，候选列表整条长到显示区之外（只看得见最下面一条）。
	if (Anchor.W > 0.f || Anchor.H > 0.f)
	{
		const ImVec2 Min = ScreenMin(Anchor);
		const float EstH = PopupHeights[Id.GetId()];
		float Y = Min.y + Anchor.H;
		if (EstH > 0.f)
		{
			// 宿主窗口底边（`GetCurrentWindow` 属 imgui_internal，翻译器只走公开 API）
			const float HostBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			// 翻上去也不能翻出主视口（超出就宁可往下溢：至少顶部那几行在读）
			const float Above = Min.y - EstH;
			if (Y + EstH > HostBottom && Above >= ImGui::GetMainViewport()->WorkPos.y) { Y = Above; }
		}
		ImGui::SetNextWindowPos(ImVec2(Min.x, Y));
	}
	// 非模态弹层**不许抢焦点**：ImGui 的弹窗在刚出现那一帧会 `want_focus=true`（除非显式
	// 声明 `NoFocusOnAppearing`），抢焦点的副作用是 `ClearActiveID` —— 被它盖住的那个输入框
	// （弹层通常正是贴着某个输入框弹出的候选列表）当帧就丢了 ActiveId，于是"能输入/能框选"
	// 的前置条件（`g.ActiveId == id`）每帧被打断：表现为候选列表一闪而过、按键进不去。
	// 模态弹层保持抢焦点（那正是模态的语义）。
	//
	// 非模态弹层的置顶不是"挂到哪个宿主名下"能解决的：贴着命令行弹出的候选列表常常翻到宿主
	// 矩形**之外**（宿主矮、候选项多），那一段被谁盖住只取决于**宿主的画序** —— 宿主一被聚焦
	// （打字时它一直是活跃窗口）就整棵子树被 `BringWindowToDisplayFront` 甩到兄弟面板后面，
	// 挂成它的子窗口也一样被盖。故弹层保持顶层窗口，并在每帧 `Begin` 之后把它自己挪到画序
	// 末尾（见下方调用）。顶层弹层的裁剪框是视口（`Begin` 里 `host_rect` 只对非弹层的子窗口
	// 取宿主矩形），故翻到宿主矩形之外也不会被裁。
	const ImGuiWindowFlags PopupFlags = bModal
		? ImGuiWindowFlags_AlwaysAutoResize
		: (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
	const bool bShown = bModal ? ImGui::BeginPopupModal(Name.c_str(), nullptr, PopupFlags)
							   : ImGui::BeginPopup(Name.c_str(), PopupFlags);
	if (!bShown) { return false; }

	// 每帧重新置顶：宿主（贴着弹层的那个面板）每帧都可能被聚焦而排到画序末尾，弹层必须重新
	// 压回去。公开 API 里"只置顶、不动焦点"是缺的（`SetNextWindowFocus`/`SetWindowFocus` 都走
	// `FocusWindow`，顺手 `ClearActiveID()` 清掉输入框的活跃位 —— 候选项闪烁的老病），故这一处
	// 破例走内部原语：把窗口挪到 `g.Windows` 末尾 = 本帧最后画，且鼠标命中优先它。
	if (!bModal)
	{
		// 取当前窗口不能走内联的 `GetCurrentWindow()`：它直接引 `GImGui` 这个**数据**符号，而
		// `WINDOWS_EXPORT_ALL_SYMBOLS` 只导函数不导数据，跨 DLL 链不过（LNK2001）。公开的
		// `GetCurrentContext()` 是导出函数，`CurrentWindow` 只是结构体字段访问（无符号需求）。
		if (ImGuiContext* Ctx = ImGui::GetCurrentContext(); Ctx != nullptr && Ctx->CurrentWindow != nullptr)
		{
			ImGui::BringWindowToDisplayFront(Ctx->CurrentWindow);
		}
	}

	// 本帧实测高度：下一帧贴合翻转的估计值（弹层窗口已是当前窗口，直接量它）
	PopupHeights[Id.GetId()] = ImGui::GetWindowSize().y;

	// 业务把 bOpen 置回 false：本帧收窗（不然后端窗口会一直挂着）
	if (!bOpen) { ImGui::CloseCurrentPopup(); }

	// 弹层是新窗口：原点切到它的内容区，出栈时恢复。
	OriginStack.push_back(Origin);
	Origin = FUIVector2{ ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y };
	bPopupOpen = true;
	return true;
}

void FImGuiTranslator::EndPopup()
{
	ImGui::EndPopup();
	if (!OriginStack.empty())
	{
		Origin = OriginStack.back();
		OriginStack.pop_back();
	}
	bPopupOpen = false;
}

// -- 拖放 --------------------------------------------------------------------------------

bool FImGuiTranslator::BeginDragSource(FUIName Id, std::string_view PayloadType, std::string_view Payload,
									   std::string_view PreviewText)
{
	(void)Id;
	if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) { return false; }
	const std::string Type(PayloadType);
	ImGui::SetDragDropPayload(Type.c_str(), Payload.data(), Payload.size());
	if (!PreviewText.empty()) { ImGui::TextUnformatted(PreviewText.data(), PreviewText.data() + PreviewText.size()); }
	return true;
}

void FImGuiTranslator::EndDragSource()
{
	ImGui::EndDragDropSource();
}

bool FImGuiTranslator::IsDropTarget(FUIName Id, std::string_view PayloadType, std::string* OutPayload)
{
	(void)Id;
	if (!ImGui::BeginDragDropTarget()) { return false; }
	bool bAccepted = false;
	const std::string Type(PayloadType);
	if (const ImGuiPayload* P = ImGui::AcceptDragDropPayload(Type.c_str()))
	{
		if (OutPayload != nullptr)
		{
			OutPayload->assign(static_cast<const char*>(P->Data), static_cast<std::size_t>(P->DataSize));
		}
		bAccepted = true;
	}
	ImGui::EndDragDropTarget();
	return bAccepted;
}

// -- 焦点与调试 --------------------------------------------------------------------------

void FImGuiTranslator::SetKeyboardFocus(FUIName Id)
{
	PendingFocusId = Id.GetId();
}

bool FImGuiTranslator::HasFocus(FUIName Id) const
{
	return FocusedId == Id.GetId();
}

bool FImGuiTranslator::IsShortcutPressed(const FUIKeyChord& Chord)
{
	if (Chord.IsNone()) { return false; }

	// 两道守卫：键盘焦点必须在本视图窗口（含子窗口 —— 日志主体/滚动区都是子窗口），
	// 且当前没有输入框在收键盘。少了后者，在 Cvar/过滤框里敲 "c" 就命中 Ctrl+C。
	if (ImGui::GetIO().WantTextInput) { return false; }
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) { return false; }

	// 单字符键：A-Z / 0-9（ImGui 的字母键与数字键是连续的，直接偏移即可）。
	ImGuiKey ChordKey = ImGuiKey_None;
	const char C = Chord.Key;
	if (C >= 'a' && C <= 'z')      { ChordKey = static_cast<ImGuiKey>(ImGuiKey_A + (C - 'a')); }
	else if (C >= 'A' && C <= 'Z') { ChordKey = static_cast<ImGuiKey>(ImGuiKey_A + (C - 'A')); }
	else if (C >= '0' && C <= '9') { ChordKey = static_cast<ImGuiKey>(ImGuiKey_0 + (C - '0')); }
	else                           { return false; }

	ImGuiKeyChord Full = ChordKey;
	if (HasModifier(Chord.Mods, EUIModifiers::Ctrl))  { Full |= ImGuiMod_Ctrl; }
	if (HasModifier(Chord.Mods, EUIModifiers::Shift)) { Full |= ImGuiMod_Shift; }
	if (HasModifier(Chord.Mods, EUIModifiers::Alt))   { Full |= ImGuiMod_Alt; }

	// `IsKeyChordPressed` 是**精确**修饰键匹配（多按一个 Shift 即不命中）—— 这是 ImGui 的既定
	// 语义，与它的其它快捷键一致，故不放宽。
	return ImGui::IsKeyChordPressed(Full);
}

void FImGuiTranslator::DebugDrawRect(const FUIRect& Rect, const FUIColor& C)
{
	ImGui::GetWindowDrawList()->AddRect(ScreenMin(Rect), ScreenMax(Rect), ToColor(C));
}

}} // namespace Maho::UI
