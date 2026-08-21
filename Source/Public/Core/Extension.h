#pragma once

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Extension — the identity every service/plugin shares.
//
// An extension IS its dependency table: a host plugin declares what it depends
// on (and at which stage) via FDependsPack. The scheduler reads FDependsPack
// (through Topology / TNodeDeps_t) to order service groups and drive them; it
// never reaches inside the extension because the type knows its own deps.
//
//   class FInput : public IExtension
//   {
//   public:
//       using FDependsPack = TDependsPack<>;                       // no deps
//   };
//
//   class FSystem : public IExtension
//   {
//   public:
//       using FDependsPack = TDependsPack<
//           TDependsOn<EStage::Init, TTypeList<FInput>>>;          // deps: FInput
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

} // namespace Maho
