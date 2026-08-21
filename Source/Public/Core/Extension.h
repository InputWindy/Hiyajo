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

/**
 * Interface plug — a variadic base that installs any number of interfaces as
 * its bases, so a class can carry them without spelling each out in its own
 * inheritance list:
 *
 *   class FLog : public Maho::TSingleton<FLog>,
 *                public Maho::TPlug<IA, IB>     // IA + IB
 *   {
 *   };
 */
template <typename... TInterfaces>
class TPlug : public TInterfaces...
{
};

/**
 * Extend a parent extension's deps across one or more interface Keys.
 *
 * Each group (Key, Parent, extras...) declares one dependency slot: at Key this
 * class runs after its parent's edges (parent & child are the SAME 3D node, so
 * the parent node is never a dep — only its EDGES are inherited) and after the
 * extra types. All slots land in one FDependsPack.
 *
 *   struct SD : SC, IA, IB
 *   {
 *       MAHO_EXTEND_DEPS(
 *           (IA, SC, SB),   // IA 调度：排在 SC（边）、SB 之后
 *           (IB, SD, X));   // IB 调度：排在 SD（边）、X 之后
 *   };
 */
#define MAHO_DEPEND_ONE_IMPL(Key, Parent, ...) \
	::Maho::TDependsOn<Key, \
		::Maho::TUnionList_t<::Maho::Topo::TNodeDeps_t<Parent, Key>, \
			::Maho::TTypeList<__VA_ARGS__>>>
#define MAHO_DEPEND_ONE(Group) MAHO_DEPEND_ONE_IMPL Group

// MSVC-friendly 1..4 FOR_EACH: emit `Name(arg), Name(arg), ...`.
#define MAHO_DEPS_EXPAND(...) __VA_ARGS__
#define MAHO_DEPS_FE1(F, A)                    F(A)
#define MAHO_DEPS_FE2(F, A, ...)               F(A), MAHO_DEPS_EXPAND(MAHO_DEPS_FE1(F, __VA_ARGS__))
#define MAHO_DEPS_FE3(F, A, ...)               F(A), MAHO_DEPS_EXPAND(MAHO_DEPS_FE2(F, __VA_ARGS__))
#define MAHO_DEPS_FE4(F, A, ...)               F(A), MAHO_DEPS_EXPAND(MAHO_DEPS_FE3(F, __VA_ARGS__))
#define MAHO_DEPS_SELECT(_1, _2, _3, _4, NAME, ...) NAME
#define MAHO_DEPS_FOR_EACH(F, ...) \
	MAHO_DEPS_EXPAND( \
		MAHO_DEPS_SELECT(__VA_ARGS__, \
			MAHO_DEPS_FE4, MAHO_DEPS_FE3, MAHO_DEPS_FE2, MAHO_DEPS_FE1)(F, __VA_ARGS__))

#define MAHO_EXTEND_DEPS(...) \
	using FDependsPack = ::Maho::TDependsPack< \
		MAHO_DEPS_EXPAND(MAHO_DEPS_FOR_EACH(MAHO_DEPEND_ONE, __VA_ARGS__))>;

} // namespace Maho
