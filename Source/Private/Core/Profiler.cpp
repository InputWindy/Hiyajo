#include <Core/Profiler.h>

#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

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

/** The lane this thread is currently inside; null means "no group established here yet".
 *
 *  A lane is a GROUP -- an architectural partition ("Engine", "Render", "RHIServer"), not an OS
 *  thread and not a frame. The two questions the timeline answers are "which partition is busy" and
 *  "what is the engine running in parallel": both are grouping questions, and neither is answered by
 *  binding a row to a frame (which scatters one frame's work across lanes as the pool sees fit). */
thread_local const char* GCurrentGroup = nullptr;

/** The in-flight RING SLOT this thread's current lane belongs to, or -1 when there is none.
 *
 *  It is what makes one group's row legible: a group has several frames in flight, and the timeline
 *  can only place bars that nest or follow -- partial overlaps have no representation. A node sets
 *  this from its phase, and a manual scope INHERITS it (it ran inside that node), so a group's row
 *  becomes one row per pipeline position: "Render #0 / #1 / #2". */
thread_local std::int32_t GCurrentSlot = -1;

/** The FRAME this thread is currently inside (the node's own name), or null outside one.
 *
 *  A manual scope has no frame of its own -- it ran inside whatever node was executing -- and it
 *  needs one to land on the right row: with several instances of a frame in flight, "the same row"
 *  means the same (owner, slot) pair, so the pair sets BOTH here and the manual scope inherits both. */
thread_local const char* GCurrentFrame = nullptr;

/** How many traced scopes deep this thread is. It is added to a scope's start as `depth x 1us`.
 *
 *  Why an offset at all: the trace's resolution is a microsecond and a parent opens its child almost
 *  immediately, so nested bars routinely share a timestamp -- and a reader of the timeline decides
 *  nesting by STRICTLY increasing timestamps, so those ties come back as "partial overlap" import
 *  errors (Perfetto then spills the child). Nudging each nesting level by a microsecond makes the
 *  hierarchy unambiguous at the source, for a cost of a few microseconds of apparent time depth. */
thread_local std::int32_t GDepth = 0;

/** The reserved group. Registered below, so an event with no group still has one coherent row
 *  instead of landing under a numeric id. */
const char* kGlobalGroupName = "Global";

/** The registered groups. Names are static by contract (a literal), so an entry is stable once
 *  written, and ADDING one is the only mutation -- which is what lets the reader below walk the
 *  table without a lock while registration (install time) takes one. */
constexpr std::size_t kMaxTraceGroups = 64;
const char*           GGroupNames[kMaxTraceGroups] = {};
std::atomic<std::uint32_t> GGroupCount{ 0 };
std::mutex                 GGroupMutex;

/** Unknown names we have already complained about, so a typo costs one line, not one per event. */
constexpr std::size_t kMaxWarnedGroups = 16;
const char*           GWarnedGroups[kMaxWarnedGroups] = {};
std::uint32_t         GWarnedCount = 0;

/** The threads that have emitted, in the order they first did: the `worker=` index is a position in
 *  this list. Assigned under the same lock as the group table and cached in thread-local storage by
 *  TraceThreadIndex, so an emit costs one load. */
constexpr std::uint32_t kUnassignedThread = 0xffffffffu;
std::vector<std::thread::id> GThreadIds;
std::mutex                   GThreadMutex;

