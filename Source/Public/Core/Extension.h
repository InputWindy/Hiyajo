#pragma once

#include <Core/Singleton.h>
#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// ① Capability: singleton.
//
// Every traversed extension must be a singleton (the drive calls T::Get()),
// so TExtension inherits TSingleton (CRTP — TDerived is the concrete type).
//
// TExtension does NOT inherit IAssembly — most plugins are never loaded
// dynamically, so they don't need a Main / loadable identity. Only the
// application (the host) inherits IAssembly (or IEntryPoint) explicitly.
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived>
class TExtension : public TSingleton<TDerived>
{
public:
	virtual ~TExtension() = default;
};

// ───────────────────────────────────────────────────────────────────────
// ② + ③ Assembly: one class inher capability and the list.
//
// TExtensionList<TDerived, TExtensions...> is a TExtension (singleton) and a
// TTypeList (the assembled group) at once.
//
// ④ Being a TExtension, a TExtensionList nests recursively.
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived, typename... TExtensions>
class TExtensionList
	: public virtual TExtension<TDerived>
	, public TTypeList<TExtensions...>
{
public:
	using FExtensions = TTypeList<TExtensions...>;
};

} // namespace Maho
