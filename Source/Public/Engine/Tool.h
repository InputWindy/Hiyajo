#pragma once

#include <Core/Extension.h>
#include <Core/Singleton.h>
#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Tool — the plug-in-and-play service. A singleton, self-managed: read AND
// write are public, no scheduler ownership. Callers call T::Get().xxx()
// directly whenever they need it.
//
// Dependencies are declared like any extension: inherit IExtension (directly or
// through a base) and define using FDependsPack — Topology reads it for order.
//
// C++14-compatible: pulls only TSingleton (+ IExtension), so a plugin that needs
// an older standard (e.g. Math + GLM) can derive from it without the C++20
// concept headers (Layer.h).
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived>
class TTool
	: public TSingleton<TDerived>
{
public:
	using FTags = TTypeList<>;
};

} // namespace Maho
