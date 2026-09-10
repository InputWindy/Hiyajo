#include "UIStyleResolver.h"

#include <UITheme.h>

namespace Maho { namespace UI {

namespace
{
/** 逐项回退：实例[State] → 实例[Normal] → 父已解析值 → 类型默认[State] → 类型默认[Normal] → 主题 token。 */
template <typename T, typename TMember>
void ResolveItem(T& Out, TMember FUIStateStyle::* Member, EUIState State,
				 const FUIStateStyle& InstanceState, const FUIStateStyle& InstanceNormal,
				 const FUIStateStyle* TypeState, const FUIStateStyle* TypeNormal,
				 const T* Inherited, const T* Theme)
{
	if (const auto& V = InstanceState.*Member) { Out = *V; return; }
	if (const auto& V = InstanceNormal.*Member) { Out = *V; return; }
	if (Inherited) { Out = *Inherited; return; }
	if (TypeState)
	{
		if (const auto& V = (*TypeState).*Member) { Out = *V; return; }
	}
	if (TypeNormal)
	{
		if (const auto& V = (*TypeNormal).*Member) { Out = *V; return; }
	}
	if (Theme) { Out = *Theme; }
}

/** 资源引用项（Font/Icon）多一级：来源内的"五态快捷槽"。 */
template <typename TMember>
void ResolveResourceItem(FUIName& Out, TMember FUIStateStyle::* Member, EUIState State,
						 const FUIStyle& Instance, const FUIStyle& Type,
						 const std::optional<FUIName>& InstanceShortcut,
						 const std::optional<FUIName>& TypeShortcut,
						 const FUIName* Inherited, const FUIName* Theme)
{
	if (const auto& V = Instance[State].*Member) { Out = *V; return; }
	if (const auto& V = Instance[EUIState::Normal].*Member) { Out = *V; return; }
	if (InstanceShortcut) { Out = *InstanceShortcut; return; }
	if (Inherited) { Out = *Inherited; return; }
	if (const auto& V = Type[State].*Member) { Out = *V; return; }
	if (const auto& V = Type[EUIState::Normal].*Member) { Out = *V; return; }
	if (TypeShortcut) { Out = *TypeShortcut; return; }
	if (Theme && !Theme->IsNone()) { Out = *Theme; }
}
} // namespace

FUIResolvedStyle FUIStyleResolver::Resolve(const FUIBuilder& Node, const FUIResolvedStyle* ParentResolved)
{
	const EUIState State = Node.GetVisualState();
	const FUIStyle& Instance = Node.GetStyle();
	const FUIStyle& Type = Node.GetTypeDefaultStyle();
	const FUITheme& Theme = GetUITheme();

	const FUIStateStyle& InstanceState = Instance[State];
	const FUIStateStyle& InstanceNormal = Instance[EUIState::Normal];
	const FUIStateStyle& TypeState = Type[State];
	const FUIStateStyle& TypeNormal = Type[EUIState::Normal];

	FUIResolvedStyle Out;
	Out.State = State;

	// 自绘属性：不继承（继承会让子容器套上父容器底色）
	const FUIColor*     NoColor = nullptr;
	const float*        NoFloat = nullptr;
	const FMargin*      NoMargin = nullptr;

	const FUIColor& ThemeText = (State == EUIState::Disabled) ? Theme.TextDisabled : Theme.Text;
	const float*    ThemeFontSize = &Theme.FontSize;
	const float*    ThemeRadius = &Theme.Radius;
	const float*    ThemeStrokeWidth = &Theme.StrokeWidth;
	const FUIName*  ThemeFont = &Theme.Font;
	const FUIName*  NoName = nullptr;

	ResolveItem(Out.Fill, &FUIStateStyle::Fill, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, NoColor, NoColor);
	ResolveItem(Out.Stroke, &FUIStateStyle::Stroke, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, NoColor, NoColor);
	ResolveItem(Out.StrokeWidth, &FUIStateStyle::StrokeWidth, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, NoFloat, ThemeStrokeWidth);
	ResolveItem(Out.Radius, &FUIStateStyle::Radius, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, NoFloat, ThemeRadius);
	ResolveItem(Out.Padding, &FUIStateStyle::Padding, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, NoMargin, NoMargin);

	// 前景属性：逐级继承
	const FUIResolvedStyle* P = ParentResolved;
	ResolveItem(Out.Text, &FUIStateStyle::Text, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, P ? &P->Text : nullptr, &ThemeText);
	ResolveItem(Out.FontSize, &FUIStateStyle::FontSize, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, P ? &P->FontSize : nullptr, ThemeFontSize);
	ResolveItem(Out.IconSize, &FUIStateStyle::IconSize, State, InstanceState, InstanceNormal,
				&TypeState, &TypeNormal, P ? &P->IconSize : nullptr, NoFloat);

	ResolveResourceItem(Out.Font, &FUIStateStyle::Font, State, Instance, Type,
						Instance.Font, Type.Font, P ? &P->Font : nullptr, ThemeFont);
	ResolveResourceItem(Out.Icon, &FUIStateStyle::Icon, State, Instance, Type,
						Instance.Icon, Type.Icon, P ? &P->Icon : nullptr, NoName);

	return Out;
}

}} // namespace Maho::UI
