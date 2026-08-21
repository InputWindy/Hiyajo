#pragma once

#include <Core/Singleton.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Tool — the plug-in-and-play service. A CRTP singleton (T::Get()), self-
// managed: read AND write are public, no scheduler ownership. Callers call
// T::Get().xxx() directly whenever they need it.
//
// Dependencies are declared with the macros (MAHO_EXTEND_DEPS → FDependsPack),
// exactly like every other extension; Topology reads them for order.
//
//   class FLog : public Maho::TTool<FLog>
//   {
//   public:
//       MAHO_EXTEND_DEPS((IA, /* parent-or-omit */, /* extras */));
//       void Initialize();
//   };
//   FLog::Get().Initialize();
//
// A Tool is a singleton (TSingleton → ISingleton), so Query<...>.Select<ISingleton>()
// picks every tool out of a mixed list. C++14-compatible (no concept headers).
// ───────────────────────────────────────────────────────────────────────
template <typename TDerived>
class TTool : public TSingleton<TDerived>
{
};

} // namespace Maho
