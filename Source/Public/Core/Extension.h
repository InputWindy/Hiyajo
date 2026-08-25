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

/**
 * Declare a class's dependency slot (single Key form). ONE line, expanded by the
 * compiler into the FDepends type the topo layer reads; the dependency list
 * itself is code-gen: Tools/scan_deps.py scans this marker and writes
 * `#define MAHO_DEPS_<Class>_<Key> <deps...>` into the sibling .gen.h.
 *
 *   class FWorld : public ... {
 *   public:
 *       MAHO_EXTEND_DEPS(FWorld, FDefaultSlot, (FNoParent, FAI));  // after FAI
 *   };
 *
 * expands to:
 *   using FDepends = TTypeList<FDefaultSlot, TTypeList<MAHO_DEPS_FWorld_FDefaultSlot>>;
 * (== TTypeList<FDefaultSlot, TTypeList<FAI>> after the .gen.h macro).
 */
#define MAHO_EXTEND_DEPS(Class, Key, ...) \
	using FDepends = TTypeList<Key, TTypeList<MAHO_DEPS_##Class##_##Key>>

} // namespace Maho
