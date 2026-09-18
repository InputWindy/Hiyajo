#include <Core/Profiler.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
	// BINARY mode, deliberately: in text mode the CRT scans every write for '\n' and expands it to
	// CRLF, which is pure overhead on a hot path whose whole point is to be cheap. The file then
	// holds plain LF, which every reader of it (Tools/trace_to_chrome.py, grep, an editor) accepts.
	static std::FILE* File = std::fopen(kTracePath, "wb");
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
 *  label, but copying it out to strip them would put work on the emit path. That cost is not
 *  hypothetical: once the pool traces every task, the emitter runs tens of thousands of times a
 *  second, and anything it does shows up in the very numbers it reports. */
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

/**
 * One line, assembled by hand into a stack buffer.
 *
 * `fprintf` was the emitter's bulk cost: for EVERY conversion the UCRT parses the format string
 * and consults the locale, and the fields here are two integers, a lane id and three names. So the
 * formatter is a memcpy and a decimal loop, and the line leaves through ONE fwrite -- one FILE
 * lock, no format parsing. Nothing allocates and nothing can throw.
 *
 * An event whose text does not fit is DROPPED, never truncated: a clipped frame name would mint a
 * second row for a frame that already has one, which is worse than losing a single bar. The flag
 * for that lives here so the decision is made once, at the end.
 */
struct FLine
{
	static constexpr std::size_t kCapacity = 1024;

	char        Bytes[kCapacity];
	std::size_t Used      = 0;
	bool        bOverflow = false;

	void Append(const char* Text, std::size_t Size)
	{
		// One byte short of capacity, so the terminator always has room.
		if (Size > kCapacity - 1 - Used)
		{
			bOverflow = true;
			return;
		}
		std::memcpy(Bytes + Used, Text, Size);
		Used += Size;
	}

	/** A string literal, with its length known at compile time. */
	template <std::size_t N>
	void Literal(const char (&Text)[N])
	{
		Append(Text, N - 1);
	}

	void CString(const char* Text)
	{
		Append(Text, std::strlen(Text));
	}

	/** Decimal, no locale, no padding. Digits are produced backwards into a scratch array so no
	 *  temporary or reversal pass is needed. */
	void Decimal(std::uint64_t Value)
	{
		char  Digits[20];
		char* End = Digits + sizeof(Digits);
		char* P   = End;
		do
		{
			*--P = static_cast<char>('0' + (Value % 10));
			Value /= 10;
		} while (Value != 0);
		Append(P, static_cast<std::size_t>(End - P));
	}

	void Newline()
	{
		Append("\n", 1);
	}
};

/** Write one assembled line, or nothing at all when it overflowed. */
void EmitLine(const FLine& Line)
{
	std::FILE* File = TraceFileHandle();
	if (File == nullptr || Line.bOverflow)
	{
		return;
	}
	std::fwrite(Line.Bytes, 1, Line.Used, File);
}

} // namespace

bool TraceEnabled()
{
	// Read ONCE, like MAHO_TRACE_STAGES: no getenv on the hot path, and the answer is stable for
	// the process -- a trace that switched on halfway through would have no meaningful origin.
	static const bool bOn = (std::getenv("MAHO_TRACE") != nullptr);
	return bOn;
}

std::uint64_t TraceTaskFloorMicros()
{
	// Read ONCE, like every other switch here. 0 -- the default -- keeps every bar; the floor
	// exists for when the trace's own cost matters more than its completeness.
	static const std::uint64_t Floor = []
	{
		if (const char* Raw = std::getenv("MAHO_TRACE_MIN_US"))
		{
			const long long Parsed = std::strtoll(Raw, nullptr, 10);
			if (Parsed > 0)
			{
				return static_cast<std::uint64_t>(Parsed);
			}
		}
		return std::uint64_t{ 0 };
	}();
	return Floor;
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
	FLine Line;
	Line.Literal("[tr] ts=");
	Line.Decimal(StartMicros);
	Line.Literal(" dur=");
	Line.Decimal(DurMicros);
	Line.Literal(" tid=");
	Line.Decimal(GCurrentLane);
	Line.Literal(" name=");
	Line.CString(Name);
	Line.Newline();
	EmitLine(Line);
}

void TraceEmitPair(const char* Group, const char* First, const char* Second,
	std::uint64_t StartMicros, std::uint64_t DurMicros)
{
	// The floor applies to TASK bars, and this is the only thing that produces them: a task bar is
	// generated for EVERY task, which is exactly what makes a trace expensive, whereas a manual
	// MAHO_TRACE_SCOPE is a human decision about what matters and is never filtered.
	//
	// What filtering costs, stated once: rows are derived from events, so a frame whose every bar
	// falls below the floor loses its ROW (its manual scopes then land under the numeric lane id).
	// That is the intended trade -- a frame with no bar above the floor is a frame not worth a row.
	if (DurMicros < TraceTaskFloorMicros())
	{
		return;
	}

	const FNameSlice Short = ShortStageName(Second);

	FLine Line;
	Line.Literal("[tr] ts=");
	Line.Decimal(StartMicros);
	Line.Literal(" dur=");
	Line.Decimal(DurMicros);
	Line.Literal(" tid=");
	Line.Decimal(GCurrentLane);
	Line.Literal(" grp=");
	if (Group != nullptr)
	{
		Line.CString(Group);
	}
	Line.Literal(" name=");
	Line.CString(First);
	Line.Literal("::");
	Line.Append(Short.Data, Short.Size);
	Line.Newline();
	EmitLine(Line);
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
