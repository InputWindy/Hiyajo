#include "Timer.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <sstream>

namespace Maho::Timer
{

FTimer* GTimer = nullptr;

MAHO_TIMER_API FTimer* GetTimer()
{
	return GTimer;
}

void FTimer::Initialize(FEngineBase& Engine)
{
	(void)Engine;
	Reset();
	GTimer = this;
}

void FTimer::Shutdown(FEngineBase&)
{
	Reset();
	GTimer = nullptr;
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
		return;   // unbalanced - ignore
	}

	const auto Now = std::chrono::steady_clock::now();
	const double Elapsed = std::chrono::duration<double>(Now - Current->Start).count();
	Current->TotalSeconds += Elapsed;
	Current->MaxSeconds = (std::max)(Current->MaxSeconds, Elapsed);
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
	GetTimer()->BeginScope(Name);
}

FScopedTimer::~FScopedTimer()
{
	GetTimer()->EndScope();
}

} // namespace Maho::Timer

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_TIMER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Timer::FTimer::CreateLayer();
}
