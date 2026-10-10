#include <Core/StageMetadata.h>

// StageMetadata.cpp -- the graph-level access table's storage, its lookup and its dump.
//
// ── WHY A FUNCTION-LOCAL STATIC, AND NOT A MEMBER OF THE GRAPH ──────────────────────────────────
//
// The table is process-wide by nature (every frame in every graph registers into it, and step 4 has to
// read ONE table when it derives edges), while the graph is per DRIVER (the host owns one, a collector
// frame owns another -- see the note on FEngineBase::Graph). A table living in a graph would therefore
// be a table per driver, and "who touches what" is not a per-driver question. A function-local static
// also gives one instance per PROCESS within this module, which is the same shape Core's other
// process-wide values (K, the trace switch) use -- and it is reachable from every plugin because the
// accessor is exported (`MAHO_API`), not because each DLL has its own copy.
//
// ── REGISTRATION IS IDEMPOTENT BY CONTRACT, NOT BY LUCK ────────────────────────────────────────
//
// A declaration written in a HEADER is expanded in every translation unit that includes it, in every
// DLL that mounts the plugin -- so one row is routinely registered many times, always with the same
// content (the same literals, resolved in different modules). That is legal and expected: the first
// insertion wins and later identical ones are dropped. A row that DIFFERS under the same key can only
// come from two spellings of one identity, which is a real conflict, and the report names both sides.

#include <Core/Fatal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

namespace
{

	/** The table's key: a node's identity without the phase (which is a property of a ROUND, not of the
	 *  declaration -- a row says "this stage of this frame", and it drives the same node every round). */
	struct FBindingKey
	{
		std::string_view Frame{};
		std::string_view Stage{};

		[[nodiscard]] bool operator==(const FBindingKey& Other) const noexcept
		{
			return Frame == Other.Frame && Stage == Other.Stage;
		}
	};

	struct FBindingKeyHash
	{
		[[nodiscard]] std::size_t operator()(const FBindingKey& Key) const noexcept
		{
			// The same mixing the graph's own node hash uses (FTaskKeyHash): string_view's hash plus the
			// golden-ratio constant, combined so that "A::BC" and "AB::C" do not collide.
			std::size_t H = std::hash<std::string_view>{}(Key.Frame);
			H ^= std::hash<std::string_view>{}(Key.Stage) + 0x9e3779b9u + (H << 6) + (H >> 2);
			return H;
		}
	};

	/** One stored row: the public face, plus the accesses it points at. The storage is a `std::vector`
	 *  filled ONCE at registration and never touched again, which is what makes the pointer in the row
	 *  valid for the process's lifetime -- and the map's nodes are stable, so the row pointer is too. */
	struct FEntry
	{
		FStageAccessRow             Row{};
		std::vector<FResourceAccess> Accesses;
	};

	struct FTable
	{
		std::mutex                                        Mutex;
		std::unordered_map<FBindingKey, FEntry, FBindingKeyHash> Rows;
	};

	FTable& GetTable()
	{
		static FTable Instance;
		return Instance;
	}

	/** Set by the first Dump, so the dump is a one-shot however many drivers call it. */
	std::once_flag GDumpOnce;

} // namespace

const char* ToString(EResourceAccess Access) noexcept
{
	switch (Access)
	{
	case EResourceAccess::Create:  return "Create";
	case EResourceAccess::Read:    return "Read";
	case EResourceAccess::Write:   return "Write";
	case EResourceAccess::Destroy: return "Destroy";
	}
	return "?";
}

const char* ToString(EResourceOrdering Ordering) noexcept
{
	switch (Ordering)
	{
	case EResourceOrdering::LifetimeOnly: return "LifetimeOnly";
	case EResourceOrdering::Snapshot:     return "Snapshot";
	case EResourceOrdering::Strict:       return "Strict";
	}
	return "?";
}

