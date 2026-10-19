/// @file
/// @brief The vocabulary of a MEMORY ACCESS: which piece of memory a piece of work touches, and how.
///
/// This is the lower half of the declaration/derivation pair. A *declaration* (a layer saying what its work
/// items touch) and a *derivation* (turning those declarations into the predecessor keys a batch carries)
/// both need to say the same sentence -- "this work reads that memory" -- and neither may depend on the
/// other, so the sentence lives here, next to `Core/Task/TaskBuilder.h`'s key vocabulary: both are what a layer
/// SPEAKS, not what the graph RUNS.
///
/// There is deliberately no RESOURCE here, and no ordering policy: this layer says what memory is touched
/// and how (a fact); what a resource IS, how accesses to it are ordered, and which accesses are exempt from
/// the derived order are decisions of the layer above. (A scratch buffer, an upload-ring slot, or a
/// sub-range of a bigger object are all "memory" and none of them is a "resource".)
///
/// The one property this header relies on is that a REGION TOKEN is stable and comparable: it is an
/// address of something that outlives every use -- the repo's usual "row address" identity, e.g. the address
/// of a per-type constant. It is never dereferenced here.
#pragma once


#include <cstdint>
#include <cstddef>

namespace Maho
{
namespace Core
{

/// @brief What one piece of work does to one region of memory during its call.
///
/// Two modes, because these are the two things a barrier system can order: a read (which needs what it
/// reads to be visible) and a write (which must not race anything). Existence (create/destroy) and
/// hand-offs are NOT here: the first is a lifetime question, the second a relation between two specific
/// accesses -- both belong to the layer that owns the objects, not to the memory-access vocabulary.
enum class EMemoryAccess : std::uint8_t
{
	Read,
	Write,
};

/// @brief One memory access, as a declaration states it.
///
/// Two tokens, not one: the REGION says which DECLARATION (which kind of memory) and the INSTANCE says which
/// COPY of it. Ordering state is keyed by the pair, because two entities' `Position` are two pieces of memory
/// (and so are the K slots of a ring) -- an access that named only the region would relate them falsely.
struct FMemoryAccess
{
	/// @brief Which kind of memory: an opaque, stable token (an address -- never dereferenced, only compared).
	const void* Region = nullptr;

	/// @brief Which copy of it (an address), or null for a region that has only one.
	const void* Instance = nullptr;

	/// @brief What is done with it.
	EMemoryAccess Mode = EMemoryAccess::Read;
};

/// @brief Whether an access is a WRITE (the cheap question every ordering rule starts from).
/// @param Access The access to ask about.
/// @return True when it writes.
[[nodiscard]] constexpr bool IsWrite(const FMemoryAccess& Access) noexcept
{
	return Access.Mode == EMemoryAccess::Write;
}

}
}
