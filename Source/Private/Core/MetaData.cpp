#include <Core/MetaData.h>

// MetaData.cpp -- the generic store's storage, its lookup and its dump. NOTHING in this file knows
// what a type means; see the header for why that is the design.
//
// ── IDEMPOTENT BY CONTENT, AND WHY THAT IS THE ONLY RULE ────────────────────────────────────────
//
// A declaration written in a header is expanded in every translation unit that includes it -- in every
// DLL that mounts the plugin -- so the same `(type, key, bytes)` is registered many times, always
// identical (the same literals, encoded in different modules). The first insertion wins and identical
// later ones are dropped. The same key with DIFFERENT bytes cannot happen by accident and is a real
// conflict: two spellings of one identity, which the message reports by SIZE only -- the manager has
// no decoder, and inventing one would be the schema it exists to avoid.

#include <Core/Fatal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

// ── storage ─────────────────────────────────────────────────────────────────────────────────────

struct FMetaData::FImpl
{
	std::mutex Mutex;

	/** type -> key -> entry. Nodes of an `unordered_map` are STABLE, which is what lets `Find` hand out
	 *  a pointer that stays valid while other modules keep registering. */
	std::unordered_map<std::string, std::unordered_map<std::string, FMetaDataEntry>> Types;

	std::once_flag DumpOnce;
};

FMetaData::FMetaData()
	: Impl(std::make_unique<FImpl>())
{
}

FMetaData::~FMetaData() = default;

FMetaData& GetMetaData() noexcept
{
	// Deliberately a heap instance that is never destroyed: registration runs in static initializers of
	// modules that may still be mapped during teardown, and a late reader must not find a destroyed
	// table (see the header). The process is exiting; the memory is not the thing to reclaim.
	static FMetaData* const Instance = new FMetaData();
	return *Instance;
}

std::string_view BareTypeName(std::string_view Qualified) noexcept
{
	const std::size_t Pos = Qualified.rfind("::");
	return Pos == std::string_view::npos ? Qualified : Qualified.substr(Pos + 2);
}

// ── registration ────────────────────────────────────────────────────────────────────────────────

void FMetaData::Register(std::string_view Type, std::string_view Key, std::string_view Encoded)
{
	if (Type.empty() || Key.empty())
	{
		// Nothing to file it under: a type or a key that came out empty is a declaration bug at the
		// registering side (a macro argument that did not expand, a name trait with no specialization),
		// and storing it would put an unfindable row in the table.
		MAHO_CHECKF(false, "FMetaData::Register: empty %s (type='%.*s', key='%.*s')",
			Type.empty() ? "type" : "key",
			static_cast<int>(Type.size()), Type.data(),
			static_cast<int>(Key.size()), Key.data());
		return;
	}

	std::string Conflict;
	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		std::unordered_map<std::string, FMetaDataEntry>& ByKey = Impl->Types[std::string(Type)];
		auto [It, bInserted] = ByKey.try_emplace(std::string(Key));
		FMetaDataEntry& Entry = It->second;
		if (bInserted)
		{
			Entry.Type.assign(Type);
			Entry.Key.assign(Key);
			Entry.Value.assign(Encoded);
		}
		else if (Entry.Value != Encoded)
		{
			Conflict = std::string("type '") + std::string(Type) + "', key '" + std::string(Key)
				+ "': registered twice with DIFFERENT content (" + std::to_string(Entry.Value.size())
				+ " vs " + std::to_string(Encoded.size()) + " bytes) -- one key must identify one record";
		}
	}

	MAHO_CHECKF(Conflict.empty(), "%s", Conflict.c_str());
}

const FMetaDataEntry* FMetaData::Find(std::string_view Type, std::string_view Key) const
{
	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	const auto ByType = Impl->Types.find(std::string(Type));
	if (ByType == Impl->Types.end())
	{
		return nullptr;
	}
	const auto It = ByType->second.find(std::string(Key));
	return It == ByType->second.end() ? nullptr : &It->second;
}

std::size_t FMetaData::Count(std::string_view Type) const
{
	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	const auto ByType = Impl->Types.find(std::string(Type));
	return ByType == Impl->Types.end() ? 0 : ByType->second.size();
}

std::vector<std::string> FMetaData::Types() const
{
	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	std::vector<std::string> Names;
	Names.reserve(Impl->Types.size());
	for (const auto& Pair : Impl->Types)
	{
		Names.push_back(Pair.first);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}

std::vector<FMetaDataEntry> FMetaData::Snapshot(std::string_view Type) const
{
	std::vector<FMetaDataEntry> Rows;
	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		const auto ByType = Impl->Types.find(std::string(Type));
		if (ByType == Impl->Types.end())
		{
			return Rows;
		}
		Rows.reserve(ByType->second.size());
		for (const auto& Pair : ByType->second)
		{
			Rows.push_back(Pair.second);
		}
	}
	std::sort(Rows.begin(), Rows.end(),
		[](const FMetaDataEntry& A, const FMetaDataEntry& B) { return A.Key < B.Key; });
	return Rows;
}

// ── the dump ────────────────────────────────────────────────────────────────────────────────────

void FMetaData::DumpOnce() const
{
	std::call_once(Impl->DumpOnce, [this]()
	{
		if (std::getenv("MAHO_METADATA") == nullptr)
		{
			return;
		}

		const std::vector<std::string> Names = Types();
		std::size_t Total = 0;
		for (const std::string& Type : Names)
		{
			Total += Count(Type);
		}

		std::fprintf(stderr, "[metadata] types=%zu entries=%zu\n", Names.size(), Total);
		for (const std::string& Type : Names)
		{
			const std::vector<FMetaDataEntry> Rows = Snapshot(Type);
			std::fprintf(stderr, "[metadata] type %s entries=%zu\n", Type.c_str(), Rows.size());
			for (const FMetaDataEntry& Row : Rows)
			{
				// The value is opaque: printed as text when it happens to be text (the common case --
				// names and short records), as hex otherwise. Rendering is the only place the store
				// touches the bytes at all, and it never interprets them.
				const bool bText = std::all_of(Row.Value.begin(), Row.Value.end(),
					[](unsigned char C) { return C >= 0x20 && C < 0x7F; });
				std::fprintf(stderr, "[metadata] %s %s value=", Type.c_str(), Row.Key.c_str());
				if (bText)
				{
					std::fprintf(stderr, "%.*s", static_cast<int>(Row.Value.size()), Row.Value.data());
				}
				else
				{
					for (const unsigned char C : Row.Value)
					{
						std::fprintf(stderr, "%02x", C);
					}
				}
				std::fprintf(stderr, " (%zu bytes)\n", Row.Value.size());
			}
		}
		std::fflush(stderr);
	});
}

// ── NOTHING ELSE ────────────────────────────────────────────────────────────────────────────────
//
// The registrars a declaration macro expands are NOT here, and that is the point of this file: the
// store is generic, so the code that defines a record's meaning belongs to the layer that owns the
// concept -- frames, stages and the resources they touch are the ENGINE's (see Engine/Frame.h and
// Engine/StageMetadata.h). Core would otherwise grow one header per metadata type the application
// happens to invent, which is the schema it exists to avoid.

} // namespace Maho
