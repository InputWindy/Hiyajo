#pragma once

#include <Core/Misc/Export.h>

#include <cstdint>

namespace Maho
{

/**
 * Opaque handle for a context owned by an FThreadedServer.
 * Stale ids (after the owning task finishes) fail lookup via generation check.
 *
 * Example:
 * ```
 *   Maho::FTaskContextId Id = Server.AllocContext<MyContext>();
 *   if (Id.IsValid())
 *   {
 *       MyContext* Ctx = Server.GetContextAs<MyContext>(Id);
 *   }
 * ```
 */
struct FTaskContextId
{
	std::uint32_t Index = 0;
	std::uint32_t Generation = 0;

	[[nodiscard]] bool IsValid() const { return Generation != 0; }

	bool operator==(const FTaskContextId& Other) const
	{
		return Index == Other.Index && Generation == Other.Generation;
	}

	bool operator!=(const FTaskContextId& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * Base task context. Storage is allocated via FThreadedServer::AllocContext
 * and recycled automatically when the bound task finishes Execute.
 *
 * Example:
 * ```
 *   struct FBakeContext : Maho::FTaskContext
 *   {
 *       int Resolution = 512;
 *   };
 *   auto Id = Server.AllocContext<FBakeContext>();
 *   Server.GetContextAs<FBakeContext>(Id)->Resolution = 1024;
 * ```
 */
class MAHO_API FTaskContext
{
public:
	virtual ~FTaskContext() = default;
};

} // namespace Maho
