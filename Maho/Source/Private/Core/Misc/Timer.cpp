#include <Core/Misc/Timer.h>

#include <Core/App.h>

#include <sstream>
#include <utility>

namespace Maho
{

std::string FTimerDataPackage::Serialize() const
{
	std::ostringstream Stream;
	Stream << "[Timer:" << SourceName << "] (" << Samples.size() << " scopes)\n";
	for (const FTimerScopeSample& Sample : Samples)
	{
		Stream
			<< "  - " << Sample.Name
			<< "  count=" << Sample.CallCount
			<< "  total_ms=" << Sample.TotalMilliseconds
			<< "  avg_ms=" << Sample.AverageMilliseconds()
			<< "  min_ms=" << Sample.MinMilliseconds
			<< "  max_ms=" << Sample.MaxMilliseconds
			<< "  last_ms=" << Sample.LastMilliseconds
			<< '\n';
	}
	return Stream.str();
}

FTimer* FTimer::TryGet()
{
	return GApp ? &GApp->GetTimer() : nullptr;
}

FTimer& FTimer::Get()
{
	return GApp->GetTimer();
}

void FTimer::Record(const char* CategoryName, const char* ScopeName, double ElapsedMilliseconds)
{
	if (!CategoryName || CategoryName[0] == '\0' || !ScopeName || ScopeName[0] == '\0')
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(Mutex);
	FScopeAccum& Accum = Categories[CategoryName][ScopeName];
	if (Accum.CallCount == 0)
	{
		Accum.MinMilliseconds = ElapsedMilliseconds;
		Accum.MaxMilliseconds = ElapsedMilliseconds;
	}
	else
	{
		if (ElapsedMilliseconds < Accum.MinMilliseconds)
		{
			Accum.MinMilliseconds = ElapsedMilliseconds;
		}
		if (ElapsedMilliseconds > Accum.MaxMilliseconds)
		{
			Accum.MaxMilliseconds = ElapsedMilliseconds;
		}
	}

	Accum.LastMilliseconds = ElapsedMilliseconds;
	Accum.TotalMilliseconds += ElapsedMilliseconds;
	++Accum.CallCount;
}

void FTimer::Reset(const char* CategoryName)
{
	if (!CategoryName || CategoryName[0] == '\0')
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(Mutex);
	Categories.erase(CategoryName);
}

void FTimer::ResetAll()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Categories.clear();
}

bool FTimer::TryQuery(const char* CategoryName, FTimerDataPackage& OutPackage) const
{
	if (!CategoryName || CategoryName[0] == '\0')
	{
		return false;
	}

	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Categories.find(CategoryName);
	if (It == Categories.end())
	{
		return false;
	}

	OutPackage.SourceName = CategoryName;
	OutPackage.Samples.clear();
	OutPackage.Samples.reserve(It->second.size());
	for (const auto& Pair : It->second)
	{
		FTimerScopeSample Sample;
		Sample.Name = Pair.first;
		Sample.CallCount = Pair.second.CallCount;
		Sample.TotalMilliseconds = Pair.second.TotalMilliseconds;
		Sample.MinMilliseconds = Pair.second.MinMilliseconds;
		Sample.MaxMilliseconds = Pair.second.MaxMilliseconds;
		Sample.LastMilliseconds = Pair.second.LastMilliseconds;
		OutPackage.Samples.push_back(std::move(Sample));
	}
	return true;
}

std::vector<FTimerDataPackage> FTimer::QueryAll() const
{
	std::lock_guard<std::mutex> Lock(Mutex);

	std::vector<FTimerDataPackage> Packages;
	Packages.reserve(Categories.size());
	for (const auto& CategoryPair : Categories)
	{
		FTimerDataPackage Package;
		Package.SourceName = CategoryPair.first;
		Package.Samples.reserve(CategoryPair.second.size());
		for (const auto& ScopePair : CategoryPair.second)
		{
			FTimerScopeSample Sample;
			Sample.Name = ScopePair.first;
			Sample.CallCount = ScopePair.second.CallCount;
			Sample.TotalMilliseconds = ScopePair.second.TotalMilliseconds;
			Sample.MinMilliseconds = ScopePair.second.MinMilliseconds;
			Sample.MaxMilliseconds = ScopePair.second.MaxMilliseconds;
			Sample.LastMilliseconds = ScopePair.second.LastMilliseconds;
			Package.Samples.push_back(std::move(Sample));
		}
		Packages.push_back(std::move(Package));
	}
	return Packages;
}

FScopedTimer::FScopedTimer(const char* InCategoryName, const char* InScopeName)
	: Timer(FTimer::TryGet())
	, CategoryName(InCategoryName)
	, ScopeName(InScopeName)
	, StartTime(std::chrono::steady_clock::now())
	, bActive(
		Timer != nullptr
		&& InCategoryName != nullptr && InCategoryName[0] != '\0'
		&& InScopeName != nullptr && InScopeName[0] != '\0')
{
}

FScopedTimer::~FScopedTimer()
{
	if (!bActive || !Timer)
	{
		return;
	}

	const auto EndTime = std::chrono::steady_clock::now();
	const double ElapsedMilliseconds =
		std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
	Timer->Record(CategoryName, ScopeName, ElapsedMilliseconds);
}

} // namespace Maho