void RegisterStageAccess(std::string_view Frame, std::string_view Stage, std::string_view Method,
	std::initializer_list<FResourceAccess> Accesses)
{
	FTable& Table = GetTable();
	std::lock_guard<std::mutex> Lock(Table.Mutex);

	const FBindingKey Key{ Frame, Stage };
	auto [It, bInserted] = Table.Rows.try_emplace(Key);
	FEntry& Entry = It->second;

	if (bInserted)
	{
		Entry.Row.Frame = Frame;
		Entry.Row.Stage = Stage;
		Entry.Row.Method = Method;
		// `Frame == Stage` is how the declaration macro spells "this is the stage's own dispatch": the
		// declaration's first argument IS the stage interface there (see MAHO_IMPLEMENT_STAGE).
		Entry.Row.bInterfaceBinding = (Frame == Stage);
		Entry.Accesses.assign(Accesses.begin(), Accesses.end());
		Entry.Row.Accesses = Entry.Accesses.data();
		Entry.Row.AccessCount = Entry.Accesses.size();
		return;
	}

	// Idempotent re-registration (a header's declaration expanded in another TU): silent when the row
	// is identical, reported when it is not -- two different declarations under one key means the key no
	// longer identifies one thing, and every later consumer (the dump, the derived edges) would be
	// reading whichever of the two happened to arrive first.
	const bool bSame = Entry.Row.Method == Method && Entry.Accesses.size() == Accesses.size()
		&& std::equal(Entry.Accesses.begin(), Entry.Accesses.end(), Accesses.begin(),
			[](const FResourceAccess& A, const FResourceAccess& B)
			{
				return A.Resource == B.Resource && A.Access == B.Access && A.Ordering == B.Ordering;
			});
	MAHO_CHECKF(bSame, "stage metadata: '%.*s::%.*s' is registered twice with different content "
		"(first method '%.*s', second '%.*s') -- one (frame, stage) key may only be declared once",
		static_cast<int>(Frame.size()), Frame.data(), static_cast<int>(Stage.size()), Stage.data(),
		static_cast<int>(Entry.Row.Method.size()), Entry.Row.Method.data(),
		static_cast<int>(Method.size()), Method.data());
	(void)bSame;
}

const FStageAccessRow* FindStageAccess(std::string_view Frame, std::string_view Stage) noexcept
{
	FTable& Table = GetTable();
	std::lock_guard<std::mutex> Lock(Table.Mutex);
	const auto It = Table.Rows.find(FBindingKey{ Frame, Stage });
	return It == Table.Rows.end() ? nullptr : &It->second.Row;
}

std::size_t StageAccessRowCount() noexcept
{
	FTable& Table = GetTable();
	std::lock_guard<std::mutex> Lock(Table.Mutex);
	return Table.Rows.size();
}

void DumpStageAccess()
{
	std::call_once(GDumpOnce, []()
	{
		if (std::getenv("MAHO_GRAPH_META") == nullptr)
		{
			return;
		}

		FTable& Table = GetTable();
		std::vector<const FStageAccessRow*> Rows;
		{
			std::lock_guard<std::mutex> Lock(Table.Mutex);
			Rows.reserve(Table.Rows.size());
			for (const auto& Pair : Table.Rows)
			{
				Rows.push_back(&Pair.second.Row);
			}
		}

		// Sorted by (frame, stage): the map's order is a hash order, and a diagnostic that moves around
		// between runs cannot be diffed -- which is exactly how anyone reads this table.
		std::sort(Rows.begin(), Rows.end(), [](const FStageAccessRow* A, const FStageAccessRow* B)
		{
			return A->Frame != B->Frame ? A->Frame < B->Frame : A->Stage < B->Stage;
		});

		std::fprintf(stderr, "[fgmeta] rows=%zu\n", Rows.size());
		for (const FStageAccessRow* Row : Rows)
		{
			std::fprintf(stderr, "[fgmeta] row %.*s::%.*s method=%.*s kind=%s accesses=",
				static_cast<int>(Row->Frame.size()), Row->Frame.data(),
				static_cast<int>(Row->Stage.size()), Row->Stage.data(),
				static_cast<int>(Row->Method.size()), Row->Method.data(),
				Row->bInterfaceBinding ? "interface" : "frame");
			if (Row->AccessCount == 0)
			{
				std::fprintf(stderr, "-\n");
				continue;
			}
			for (std::size_t I = 0; I < Row->AccessCount; ++I)
			{
				const FResourceAccess& A = Row->Accesses[I];
				std::fprintf(stderr, "%s%.*s/%s(%s)",
					I == 0 ? "" : ",",
					static_cast<int>(A.Resource.size()), A.Resource.data(),
					ToString(A.Access), ToString(A.Ordering));
			}
			std::fprintf(stderr, "\n");
		}
		std::fflush(stderr);
	});
}

} // namespace Maho
