#pragma once

#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// ① Extension contract — the identity every extension shares.
//
// An extension is a dependency table: which extensions it assembles. How it is
// driven it is NOT part of the extension — the scheduler traverses the lists
// (compile-time) or the instances (runtime) and passes a visitor lambda.
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
	using Type = TTypeList<TExtensions...>;
};

} // namespace Maho
