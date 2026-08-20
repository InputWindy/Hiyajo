#pragma once

#include <Core/Singleton.h>
#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// ① Extension contract — the identity every driven extension shares.
//
// Subclasses must implement `Execute(TStage Stage)` for their own stage
// type. A template member cannot be virtual, so the contract is enforced at
// compile time by FExtensionExecute (below) instead of the vtable.
// ───────────────────────────────────────────────────────────────────────

class IExtension
{
public:
	virtual ~IExtension() = default;
};

// IExtension's contract: T must expose Execute(Stage) for the stage type
// the drive passes. The drive static_asserts this so a missing Execute is a
// clear compile-time error, not a runtime/link-time surprise.
template <typename T, typename TStage>
concept FExtensionExecute = requires(T& Ext, TStage Stage)
{
	{ Ext.Execute(Stage) };
};

// ───────────────────────────────────────────────────────────────────────
// ② Assembly: one class inher the contract and the list.
//
// TExtension<TExtensions...> is an IExtension (drivable) and a TTypeList
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
