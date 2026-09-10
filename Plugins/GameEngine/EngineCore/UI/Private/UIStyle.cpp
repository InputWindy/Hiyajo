#include <UIStyle.h>

namespace Maho { namespace UI {

FUIColor FUIColor::Hex(std::uint32_t RGB, float InA)
{
	return {
		static_cast<float>((RGB >> 16) & 0xFFu) / 255.f,
		static_cast<float>((RGB >> 8) & 0xFFu) / 255.f,
		static_cast<float>(RGB & 0xFFu) / 255.f,
		InA
	};
}

FUIColor FUIColor::WithAlpha(float InA) const
{
	return { R, G, B, InA };
}

FUIColor FUIColor::Lerp(const FUIColor& O, float T) const
{
	return {
		R + (O.R - R) * T,
		G + (O.G - G) * T,
		B + (O.B - B) * T,
		A + (O.A - A) * T
	};
}

void FUIStyle::SetFillAll(FUIColor C)
{
	for (auto& S : States) { S.Fill = C; }
}

void FUIStyle::SetTextAll(FUIColor C)
{
	for (auto& S : States) { S.Text = C; }
}

void FUIStyle::SetPaddingAll(FMargin M)
{
	for (auto& S : States) { S.Padding = M; }
}

void FUIStyle::SetFontAll(FUIName F)
{
	Font = F;
	for (auto& S : States) { S.Font = F; }
}

void FUIStyle::SetIconAll(FUIName I)
{
	Icon = I;
	for (auto& S : States) { S.Icon = I; }
}

bool FUIStyle::IsEmpty() const
{
	if (Font.has_value() || Icon.has_value()) { return false; }
	for (const auto& S : States)
	{
		if (S.Fill || S.Stroke || S.Text || S.StrokeWidth || S.Radius ||
			S.FontSize || S.Padding || S.Font || S.Icon || S.IconSize)
		{
			return false;
		}
	}
	return true;
}

}} // namespace Maho::UI
