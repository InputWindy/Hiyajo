#include <Core/Profiler.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string_view>

namespace Maho
{

namespace
{

/** Where the trace lands. A literal path in the working directory, beside the executable -- the
 *  same convention the engine's other diagnostic dumps use. */
const char* kTracePath = "Profile_trace.txt";

std::uint64_t TraceOriginMicros()
{
	static const std::uint64_t Origin = []
	{
		using namespace std::chrono;
		return static_cast<std::uint64_t>(
			duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
	}();
	return Origin;
}

std::FILE* TraceFileHandle()
{
	static std::FILE* File = std::fopen(kTracePath, "w");
	return File;
}

/** The lane a thread is currently inside; 0 means "no frame scope on this thread".
 *
 *  A lane is a FRAME, not an OS thread. The trace has to answer "what did this frame do, in what
 *  order, overlapping what" -- and the thread pool hands a frame's nodes to whichever worker is
 *  free, so a per-thread grouping scatters one frame across rows and hides exactly the shape the
 *  profile exists to show. The first name of each pair scope selects the row instead. */
thread_local std::uint32_t GCurrentLane = 0;

/** Stable lane id for a frame name. Names are static by contract, so hashing the string is
 *  deterministic for the life of the process and needs no table. */
std::uint32_t LaneOf(const char* FrameName)
{
	const std::size_t Hashed = std::hash<std::string_view>{}(std::string_view(FrameName));
	return static_cast<std::uint32_t>(Hashed & 0x7fffffffu) + 1u;   // 0 stays "no lane"
}

/** A slice of a string, for names we only ever print. */
struct FNameSlice
{
	const char* Data = "";
	std::size_t Size = 0;
};

/** The usable part of a stage name, as a SLICE of the input -- `type_info::name()` spells a stage
 *  as "class Maho::IRender", and neither the namespace nor the keyword belongs on a timeline lane
 *  label, but copying it out to strip them would put an allocation on the emit path. That cost is
 *  not hypothetical: once the pool traces every task, the emitter runs tens of thousands of times
 *  a second, and an allocating form shows up in the very numbers it reports. */
FNameSlice ShortStageName(const char* Raw)
{
	std::string_view View(Raw);
	const std::size_t Pos = View.rfind("::");
	View.remove_prefix(Pos == std::string_view::npos ? 0 : Pos + 2);
	for (const std::string_view Keyword : { "class ", "struct ", "enum " })
	{
		if (View.starts_with(Keyword))
		{
			View.remove_prefix(Keyword.size());
			break;
		}
	}
	return FNameSlice{ View.data(), View.size() };
}

} // namespace

bool TraceEnabled()
{
	// Read ONCE, like MAHO_TRACE_STAGES: no getenv on the hot path, and the answer is stable for
	// the process -- a trace that switched on halfway through would have no meaningful origin.
	static const bool bOn = (std::getenv("MAHO_TRACE") != nullptr);
	return bOn;
}

std::uint64_t TraceNowMicros()
{
	// Origin FIRST: it is initialized on the very first call, and reading the clock before that
	// would place the origin a moment LATER than the reading -- making the first event's timestamp
	// negative, i.e. UINT64_MAX once it wraps.
	const std::uint64_t Origin = TraceOriginMicros();
	using namespace std::chrono;
	const std::uint64_t Now = static_cast<std::uint64_t>(
		duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
	return Now - Origin;
}

void TraceEmit(const char* Name, std::uint64_t StartMicros, std::uint64_t DurMicros)
{
	if (std::FILE* File = TraceFileHandle())
	{
		std::fprintf(File, "[tr] ts=%llu dur=%llu tid=%u name=%s\n",
			static_cast<unsigned long long>(StartMicros),
			static_cast<unsigned long long>(DurMicros),
			unsigned(GCurrentLane), Name);
	}
}

void TraceEmitPair(const char* Group, const char* First, const char* Second,
	std::uint64_t StartMicros, std::uint64_t DurMicros)
{
	if (std::FILE* File = TraceFileHandle())
	{
		const FNameSlice Short = ShortStageName(Second);
		std::fprintf(File, "[tr] ts=%llu dur=%llu tid=%u grp=%s name=%s::%.*s\n",
			static_cast<unsigned long long>(StartMicros),
			static_cast<unsigned long long>(DurMicros),
			unsigned(GCurrentLane), (Group != nullptr) ? Group : "",
			First, static_cast<int>(Short.Size), Short.Data);
	}
}

void TraceFlush()
{
	if (std::FILE* File = TraceFileHandle())
	{
		std::fflush(File);
	}
}

FScopedTracePair::FScopedTracePair(const char* InGroup, const char* InFirst, const char* InSecond)
	: Group(InGroup), First(InFirst), Second(InSecond)
{
	if (TraceEnabled())
	{
		Start = TraceNowMicros();
		PreviousLane = GCurrentLane;
		GCurrentLane = LaneOf(First);
	}
}

FScopedTracePair::~FScopedTracePair()
{
	if (Start != 0)
	{
		TraceEmitPair(Group, First, Second, Start, TraceNowMicros() - Start);
		GCurrentLane = PreviousLane;
	}
}

} // namespace Maho
