#include "UIImGuiTranslator.h"

#include <UITranslate.h>
#include <UIResource.h>
#include <UITheme.h>
#include <UIViewRegistry.h>

#include <cstdint>
#include <string>

namespace Maho { namespace UI {

namespace
{
ImGuiWindowFlags ToImGuiWindowFlags(EUIShellFlags Flags)
{
	ImGuiWindowFlags Out = ImGuiWindowFlags_None;
	if (HasFlag(Flags, EUIShellFlags::NoCollapse)) { Out |= ImGuiWindowFlags_NoCollapse; }
	if (HasFlag(Flags, EUIShellFlags::NoMove)) { Out |= ImGuiWindowFlags_NoMove; }
	if (HasFlag(Flags, EUIShellFlags::NoResize)) { Out |= ImGuiWindowFlags_NoResize; }
	if (HasFlag(Flags, EUIShellFlags::NoScrollbar)) { Out |= ImGuiWindowFlags_NoScrollbar; }
	if (HasFlag(Flags, EUIShellFlags::NoTitleBar)) { Out |= ImGuiWindowFlags_NoTitleBar; }
	if (HasFlag(Flags, EUIShellFlags::NoSavedSettings)) { Out |= ImGuiWindowFlags_NoSavedSettings; }
	return Out;
}

/** 叠加层根（无窗口壳）：铺满视口、无背景无装饰，内容区 == 窗口区。 */
constexpr ImGuiWindowFlags kOverlayRootFlags =
	ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
	ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings
#ifdef IMGUI_HAS_DOCK
	| ImGuiWindowFlags_NoDocking     // 叠加层绝不能被宿主 dockspace 吸走
#endif
	;

std::string WindowNameOf(const FUIView& View, const FUIViewShell& Shell)
{
	// `###` 后的部分才是 ImGui 的身份键：同标题的不同视图不会互相串状态。
	std::string Name = Shell.bEnabled && !Shell.Title.empty() ? Shell.Title : std::string("##view");
	Name += "###uiView";
	Name += std::to_string(View.GetId().GetId());
	return Name;
}

void TranslateViewImpl(FUIView& View, const FUIRect& LocalRect, bool bDebugDraw)
{
	ImGuiContext* Context = ImGui::GetCurrentContext();
	if (Context == nullptr) { return; }

	FImGuiTranslator T(Context);
	T.bDebugDraw = bDebugDraw;

	// 树锁在翻译期间只读持有：所有者线程修改树会阻塞到本帧翻译结束（不阻塞渲染循环）。
	const std::shared_lock<std::shared_mutex> Lock(View.GetTreeMutex());
	T.BeginView(View, LocalRect);
	View.GetRoot().Translate(T, LocalRect);
	T.EndView(View);
}
} // namespace

void TranslateView(FUIView& View)
{
	const FUIVector2 Size = View.GetDisplaySize();
	TranslateViewImpl(View, FUIRect{ 0.f, 0.f, Size.X, Size.Y }, false);
}

std::uint32_t TranslateRegisteredViews(const FUIViewFrameDesc& Desc)
{
	FUIViewRegistry* Registry = GetUIViewRegistry();
	if (Registry == nullptr) { return 0; }

	if (Desc.ImGuiContext != nullptr)
	{
		ImGui::SetCurrentContext(static_cast<ImGuiContext*>(Desc.ImGuiContext));
	}
	if (ImGui::GetCurrentContext() == nullptr) { return 0; }

	std::uint32_t Translated = 0;
	for (FUIView* View : Registry->SnapshotViews())
	{
		if (View == nullptr) { continue; }
		if (!Desc.OnlyView.IsNone() && View->GetId() != Desc.OnlyView) { continue; }
		// 上下文筛选：视图登记了自己的目标上下文（编辑器 / 游戏各一个），只翻译匹配的那些。
		// 未登记（nullptr）= 与任何上下文兼容（无竞争的单一 UI 场景）。
		if (View->GetRenderContext() != nullptr && View->GetRenderContext() != Desc.ImGuiContext) { continue; }

		const FUIViewShell& Shell = View->GetWindowShell();
		const std::string Name = WindowNameOf(*View, Shell);
		bool bOpen = true;

		if (Shell.bEnabled)
		{
			// 显示区（比例锚点用）：宿主注入的视口尺寸，缺省回退到视图自己的记录值。
			const float DisplayW = Desc.DisplayWidth > 0.f ? Desc.DisplayWidth : View->GetDisplaySize().X;
			const float DisplayH = Desc.DisplayHeight > 0.f ? Desc.DisplayHeight : View->GetDisplaySize().Y;

			if (Desc.DockSpaceId != 0)
			{
				ImGui::SetNextWindowDockID(static_cast<ImGuiID>(Desc.DockSpaceId), ImGuiCond_FirstUseEver);
			}
			// 位置：比例锚点优先（首次生效，其后拖动/停靠白拿），否则用像素首次位置。
			if (Shell.PosFraction.X > 0.f || Shell.PosFraction.Y > 0.f)
			{
				ImGui::SetNextWindowPos(
					ImVec2(Shell.PosFraction.X * DisplayW, Shell.PosFraction.Y * DisplayH),
					ImGuiCond_FirstUseEver);
			}
			else if (Shell.DefaultPos.X != 0.f || Shell.DefaultPos.Y != 0.f)
			{
				ImGui::SetNextWindowPos(ImVec2(Shell.DefaultPos.X, Shell.DefaultPos.Y), ImGuiCond_FirstUseEver);
			}
			// 尺寸：比例锚点每帧生效（HUD 随显示区缩放），否则只在首次生效。
			if (Shell.SizeFraction.X > 0.f && Shell.SizeFraction.Y > 0.f)
			{
				ImGui::SetNextWindowSize(
					ImVec2(Shell.SizeFraction.X * DisplayW, Shell.SizeFraction.Y * DisplayH),
					ImGuiCond_Always);
			}
			else if (Shell.DefaultSize.X > 0.f && Shell.DefaultSize.Y > 0.f)
			{
				ImGui::SetNextWindowSize(ImVec2(Shell.DefaultSize.X, Shell.DefaultSize.Y), ImGuiCond_FirstUseEver);
			}
			const ImGuiWindowFlags Flags = ToImGuiWindowFlags(Shell.Flags);
			if (ImGui::Begin(Name.c_str(), &bOpen, Flags))
			{
				const ImVec2 Content = ImGui::GetContentRegionAvail();
				View->SetDisplaySize(Content.x, Content.y);
				TranslateViewImpl(*View, FUIRect{ 0.f, 0.f, Content.x, Content.y }, Desc.bDrawDebug);
				++Translated;
			}
			ImGui::End();
			if (!bOpen) { View->RequestCloseWindow(); }
			continue;
		}

		// 叠加层（HUD 更稳）：无窗口壳时铺满视口，内容区从 (0,0) 起 —— 局部坐标即屏幕坐标。
		const float W = Desc.DisplayWidth > 0.f ? Desc.DisplayWidth : View->GetDisplaySize().X;
		const float H = Desc.DisplayHeight > 0.f ? Desc.DisplayHeight : View->GetDisplaySize().Y;
		ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
		ImGui::SetNextWindowSize(ImVec2(W, H));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		if (ImGui::Begin(Name.c_str(), nullptr, kOverlayRootFlags))
		{
			View->SetDisplaySize(W, H);
			TranslateViewImpl(*View, FUIRect{ 0.f, 0.f, W, H }, Desc.bDrawDebug);
			++Translated;
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	return Translated;
}

void BakeUIThemeFonts()
{
	ImGuiContext* Context = ImGui::GetCurrentContext();
	if (Context == nullptr) { return; }

	const FUITheme& Theme = GetUITheme();
	ImFontAtlas* Atlas = ImGui::GetIO().Fonts;
	if (Atlas == nullptr) { return; }

	// 一次性全烘：档位集合里每一档各进一条图集条目（运行期不再改图集）。
	for (const float Step : Theme.FontSizeSteps)
	{
		if (Step <= 0.f) { continue; }
		// 主题字体为 None 时用后端缺省字体；位图字体按像素对齐烘制（与 `AddFontDefault()` 的缺省一致）。
		ImFontConfig Config;
		Config.SizePixels = Step;
		Config.OversampleH = 1;
		Config.OversampleV = 1;
		Config.PixelSnapH = true;
		if (ImFont* Baked = Atlas->AddFontDefault(&Config))
		{
			RegisterUIFont(Context, Theme.Font, Step, Baked);
		}
	}
}

void ApplyThemeToImGuiStyle()
{
	if (ImGui::GetCurrentContext() == nullptr) { return; }

	const FUITheme& Theme = GetUITheme();
	const auto Color = [](const FUIColor& C)
	{ return ImVec4(C.R, C.G, C.B, C.A); };

	ImGuiStyle& S = ImGui::GetStyle();
	S.WindowRounding = Theme.Radius;
	S.FrameRounding = Theme.Radius;
	S.GrabRounding = Theme.Radius;
	S.PopupRounding = Theme.Radius;
	S.ChildRounding = Theme.Radius;
	S.ScrollbarRounding = Theme.Radius;
	S.FrameBorderSize = Theme.StrokeWidth;

	S.Colors[ImGuiCol_WindowBg] = Color(Theme.PanelFill);
	S.Colors[ImGuiCol_ChildBg] = Color(FUIColor{ 0.f, 0.f, 0.f, 0.f });
	S.Colors[ImGuiCol_PopupBg] = Color(Theme.PanelFill);
	S.Colors[ImGuiCol_Border] = Color(Theme.Border);
	S.Colors[ImGuiCol_Separator] = Color(Theme.Separator);
	S.Colors[ImGuiCol_FrameBg] = Color(Theme.ControlFill);
	S.Colors[ImGuiCol_FrameBgHovered] = Color(Theme.ControlHover);
	S.Colors[ImGuiCol_FrameBgActive] = Color(Theme.ControlPress);
	S.Colors[ImGuiCol_Button] = Color(Theme.ControlFill);
	S.Colors[ImGuiCol_ButtonHovered] = Color(Theme.ControlHover);
	S.Colors[ImGuiCol_ButtonActive] = Color(Theme.ControlPress);
	S.Colors[ImGuiCol_Header] = Color(Theme.ControlFill);
	S.Colors[ImGuiCol_HeaderHovered] = Color(Theme.ControlHover);
	S.Colors[ImGuiCol_HeaderActive] = Color(Theme.ControlPress);
	S.Colors[ImGuiCol_CheckMark] = Color(Theme.Accent);
	S.Colors[ImGuiCol_SliderGrab] = Color(Theme.Accent);
	S.Colors[ImGuiCol_SliderGrabActive] = Color(Theme.Accent);
	S.Colors[ImGuiCol_Text] = Color(Theme.Text);
	S.Colors[ImGuiCol_TextDisabled] = Color(Theme.TextDisabled);
	S.Colors[ImGuiCol_ScrollbarBg] = Color(FUIColor{ 0.f, 0.f, 0.f, 0.f });
	S.Colors[ImGuiCol_ScrollbarGrab] = Color(Theme.ControlHover);
	S.Colors[ImGuiCol_ScrollbarGrabHovered] = Color(Theme.ControlPress);
	S.Colors[ImGuiCol_ScrollbarGrabActive] = Color(Theme.Accent);
	S.Colors[ImGuiCol_TitleBg] = Color(Theme.PanelFill);
	S.Colors[ImGuiCol_TitleBgActive] = Color(Theme.PanelStroke);
	S.Colors[ImGuiCol_ResizeGrip] = Color(Theme.ControlFill);
	S.Colors[ImGuiCol_ResizeGripHovered] = Color(Theme.ControlHover);
	S.Colors[ImGuiCol_ResizeGripActive] = Color(Theme.Accent);
}

}} // namespace Maho::UI
