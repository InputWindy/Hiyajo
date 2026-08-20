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

// ───────────────────────────────────────────────────────────────────────
// ③ The extension → scheduler interaction protocol.
//
// ExecuteExtension<T>(Stage) is the ONE entry point through which an unknown
// scheduler interacts with extension T. It lives beside T (the extension
// declares it), and the driver (scheduler) SPECIALISES it for its own stage
// enum — deciding what T does at each stage.
//
// The primary template is the no-op fallback: any (Extension, Stage) pair
// the driver does NOT specialise simply does nothing.
// ───────────────────────────────────────────────────────────────────────

template <typename TExtension, typename TStage>
bool ExecuteExtension(TStage Stage)
{
	(void)Stage;
	return true;
}

} // namespace Maho
