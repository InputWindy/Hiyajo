#include <Trace.h>

#include <algorithm>
#include <map>
#include <string>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Maho
{

#if MAHO_WITH_TRACE

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

/** The ROW this thread is currently on: the lane (architectural partition), the track (the frame
 *  that is running), and the stage whose bar opened it. A MAHO_TRACE_STAGE or a MAHO_TRACE_SCOPE
 *  establishes all three and gives them back on exit; a MAHO_TRACE_SECTION only reads them, so a
 *  section inside a stage lands on that stage's row and nests inside its bar.
 *
 *  Thread-local because a worker thread runs node after node: the row is a property of the CALL, not
 *  of the collector that scheduled it. */
thread_local const char* GCurrentGroup = nullptr;
thread_local const char* GCurrentTrack = nullptr;
thread_local FStaticName GCurrentStage{};

/** How many traced scopes deep this thread is. It is added to a scope's start as `depth x 1us`.
 *
 *  Why an offset at all: the trace's resolution is a microsecond and a parent opens its child almost
 *  immediately, so nested bars routinely share a timestamp -- and a reader of the timeline decides
 *  nesting by STRICTLY increasing timestamps, so those ties come back as "partial overlap" import
 *  errors (Perfetto then spills the child). Nudging each nesting level by a microsecond makes the
 *  hierarchy unambiguous at the source, for a cost of a few microseconds of apparent time depth. */
thread_local std::int32_t GDepth = 0;

/** The lane-less fallback: an event with no row of its own still needs one coherent name. */
const char* kGlobalLane = "Global";

/** A slice of a string, for names we only ever print. */
using FNameSlice = FStaticName;

/** The usable part of a stage name, as a SLICE of the input -- `type_info::name()` spells a stage
 *  as "class Maho::IRender", and neither the namespace nor the keyword belongs on a timeline lane
 *  label, but copying it out to strip them would put work on the emit path. That cost is not
 *  hypothetical: the emitter runs tens of thousands of times a second while tracing is on, and
 *  anything it does shows up in the very numbers it reports. */
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

/** The threads that have emitted, in the order they first did: the `worker=` index is a position in
 *  this list. Assigned under a lock and cached in thread-local storage, so an emit costs one load.
 *  An OS tid is deliberately NOT used: it is both wide and nondeterministic, and the number only has
 *  to be stable within one trace. */
constexpr std::uint32_t kUnassignedThread = 0xffffffffu;
std::vector<std::thread::id> GThreadIds;
std::mutex                   GThreadMutex;

/** This thread's dense index -- the `worker=` field (who ran the node, which is information, never
 *  the row). */
std::uint32_t TraceThreadIndex()
{
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

/** The lane every bar is ALSO filed under, next to the frame's own partition: the POOL's own group. */
const char* kPoolLane = "ThreadPool";

/** A salt for the pool group's flow ids, so one edge's twin arrow never shares an id with the frame
 *  group's own (an id with four endpoints draws a tangle, not two arrows). XOR keeps the mapping
 *  one-to-one, so distinct edges stay distinct -- and the top bit is deliberately left CLEAR, because
 *  that bit is what pins every id into the range Perfetto will not mistake for an interning id. */
constexpr std::uint64_t kPoolFlowSalt = 0x5E3779B97F4A7C15ull;

/** The row a bar gets in the pool's group: the thread that ran it. Built once per thread (same dense
 *  index the `worker=` field carries) and reused -- this is on the emit path, which runs tens of
 *  thousands of times a second while tracing is on. */
const char* TraceThreadRowName()
{
	thread_local const std::string Name = "Thread " + std::to_string(TraceThreadIndex());
	return Name.c_str();
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

	/** A slice of a string we only ever print (a shortened stage or function name). */
	void Slice(const FNameSlice& Name)
	{
		Append(Name.Data, Name.Size);
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

	/** Signed, for the one field that can be negative (`off=` -- a frame offset). */
	void SignedDecimal(std::int64_t Value)
	{
		if (Value < 0)
		{
			// Negated into an UNSIGNED, so INT64_MIN cannot overflow on the way.
			Append("-", 1);
			Decimal(static_cast<std::uint64_t>(0) - static_cast<std::uint64_t>(Value));
			return;
		}
		Decimal(static_cast<std::uint64_t>(Value));
	}

	void Newline()
	{
		Append("\n", 1);
	}
};

/**
 * One protobuf packet, assembled by hand.
 *
 * Perfetto's own wire format is the target: a Trace is a sequence of length-delimited TracePackets,
 * and a bar is a TYPE_SLICE_BEGIN packet plus a TYPE_SLICE_END packet. Fields are written here rather
 * than formatted, so this is CHEAPER than the text form (a varint instead of a decimal loop), and the
 * file opens in Perfetto directly -- no converter, no "which slice does this timestamp belong to".
 *
 * Field numbers are Perfetto's, read back off a real .perfetto_trace rather than from memory:
 *     TracePacket      { timestamp = 8, trusted_packet_sequence_id = 10,
 *                        track_descriptor = 60, track_event = 11 }
 *     TrackDescriptor  { uuid = 1, name = 2, process = 3, thread = 4 }
 *     TrackEvent       { type = 9, track_uuid = 11, categories = 22, name = 23, flow_ids = 36 }
 *
 * There is no nested-writer API on purpose: every message we emit is at most one level deep, so the
 * inner message is built in its own FProtoWriter and attached with BytesField.
 */
struct FProtoWriter
{
	static constexpr std::size_t kCapacity = 4096;

	char        Bytes[kCapacity];
	std::size_t Used      = 0;
	bool        bOverflow = false;

	void Raw(const void* Data, std::size_t Size)
	{
		if (Size > kCapacity - Used)
		{
			bOverflow = true;
			return;
		}
		std::memcpy(Bytes + Used, Data, Size);
		Used += Size;
	}

	void Varint(std::uint64_t Value)
	{
		char Scratch[10];
		std::size_t Count = 0;
		while (Value >= 0x80)
		{
			Scratch[Count++] = static_cast<char>((Value & 0x7F) | 0x80);
			Value >>= 7;
		}
		Scratch[Count++] = static_cast<char>(Value);
		Raw(Scratch, Count);
	}

	void VarintField(unsigned Field, std::uint64_t Value)
	{
		Varint((static_cast<std::uint64_t>(Field) << 3) | 0);
		Varint(Value);
	}

	void BytesField(unsigned Field, const void* Data, std::size_t Size)
	{
		Varint((static_cast<std::uint64_t>(Field) << 3) | 2);
		Varint(Size);
		Raw(Data, Size);
	}

	void StringField(unsigned Field, const char* Text)
	{
		BytesField(Field, Text, std::strlen(Text));
	}

	/** A field from a name that is only ever a SLICE (a shortened stage name, an event label): the
	 *  length is already known, so nothing is scanned for a terminator. */
	void SliceField(unsigned Field, std::string_view Text)
	{
		BytesField(Field, Text.data(), Text.size());
	}

	/** A TrackEvent. `FlowIds` (field 36, `flow_ids`) STARTS one or more flows here; `TerminatingIds`
	 *  (field 48, `terminating_flow_ids`) ENDS them here without continuing them. That pair is what
	 *  makes an arrow: the producer's END carries the id, the consumer's BEGIN terminates it, and a
	 *  terminating id with no flow open is simply ignored.
	 *
	 *  Both are LISTS because one bar can take part in several edges: a stage may be the input of more
	 *  than one target (and of a reverse edge too). */
	static void BuildTrackEvent(FProtoWriter& Out, std::uint32_t Type, std::uint64_t TrackUuid,
		const char* Category, std::string_view Name, const std::uint64_t* FlowIds, std::size_t FlowCount,
		const std::uint64_t* TerminatingIds = nullptr, std::size_t TerminatingCount = 0)
	{
		Out.VarintField(9, Type);
		Out.VarintField(11, TrackUuid);
		if (Category != nullptr && Category[0] != '\0')
		{
			Out.StringField(22, Category);
		}
		if (!Name.empty())
		{
			Out.SliceField(23, Name);
		}
		for (std::size_t i = 0; i < FlowCount; ++i)
		{
			Out.VarintField(36, FlowIds[i]);
		}
		for (std::size_t i = 0; i < TerminatingCount; ++i)
		{
			Out.VarintField(48, TerminatingIds[i]);
		}
	}

	/** A stable id for "this PRODUCER's bar": both ends of an edge derive it from the producer's row
	 *  identity alone, so neither has to know the other's instance. The LANE is deliberately not part of
	 *  it -- a cross-partition edge has a different lane at each end. FNV-1a, pinned into the 64-bit
	 *  range Perfetto will not confuse with a real interning id. */
	static std::uint64_t FlowIdFor(std::string_view Owner, std::string_view Stage)
	{
		std::uint64_t Hash = 1469598103934665603ull;
		for (std::string_view Part : { Owner, Stage })
		{
			for (char Ch : Part)
			{
				Hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(Ch));
				Hash *= 1099511628211ull;
			}
			Hash ^= 0x7Cull;
			Hash *= 1099511628211ull;
		}
		return Hash | 0x8000000000000000ull;
	}

	/** A row: LANE + OWNER. Verified field numbers, read off a real .perfetto_trace:
	 *      ProcessDescriptor { pid = 1, process_name = 6 }            <- a lane is a process
	 *      ThreadDescriptor  { pid = 1, tid = 2, thread_name = 5 }    <- an owner is a thread in it
	 *  A thread is grouped under a process by PID EQUALITY, so the lane's pid must be the one every
	 *  row of that lane carries -- get it wrong and every row becomes its own process. */
	static void BuildLaneDescriptor(FProtoWriter& Out, std::uint64_t Uuid, const char* Name)
	{
		Out.VarintField(1, Uuid);
		Out.StringField(2, Name);
		FProtoWriter Process;
		Process.VarintField(1, Uuid);         // pid
		Process.StringField(6, Name);         // process_name
		Out.BytesField(3, Process.Bytes, Process.Used);
	}

	static void BuildRowDescriptor(FProtoWriter& Out, std::uint64_t Uuid, std::uint64_t ParentUuid,
		std::uint64_t Pid, const char* Name)
	{
		Out.VarintField(1, Uuid);
		Out.StringField(2, Name);
		Out.VarintField(5, ParentUuid);       // parent_uuid: the lane's process track
		FProtoWriter Thread;
		Thread.VarintField(1, Pid);           // pid -- must equal the lane's
		Thread.VarintField(2, Uuid);          // tid
		Thread.StringField(5, Name);          // thread_name
		Out.BytesField(4, Thread.Bytes, Thread.Used);
	}
};

// -- the NATIVE trace: Perfetto's own protobuf, written beside the text one ------------------------
// Perfetto draws a bar from a TYPE_SLICE_BEGIN/TYPE_SLICE_END pair and hangs a flow on the packets
// themselves, so the converter's whole job -- re-discovering which slice a timestamp belongs to --
// stops existing here. See FProtoWriter for the field numbers.

const char* kProtoPath = "Profile_trace.perfetto_trace";
constexpr std::uint64_t kProtoSeq = 1;   // one packet sequence; every packet comes from this process

/** Which files the trace writes: 0 = the text one (grep-able, the crash-case fallback), 1 = the
 *  native one (Perfetto's protobuf: bars, lanes and flows, with no converter in between), 2 = both.
 *  Read once from MAHO_TRACE_FORMAT; a fixed default of 2 keeps both paths exercised. */
int TraceFormat()
{
	static const int Format = []
	{
		const char* Raw = std::getenv("MAHO_TRACE_FORMAT");
		const int Parsed = (Raw != nullptr) ? std::atoi(Raw) : 2;
		return (Parsed >= 0 && Parsed <= 2) ? Parsed : 2;
	}();
	return Format;
}

std::FILE* TraceProtoHandle()
{
	static std::FILE* File = std::fopen(kProtoPath, "wb");
	return File;
}

std::uint64_t ProtoTrackUuid(const char* Lane, const char* Owner)
{
	// Two levels: a LANE is a process descriptor (the collapsible group a viewer shows), and every row
	// inside it is a thread descriptor whose parent_uuid (field 5, read off a real .perfetto_trace) is
	// the lane's uuid. Without the parent every row shows up as its own process.
	static std::map<std::string, std::uint64_t> Rows;    // "lane\x1fowner" -> uuid
	static std::map<std::string, std::uint64_t> Lanes;   // lane -> uuid
	static std::uint64_t Next = 1;

	const std::string Key = std::string(Lane) + "\x1f" + Owner;
	const auto It = Rows.find(Key);
	if (It != Rows.end())
	{
		return It->second;
	}

	std::uint64_t LaneUuid = 0;
	const auto LaneIt = Lanes.find(Lane);
	if (LaneIt != Lanes.end())
	{
		LaneUuid = LaneIt->second;
	}
	else
	{
		LaneUuid = Next++;
		Lanes.emplace(Lane, LaneUuid);

		FProtoWriter Who;
		Who.VarintField(1, LaneUuid);
		Who.StringField(2, Lane);

		FProtoWriter LaneTrack;
		FProtoWriter::BuildLaneDescriptor(LaneTrack, LaneUuid, Lane);

		FProtoWriter Packet;
		Packet.VarintField(10, kProtoSeq);
		Packet.BytesField(60, LaneTrack.Bytes, LaneTrack.Used);
		FProtoWriter Wrapped;
		Wrapped.BytesField(1, Packet.Bytes, Packet.Used);
		if (std::FILE* File = TraceProtoHandle())
		{
			std::fwrite(Wrapped.Bytes, 1, Wrapped.Used, File);
		}
	}

	const std::uint64_t Uuid = Next++;
	Rows.emplace(Key, Uuid);

	FProtoWriter Descriptor;
	FProtoWriter::BuildRowDescriptor(Descriptor, Uuid, LaneUuid, LaneUuid, Owner);

	FProtoWriter Packet;
	Packet.VarintField(10, kProtoSeq);
	Packet.BytesField(60, Descriptor.Bytes, Descriptor.Used);
	FProtoWriter Wrapped;
	Wrapped.BytesField(1, Packet.Bytes, Packet.Used);
	if (std::FILE* File = TraceProtoHandle())
	{
		std::fwrite(Wrapped.Bytes, 1, Wrapped.Used, File);
	}
	return Uuid;
}

/** One declared edge as the JOIN needs it: the other end's row identity, and how many frames away it
 *  is.
 *
 *  The names are COPIED, not referenced. A bar is emitted while its own frame is alive, but the join
 *  runs once the run is over -- and the names are static data in the module that spells them, which
 *  does NOT have to still be mapped by then. Measured, a full 45% of the queued rows held an owner
 *  (and lane) address that could no longer be read at flush time, and the join died inside `strlen`
 *  on it. Anything the flush needs has to be taken while the emitter is still there -- the row id is
 *  resolved at the same moment, for the same reason. */
struct FWaitKey
{
	std::string  Owner{};
	std::string  Stage{};
	std::int32_t Offset = 0;
};

/** "This record has no twin in the pool's group." An INDEX, not a pointer: the queue moves as it
 *  grows, and the twin link has to survive that. */
constexpr std::size_t kNoTwin = SIZE_MAX;

/** One bar, queued STRUCTURALLY rather than as a finished packet.
 *
 *  Why not build the packet here: a flow id can only be minted once BOTH ends are known, and at emit
 *  time neither end knows the other. A frame sees only its OWN declaration table -- that is the
 *  architecture's rule, the consumer declares the edge -- so a producer cannot even learn that it has
 *  a consumer. The trace is the one place that holds every bar and every declaration, so the join
 *  happens at TraceFlush. */
struct FQueuedRecord
{
	std::uint64_t Ts        = 0;
	std::uint8_t  Type      = 0;         // 1 = TYPE_SLICE_BEGIN, 2 = TYPE_SLICE_END
	std::uint64_t TrackUuid = 0;         // the row, resolved at emit: see the note on FWaitKey
	std::string   Owner{};               // the frame that ran this bar
	std::string   Stage{};               // the short stage name: half of the row's identity
	std::string   Name{};                // the label the bar is drawn with
	/** The edges I WAIT for (a non-reverse declaration): I am the CONSUMER, each target is a producer. */
	std::vector<FWaitKey> WaitsFor;
	/** The edges I BLOCK (`BlockOn`): I am the PRODUCER, each target is a consumer. */
	std::vector<FWaitKey> Blocks;
	/** Filled by the join: ids that open a flow here, and ids that close one here. */
	std::vector<std::uint64_t> FlowIds;
	std::vector<std::uint64_t> Terminating;
	/** The record's twin in the POOL's group (same bar, row = the thread that ran it), or kNoTwin.
	 *  Written when the two are queued, and read by the join to put the same arrows on both. */
	std::size_t MirrorIndex = kNoTwin;
};

/** One declared edge, copied out of the frame's declaration table while the frame is still alive. */
FWaitKey ProtoWaitKey(const FFrameExtension::FEdge& Edge)
{
	const FStaticName Target = ShortStageName(Edge.TargetStage.name());
	return FWaitKey{ std::string(Edge.TargetName), std::string(Target.Data, Target.Size),
		Edge.FrameOffset };
}

std::vector<FQueuedRecord>& ProtoQueue()
{
	static std::vector<FQueuedRecord> Queue;
	return Queue;
}

/** The group a record belongs to: owner + stage + TYPE.
 *
 *  The type is part of the key on purpose. One stage emits exactly one BEGIN and one END per frame, so
 *  ordering ONE group by timestamp makes a record's position in it its FRAME INSTANCE number -- which
 *  is what turns a declared `FrameOffset` into "the same bar, N frames away". Merging BEGINs and ENDs
 *  into a single group would pair a BEGIN with an ordinal that counts two records per frame. */
std::string ProtoGroupKey(std::string_view Owner, std::string_view Stage, std::uint8_t Type)
{
	std::string Key(Owner);
	Key += '\x1f';
	Key.append(Stage.data(), Stage.size());
	Key += '\x1f';
	Key += static_cast<char>('0' + Type);
	return Key;
}

/** Pair every declared edge with the bar at the other end, and mint one flow id per pair.
 *
 *  An id is keyed by the PRODUCER's identity -- owner + stage, and deliberately NOT the lane: a
 *  cross-partition edge has a different lane at each end, so the id has to meet in the middle.
 *
 *  An edge whose two ends sit on the SAME ROW (lane + owner) is dropped: that is a bar talking to its
 *  own row, which the viewer draws as a loop that says nothing (and the stage's own chain is the
 *  degenerate case of exactly that).
 *
 *  A kept edge is drawn TWICE: once between the frame rows, and once between the two bars' TWINS in
 *  the pool's group -- provided the two ends ran on different THREADS, which is the only thing that
 *  group's rows can say. The twins get an id of their own, so a flow never has four endpoints.
 */
void ProtoJoinFlows(std::vector<FQueuedRecord>& Queue)
{
	// 1) Group, then order each group by timestamp: position in the group == frame instance number.
	std::map<std::string, std::vector<std::size_t>> Groups;
	for (std::size_t i = 0; i < Queue.size(); ++i)
	{
		Groups[ProtoGroupKey(Queue[i].Owner, Queue[i].Stage, Queue[i].Type)].push_back(i);
	}
	std::vector<std::size_t> Ordinal(Queue.size(), 0);
	for (auto& Pair : Groups)
	{
		std::vector<std::size_t>& Members = Pair.second;
		std::stable_sort(Members.begin(), Members.end(),
			[&Queue](std::size_t A, std::size_t B) { return Queue[A].Ts < Queue[B].Ts; });
		for (std::size_t Position = 0; Position < Members.size(); ++Position)
		{
			Ordinal[Members[Position]] = Position;
		}
	}

	// 2) Resolve one declared key against a known instance index. Null when the other end is not in
	//    this flush at all (a frame older than the trace, or a stage that has not reached its turn).
	auto Lookup = [&Queue, &Groups](const FWaitKey& Key, std::uint8_t Type,
		std::size_t MyOrdinal) -> FQueuedRecord*
	{
		const std::int64_t Target = static_cast<std::int64_t>(MyOrdinal) + Key.Offset;
		if (Target < 0)
		{
			return nullptr;
		}
		const auto It = Groups.find(ProtoGroupKey(Key.Owner, Key.Stage, Type));
		if (It == Groups.end() || static_cast<std::size_t>(Target) >= It->second.size())
		{
			return nullptr;
		}
		return &Queue[It->second[static_cast<std::size_t>(Target)]];
	};

	// The pool-group twin of a record -- the same bar, on the row of the thread that ran it.
	auto Twin = [&Queue](const FQueuedRecord& Record) -> FQueuedRecord*
	{
		return (Record.MirrorIndex < Queue.size()) ? &Queue[Record.MirrorIndex] : nullptr;
	};

	// Draw one edge, on the frame rows and (when it also changes THREAD) on their twins. The twin pair
	// gets its own id: sharing the frame group's id would make one flow with four endpoints.
	auto DrawEdge = [&Twin](FQueuedRecord& Producer, FQueuedRecord& Consumer, std::uint64_t Id)
	{
		Producer.FlowIds.push_back(Id);
		Consumer.Terminating.push_back(Id);

		FQueuedRecord* const ProducerTwin = Twin(Producer);
		FQueuedRecord* const ConsumerTwin = Twin(Consumer);
		if (ProducerTwin == nullptr || ConsumerTwin == nullptr
			|| ProducerTwin->TrackUuid == ConsumerTwin->TrackUuid)
		{
			// Same thread: the pool group's rows are threads, so that arrow would be a loop inside one
			// of ITS rows -- the same noise the same-row rule drops above, one level down.
			return;
		}
		const std::uint64_t PoolId = Id ^ kPoolFlowSalt;
		ProducerTwin->FlowIds.push_back(PoolId);
		ConsumerTwin->Terminating.push_back(PoolId);
	};

	for (std::size_t i = 0; i < Queue.size(); ++i)
	{
		FQueuedRecord& Record = Queue[i];
		if (Record.Type == 1)
		{
			// I am a CONSUMER: the arrow leaves the producer's END and lands on my BEGIN.
			for (const FWaitKey& Wait : Record.WaitsFor)
			{
				FQueuedRecord* Producer = Lookup(Wait, 2, Ordinal[i]);
				if (Producer == nullptr || Producer->TrackUuid == Record.TrackUuid)
				{
					// The other end is missing -- or it is on MY OWN ROW. An arrow that leaves a row
					// and lands back on it says nothing: a stage's own chain (a stage waiting on
					// itself, the degenerate case) is a row-internal loop too, so the ROW comparison
					// covers both. What is worth drawing is the edge that changes row.
					continue;
				}
				DrawEdge(*Producer, Record, FProtoWriter::FlowIdFor(Wait.Owner, Wait.Stage));
			}
		}
		else
		{
			// I am a PRODUCER (`BlockOn`: I run first). The id is keyed by me -- the one end that knows
			// the edge exists -- and the consumer only receives it.
			for (const FWaitKey& Block : Record.Blocks)
			{
				FQueuedRecord* Consumer = Lookup(Block, 1, Ordinal[i]);
				if (Consumer == nullptr || Consumer->TrackUuid == Record.TrackUuid)
				{
					continue;
				}
				DrawEdge(Record, *Consumer, FProtoWriter::FlowIdFor(Record.Owner, Record.Stage));
			}
		}
	}
}

/** Perfetto's `TracePacket.timestamp` (field 8) is in NANOSECONDS; the engine's clock is microseconds
 *  (`TraceNowMicros`). This is the ONE place the two units meet, and the whole trace's time axis
 *  depends on getting it right: written as microseconds, every bar comes out 1000x too short and the
 *  ruler is 1000x too tight -- which is exactly how this was found (a 2.5ms `Render tick` displayed as
 *  2.548us, and its Start time 2.316395ms for a raw value of 2316400: nanoseconds, /1e9). */
constexpr std::uint64_t kNanosPerMicro = 1000;

/** The queued records, turned into packets and written. Called with the queue already sorted. */
void ProtoWriteRecords(const std::vector<FQueuedRecord>& Queue)
{
	std::FILE* File = TraceProtoHandle();
	if (File == nullptr)
	{
		return;
	}
	for (const FQueuedRecord& Record : Queue)
	{
		FProtoWriter Event;
		FProtoWriter::BuildTrackEvent(Event, Record.Type, Record.TrackUuid, "maho", Record.Name,
			Record.FlowIds.data(), Record.FlowIds.size(),
			Record.Terminating.data(), Record.Terminating.size());

		FProtoWriter Packet;
		Packet.VarintField(8, Record.Ts * kNanosPerMicro);
		Packet.VarintField(10, kProtoSeq);
		Packet.BytesField(11, Event.Bytes, Event.Used);

		FProtoWriter Wrapped;
		Wrapped.BytesField(1, Packet.Bytes, Packet.Used);
		std::fwrite(Wrapped.Bytes, 1, Wrapped.Used, File);
	}
	std::fflush(File);
}

/** One bar: BEGIN in the stage's constructor, END in its destructor -- no duration is ever written,
 *  which is also why nothing here can overlap or need trimming.
 *
 *  This only QUEUES; the packets are assembled at TraceFlush, once every end of every edge is in.
 *  See FQueuedRecord.
 *
 *  `bPoolMirror` is for the bars that belong to a RESIDENT thread (the SCOPE form): those are not the
 *  pool's work, so they are not filed under the pool's group. Everything else -- every stage bar -- is
 *  mirrored there as well. */
void ProtoEmitSlice(const char* Lane, const char* Owner, const FNameSlice& Stage, const FNameSlice& Name,
	std::uint64_t Ts, bool bBegin, std::vector<FWaitKey> WaitsFor = {},
	std::vector<FWaitKey> Blocks = {}, bool bPoolMirror = true)
{
	// Bars are emitted from every pool worker, so the row registry and the queue both need the lock.
	if (TraceFormat() == 0)
	{
		return;
	}

	// This thread's row name in the pool's group. Resolved BEFORE the lock on purpose: it walks the
	// thread registry, which takes the SAME mutex the queue does -- asking for it here would be a
	// recursive lock (MSVC reports that as "resource deadlock would occur").
	const char* const ThreadRow = TraceThreadRowName();

	std::lock_guard<std::mutex> Lock(GThreadMutex);

	std::vector<FQueuedRecord>& Queue = ProtoQueue();
	const std::size_t PrimaryIndex = Queue.size();
	FQueuedRecord& Item = Queue.emplace_back();
	Item.Ts = Ts;
	Item.Type = bBegin ? 1 : 2;
	// The row is resolved NOW, and the names are copied NOW: the emitting frame, and the module that
	// spells its names, do not have to be alive when the flush runs (see FWaitKey).
	Item.TrackUuid = ProtoTrackUuid(Lane, Owner);
	Item.Owner.assign(Owner);
	Item.Stage.assign(Stage.Data, Stage.Size);
	Item.Name = (Name.Data != nullptr && Name.Size > 0) ? std::string(Name.Data, Name.Size)
													   : std::string{};
	Item.WaitsFor = std::move(WaitsFor);
	Item.Blocks   = std::move(Blocks);

	if (!bPoolMirror)
	{
		return;
	}

	// MIRROR -- the same bar, filed under the POOL's group as well. The engine runs every graph on ONE
	// shared FThreadPool, so lining the bars up by the THREAD that ran them is what makes the stage
	// work of every frame read as a single timeline: the frame groups answer "which frame", this one
	// answers "which thread, and what else was it doing". The row no longer says where the bar came
	// from, so the NAME carries the frame it belongs to.
	//
	// Its arrows are added by the join, as the twins of the frame group's own: the twin link is the
	// index below, since a twin never declares anything (its row is a thread, and no declared edge
	// could resolve against that).
	std::string PoolName(Owner != nullptr ? Owner : "");
	if (Name.Data != nullptr && Name.Size > 0)
	{
		if (!PoolName.empty())
		{
			PoolName += ' ';
		}
		PoolName.append(Name.Data, Name.Size);
	}

	const std::size_t MirrorIndex = Queue.size();
	FQueuedRecord& Mirror = Queue.emplace_back();
	Mirror.Ts = Ts;
	Mirror.Type = bBegin ? 1 : 2;
	Mirror.TrackUuid = ProtoTrackUuid(kPoolLane, ThreadRow);
	Mirror.Owner.assign(ThreadRow);
	Mirror.Stage.assign(Stage.Data, Stage.Size);
	Mirror.Name = std::move(PoolName);

	// Written by INDEX, after both records exist: an intervening emplace_back can move the vector, so
	// the primary's reference is not usable for this.
	Queue[PrimaryIndex].MirrorIndex = MirrorIndex;
}

/** Write one assembled line, or nothing at all when it overflowed. */
void EmitLine(const FLine& Line)
{
	if (TraceFormat() == 1)
	{
		return;   // native only: the text file is one of the two ways out, not the only one
	}
	std::FILE* File = TraceFileHandle();
	if (File == nullptr || Line.bOverflow)
	{
		return;
	}
	std::fwrite(Line.Bytes, 1, Line.Used, File);
}

/** `ts`/`dur`/`lane`/`worker`/`owner` -- the prefix every bar shares.
 *
 *  LANE + OWNER is the ROW: the lane folds into one section (the architecture), and inside it each
 *  frame is a track. The worker is emitted too, but only as information: it is who RAN the node,
 *  not what the row means. */
void WriteHead(FLine& Line, const char* Group, const char* Track,
	std::uint64_t StartMicros, std::uint64_t DurMicros)
{
	Line.Literal("[tr] ts=");
	Line.Decimal(StartMicros);
	Line.Literal(" dur=");
	Line.Decimal(DurMicros);
	Line.Literal(" lane=");
	Line.CString((Group != nullptr && Group[0] != '\0') ? Group : kGlobalLane);
	Line.Literal(" worker=");
	Line.Decimal(TraceThreadIndex());
	if (Track != nullptr && Track[0] != '\0')
	{
		Line.Literal(" owner=");
		Line.CString(Track);
	}
}

/** The trailing `[stage=] name= [func=] [tip=]` every bar ends with. `Stage`/`Func` are SLICES (the
 *  shortened forms); an empty slice means the field is not written. */
void WriteTail(FLine& Line, const FNameSlice& Stage, const FNameSlice& Func, const FNameSlice& Name,
	const char* Tip)
{
	if (Stage.Size != 0)
	{
		Line.Literal(" stage=");
		Line.Slice(Stage);
	}
	if (Func.Size != 0)
	{
		Line.Literal(" func=");
		Line.Slice(Func);
	}
	Line.Literal(" name=");
	Line.Slice(Name);
	if (Tip != nullptr && Tip[0] != '\0')
	{
		Line.Literal(" tip=");
		Line.CString(Tip);
	}
	Line.Newline();
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

void TraceFlush()
{
	// The native file is written HERE: the flow ids can only be minted once every bar of the frame is
	// in (FQueuedRecord), and the packets have to leave in timestamp order. Descriptors are written
	// directly, as they are discovered, so they stay ahead of the packets that reference them.
	{
		std::lock_guard<std::mutex> Lock(GThreadMutex);
		std::vector<FQueuedRecord>& Queue = ProtoQueue();
		if (!Queue.empty())
		{
			ProtoJoinFlows(Queue);
			std::stable_sort(Queue.begin(), Queue.end(),
				[](const FQueuedRecord& A, const FQueuedRecord& B)
				{
					// Equal timestamps are the norm for two bars in the same microsecond, and a BEGIN
					// must come first there -- otherwise the viewer reports MISPLACED_END_EVENT.
					return A.Ts != B.Ts ? A.Ts < B.Ts : A.Type < B.Type;
				});
			ProtoWriteRecords(Queue);
			Queue.clear();
		}
	}
	if (std::FILE* File = TraceFileHandle())
	{
		std::fflush(File);
	}
}

FStageTrace::FStageTrace(const FFrameExtension* InFrame, const std::type_info& InStage,
	const char* InGroup, const char* InTrack, const char* InLabel, const char* InTip,
	const std::type_info* InPrevStage)
	: Frame(InFrame), Stage(InStage), Group(InGroup), Track(InTrack), Label(InLabel), Tip(InTip),
	  PrevStage(InPrevStage)
{
	if (!TraceEnabled() || Frame == nullptr)
	{
		return;
	}

	// The edges THIS stage declared, resolved ONCE: a frame authors its declarations in its own
	// constructor, so the vector is stable for the whole stage call -- and the flow hints below are
	// written straight from it, with no lookup on the way out.
	const auto& Declared = Frame->GetDependencies();
	const auto  It = Declared.find(Stage);
	if (It != Declared.end())
	{
		Edges = &It->second;
	}

	PreviousGroup = GCurrentGroup;
	PreviousTrack = GCurrentTrack;
	PreviousStage = GCurrentStage;

	// This bar ESTABLISHES the row: every section opened inside the stage body (in this function or
	// in any helper the stage calls) inherits it, which is what puts them on the frame's track.
	GCurrentGroup = Group;
	GCurrentTrack = Track;
	GCurrentStage = ShortStageName(Stage.name());

	// The edges this stage WAITS for, queued as raw KEYS: the id cannot be minted here, because whether
	// the other end exists -- and which of its frame instances it is -- is only knowable once every bar
	// of the frame is in. The frame offset travels with the key; that is the whole answer to "which
	// instance". Only the FORWARD declarations live in this table -- the reverse ones (`BlockOn`, where
	// I am the producer instead) are in the frame's separate dependents table, and are queued at the
	// END: the arrow leaves MY end, so that is the record the id belongs on.
	const FStaticName StageShort = ShortStageName(Stage.name());
	std::vector<FWaitKey> WaitsFor;
	if (Edges != nullptr)
	{
		for (const FFrameExtension::FEdge& Edge : *Edges)
		{
			WaitsFor.push_back(ProtoWaitKey(Edge));
		}
	}

	Start = TraceNowMicros() + static_cast<std::uint64_t>(GDepth);
	++GDepth;
	// The NATIVE begin is a SECOND, raw reading: `Start` carries a depth offset (the text path needs it
	// so nested bars do not look simultaneous), and mixing it with a raw END produced END-before-BEGIN,
	// which the viewer reports as MISPLACED_END_EVENT. Two raw readings cannot invert.
	ProtoEmitSlice(Group, Track, StageShort, FNameSlice{ Label, std::strlen(Label) },
		TraceNowMicros(), true, std::move(WaitsFor));
}

FStageTrace::~FStageTrace()
{
	if (Start == kNotTracing)
	{
		// Tracing was off when this scope opened.
		return;
	}

	const std::uint64_t Now = TraceNowMicros();
	const std::uint64_t Dur = (Now > Start) ? (Now - Start) : 0;
	const FNameSlice    StageShort = ShortStageName(Stage.name());
	const FNameSlice    LabelSlice{ Label, std::strlen(Label) };
	// The edges I BLOCK (`BlockOn`): the arrow leaves THIS bar, so the id is opened here. The target
	// stays ignorant of the edge by design -- it only ever receives the terminating id -- which is also
	// why these do not live with my own `Edges`: a reverse declaration is filed under MY stage in the
	// dependents table, and nothing on the other side ever learns that it is a consumer.
	std::vector<FWaitKey> Blocks;
	if (Frame != nullptr)
	{
		for (const auto& Dependent : Frame->GetDependents())
		{
			if (Dependent.first == Stage && Dependent.second.bReverse)
			{
				Blocks.push_back(ProtoWaitKey(Dependent.second));
			}
		}
	}
	// The native bar closes here, unconditionally: BEGIN/END must pair, and the native path has no
	// duration floor (there is nothing to filter -- a bar that is short is simply short).
	ProtoEmitSlice(Group, Track, StageShort, LabelSlice, Now, false, {}, std::move(Blocks));

	// The floor applies to STAGE bars and to nothing else: a stage bar is generated for EVERY stage
	// of EVERY frame, which is exactly what makes a trace expensive, whereas a section or a scope is
	// a human decision about what matters. A dropped bar takes its flow hints with it -- an arrow
	// needs both ends to exist.
	if (Dur >= TraceTaskFloorMicros())
	{
		FLine Line;
		WriteHead(Line, Group, Track, Start, Dur);
		WriteTail(Line, StageShort, FNameSlice{}, LabelSlice, Tip);
		EmitLine(Line);

		if (Edges != nullptr)
		{
			// One hint per DECLARED edge: the producer this bar waited for. The reader binds both
			// ends by (lane, owner, stage) -- which is why the short stage name travels on both
			// sides of the arrow.
			for (const FFrameExtension::FEdge& Edge : *Edges)
			{
				FLine Flow;
				Flow.Literal("[tr] flow ts=");
				Flow.Decimal(Start);
				Flow.Literal(" lane=");
				Flow.CString((Group != nullptr && Group[0] != '\0') ? Group : kGlobalLane);
				Flow.Literal(" owner=");
				Flow.CString((Track != nullptr && Track[0] != '\0') ? Track : kGlobalLane);
				Flow.Literal(" stage=");
				Flow.Slice(StageShort);
				Flow.Literal(" from=");
				Flow.Append(Edge.TargetName.data(), Edge.TargetName.size());
				Flow.Literal(" fromStage=");
				Flow.Slice(ShortStageName(Edge.TargetStage.name()));
				Flow.Literal(" off=");
				Flow.SignedDecimal(static_cast<std::int64_t>(Edge.FrameOffset));
				Flow.Newline();
				EmitLine(Flow);
			}
		}

		if (PrevStage != nullptr)
		{
			// The IN-FRAME CHAIN edge: the stage the scheduler ran just before this one, in the
			// order the frame's own stage list declares. It is structural (FFrameBridge::Build emits
			// it), so no frame can declare it -- deducing it is what puts an arrow on the inside of
			// every frame's pipeline, not only on the edges someone spelled out by hand.
			FLine Flow;
			Flow.Literal("[tr] flow ts=");
			Flow.Decimal(Start);
			Flow.Literal(" lane=");
			Flow.CString((Group != nullptr && Group[0] != '\0') ? Group : kGlobalLane);
			Flow.Literal(" owner=");
			Flow.CString((Track != nullptr && Track[0] != '\0') ? Track : kGlobalLane);
			Flow.Literal(" stage=");
			Flow.Slice(StageShort);
			Flow.Literal(" from=");
			Flow.CString((Track != nullptr && Track[0] != '\0') ? Track : kGlobalLane);
			Flow.Literal(" fromStage=");
			Flow.Slice(ShortStageName(PrevStage->name()));
			Flow.Literal(" off=");
			Flow.SignedDecimal(0);
			Flow.Newline();
			EmitLine(Flow);
		}
	}

	--GDepth;
	GCurrentGroup = PreviousGroup;
	GCurrentTrack = PreviousTrack;
	GCurrentStage = PreviousStage;
}

FScopeTrace::FScopeTrace(const char* InGroup, const char* InTip, const char* InFunc)
	: Group(InGroup), Tip(InTip), Func(InFunc)
{
	if (!TraceEnabled())
	{
		return;
	}

	PreviousGroup = GCurrentGroup;
	PreviousTrack = GCurrentTrack;
	PreviousStage = GCurrentStage;

	// A lane of its OWN: the group is the track too, so a resident worker reads as one row named by
	// its role, and whatever it starts (a task body, a nested scope) lands there.
	GCurrentGroup = Group;
	GCurrentTrack = Group;
	GCurrentStage = FStaticName{};

	Start = TraceNowMicros() + static_cast<std::uint64_t>(GDepth);
	++GDepth;

	// The NATIVE bar too, and this is the form it exists for: a resident worker gets a group of its
	// OWN with a single row (its role). It declares no edges -- a scope has no declaration table to
	// read -- so it never carries an arrow, which is what a worker's own timeline should look like;
	// and it is NOT mirrored into the pool's group, because a resident thread is not the pool. The
	// raw reading matches the END below, for the reason spelled out in FStageTrace.
	ProtoEmitSlice(Group, Group, FStaticName{}, ShortScopeName(Func), TraceNowMicros(), true, {}, {},
		false);
}

FScopeTrace::~FScopeTrace()
{
	if (Start == kNotTracing)
	{
		return;
	}

	const std::uint64_t Now = TraceNowMicros();
	const FNameSlice    FuncShort = ShortScopeName(Func);

	// The native bar closes with the same name it opened with (see the constructor).
	ProtoEmitSlice(Group, Group, FStaticName{}, FuncShort, Now, false, {}, {}, false);

	// No floor here: a hand-written scope is never filtered (see the header).
	FLine Line;
	WriteHead(Line, Group, Group, Start, (Now > Start) ? (Now - Start) : 0);
	WriteTail(Line, FStaticName{}, FuncShort, FuncShort, Tip);
	EmitLine(Line);

	--GDepth;
	GCurrentGroup = PreviousGroup;
	GCurrentTrack = PreviousTrack;
	GCurrentStage = PreviousStage;
}

FSectionTrace::FSectionTrace(const char* InLabel, const char* InTip, const char* InFunc)
	: Label(InLabel), Tip(InTip), Func(InFunc)
{
	if (!TraceEnabled())
	{
		return;
	}

	// INHERITED, not established: the enclosing stage (or scope) already put this thread on a row,
	// and this section belongs on it -- nesting inside that stage's bar.
	Group = GCurrentGroup;
	Track = GCurrentTrack;
	Stage = GCurrentStage;

	Start = TraceNowMicros() + static_cast<std::uint64_t>(GDepth);
	++GDepth;
}

FSectionTrace::~FSectionTrace()
{
	if (Start == kNotTracing)
	{
		return;
	}

	const std::uint64_t Now = TraceNowMicros();
	const FNameSlice    LabelSlice{ Label, std::strlen(Label) };

	// No floor here either; and no row restore, because nothing was replaced.
	FLine Line;
	WriteHead(Line, Group, Track, Start, (Now > Start) ? (Now - Start) : 0);
	WriteTail(Line, Stage, ShortScopeName(Func), LabelSlice, Tip);
	EmitLine(Line);

	--GDepth;
}

#else // MAHO_WITH_TRACE

// Shipping: none of the trace machinery is compiled -- no writer, no queue, no proto, no scope
// objects. What survives is the SWITCH SURFACE the rest of the engine still calls: the CVar driver
// and FLog's shutdown (the log plugin owns the trace, so the trace's absence must not break it).

bool TraceEnabled()
{
	return false;
}

void TraceSetEnabled(bool /*bEnabled*/)
{
}

std::uint64_t TraceTaskFloorMicros()
{
	return 0;
}

std::uint64_t TraceNowMicros()
{
	return 0;
}

void TraceFlush()
{
}

#endif // MAHO_WITH_TRACE

} // namespace Maho
