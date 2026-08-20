#pragma once

#include <Core/Singleton.h>
#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// ① Extension contract — the identity every extension shares.
//
// An extension is NOTHING but a dependency table: which extensions it
// assembles. How it executes (ExecuteExtension<T>(Stage)) is the scheduler's
// protocol, declared in Scheduler.h — not here.
// ───────────────────────────────────────────────────────────────────────

class IExtension
{
public:
	virtual ~IExtension() = default;
};

// ───────────────────────────────────────────────────────────────────────
// ② Assembly: the dependency table.
//
// TExtension<TExtensions...> is an IExtension (identity) and a TTypeList
// (the assembled group) at once. NOT a singleton — single-instance access is
// a plugin's own choice (derive TSingleton<Self> alongside). Being a
// TExtension, it nests recursively.
// ───────────────────────────────────────────────────────────────────────

template <typename... TExtensions>
class TExtension
	: public virtual IExtension
	, public TTypeList<TExtensions...>
{
public:
	using FExtensions = TTypeList<TExtensions...>;
};

} // namespace Maho
