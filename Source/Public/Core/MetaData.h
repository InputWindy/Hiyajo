#pragma once

// MetaData -- the engine's ONE generic metadata store, and the query entry point for all of it.
//
// ── IT KNOWS NOTHING ABOUT WHAT IT STORES ────────────────────────────────────────────────────────
//
// No frames, no stages, no resources, no schema: a keyed store of ENCODED VALUES, and that is the
// whole interface.
//
//     Register(Type, Key, Encoded)     // the REGISTERING side encodes
//     Find(Type, Key) -> bytes         // the CONSUMING side decodes
//
// The manager never interprets a byte. That is the design, not an omission: what the engine knows
// about itself changes faster than any schema would, and a manager that NAMED those things would
// have to change every time one was added. So the TYPE is a string the owner picks, the KEY is a
// string the owner composes (from type names, from a `frame::stage` pair, from whatever identifies
// its record), and the VALUE is bytes the owner produced.
//
// What a type MEANS therefore lives with the code that writes it -- and so does the decoder for it:
//
//     type "Frame"           Engine/Frame.h        value: the collector a frame declares
//     type "FrameInstalled"  Engine/FrameBuilder.h value: the collector that installed it
//     type "Stage"           Engine/Frame.h        key: `Collector::Stage`, value: the method
//     type "Resource"        Engine/ResourceName.h   (declared name; the value is empty)
//     type "ResourceOrdering" Engine/StageMetadata.h value: the ordering, as spelled in the declaration
//
// It is the engine's own RTTI in that sense: the same question `typeid` answers -- "what is this / who
// declares it / what does it belong to" -- asked without RTTI and across a DLL boundary, with the
// answer being whatever the declaring module chose to file.
//
// ── WHY ONE INSTANCE, WHY IN CORE, WHY NEVER TORN DOWN ───────────────────────────────────────────
//
// Every fact in here is produced by MANY modules: a stage is declared in its plugin's header, a frame
// is installed by another plugin at run time, a resource is authored somewhere else entirely. Nothing
// owns the whole picture, so it has to be collected -- and only ONE table may collect it, or "what do
// we know" has two answers.
//
// It lives in Core because Core is the only layer every plugin links and includes (the engine must not
// depend on a plugin), and the instance is reached through an exported ACCESSOR so every DLL sees the
// same one (a variable would be copied per module -- the bug this exists to avoid). It is never
// destroyed: registration runs in static initializers of modules that may still be mapped while the
// process is tearing down, and a late reader must not find a destroyed table.
//
// AND NOTHING ELSE ABOUT IT LIVES IN CORE -- the store is the whole subject. Everything that defines
// what a record MEANS (the types a frame, a stage or a resource is filed under, and the code that
// encodes and decodes them) belongs to the layer that owns the CONCEPT: frames and stages and the
// resources they touch are the ENGINE's, so those headers are `Engine/Frame.h` and
// `Engine/StageMetadata.h`. Core growing one header per metadata type the application invents is
// exactly the schema this design refuses.

#include <Core/Export.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

/** One stored entry, as a READER sees it. OWNED strings, so a consumer that decodes it can do so
 *  without holding any lock (registration keeps going on in other modules). */
struct FMetaDataEntry
{
	std::string Type;
	std::string Key;
	/** The encoded bytes, exactly as they were registered. What they MEAN is the owner's business. */
	std::string Value;
};

/** The store. Thread safe (registration happens from many modules' static initializers at once). */
class MAHO_API FMetaData
{
public:
	FMetaData();
	~FMetaData();

	FMetaData(const FMetaData&) = delete;
	FMetaData& operator=(const FMetaData&) = delete;

	/** File `Encoded` under `(Type, Key)`.
	 *
	 *  IDEMPOTENT BY CONTENT: a declaration written in a HEADER is expanded in every translation unit
	 *  that includes it, so the same `(type, key, bytes)` arrives many times, always identical (the
	 *  same literals, encoded in different modules) -- the first insertion wins and identical later
	 *  ones are dropped. The same key with DIFFERENT bytes is a real conflict (two spellings of one
	 *  identity) and is REPORTED: the message can only state the sizes, because the manager has no
	 *  decoder to say what differs. */
	void Register(std::string_view Type, std::string_view Key, std::string_view Encoded);

	/** The entry, or nullptr. Valid for the life of the store (its storage is never moved). */
	[[nodiscard]] const FMetaDataEntry* Find(std::string_view Type, std::string_view Key) const;

	/** How many entries a type holds; 0 for a type nobody registered. */
	[[nodiscard]] std::size_t Count(std::string_view Type) const;

	/** Every type that currently has entries, sorted (a diagnostic that moves between runs cannot be
	 *  diffed against a baseline). */
	[[nodiscard]] std::vector<std::string> Types() const;

	/** A COPY of one type's entries, sorted by key. This is what a consumer that DECODES them runs on:
	 *  taken in one pass under the lock, then decoded with no lock held (decoding is the caller's
	 *  code, and it may take as long as it likes). */
	[[nodiscard]] std::vector<FMetaDataEntry> Snapshot(std::string_view Type) const;

	/** Print everything to stderr, ONCE, when `MAHO_METADATA` is set (the `MAHO_GRAPH_*` family's
	 *  spelling: an env var lets ONE binary produce both the baseline run and the diagnostic run).
	 *  Called from the host's frame loop, i.e. after every registering module is mapped -- the values
	 *  are bytes inside those modules' literals. Idempotent. */
	void DumpOnce() const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

/** The process's metadata store. A free function, not an exported variable: a variable would be
 *  copied per module. The instance is a heap object in Core's own .cpp (see the note above). */
[[nodiscard]] MAHO_API FMetaData& GetMetaData() noexcept;

/** Drop any namespace qualification: `Maho::FRender` and `FRender` are the same identity. Generic
 *  name plumbing, here because every owner composes its KEYS out of type names -- and because the two
 *  sides of a check are often spelled in different files, one of which can only reach a qualified
 *  name. */
[[nodiscard]] MAHO_API std::string_view BareTypeName(std::string_view Qualified) noexcept;

} // namespace Maho
