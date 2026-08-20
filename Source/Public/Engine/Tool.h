#pragma once

#include <Core/Extension.h>
#include <Core/Singleton.h>
#include <Core/Tags.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Tool — the driven plugin. A singleton with a dependency table, no
// scheduler of its own; a host drives it via ExecuteExtension.
//
// C++14-compatible: depends only on TExtension + TSingleton + a marker tag,
// so a plugin that needs an older standard (e.g. Math + GLM) can derive from
// it without pulling in the C++20 concept headers (Layer.h / Engine.h).
// ───────────────────────────────────────────────────────────────────────

struct FToolTag {};

template <typename TDerived, typename... TExtensions>
class TTool
	: public FToolTag
	, public TExtension<TExtensions...>
	, public TSingleton<TDerived>
{
public:
	/** UCLASS-style markers — the identity tag FToolTag is always present. A
	 *  plugin appends more via WithTags (e.g. FStandaloneTag for self-managed). */
	using FTags = TTypeList<FToolTag>;

	/** Append extra tags while keeping the identity tag. */
	template <typename... TExtra>
	using WithTags = FWithTags<FTags, TExtra...>;
};

} // namespace Maho
