#pragma once

#include <Core/Singleton.h>
#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// ① Capability: an extension is anything the scheduler can drive.
//
// TExtension is a PURE marker — it carries no singleton, no IAssembly, no
// stage. A plugin decides for itself whether it wants process-wide
// single-instance access (see TSingletonExtensionList) or not (an
// application, loadable dynamically, can be instantiated many times).
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived>
class TExtension
{
public:
	virtual ~TExtension() = default;
};

// ───────────────────────────────────────────────────────────────────────
// ② + ③ Assembly: one class inher capability and the list.
//
// TExtensionList<TDerived, TExtensions...> is a TExtension (drivable) and a
// TTypeList (the assembled group) at once. NOT a singleton — being a
// TExtension, it nests recursively.
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived, typename... TExtensions>
class TExtensionList
	: public virtual TExtension<TDerived>
	, public TTypeList<TExtensions...>
{
public:
	using FExtensions = TTypeList<TExtensions...>;
};

// ───────────────────────────────────────────────────────────────────────
// ④ Singleton assembly — the convenience form for plugins that DO want
// process-wide single-instance access. Tool plugins (Log, Json, …) derive
// from this; the compile-time drive reaches them via T::Get().
//
//   class FLog : public TSingletonExtensionList<FLog> { ... };
//
// An application (host) that is loaded dynamically and may have many
// instances derives from plain TExtensionList instead (no singleton).
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived, typename... TExtensions>
class TSingletonExtensionList
	: public TExtensionList<TDerived, TExtensions...>
	, public TSingleton<TDerived>
{
};

} // namespace Maho