/** The lane a manual scope should record on: whatever this thread established, else Global. */
const char* GroupOf()
{
	return (GCurrentGroup != nullptr) ? GCurrentGroup : kGlobalGroupName;
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

/** The usable part of a MANUAL scope's name. `__FUNCTION__` on MSVC spells the FULLY qualified name
 *  ("Maho::FUIFeature::OnInstalled"), and a namespace is not what a bar needs on it: keep the last
 *  two components (`Class::Function`), as a SLICE -- the same no-copy trick as ShortStageName, on a
 *  path that runs hundreds of times a frame. */
FNameSlice ShortScopeName(const char* Raw)
{
	std::string_view View(Raw);
	for (const std::string_view Keyword : { "class ", "struct ", "enum " })
	{
		if (View.starts_with(Keyword))
		{
			View.remove_prefix(Keyword.size());
			break;
		}
	}
	const std::size_t Last = View.rfind("::");
	if (Last == std::string_view::npos || Last == 0)
	{
		return FNameSlice{ View.data(), View.size() };
	}
	const std::size_t SecondLast = View.rfind("::", Last - 1);
	if (SecondLast == std::string_view::npos)
	{
		return FNameSlice{ View.data(), View.size() };
	}
	View.remove_prefix(SecondLast + 2);
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

/** The process-wide recording switch: MAHO_TRACE decides its initial value (read once, while the
 *  process loads), and the `r.Trace` CVar -- declared by the Log plugin, which owns the profiler's
 *  file sink -- flips it at runtime. One atomic, read relaxed on every scope. */
std::atomic<bool> GTraceEnabled{ std::getenv("MAHO_TRACE") != nullptr };

bool TraceEnabled()
{
	// A relaxed load on a hot path. The switch is process-wide and may flip at runtime (`r.Trace`),
	// so it is one file-scope atomic that both this and TraceSetEnabled touch; its INITIAL value still
	// comes from MAHO_TRACE, so a run can be traced from its very first event with no config at all.
	return GTraceEnabled.load(std::memory_order_relaxed);
}

void TraceSetEnabled(bool bEnabled)
{
	GTraceEnabled.store(bEnabled, std::memory_order_relaxed);
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

namespace
{
	/** `ts`/`dur`/`lane`/`slot`/`worker`/`owner` -- the prefix both shapes share.
	 *
	 *  The slot is omitted when there is none to report (a resident thread's task). LANE + OWNER +
	 *  SLOT is the ROW: the group folds into one section, and inside it each (frame, pipeline
	 *  position) is a track -- see the header for why that nesting holds where "everything in the
	 *  group on one track" does not. */
	void WriteHead(FLine& Line, const char* Group, std::int32_t Slot, const char* Owner,
		std::uint64_t StartMicros, std::uint64_t DurMicros)
	{
		Line.Literal("[tr] ts=");
		Line.Decimal(StartMicros);
		Line.Literal(" dur=");
		Line.Decimal(DurMicros);
		Line.Literal(" lane=");
		Line.CString(Group);
		if (Slot >= 0)
		{
			Line.Literal(" slot=");
			Line.Decimal(static_cast<std::uint64_t>(Slot));
		}
		Line.Literal(" worker=");
		Line.Decimal(TraceThreadIndex());
		if (Owner != nullptr && Owner[0] != '\0')
		{
			Line.Literal(" owner=");
			Line.CString(Owner);
		}
	}
}

const char* TraceGroupGlobal()
{
	return kGlobalGroupName;
}

std::uint32_t TraceThreadIndex()
{
	// Assigned once per thread and cached in thread-local storage, so an emit pays a load: the
	// registry below only ever grows, and only the FIRST emit from a thread takes the lock.
	thread_local std::uint32_t Cached = kUnassignedThread;
	if (Cached != kUnassignedThread)
	{
		return Cached;
	}
	const std::thread::id Self = std::this_thread::get_id();
	std::lock_guard<std::mutex> Lock(GThreadMutex);
	for (std::uint32_t Index = 0; Index < static_cast<std::uint32_t>(GThreadIds.size()); ++Index)
	{
		if (GThreadIds[Index] == Self)
		{
			Cached = Index;
			return Cached;
		}
	}
	Cached = static_cast<std::uint32_t>(GThreadIds.size());
	GThreadIds.push_back(Self);
	return Cached;
}

const char* RegisterTraceGroup(const char* Name)
{
	if (Name == nullptr || Name[0] == '\0')
	{
		return kGlobalGroupName;
	}
	std::lock_guard<std::mutex> Lock(GGroupMutex);
	const std::uint32_t Count = GGroupCount.load(std::memory_order_relaxed);
	for (std::uint32_t Index = 0; Index < Count; ++Index)
	{
		if (std::strcmp(GGroupNames[Index], Name) == 0)
		{
			return GGroupNames[Index];   // the FIRST pointer wins, so one group is one pointer
		}
	}
	if (Count >= kMaxTraceGroups)
	{
		std::fprintf(stderr, "[trace] group table full (%zu); '%s' falls back to Global\n",
			kMaxTraceGroups, Name);
		return kGlobalGroupName;
	}
	GGroupNames[Count] = Name;
	GGroupCount.store(Count + 1, std::memory_order_release);
	return Name;
}

const char* ResolveTraceGroup(const char* Name)
{
	if (Name == nullptr || Name[0] == '\0')
	{
		return kGlobalGroupName;
	}
	const std::uint32_t Count = GGroupCount.load(std::memory_order_acquire);
	for (std::uint32_t Index = 0; Index < Count; ++Index)
	{
		if (std::strcmp(GGroupNames[Index], Name) == 0)
		{
			return GGroupNames[Index];
		}
	}

	// Unknown name -- a typo, or a group nobody registered. Warn ONCE per distinct name (a lane that
	// appears for one misspelling looks like "the timeline is wrong", which is the expensive kind of
	// bug) and fall back to Global, never to a freshly minted lane.
	bool bAlreadyWarned = false;
	{
		std::lock_guard<std::mutex> Lock(GGroupMutex);
		for (std::uint32_t Index = 0; Index < GWarnedCount; ++Index)
		{
			if (std::strcmp(GWarnedGroups[Index], Name) == 0)
			{
				bAlreadyWarned = true;
				break;
			}
		}
		if (!bAlreadyWarned && GWarnedCount < kMaxWarnedGroups)
		{
			GWarnedGroups[GWarnedCount++] = Name;
		}
	}
	if (!bAlreadyWarned)
	{
		std::fprintf(stderr, "[trace] unregistered group '%s' -- events fall back to Global "
			"(register it where the group is introduced)\n", Name);
	}
	return kGlobalGroupName;
}

void TraceEmit(const char* Name, const char* Tip, const char* Func,
	std::uint64_t StartMicros, std::uint64_t DurMicros)
{
	FLine Line;
	WriteHead(Line, GroupOf(), GCurrentSlot, GCurrentFrame, StartMicros, DurMicros);
	if (Func != nullptr && Func[0] != '\0')
	{
		// Where the section lives, for a bar named by a hand-written label rather than by its
		// function. Only present when the caller had a different name to give.
		Line.Literal(" func=");
		const FNameSlice FuncShort = ShortScopeName(Func);
		Line.Append(FuncShort.Data, FuncShort.Size);
	}
	Line.Literal(" name=");
	const FNameSlice Short = ShortScopeName(Name);
	Line.Append(Short.Data, Short.Size);
	if (Tip != nullptr && Tip[0] != '\0')
	{
		Line.Literal(" tip=");
		Line.CString(Tip);
	}
	Line.Newline();
	EmitLine(Line);
}

void TraceEmitPair(const char* Group, const char* First, const char* Second, const char* Tip,
	std::int32_t Phase, bool bMirrorToGlobal, std::uint64_t StartMicros, std::uint64_t DurMicros)
{
	// The floor applies to TASK bars, and this is the only thing that produces them: a task bar is
	// generated for EVERY task, which is exactly what makes a trace expensive, whereas a manual
	// MAHO_TRACE_SCOPE is a human decision about what matters and is never filtered. It is applied
	// BEFORE either line is written, so the bar and its Global mirror live or die together.
	//
	// What filtering costs, stated once: rows are derived from events, so a frame whose every bar
	// falls below the floor loses its ROW.
	if (DurMicros < TraceTaskFloorMicros())
	{
		return;
	}

	const char* Lane = ResolveTraceGroup(Group);
	const FNameSlice Short = ShortStageName(Second);

	// `lane=` is what the timeline draws on and `grp=` is what tells a TASK bar from a manual scope
	// (and what the fold grouping uses); they always agree, and the mirror keeps the node's OWN
	// group in `grp=` while moving only the lane.
	const auto WriteBar = [&](const char* LaneName, FLine& Line)
	{
		WriteHead(Line, LaneName, Phase, First, StartMicros, DurMicros);
		Line.Literal(" grp=");
		Line.CString(Lane);
		Line.Literal(" name=");
		Line.CString(First);
		Line.Literal("::");
		Line.Append(Short.Data, Short.Size);
		if (Tip != nullptr && Tip[0] != '\0')
		{
			Line.Literal(" tip=");
			Line.CString(Tip);
		}
		Line.Newline();
	};

	FLine Line;
	WriteBar(Lane, Line);
	EmitLine(Line);

	if (bMirrorToGlobal && std::strcmp(Lane, kGlobalGroupName) != 0)
	{
		FLine Mirror;
		WriteBar(kGlobalGroupName, Mirror);
		EmitLine(Mirror);
	}
}

void TraceFlush()
{
	if (std::FILE* File = TraceFileHandle())
	{
		std::fflush(File);
	}
}

std::function<void()> TraceWrap(std::function<void()> Body, const FTaskTrace& Identity)
{
	if (!TraceEnabled() || Identity.Name == nullptr || Identity.Name[0] == '\0')
	{
		// Nothing to trace: the caller's body, untouched -- no wrapper, no allocation.
		return Body;
	}

	// The identity is a few static pointers, so it is copied INTO the closure and travels with the
	// task; the bar opens inside the wrapper, i.e. at the moment the body actually runs.
	return [Identity, Body = std::move(Body)]()
	{
		FScopedTracePair Scope(Identity.Group, Identity.Name, Identity.Stage, nullptr, Identity.Phase, true);
		Body();
	};
}

std::function<void()> TraceWrapResident(std::function<void()> Body, const char* Role, const char* Stage)
{
	if (!TraceEnabled() || Role == nullptr || Role[0] == '\0')
	{
		return Body;
	}

	// The lane's group is introduced with the role, so registering it here (idempotent) is the reason
	// a resident server never has to mention the trace at all.
	const char* const Group = RegisterTraceGroup(Role);
	return [Group, Stage, Body = std::move(Body)]()
	{
		// mirror = false: a resident thread is not part of the graph's parallel schedule, and the
		// phase is -1 because there is no in-flight slot to report.
		FScopedTracePair Scope(Group, Group, Stage, nullptr, -1, false);
		Body();
	};
}

void TraceEmitFlow(const char* GateGroup, const char* GateOwner, const char* GateStage,
	std::int32_t GateSlot, std::uint64_t GateEndMicros,
	const char* NextGroup, const char* NextOwner, const char* NextStage, std::int32_t NextSlot)
{
	if (!TraceEnabled())
	{
		return;
	}

	// One line, both endpoints fully described: the ROW each end belongs to, the BAR each end is
	// (composed exactly like a node bar, so a reader can bind them) and the gate's end timestamp.
	// The released node's start is the moment of this call -- a few microseconds before its own bar
	// opens, which is why the NAME travels: the reader re-anchors the arrow's far end to the bar it
	// belongs to instead of trusting the timestamp.
	const std::uint64_t Now = TraceNowMicros();
	const FNameSlice GateShort = (GateStage != nullptr) ? ShortStageName(GateStage) : FNameSlice{};
	const FNameSlice NextShort = (NextStage != nullptr) ? ShortStageName(NextStage) : FNameSlice{};

	FLine Line;
	Line.Literal("[tr] flow a_ts=");
	Line.Decimal(GateEndMicros);
	Line.Literal(" a_lane=");
	Line.CString(GateGroup);
	if (GateSlot >= 0)
	{
		Line.Literal(" a_slot=");
		Line.Decimal(static_cast<std::uint64_t>(GateSlot));
	}
	if (GateOwner != nullptr && GateOwner[0] != '\0')
	{
		Line.Literal(" a_owner=");
		Line.CString(GateOwner);
		Line.Literal(" a_name=");
		Line.CString(GateOwner);
		Line.Literal("::");
		Line.Append(GateShort.Data, GateShort.Size);
	}
	Line.Literal(" b_ts=");
	Line.Decimal(Now);
	Line.Literal(" b_lane=");
	Line.CString(NextGroup);
	if (NextSlot >= 0)
	{
		Line.Literal(" b_slot=");
		Line.Decimal(static_cast<std::uint64_t>(NextSlot));
	}
	if (NextOwner != nullptr && NextOwner[0] != '\0')
	{
		Line.Literal(" b_owner=");
		Line.CString(NextOwner);
		Line.Literal(" b_name=");
		Line.CString(NextOwner);
		Line.Literal("::");
		Line.Append(NextShort.Data, NextShort.Size);
	}
	Line.Newline();
	EmitLine(Line);
}

FScopedTrace::FScopedTrace(const char* InGroup, const char* InTip, const char* InName,
	const char* InFunc)
	: Name(InName), Tip(InTip), Func(InFunc)
{
	if (!TraceEnabled())
	{
		return;
	}
	Start = TraceNowMicros() + static_cast<std::uint64_t>(GDepth);
	++GDepth;
	if (InGroup != nullptr)
	{
		// A group of its own: this scope establishes the lane and restores the previous one on exit.
		// A null group means "record on whatever lane this thread is already on" -- what a scope
		// inside a frame's stage wants, because the stage established it.
		ResolvedGroup = ResolveTraceGroup(InGroup);
		PreviousGroup = GCurrentGroup;
		GCurrentGroup = ResolvedGroup;
	}
}

FScopedTrace::~FScopedTrace()
{
	if (Start == 0)
	{
		return;
	}
	// Emit FIRST (it reads the lane this scope established), then give the lane back -- and step the
	// nesting depth back down, which is what keeps the next sibling's start on the parent's level.
	// The duration is measured from the OFFSET start, so a scope whose body is shorter than its depth
	// offset reports 0 rather than wrapping around.
	const std::uint64_t Now = TraceNowMicros();
	TraceEmit(Name, Tip, Func, Start, (Now > Start) ? (Now - Start) : 0);
	--GDepth;
	if (ResolvedGroup != nullptr)
	{
		GCurrentGroup = PreviousGroup;
	}
}

FScopedTracePair::FScopedTracePair(const char* InGroup, const char* InFirst, const char* InSecond,
	const char* InTip, std::int32_t InPhase, bool bMirrorToGlobal)
	: Group(InGroup), First(InFirst), Second(InSecond), Tip(InTip), Phase(InPhase), bMirror(bMirrorToGlobal)
{
	if (!TraceEnabled())
	{
		return;
	}
	Start = TraceNowMicros() + static_cast<std::uint64_t>(GDepth);
	++GDepth;
	PreviousGroup = GCurrentGroup;
	GCurrentGroup = ResolveTraceGroup(InGroup);
	// The SLOT goes with the lane: this node's bars (and every manual scope inside it) belong to
	// pipeline position `InPhase`. -1 (a resident thread's task) clears it for the same reason --
	// those bars belong to the thread, not to a pipeline position.
	PreviousSlot = GCurrentSlot;
	GCurrentSlot = InPhase;
	// ... and so does the FRAME: a manual scope inside this node belongs to the same row, which is
	// the (owner, slot) pair -- see GCurrentFrame.
	PreviousFrame = GCurrentFrame;
	GCurrentFrame = First;
}

FScopedTracePair::~FScopedTracePair()
{
	if (Start == 0)
	{
		return;
	}
	const std::uint64_t Now = TraceNowMicros();
	TraceEmitPair(Group, First, Second, Tip, Phase, bMirror, Start, (Now > Start) ? (Now - Start) : 0);
	--GDepth;
	GCurrentGroup = PreviousGroup;
	GCurrentSlot = PreviousSlot;
	GCurrentFrame = PreviousFrame;
}

} // namespace Maho
