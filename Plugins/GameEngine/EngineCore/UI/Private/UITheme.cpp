#include <UITheme.h>

#include <Log.h>

#include <atomic>
#include <cmath>

namespace Maho { namespace UI {

namespace
{
FUITheme GTheme;
TMulticastEvent<void()> GThemeChanged;
std::atomic<bool> GFontSizeWarned{ false };
std::atomic<std::uint32_t> GThemeStamp{ 1 };

FUITheme& MutableTheme()
{
	return GTheme;
}
} // namespace

FUITheme& GetUITheme()
{
	return MutableTheme();
}

FSubscriptionID SubscribeThemeChanged(std::function<void()> Handler)
{
	return GThemeChanged.Bind(std::move(Handler));
}

void NotifyThemeChanged()
{
	GThemeStamp.fetch_add(1, std::memory_order_relaxed);
	GThemeChanged.Broadcast();
}

std::uint32_t GetUIThemeStamp()
{
	return GThemeStamp.load(std::memory_order_relaxed);
}

float SnapFontSize(float Size)
{
	const std::vector<float>& Steps = GTheme.FontSizeSteps;
	if (Steps.empty()) { return Size; }

	float Best = Steps.front();
	float BestDelta = std::fabs(Size - Best);
	for (const float Step : Steps)
	{
		const float Delta = std::fabs(Size - Step);
		if (Delta < BestDelta)
		{
			BestDelta = Delta;
			Best = Step;
		}
	}

	// 档位外字号只记一条诊断（避免逐帧刷屏）
	if (BestDelta > 0.001f)
	{
		bool bExpected = false;
		if (!GFontSizeWarned.compare_exchange_strong(bExpected, true))
		{
			return Best;
		}
		MAHO_IF_NOT_NULL(GetLog(), L)
		{
			L->Warn("UI: 字号 {} 不在主题档位集合内，已吸附到 {}", Size, Best);
		}
	}
	return Best;
}

}} // namespace Maho::UI
