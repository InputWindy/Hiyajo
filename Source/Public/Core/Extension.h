#pragma once

#include <Core/Topology.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Extension — the dependency-declaration macros.
//
// Identity/abilities are declared by inheritance (IPlugin<...>, FLayer, etc.);
// an extension additionally declares WHO it depends on (and in which slot /
// phase) via FDependsPack. The topology layer (Topology / TNodeDeps_t) reads
// FDependsPack to order groups and drive them.
//
//   struct SD : SC, IA, IB
//   {
//       MAHO_EXTEND_DEPS(
//           (IA, SC, SB),   // IA 调度：排在 SC（边）、SB 之后
//           (IB, SD, X));   // IB 调度：排在 SD（边）、X 之后
//   };
//
// Driving the matched types is the host/scheduler's job — an extension just
// declares its dependency table.
// ───────────────────────────────────────────────────────────────────────

/**
 * Empty dependency anchor for MAHO_EXTEND_DEPS — declares no FDependsPack, so
 * TNodeDeps_t<FNoParent, Key> is empty at every Key. Use it as the Parent of a
 * root layer (no parent edges) or when a dep is spelled purely in extras:
 *
 *   struct FWindow : FLayer<>, IRender
 *   {
 *       MAHO_EXTEND_DEPS((IRender, FNoParent));      // root — no deps
 *   };
 *   struct FScene : FLayer<>, IRender
 *   {
 *       MAHO_EXTEND_DEPS((IRender, FNoParent, FWindow));  // deps: FWindow
 *   };
 */
struct FNoParent
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
