#pragma once

#include <Core/Extension.h>
#include <Core/Singleton.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Tool — the plug-in-and-play service. A singleton with a dependency table,
// self-managed: read AND write are public, no scheduler ownership. Callers
// call T::Get().xxx() directly whenever they need it.
//
// C++14-compatible: depends only on TExtension + TSingleton + a marker tag,
// so a plugin that needs an older standard (e.g. Math + GLM) can derive from
// it without pulling in the C++20 concept headers (Layer.h).
// ───────────────────────────────────────────────────────────────────────

struct FToolTag {};

template <typename TDerived, typename... TExtensions>
class TTool
	: public FToolTag
	, public TExtension<TExtensions...>
	, public TSingleton<TDerived>
{
public:
	/** Identity tag — concatenate extra tag lists as needed. */
	using FTags = TCatch<TTypeList<FToolTag>>::Type;
};

} // namespace Maho
