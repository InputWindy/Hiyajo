#pragma once

#include <Core/Topology.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Extension — the identity every service/plugin shares.
//
// An extension IS its dependency table: it declares what it depends on (and in
// which slot / phase) via FDependsPack. The scheduler reads FDependsPack
// (through Topology / TNodeDeps_t) to order service groups and drive them.
//
// TExtension<TDeps> assembles the identity + the deps in one little base, so an
// extension is a one-line declaration; interfaces go on the class itself.
//
//   struct FLog : TExtension<TDependsPack<>>
//   {
//       // identity + empty deps
//   };
//
//   struct FSystem : TExtension<TDependsPack<TDependsOn<EStage::Init, TTypeList<FLog>>>>
//   {
//   };
//
//   struct FRender : TExtension<TDependsPack<TDependsOn<EStage::Init, TTypeList<FSystem>>>>
//       , IRenderFeature                              // interfaces attach here
//   {
//   };
//
// Driving the matched types (by instance or singleton) is the host/scheduler's
// job — an extension is pure declaration (identity + dependency table).
// ───────────────────────────────────────────────────────────────────────

class IExtension
{
public:
	virtual ~IExtension() = default;
};

/**
 * Assembled extension base: carries the IExtension identity and declares the
 * type's dependency pack (FDependsPack). Derive and add interfaces as needed.
 */
template <typename TDeps>
class TExtension : public IExtension
{
public:
	using FDependsPack = TDeps;
};

} // namespace Maho
