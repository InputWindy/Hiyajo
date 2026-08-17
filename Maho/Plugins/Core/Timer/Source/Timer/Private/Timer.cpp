#include <Timer.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <sstream>

namespace Maho::Timer
{

bool FTimer::ExecuteStage(EToolStage Stage)
{
	if (Stage == EToolStage::Init || Stage == EToolStage::Shutdown)
	{
		Reset();
	}
	return true;
}

void FTimer::BeginScope(std::string_view Name)
{
	FNode& Child = Current->Children[std::string(Name)];
	Child.Name = Name;
	Child.Start = std::chrono::steady_clock::now();
	Current = &Child;
}

void FTimer::EndScope()
{
	if (Current == &Root)
	{
		return;   // unbalanced — ignore
	}

	const auto Now = std::chrono::steady_clock::now();
	const double Elapsed = std::chrono::duration<double>(Now - Current->Start).count();
	Current->TotalSeconds += Elapsed;
	Current->MaxSeconds = std::max(Current->MaxSeconds, Elapsed);
	++Current->Count;
	Current = Current->Parent;
}

void FTimer::Reset()
{
	Root = FNode{"Root"};
	Current = &Root;
}

std::string FTimer::DumpToString() const
{
	std::ostringstream Out;
	std::function<void(const FNode&, int)> Format = [&](const FNode& Node, int Depth)
	{
		Out << std::string(static_cast<std::size_t>(Depth) * 2, ' ') << Node.Name << ": ";
		if (Node.Count > 0)
		{
			Out << (Node.TotalSeconds * 1000.0) << " ms ("
				<< Node.Count << " calls, avg "
				<< (Node.TotalSeconds / Node.Count * 1000.0) << " ms, max "
				<< (Node.MaxSeconds * 1000.0) << " ms)";
		}
		Out << '\n';
		for (const auto& [ChildName, Child] : Node.Children)
		{
			(void)ChildName;
			Format(Child, Depth + 1);
		}
	};
	Format(Root, 0);
	return Out.str();
}

FScopedTimer::FScopedTimer(std::string_view Name)
{
	FTimer::Get().BeginScope(Name);
}

FScopedTimer::~FScopedTimer()
{
	FTimer::Get().EndScope();
}

// ── FGameClock ──

double FGameClock::GetRealSeconds() const
{
	const auto Now = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(Now - InitTime).count();
}

double FGameClock::GetGameSeconds()
{
	const auto Now = std::chrono::steady_clock::now();
	if (!bPaused)
	{
		const double RealDelta = std::chrono::duration<double>(Now - LastReal).count();
		GameSeconds += RealDelta * TimeScale;
		DeltaSeconds = RealDelta * TimeScale;
	}
	LastReal = Now;
	return GameSeconds;
}

double FGameClock::GetDeltaSeconds()
{
	GetGameSeconds();
	return DeltaSeconds;
}

void FGameClock::SetTimeScale(double InScale)
{
	TimeScale = InScale;
}

void FGameClock::SetPaused(bool bInPaused)
{
	if (bInPaused && !bPaused)
	{
		GetGameSeconds();   // advance before freezing
	}
	bPaused = bInPaused;
}

double FGameClock::GetTimeScale() const
{
	return TimeScale;
}

bool FGameClock::IsPaused() const
{
	return bPaused;
}

} // namespace Maho::Timer

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FTimerAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Timer::FTimer::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_TIMER_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FTimerAdapter();
}
